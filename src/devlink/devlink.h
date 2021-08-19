/* SPDX-License-Identifier: LGPL-2.1-or-later */
#pragma once

#include <linux/devlink.h>

#include "conf-parser.h"
#include "list.h"
#include "log-link.h"
#include "sd-netlink.h"
#include "time-util.h"

#include "devlink-match.h"
#include "devlinkd-manager.h"

#define DEVLINK_COMMON_SECTIONS "Match\0"
/* This is the list of known sections. We need to ignore them in the initial parsing phase. */
#define DEVLINK_OTHER_SECTIONS                          \
        "-MatchPort\0"                                  \
        "-MatchParam\0"                                 \
        "-Eswitch\0"                                    \
        "-Reload\0"                                     \
        "-Split\0"                                      \
        "-Param\0"

typedef enum DevlinkKind {
        DEVLINK_KIND_DEV,
        DEVLINK_KIND_PORT,
        DEVLINK_KIND_PARAM,
        _DEVLINK_KIND_MAX,
        _DEVLINK_KIND_INVALID = -EINVAL,
} DevlinkKind;

typedef struct DevlinkKey {
        DevlinkKind kind;
        DevlinkMatchSet matchset;
        DevlinkMatch match;
} DevlinkKey;

typedef struct Devlink {
        DevlinkKey key;
        Manager *manager;
        unsigned n_ref;
        char *filename;
        bool in_hashmap;
} Devlink;

typedef struct DevlinkVTable {
        /* How much memory does an object of this unit type need */
        size_t object_size;

        /* Config file sections this devlnk kind understands, separated
         * by NUL chars */
        const char *sections;

        /* Matchsets bitfields, ended by 0 */
        const DevlinkMatchSet *matchsets;

        void (*init)(Devlink *devlink);

        /* This should free all kind-specific variables. It should be
         * idempotent. */
        void (*done)(Devlink *devlink);

        int (*log_prefix)(const Devlink *devlink, char *buf, int len);

        int (*config_verify)(const Devlink *devlink, const char *filename);

        enum devlink_command genl_monitor_cmd;
        enum devlink_command genl_enumerate_cmd;
        int (*genl_msg_process)(Devlink *devlink, DevlinkKey *lookup_key,
                                sd_netlink_message *message, int message_iterator);
} DevlinkVTable;

extern const DevlinkVTable * const devlink_vtable[_DEVLINK_KIND_MAX];

#define _DEVLINK_VTABLE(kind) (kind != _DEVLINK_KIND_INVALID ? devlink_vtable[kind] : NULL)
#define DEVLINK_VTABLE(n) _DEVLINK_VTABLE((n)->key.kind)
#define devlink_for_each_vtable(_vtable, _iterator)                       \
        for (_iterator = 0, _vtable = devlink_vtable[_iterator];          \
             _iterator < _DEVLINK_KIND_MAX;                               \
             _iterator++, _vtable = devlink_vtable[_iterator])

#define devlink_for_each_kind(_iterator)                                  \
        for (_iterator = 0;                                               \
             _iterator < _DEVLINK_KIND_MAX;                               \
             _iterator++)

/* For casting a devlink into the various devlink kinds */
#define DEFINE_DEVLINK_CAST(type, structname)                             \
        static inline structname* DEVLINK_##type(Devlink *devlink) {      \
                if (_unlikely_(!devlink ||                                \
                               devlink->key.kind != DEVLINK_KIND_##type)) \
                        return NULL;                                      \
                                                                          \
                return (structname*) devlink;                             \
        }                                                                 \
        static inline const structname*                                   \
        DEVLINK_CONST_##type(const Devlink *devlink) {                    \
                if (_unlikely_(!devlink ||                                \
                               devlink->key.kind != DEVLINK_KIND_##type)) \
                        return NULL;                                      \
                                                                          \
                return (const structname*) devlink;                       \
        }

/* For casting the various devlink kinds into a devlink */
#define DEVLINK(devlink) (&(devlink)->meta)

const char *devlink_kind_to_string(DevlinkKind d) _const_;
DevlinkKind devlink_kind_from_string(const char *d) _pure_;

int devlink_log_internal(
                const Devlink *devlink,
                int level,
                int error,
                const char *file,
                int line,
                const char *func,
                const char *format, ...);

Devlink *devlink_unref(Devlink *devlink);
Devlink *devlink_ref(Devlink *devlink);
DEFINE_TRIVIAL_DESTRUCTOR(devlink_destroy_callback, Devlink, devlink_unref);
DEFINE_TRIVIAL_CLEANUP_FUNC(Devlink*, devlink_unref);

CONFIG_PARSER_PROTOTYPE(config_parse_devlink_kind);

int devlink_key_genl_append(sd_netlink_message *message, const DevlinkKey *key);
int devlink_load(Manager *manager, bool reload);
void devlink_genl_process_message(sd_netlink_message *message,
                                  Manager *m, DevlinkKind kind);

/* gperf */
const struct ConfigPerfItem* devlink_kind_gperf_lookup(const char *key, GPERF_LEN_TYPE length);
const struct ConfigPerfItem* devlink_gperf_lookup(const char *key, GPERF_LEN_TYPE length);

#define log_devlink_full_errno_zerook(devlink, level, error, ...)                   \
        ({                                                                          \
                errno = ERRNO_VALUE(error);                                         \
                devlink_log_internal(devlink, level, error, PROJECT_FILE,           \
                                     __LINE__, __func__, ##__VA_ARGS__);            \
        })

#define log_devlink_full_errno(devlink, level, error, ...)                          \
        ({                                                                          \
                int _error = (error);                                               \
                ASSERT_NON_ZERO(_error);                                            \
                log_devlink_full_errno_zerook(devlink, level, _error, __VA_ARGS__); \
        })

#define log_devlink_full(devlink, level, ...) (void) log_devlink_full_errno_zerook(devlink, level, 0, __VA_ARGS__)

#define log_devlink_debug(devlink, ...)   log_devlink_full(devlink, LOG_DEBUG, __VA_ARGS__)
#define log_devlink_info(devlink, ...)    log_devlink_full(devlink, LOG_INFO, __VA_ARGS__)
#define log_devlink_notice(devlink, ...)  log_devlink_full(devlink, LOG_NOTICE, __VA_ARGS__)
#define log_devlink_warning(devlink, ...) log_devlink_full(devlink, LOG_WARNING,  __VA_ARGS__)
#define log_devlink_error(devlink, ...)   log_devlink_full(devlink, LOG_ERR, __VA_ARGS__)

#define log_devlink_debug_errno(devlink, error, ...)   log_devlink_full_errno(devlink, LOG_DEBUG, error, __VA_ARGS__)
#define log_devlink_info_errno(devlink, error, ...)    log_devlink_full_errno(devlink, LOG_INFO, error, __VA_ARGS__)
#define log_devlink_notice_errno(devlink, error, ...)  log_devlink_full_errno(devlink, LOG_NOTICE, error, __VA_ARGS__)
#define log_devlink_warning_errno(devlink, error, ...) log_devlink_full_errno(devlink, LOG_WARNING, error, __VA_ARGS__)
#define log_devlink_error_errno(devlink, error, ...)   log_devlink_full_errno(devlink, LOG_ERR, error, __VA_ARGS__)
