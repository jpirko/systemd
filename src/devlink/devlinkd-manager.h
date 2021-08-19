/* SPDX-License-Identifier: LGPL-2.1+ */
#pragma once

#include "sd-bus.h"
#include "sd-device.h"
#include "sd-event.h"
#include "sd-id128.h"
#include "sd-netlink.h"
#include "sd-resolve.h"

#include "hashmap.h"
#include "list.h"
#include "time-util.h"

typedef struct Manager Manager;

struct Manager {
        sd_netlink *genl;
        sd_event *event;
        sd_event_source *periodic_enumeration_event_source;
        Hashmap *devlink_objs;
        Hashmap *match_port_cache;
        Hashmap *reload;
};

void manager_need_reload(Manager *m);
int manager_setup(Manager *m);
int manager_new(Manager **ret);
Manager* manager_free(Manager *m);

int manager_start(Manager *m);
int manager_load_config(Manager *m);
int manager_enumerate(Manager *m);

DEFINE_TRIVIAL_CLEANUP_FUNC(Manager *, manager_free);
