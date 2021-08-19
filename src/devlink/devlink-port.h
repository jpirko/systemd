/* SPDX-License-Identifier: LGPL-2.1-or-later */
#pragma once

#include "devlink.h"

typedef struct DevlinkPort {
        Devlink meta;
        uint32_t split_count;
} DevlinkPort;

#define _DEVLINK_PORT_SPLIT_COUNT_INVALID UINT32_MAX

DEFINE_DEVLINK_CAST(PORT, DevlinkPort);

extern int devlink_port_genl_index_read(
                sd_netlink_message *message,
                Devlink *devlink);
extern int devlink_port_genl_index_append(
                sd_netlink_message *message,
                const Devlink *devlink);

extern const DevlinkVTable devlink_port_vtable;
