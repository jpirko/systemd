/* SPDX-License-Identifier: LGPL-2.1-or-later */

#include <linux/devlink.h>

#include "alloc-util.h"
#include "conf-parser.h"
#include "netlink-util.h"
#include "parse-util.h"

#include "devlink.h"
#include "devlink-port.h"

static void devlink_port_init(Devlink *devlink) {
        DevlinkPort *port = DEVLINK_PORT(devlink);

        port->split_count = _DEVLINK_PORT_SPLIT_COUNT_INVALID;
}

static int devlink_port_genl_split_callback(
                sd_netlink *genl,
                sd_netlink_message *message,
                Devlink *devlink) {
        int r;

        assert(devlink);
        assert(message);

        r = sd_netlink_message_get_errno(message);
        if (r < 0) {
                log_devlink_warning_errno(devlink, r, "Port could not be split: %m");
                return 1;
        }

        log_devlink_debug(devlink, "Port split success");

        return 1;
}

static int devlink_port_genl_msg_process(
                Devlink *devlink,
                DevlinkKey *lookup_key,
                sd_netlink_message *message,
                int message_iterator) {
        _cleanup_(sd_netlink_message_unrefp) sd_netlink_message *req = NULL;
        DevlinkPort *port = DEVLINK_PORT(devlink);
        int r;

        if (port->split_count != _DEVLINK_PORT_SPLIT_COUNT_INVALID) {
                r = sd_genl_message_new(devlink->manager->genl, DEVLINK_GENL_NAME, DEVLINK_CMD_PORT_SPLIT, &req);
                if (r < 0)
                        return log_devlink_debug_errno(devlink, r, "Failed to create netlink message: %m");;
                r = devlink_key_genl_append(req, lookup_key);
                if (r < 0)
                        return r;
                r = sd_netlink_message_append_u32(req, DEVLINK_ATTR_PORT_SPLIT_COUNT, port->split_count);
                if (r < 0)
                        return log_devlink_debug_errno(devlink, r, "Failed to append split count to netlink message: %m");;
                r = netlink_call_async(devlink->manager->genl, NULL, req, devlink_port_genl_split_callback,
                                       devlink_destroy_callback, devlink);
                if (r < 0)
                        return log_devlink_debug_errno(devlink, r, "Could not send port split message: %m");
        }
        return 0;
}

static const DevlinkMatchSet devlink_port_matchsets[] = {
        DEVLINK_MATCH_BIT_PORT_IFNAME,
        DEVLINK_MATCH_BIT_DEV | DEVLINK_MATCH_BIT_PORT_INDEX,
        0,
};

const DevlinkVTable devlink_port_vtable = {
        .object_size = sizeof(DevlinkPort),
        .sections = DEVLINK_COMMON_SECTIONS "PortMatch\0Split\0",
        .matchsets = devlink_port_matchsets,
        .init = devlink_port_init,
        .genl_monitor_cmd = DEVLINK_CMD_PORT_NEW,
        .genl_enumerate_cmd = DEVLINK_CMD_PORT_GET,
        .genl_msg_process = devlink_port_genl_msg_process,
};
