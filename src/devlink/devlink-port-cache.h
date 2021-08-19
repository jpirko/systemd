/* SPDX-License-Identifier: LGPL-2.1+ */
#pragma once

#include "devlinkd-manager.h"

int devlink_port_cache_update(
                Manager *m,
                const char *bus_name,
                const char *dev_name,
                uint32_t index,
                const char *ifname,
                bool split);

int devlink_port_cache_query(
                Manager *m,
                const char *bus_name,
                const char *dev_name,
                uint32_t index,
                char **ifname,
                bool *split);
