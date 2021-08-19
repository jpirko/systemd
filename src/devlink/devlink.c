/* SPDX-License-Identifier: LGPL-2.1-or-later */

#include "alloc-util.h"
#include "conf-files.h"
#include "conf-parser.h"
#include "fd-util.h"
#include "list.h"
#include "netlink-util.h"
#include "parse-util.h"
#include "path-lookup.h"
#include "string-table.h"
#include "string-util.h"
#include "strv.h"
#include "stat-util.h"

#include "devlink.h"
#include "devlink-dev.h"
#include "devlink-port.h"
#include "devlink-param.h"
#include "devlinkd-manager.h"

const DevlinkVTable * const devlink_vtable[_DEVLINK_KIND_MAX] = {
        [DEVLINK_KIND_DEV] = &devlink_dev_vtable,
        [DEVLINK_KIND_PORT] = &devlink_port_vtable,
        [DEVLINK_KIND_PARAM] = &devlink_param_vtable,
};

static const char* const devlink_kind_table[_DEVLINK_KIND_MAX] = {
        [DEVLINK_KIND_DEV] = "dev",
        [DEVLINK_KIND_PORT] = "port",
        [DEVLINK_KIND_PARAM] = "param",
};

int devlink_log_internal(
                const Devlink *devlink,
                int level,
                int error,
                const char *file,
                int line,
                const char *func,
                const char *format, ...) {
        char buf[LINE_MAX], *pos = buf;
        int len = sizeof(buf);
        va_list ap;

        BUFFER_APPEND(pos, len, "%s", devlink_kind_to_string(devlink->key.kind));
        devlink_match_log_prefix(&pos, &len, &devlink->key.match, devlink->key.matchset);
        BUFFER_APPEND(pos, len, ": ");

        va_start(ap, format);
        (void) vsnprintf(pos, len, format, ap);
        va_end(ap);

        return log_internal(level, error, file, line, func, "%s", buf);
}

static void devlink_key_hash_func(const DevlinkKey *key, struct siphash *state) {
        siphash24_compress(&key->kind, sizeof(key->kind), state);
        siphash24_compress(&key->matchset, sizeof(key->matchset), state);
        devlink_match_hash_func(&key->match, key->matchset, state);
}

static int devlink_key_compare_func(const DevlinkKey *x, const DevlinkKey *y) {
        int d;

        assert(x);
        assert(y);

        d = CMP(x->kind, y->kind);
        if (d)
                return d;

        d = CMP(x->matchset, y->matchset);
        if (d)
                return d;

        return devlink_match_compare_func(&x->match, &y->match, x->matchset);
}

static void devlink_key_init(DevlinkKey *key, DevlinkKind kind) {
        key->kind = kind;
        key->matchset = 0;
        devlink_match_init(&key->match);
}

static void devlink_key_fini(DevlinkKey *key) {
        devlink_match_fini(&key->match);
}

int devlink_key_genl_append(sd_netlink_message *message, const DevlinkKey *key) {
        return devlink_match_genl_append(message, &key->match, key->matchset);
}

static Devlink *devlink_alloc(Manager *m, DevlinkKind kind) {
        Devlink *devlink;

        devlink = malloc0(_DEVLINK_VTABLE(kind)->object_size);
        if (!devlink)
                return NULL;

        *devlink = (Devlink) {
                .manager = m,
                .n_ref = 1,
        };

        devlink_key_init(&devlink->key, kind);

        if (DEVLINK_VTABLE(devlink)->init)
                DEVLINK_VTABLE(devlink)->init(devlink);

        return devlink;
}

static Devlink *devlink_free(Devlink *devlink) {
        assert(devlink);

        if (devlink->in_hashmap)
                hashmap_remove(devlink->manager->devlink_objs, &devlink->key);

        free(devlink->filename);

        if (DEVLINK_VTABLE(devlink)->done)
                DEVLINK_VTABLE(devlink)->done(devlink);

        devlink_key_fini(&devlink->key);

        return mfree(devlink);
}

DEFINE_TRIVIAL_REF_UNREF_FUNC(Devlink, devlink, devlink_free);

DEFINE_PRIVATE_HASH_OPS_WITH_VALUE_DESTRUCTOR(devlink_hash_ops, DevlinkKey, devlink_key_hash_func, devlink_key_compare_func, Devlink, devlink_free);

DEFINE_STRING_TABLE_LOOKUP(devlink_kind, DevlinkKind);

int config_parse_devlink_kind(
                const char *unit,
                const char *filename,
                unsigned line,
                const char *section,
                unsigned section_line,
                const char *lvalue,
                int ltype,
                const char *rvalue,
                void *data,
                void *userdata) {
        DevlinkKind k, *kind = data;

        assert(filename);
        assert(lvalue);
        assert(rvalue);
        assert(data);

        k = devlink_kind_from_string(rvalue);
        if (k < 0) {
                log_syntax(unit, LOG_WARNING, filename, line, k, "Failed to parse devlink kind, ignoring assignment: %s", rvalue);
                return 0;
        }

        if (*kind != _DEVLINK_KIND_INVALID && *kind != k) {
                log_syntax(unit, LOG_WARNING, filename, line, 0,
                           "Specified devlink kind is different from the previous value '%s', ignoring assignment: %s",
                           devlink_kind_to_string(*kind), rvalue);
                return 0;
        }

        *kind = k;

        return 0;
}

