/* SPDX-License-Identifier: LGPL-2.1-or-later */
#pragma once

#include "devlink.h"

typedef struct DevlinkPortCache {
        Devlink meta;
        uint32_t ifindex;
} DevlinkPortCache;

DEFINE_DEVLINK_CAST(PORT_CACHE, DevlinkPortCache);

extern const DevlinkVTable devlink_port_cache_vtable;

int devlink_port_cache_query(Manager *m, DevlinkMatch *match, uint32_t *ifindex);
