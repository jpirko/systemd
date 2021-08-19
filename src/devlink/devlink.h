/* SPDX-License-Identifier: LGPL-2.1-or-later */
#pragma once

#include <linux/devlink.h>

#include "conf-parser.h"
#include "list.h"
#include "log-link.h"
#include "sd-netlink.h"
#include "time-util.h"

#include "devlink-key.h"
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

typedef struct Devlink {
        DevlinkKey key;
        Manager *manager;
        unsigned n_ref;
        char *filename;
        bool in_hashmap;
} Devlink;

typedef struct DevlinkMonitorCommand {
        enum devlink_command cmd;
        int (*msg_process)(Devlink *devlink, DevlinkKey *lookup_key,
                           sd_netlink_message *message, int message_iterator);
} DevlinkMonitorCommand;

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

        /* Array of monitor commands, the first one is used
         * for enumeration messages processing. */
        const DevlinkMonitorCommand *genl_monitor_cmds;
        unsigned int genl_monitor_cmds_count;
        enum devlink_command genl_enumerate_cmd;
        bool genl_need_periodic_enumeration;
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

int devlink_log_internal(
                const DevlinkKey *key,
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

int devlink_load(Manager *manager, bool reload);
void devlink_genl_process_message(sd_netlink_message *message,
                                  Manager *m, DevlinkKind kind,
                                  const DevlinkMonitorCommand *monitor_cmd);

#define devlink_genl_message_new(cont, cmd, ret) \
        sd_genl_message_new(cont->manager->genl, DEVLINK_GENL_NAME, cmd, ret)

/* gperf */
const struct ConfigPerfItem* devlink_gperf_lookup(const char *key, GPERF_LEN_TYPE length);

#define log_devlink_full_errno_zerook(cont, level, error, ...)                      \
        ({                                                                          \
                errno = ERRNO_VALUE(error);                                         \
                devlink_log_internal(&cont->key, level, error, PROJECT_FILE,        \
                                     __LINE__, __func__, ##__VA_ARGS__);            \
        })

#define log_devlink_full_errno(cont, level, error, ...)                             \
        ({                                                                          \
                int _error = (error);                                               \
                ASSERT_NON_ZERO(_error);                                            \
                log_devlink_full_errno_zerook(cont, level, _error, __VA_ARGS__);    \
        })

#define log_devlink_full(cont, level, ...) (void) log_devlink_full_errno_zerook(cont, level, 0, __VA_ARGS__)

#define log_devlink_debug(cont, ...)   log_devlink_full(cont, LOG_DEBUG, __VA_ARGS__)
#define log_devlink_info(cont, ...)    log_devlink_full(cont, LOG_INFO, __VA_ARGS__)
#define log_devlink_notice(cont, ...)  log_devlink_full(cont, LOG_NOTICE, __VA_ARGS__)
#define log_devlink_warning(cont, ...) log_devlink_full(cont, LOG_WARNING,  __VA_ARGS__)
#define log_devlink_error(cont, ...)   log_devlink_full(cont, LOG_ERR, __VA_ARGS__)

#define log_devlink_debug_errno(cont, error, ...)   log_devlink_full_errno(cont, LOG_DEBUG, error, __VA_ARGS__)
#define log_devlink_info_errno(cont, error, ...)    log_devlink_full_errno(cont, LOG_INFO, error, __VA_ARGS__)
#define log_devlink_notice_errno(cont, error, ...)  log_devlink_full_errno(cont, LOG_NOTICE, error, __VA_ARGS__)
#define log_devlink_warning_errno(cont, error, ...) log_devlink_full_errno(cont, LOG_WARNING, error, __VA_ARGS__)
#define log_devlink_error_errno(cont, error, ...)   log_devlink_full_errno(cont, LOG_ERR, error, __VA_ARGS__)
