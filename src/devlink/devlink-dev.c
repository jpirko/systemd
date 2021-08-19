/* SPDX-License-Identifier: LGPL-2.1-or-later */

#include "alloc-util.h"
#include "conf-parser.h"
#include "devlink-dev.h"
#include "devlink-util.h"
#include "netlink-util.h"

DEFINE_CONFIG_PARSE_ENUM(config_parse_devlink_dev_eswitch_mode, devlink_dev_eswitch_mode, DevlinkDevESwitchMode, "Failed to parse devlink dev E-Switch mode");
DEFINE_CONFIG_PARSE_ENUM(config_parse_devlink_dev_reload_action, devlink_dev_reload_action, DevlinkDevReloadAction, "Failed to parse devlink dev reload action");

static void devlink_dev_init(Devlink *devlink) {
        DevlinkDev *dev = DEVLINK_DEV(devlink);

        dev->eswitch_mode = _DEVLINK_DEV_ESWITCH_MODE_INVALID;
        dev->reload_action = _DEVLINK_DEV_RELOAD_ACTION_INVALID;
}

static int devlink_dev_genl_set_callback(
                sd_netlink *genl,
                sd_netlink_message *message,
                Devlink *devlink) {
        int r;

        assert(devlink);
        assert(message);

        r = sd_netlink_message_get_errno(message);
        if (r < 0) {
                log_devlink_warning_errno(devlink, r, "Dev could not be set: %m");
                return 1;
        }

        log_devlink_debug(devlink, "Dev set success");

        return 1;
}

static int devlink_dev_genl_msg_process(
                Devlink *devlink,
                DevlinkKey *lookup_key,
                sd_netlink_message *message,
                int message_iterator) {
        _cleanup_(sd_netlink_message_unrefp) sd_netlink_message *req = NULL;
        DevlinkDev *dev = DEVLINK_DEV(devlink);
        int r;

        if (dev->eswitch_mode != _DEVLINK_DEV_ESWITCH_MODE_INVALID) {
                r = sd_genl_message_new(devlink->manager->genl, DEVLINK_GENL_NAME, DEVLINK_CMD_SET, &req);
                if (r < 0)
                        return log_devlink_debug_errno(devlink, r, "Failed to create netlink message: %m");;
                r = devlink_key_genl_append(req, lookup_key);
                if (r < 0)
                        return r;
                r = sd_netlink_message_append_u32(req, DEVLINK_ATTR_ESWITCH_MODE, dev->eswitch_mode);
                if (r < 0)
                        return log_devlink_debug_errno(devlink, r, "Failed to append eswitch mode to netlink message: %m");;
                r = netlink_call_async(devlink->manager->genl, NULL, req, devlink_dev_genl_set_callback,
                                       devlink_destroy_callback, devlink);
                if (r < 0)
                        return log_devlink_debug_errno(devlink, r, "Could not send dev set message: %m");
        }
        if (dev->reload_action != _DEVLINK_DEV_RELOAD_ACTION_INVALID) {
                r = sd_genl_message_new(devlink->manager->genl, DEVLINK_GENL_NAME, DEVLINK_CMD_RELOAD, &req);
                if (r < 0)
                        return log_devlink_debug_errno(devlink, r, "Failed to create netlink message: %m");;
                r = devlink_key_genl_append(req, lookup_key);
                if (r < 0)
                        return r;
                r = sd_netlink_message_append_u32(req, DEVLINK_ATTR_RELOAD_ACTION, dev->reload_action);
                if (r < 0)
                        return log_devlink_debug_errno(devlink, r, "Failed to append reload action to netlink message: %m");;
                r = netlink_call_async(devlink->manager->genl, NULL, req, devlink_dev_genl_set_callback,
                                       devlink_destroy_callback, devlink);
                if (r < 0)
                        return log_devlink_debug_errno(devlink, r, "Could not send dev set message: %m");
        }
        return 0;
}

static const DevlinkMatchSet devlink_dev_matchsets[] = {
        DEVLINK_MATCH_BIT_DEV,
        0,
};

const DevlinkVTable devlink_dev_vtable = {
        .object_size = sizeof(DevlinkDev),
        .sections = DEVLINK_COMMON_SECTIONS "Eswitch\0Reload\0",
        .matchsets = devlink_dev_matchsets,
        .init = devlink_dev_init,
        .genl_monitor_cmd = DEVLINK_CMD_NEW,
        .genl_enumerate_cmd = DEVLINK_CMD_GET,
        .genl_msg_process = devlink_dev_genl_msg_process,
};
