/* SPDX-License-Identifier: LGPL-2.1+ */
#pragma once

#include "devlinkd-manager.h"
#include "devlink-match.h"

int devlink_match_port_cache_update(
                Manager *m,
                DevlinkMatch *match,
                const char *ifname,
                bool split);
void devlink_match_port_cache_remove(
                Manager *m,
                DevlinkMatch *match);
int devlink_match_port_cache_query(
                Manager *m,
                DevlinkMatch *match,
                char **ifname,
                bool *split);
