/* SPDX-License-Identifier: LGPL-2.1-or-later */
#pragma once

#include "conf-parser.h"
#include "devlink-util.h"
#include "netlink-util.h"
#include "siphash24.h"

#include "devlinkd-manager.h"

typedef enum DevlinkMatchBit {
        DEVLINK_MATCH_BIT_DEV = 1 << 0,
        DEVLINK_MATCH_BIT_PORT_INDEX = 1 << 1,
        DEVLINK_MATCH_BIT_PORT_IFNAME = 1 << 2,
        DEVLINK_MATCH_BIT_PORT_CACHED_IFNAME = 1 << 3, /* For use with non-port objects */
        DEVLINK_MATCH_BIT_PARAM = 1 << 4,
} DevlinkMatchBit;

typedef uint32_t DevlinkMatchSet;

typedef struct DevlinkMatchDev {
        char *bus_name; /* first part of the handle */
        char *dev_name; /* second part of the handle */
} DevlinkMatchDev;

typedef struct DevlinkMatchPort {
        uint32_t index;
        char *ifname;
        bool split;
} DevlinkMatchPort;

typedef struct DevlinkMatchParam {
        char *name;
        DevlinkParamCMode cmode;
} DevlinkMatchParam;

typedef struct DevlinkMatch {
        DevlinkMatchDev dev;
        DevlinkMatchPort port;
        DevlinkMatchParam param;
} DevlinkMatch;

typedef struct DevlinkMatchVTable {
        void (*init)(DevlinkMatch *match);
        void (*free)(DevlinkMatch *match);
        bool (*check)(const DevlinkMatch *match);
        void (*log_prefix)(char **buf, int *len, const DevlinkMatch *match);
        void (*hash_func)(const DevlinkMatch *match, struct siphash *state);
        int (*compare_func)(const DevlinkMatch *x, const DevlinkMatch *y);
        int (*genl_read)(sd_netlink_message *message, Manager *m, int *message_iterator, DevlinkMatch *match);
        int (*genl_append)(sd_netlink_message *message, const DevlinkMatch *match);
} DevlinkMatchVTable;

extern const DevlinkMatchVTable devlink_match_dev_vtable;
extern const DevlinkMatchVTable devlink_match_port_index_vtable;
extern const DevlinkMatchVTable devlink_match_port_ifname_vtable;
extern const DevlinkMatchVTable devlink_match_port_cached_ifname_vtable;
extern const DevlinkMatchVTable devlink_match_param_vtable;

CONFIG_PARSER_PROTOTYPE(config_parse_devlink_dev_handle);
CONFIG_PARSER_PROTOTYPE(config_parse_devlink_param_cmode);

void devlink_match_init(DevlinkMatch *match);
void devlink_match_fini(DevlinkMatch *match);
bool devlink_match_check(const DevlinkMatch *match, DevlinkMatchSet matchset);
void devlink_match_log_prefix(char **pos, int *len, const DevlinkMatch *match, DevlinkMatchSet matchset);
void devlink_match_hash_func(
                const DevlinkMatch *match,
                DevlinkMatchSet matchset,
                struct siphash *state);
int devlink_match_compare_func(
                const DevlinkMatch *x,
                const DevlinkMatch *y,
                DevlinkMatchSet matchset);
void devlink_match_genl_read(
                sd_netlink_message *message,
                Manager *m,
                int *message_iterator,
                DevlinkMatch *match,
                DevlinkMatchSet *matchset);
int devlink_match_genl_append(
                sd_netlink_message *message,
                const DevlinkMatch *match,
                DevlinkMatchSet matchset);