static int devlink_matchset_select(Devlink *devlink, const char *filename) {
        DevlinkKey *key = &devlink->key;
        DevlinkMatchSet matchset;
        unsigned int i = 0;

        while ((matchset = DEVLINK_VTABLE(devlink)->matchsets[i++])) {
                printf("matchset %x\n", matchset);
                if (devlink_match_check(&key->match, matchset)) {
                        key->matchset = matchset;
                        printf("selected\n");
                        return 0;
                }
        }
        return -ENOENT;
}

static int devlink_load_one(Manager *m, const char *filename) {
        _cleanup_(devlink_unrefp) Devlink *devlink = NULL;
        DevlinkKind kind = _DEVLINK_KIND_INVALID;
        const char *dropin_dirname;
        int r;

        assert(m);
        assert(filename);

        r = null_or_empty_path(filename);
        if (r == -ENOENT)
                return 0;
        if (r < 0)
                return r;
        if (r > 0) {
                log_debug("Skipping empty file: %s", filename);
                return 0;
        }

        log_debug("Parsing file: %s", filename);

        dropin_dirname = strjoina(basename(filename), ".d");
        r = config_parse_many(
                        STRV_MAKE_CONST(filename), DEVLINK_DIRS, dropin_dirname,
                        /* root = */ NULL,
                        DEVLINK_COMMON_SECTIONS DEVLINK_OTHER_SECTIONS,
                        config_item_perf_lookup, devlink_kind_gperf_lookup,
                        CONFIG_PARSE_RELAXED | CONFIG_PARSE_WARN, &kind,
                        NULL, NULL);
        if (r < 0)
                return r;

        if (kind == _DEVLINK_KIND_INVALID) {
                log_warning("Devlink has no Match.Kind= configured in %s. Ignoring", filename);
                return 0;
        }

        devlink = devlink_alloc(m, kind);
        if (!devlink)
                return log_oom();

        r = config_parse_many(
                        STRV_MAKE_CONST(filename), NETWORK_DIRS, dropin_dirname,
                        /* root = */ NULL,
                        DEVLINK_VTABLE(devlink)->sections,
                        config_item_perf_lookup, devlink_gperf_lookup,
                        CONFIG_PARSE_WARN, devlink, NULL, NULL);
        if (r < 0)
                return r;

        devlink->filename = strdup(filename);
        if (!devlink->filename)
                return log_oom();

        r = devlink_matchset_select(devlink, filename);
        if (r < 0) {
                log_warning("Devlink none or incomplete Match* set configured in %s. Ignoring", filename);
                return r;
        }

        if (DEVLINK_VTABLE(devlink)->config_verify) {
                r = DEVLINK_VTABLE(devlink)->config_verify(devlink, filename);
                if (r < 0)
                        return r;
        }

        r = hashmap_ensure_put(&m->devlink_objs, &devlink_hash_ops, &devlink->key, devlink);
        if (r == -ENOMEM) {
                return log_oom();
        } else if (r == -EEXIST) {
                Devlink *d = hashmap_get(m->devlink_objs, &devlink->key);

                assert(d);
                if (!streq(devlink->filename, d->filename))
                        log_devlink_warning_errno(devlink, r, "Device was already configured by file %s", d->filename);
                return 0;
        } else if (r < 0) {
                return r;
        }
        devlink->in_hashmap = true;
        devlink_ref(devlink);

        log_devlink_debug(devlink, "Loaded");
        return 0;
}

int devlink_load(Manager *m, bool reload) {
        _cleanup_strv_free_ char **files = NULL;
        int r;

        assert(m);

        if (!reload)
                hashmap_clear_with_destructor(m->devlink_objs, devlink_unref);

        r = conf_files_list_strv(&files, ".dl", NULL, 0, DEVLINK_DIRS);
        if (r < 0)
                return log_error_errno(r, "Failed to enumerate devlink files: %m");

        STRV_FOREACH(f, files) {
                r = devlink_load_one(m, *f);
                if (r < 0)
                        log_error_errno(r, "Failed to load %s, ignoring: %m", *f);
        }

        return 0;
}

void devlink_genl_process_message(sd_netlink_message *message,
                                  Manager *m, DevlinkKind kind) {
        DevlinkMatchSet matchset;
        int message_iterator = 0;
        Devlink *devlink;
        DevlinkKey key;
        unsigned int i;
        int r;

        do {
                devlink_key_init(&key, kind);
                devlink_match_genl_read(message, m, &message_iterator, &key.match, &key.matchset);
                devlink = NULL;

                for (i = 0; i < sizeof(_DEVLINK_VTABLE(kind)->matchsets); i++) {
                        matchset = _DEVLINK_VTABLE(kind)->matchsets[i];
                        /* Check if the current matchset is subset of the one previously read. */
                        if ((matchset & key.matchset) != matchset)
                                continue;
                        /* For the hashmap lookup, the matchset needs to be set to the current one */
                        SWAP_TWO(key.matchset, matchset);
                        devlink = hashmap_get(m->devlink_objs, &key);
                        SWAP_TWO(key.matchset, matchset);
                        if (devlink)
                                break;
                }

                if (devlink) {
                        log_devlink_debug(devlink, "Matched object");
                        r = _DEVLINK_VTABLE(kind)->genl_msg_process(devlink, &key, message, message_iterator);
                        if (r < 0)
                                log_debug_errno(r, "devlink netlink: Failed to process message, ignoring: %m");
                }

                devlink_key_fini(&key);

        } while (message_iterator > 0);
}
