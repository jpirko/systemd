/* SPDX-License-Identifier: LGPL-2.1-or-later */

#include <linux/devlink.h>

#include "alloc-util.h"
#include "conf-parser.h"
#include "netlink-util.h"
#include "parse-util.h"

#include "devlink.h"
#include "devlink-port-cache.h"

static int devlink_port_cache_genl_cmd_new_msg_process(
                Devlink *devlink,
                DevlinkKey *lookup_key,
                sd_netlink_message *message) {
        DevlinkPortCache *port_cache = DEVLINK_PORT_CACHE(devlink);
        int r;

        r = sd_netlink_message_read_u32(message, DEVLINK_ATTR_PORT_NETDEV_IFINDEX, &port_cache->ifindex);
        if (r < 0)
                return r;

        return DEVLINK_MONITOR_COMMAND_RETVAL_OK;
}

static int devlink_port_cache_genl_cmd_del_msg_process(
                Devlink *devlink,
                DevlinkKey *lookup_key,
                sd_netlink_message *message) {
        return DEVLINK_MONITOR_COMMAND_RETVAL_DELETE;
}

static const DevlinkMatchSet devlink_port_matchsets[] = {
        DEVLINK_MATCH_BIT_DEV | DEVLINK_MATCH_BIT_PORT_INDEX,
        0,
};

static const DevlinkMonitorCommand devlink_port_cache_commands[] = {
        { DEVLINK_CMD_PORT_NEW, devlink_port_cache_genl_cmd_new_msg_process },
        { DEVLINK_CMD_PORT_DEL, devlink_port_cache_genl_cmd_del_msg_process },
};

const DevlinkVTable devlink_port_cache_vtable = {
        .object_size = sizeof(DevlinkPortCache),
        .matchsets = devlink_port_cache_matchsets,
        .alloc_on_demand = true;
        .genl_monitor_cmds = devlink_port_cache_commands,
        .genl_monitor_cmds_count = ELEMENTSOF(devlink_port_cache_commands),
};

int devlink_port_cache_query(Manager *m, DevlinkMatch *match, uint32_t *ifindex) {
        DevlinkPortCache *port_cache;
        Devlink *devlink;
        DevlinkKey key;
        int r;

        devlink_key_init(&key, DEVLINK_KIND_PORT_CACHE);
        devlink_key_copy_from_match(&key, match, DEVLINK_MATCH_BIT_DEV | DEVLINK_MATCH_BIT_PORT_INDEX);

        devlink = devlink_get(m, key);
        if (!devlink)
                return -ENOENT;

        port_cache = DEVLINK_PORT_CACHE(devlink);
        *ifindex = port_cache->ifindex;

        return 0;
}
