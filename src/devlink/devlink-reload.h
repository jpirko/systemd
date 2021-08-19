/* SPDX-License-Identifier: LGPL-2.1-or-later */
#pragma once

#include "devlinkd-manager.h"
#include "devlink-key.h"

int devlink_reload_queue(Manager *m, DevlinkKey *orig_key);
void devlink_reload_cleanup(Manager *m, DevlinkKey *orig_key);
