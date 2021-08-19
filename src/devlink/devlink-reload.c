/* SPDX-License-Identifier: LGPL-2.1-or-later */

#include "alloc-util.h"
#include "devlink-util.h"
#include "netlink-util.h"

#include "devlink.h"
#include "devlink-key.h"
#include "devlink-reload.h"

typedef struct DevlinkReload {
        DevlinkKey key;
        Manager *manager;
        bool in_hashmap;
        sd_event_source *timeout_event_source;
} DevlinkReload;

static DevlinkReload *devlink_reload_free(DevlinkReload *reload) {
        assert(reload);

        if (reload->in_hashmap)
                hashmap_remove(reload->manager->reload, &reload->key);

        devlink_key_fini(&reload->key);
        reload->timeout_event_source = sd_event_source_disable_unref(reload->timeout_event_source);

        return mfree(reload);
}

DEFINE_PRIVATE_HASH_OPS_WITH_VALUE_DESTRUCTOR(
                devlink_reload_hash_ops,
                DevlinkKey,
                devlink_key_hash_func,
                devlink_key_compare_func,
                DevlinkReload,
                devlink_reload_free);

DEFINE_TRIVIAL_CLEANUP_FUNC(DevlinkReload *, devlink_reload_free);

static DevlinkReload *devlink_reload_alloc(Manager *m, DevlinkKey *key) {
        _cleanup_(devlink_reload_freep) DevlinkReload *reload;
        int r;

        reload = malloc0(sizeof(DevlinkReload));
        if (!reload)
                return NULL;

        *reload = (DevlinkReload) {
                .manager = m,
        };

        devlink_key_init(&reload->key, DEVLINK_KIND_DEV);
        r = devlink_key_duplicate(&reload->key, key);
        if (r < 0)
                return NULL;

        r = hashmap_ensure_put(&m->reload, &devlink_reload_hash_ops, &reload->key, reload);
        if (r < 0)
                return NULL;

        reload->in_hashmap = true;

        return TAKE_PTR(reload);
}

static int devlink_reload(DevlinkReload *reload) {
        _cleanup_(sd_netlink_message_unrefp) sd_netlink_message *req = NULL;
        _cleanup_(sd_netlink_message_unrefp) sd_netlink_message *rep = NULL;
        int r;

        r = devlink_genl_message_new(reload, DEVLINK_CMD_RELOAD, &req);
        if (r < 0)
                return log_devlink_error_errno(reload, r, "Failed to create netlink message: %m");;

        r = devlink_key_genl_append(req, &reload->key);
        if (r < 0)
                return r;

        r = sd_netlink_message_append_u8(req, DEVLINK_ATTR_RELOAD_ACTION, DEVLINK_RELOAD_ACTION_DRIVER_REINIT);
        if (r < 0)
                return log_devlink_error_errno(reload, r, "Failed to append reload action to netlink message: %m");;

        r = sd_netlink_call(reload->manager->genl, req, 0, &rep);
        if (r < 0)
                return log_devlink_error_errno(reload, r, "Could not send reload message: %m");

        r = sd_netlink_message_get_errno(rep);
        if (r < 0)
                return log_devlink_error_errno(reload, r, "Could not be reload: %m");

        log_devlink_info(reload, "Reload success");

        return 0;
}

static int devlink_reload_event_callback(sd_event_source *source, usec_t usec, void *userdata) {
        DevlinkReload *reload = ASSERT_PTR(userdata);

        assert(source == reload->timeout_event_source);

        (void) devlink_reload(reload);

        return 0;
}

#define DEVLINK_RELOAD_TIMEOUT USEC_PER_SEC * 3

static int devlink_reload_queue_event(Manager *m, DevlinkReload *reload) {
        int r;

        log_devlink_info(reload, "Scheduling reload in %lu usec", DEVLINK_RELOAD_TIMEOUT);
        if (reload->timeout_event_source) {
                r = sd_event_source_set_time_relative(reload->timeout_event_source, DEVLINK_RELOAD_TIMEOUT);
                if (r < 0)
                        return r;

                return sd_event_source_set_enabled(reload->timeout_event_source, SD_EVENT_ONESHOT);
        }

        r = sd_event_add_time_relative(
                        m->event,
                        &reload->timeout_event_source,
                        CLOCK_MONOTONIC, DEVLINK_RELOAD_TIMEOUT, 0,
                        devlink_reload_event_callback, reload);
        if (r < 0)
                return r;

        (void) sd_event_source_set_description(reload->timeout_event_source, "devlink-reload-timeout");

        return 0;
}

int devlink_reload_queue(Manager *m, DevlinkKey *orig_key) {
        _cleanup_(devlink_reload_freep) DevlinkReload *reload;
        DevlinkKey key;
        int r;

        devlink_key_init(&key, DEVLINK_KIND_DEV);
        devlink_key_copy_subkey(&key, orig_key, DEVLINK_MATCH_BIT_DEV);

        reload = hashmap_get(m->reload, &key);
        if (!reload) {
                reload = devlink_reload_alloc(m, &key);
                if (!reload)
                        return log_devlink_error_errno(reload, SYNTHETIC_ERRNO(ENOMEM), "Failed to allocate reload: %m");
        }

        r = devlink_reload_queue_event(m, reload);
        if (r < 0)
                return log_devlink_error_errno(reload, r, "Failed to schedule reload: %m");

        TAKE_PTR(reload);
        return 0;
}

void devlink_reload_cleanup(Manager *m, DevlinkKey *orig_key) {
        _cleanup_(devlink_reload_freep) DevlinkReload *reload;
        DevlinkKey key;

        devlink_key_init(&key, DEVLINK_KIND_DEV);
        devlink_key_copy_subkey(&key, orig_key, DEVLINK_MATCH_BIT_DEV);

        reload = hashmap_get(m->reload, &key);
}
