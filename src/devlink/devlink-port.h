/* SPDX-License-Identifier: LGPL-2.1-or-later */
#pragma once

#include "devlink.h"

typedef struct DevlinkPort {
        Devlink meta;
        bool splittable_valid;
        uint32_t split_count;
} DevlinkPort;

#define _DEVLINK_PORT_SPLIT_COUNT_INVALID UINT32_MAX

typedef struct DevlinkPortMatch {
        uint32_t index;
        bool index_valid;
        char *ifname;
        bool split;
} DevlinkPortMatch;

#define _DEVLINK_PORT_INDEX_INVALID UINT32_MAX

DEFINE_DEVLINK_CAST(PORT, DevlinkPort);

extern int devlink_port_genl_index_read(
                sd_netlink_message *message,
                Devlink *devlink);
extern int devlink_port_genl_index_append(
                sd_netlink_message *message,
                const Devlink *devlink);

extern const DevlinkVTable devlink_port_vtable;

CONFIG_PARSER_PROTOTYPE(config_parse_devlink_port_ifname);
CONFIG_PARSER_PROTOTYPE(config_parse_devlink_port_split);
