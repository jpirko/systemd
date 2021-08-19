/* SPDX-License-Identifier: LGPL-2.1-or-later */

#include <linux/devlink.h>

#include "alloc-util.h"
#include "conf-parser.h"
#include "netlink-types.h"
#include "netlink-util.h"
#include "parse-util.h"

#include "devlink.h"
#include "devlink-param.h"

static void devlink_param_done(Devlink *devlink) {
        DevlinkParam *param = DEVLINK_PARAM(devlink);

        free(param->value);
}

static int devlink_param_config_verify(const Devlink *devlink, const char *filename) {
        const DevlinkParam *param = DEVLINK_CONST_PARAM(devlink);

        if (!param->value)
                return log_devlink_warning_errno(devlink, SYNTHETIC_ERRNO(EINVAL),
                                                 "%s: Param value not specified. Ignoring.",
                                                 filename);
        return 0;
}

static int devlink_param_genl_value_set_callback(
                sd_netlink *gent,
                sd_netlink_message *message,
                Devlink *devlink) {
        int r;

        assert(devlink);
        assert(message);

        r = sd_netlink_message_get_errno(message);
        if (r < 0) {
                log_devlink_warning_errno(devlink, r, "Failed to set value: %m");
                return 1;
        }

        log_devlink_debug(devlink, "Value set success");

        return 1;
}

static int devlink_param_genl_process_u8(
                Devlink *devlink,
                sd_netlink_message *message,
                sd_netlink_message *req,
                bool *same_value) {
        DevlinkParam *param = DEVLINK_PARAM(devlink);
        uint8_t val, current_val;
        int r;

        r = safe_atou8(param->value, &val);
        if (r < 0)
                return log_devlink_debug_errno(devlink, r, "Failed to parse u8 param value: %m");

        r = sd_netlink_message_read_u8(message, DEVLINK_ATTR_PARAM_VALUE_DATA, &current_val);
        if (r < 0)
                return log_devlink_debug_errno(devlink, r, "devlink netlink: Message without param value: %m");

        if (current_val == val) {
                *same_value = true;
                return 0;
        }

        r = sd_netlink_message_append_u8(req, DEVLINK_ATTR_PARAM_VALUE_DATA, val);
        if (r < 0)
                return log_devlink_debug_errno(devlink, r, "Failed to append param type to netlink message: %m");;

        return 0;
}

static int devlink_param_genl_process_u16(
                Devlink *devlink,
                sd_netlink_message *message,
                sd_netlink_message *req,
                bool *same_value) {
        DevlinkParam *param = DEVLINK_PARAM(devlink);
        uint16_t val, current_val;
        int r;

        r = safe_atou16(param->value, &val);
        if (r < 0)
                return log_devlink_debug_errno(devlink, r, "Failed to parse u16 param value: %m");

        r = sd_netlink_message_read_u16(message, DEVLINK_ATTR_PARAM_VALUE_DATA, &current_val);
        if (r < 0)
                return log_devlink_debug_errno(devlink, r, "devlink netlink: Message without param value: %m");

        if (current_val == val) {
                *same_value = true;
                return 0;
        }

        r = sd_netlink_message_append_u16(req, DEVLINK_ATTR_PARAM_VALUE_DATA, val);
        if (r < 0)
                return log_devlink_debug_errno(devlink, r, "Failed to append param type to netlink message: %m");;

        return 0;
}

static int devlink_param_genl_process_u32(
                Devlink *devlink,
                sd_netlink_message *message,
                sd_netlink_message *req,
                bool *same_value) {
        DevlinkParam *param = DEVLINK_PARAM(devlink);
        uint32_t val, current_val;
        int r;

        r = safe_atou32(param->value, &val);
        if (r < 0)
                return log_devlink_debug_errno(devlink, r, "Failed to parse u32 param value: %m");

        r = sd_netlink_message_read_u32(message, DEVLINK_ATTR_PARAM_VALUE_DATA, &current_val);
        if (r < 0)
                return log_devlink_debug_errno(devlink, r, "devlink netlink: Message without param value: %m");

        if (current_val == val) {
                *same_value = true;
                return 0;
        }

        r = sd_netlink_message_append_u32(req, DEVLINK_ATTR_PARAM_VALUE_DATA, val);
        if (r < 0)
                return log_devlink_debug_errno(devlink, r, "Failed to append param type to netlink message: %m");;

        return 0;
}

static int devlink_param_genl_process_string(
                Devlink *devlink,
                sd_netlink_message *message,
                sd_netlink_message *req,
                bool *same_value) {
        DevlinkParam *param = DEVLINK_PARAM(devlink);
        const char *current_val;
        int r;

        r = sd_netlink_message_read_string(message, DEVLINK_ATTR_PARAM_VALUE_DATA, &current_val);
        if (r < 0)
                return log_devlink_debug_errno(devlink, r, "devlink netlink: Message without param value: %m");

        if (!strcmp(current_val, param->value)) {
                *same_value = true;
                return 0;
        }

        r = sd_netlink_message_append_string(req, DEVLINK_ATTR_PARAM_VALUE_DATA, param->value);
        if (r < 0)
                return log_devlink_debug_errno(devlink, r, "Failed to append param type to netlink message: %m");;

        return 0;
}

static int devlink_param_genl_process_bool(
                Devlink *devlink,
                sd_netlink_message *message,
                sd_netlink_message *req,
                bool *same_value) {
        DevlinkParam *param = DEVLINK_PARAM(devlink);
        bool val, current_val;
        int r;

        r = parse_boolean(param->value);
        if (r < 0)
                return log_devlink_debug_errno(devlink, r, "Failed to parse boolean param value: %m");

        val = r;

        current_val = sd_netlink_message_has_flag(message, DEVLINK_ATTR_PARAM_VALUE_DATA);

        if (current_val == val) {
                *same_value = true;
                return 0;
        } else if (!val) {
                return 0;
        }

        r = sd_netlink_message_append_flag(req, DEVLINK_ATTR_PARAM_VALUE_DATA);
        if (r < 0)
                return log_devlink_debug_errno(devlink, r, "Failed to append param type to netlink message: %m");;

        return 0;
}

static int devlink_param_genl_process_append_type_value(
                Devlink *devlink,
                sd_netlink_message *message,
                int message_iterator,
                sd_netlink_message *req,
                bool *same_value) {
        uint8_t type;
        int r;

        r = sd_netlink_message_enter_container(message, DEVLINK_ATTR_PARAM);
        if (r < 0)
                return log_devlink_debug_errno(devlink, r, "devlink netlink: Message without param nest: %m");

        r = sd_netlink_message_read_u8(message, DEVLINK_ATTR_PARAM_TYPE, &type);
        if (r < 0)
                return log_devlink_debug_errno(devlink, r, "devlink netlink: Message without param type value: %m");

        r = sd_netlink_message_enter_list(message, DEVLINK_ATTR_PARAM_VALUES_LIST, DEVLINK_ATTR_PARAM_VALUE);
        if (r < 0)
                return log_devlink_debug_errno(devlink, r, "devlink netlink: Message without param values list nest: %m");

        r = sd_netlink_message_enter_container(message, message_iterator + 1);
        if (r < 0)
                return log_devlink_debug_errno(devlink, r, "devlink netlink: Message without param values list item nest: %m");

        (void) sd_netlink_message_exit_container(message);
        (void) sd_netlink_message_exit_container(message);

        r = sd_netlink_message_append_u8(req, DEVLINK_ATTR_PARAM_TYPE, type);
        if (r < 0)
                return log_devlink_debug_errno(devlink, r, "Failed to append param type to netlink message: %m");;

        switch (type) {
        case NETLINK_TYPE_U8:
                return devlink_param_genl_process_u8(devlink, message, req, same_value);
        case NETLINK_TYPE_U16:
                return devlink_param_genl_process_u16(devlink, message, req, same_value);
        case NETLINK_TYPE_U32:
                return devlink_param_genl_process_u32(devlink, message, req, same_value);
        case NETLINK_TYPE_STRING:
                return devlink_param_genl_process_string(devlink, message, req, same_value);
        case NETLINK_TYPE_FLAG:
                return devlink_param_genl_process_bool(devlink, message, req, same_value);
        default:
                return log_devlink_debug_errno(devlink, r, "devlink netlink: Unknown param type: %m");;
        }
}

static int devlink_param_genl_msg_process(
                Devlink *devlink,
                DevlinkKey *lookup_key,
                sd_netlink_message *message,
                int message_iterator) {
        _cleanup_(sd_netlink_message_unrefp) sd_netlink_message *req = NULL;
        bool same_value = false;
        int r;

        r = sd_genl_message_new(devlink->manager->genl, DEVLINK_GENL_NAME, DEVLINK_CMD_PARAM_NEW, &req);
        if (r < 0)
                return log_devlink_debug_errno(devlink, r, "Failed to create netlink message: %m");;

        r = devlink_key_genl_append(req, lookup_key);
        if (r < 0)
                return r;

        r = devlink_param_genl_process_append_type_value(devlink, message, message_iterator, req, &same_value);
        if (r < 0)
                return r;

        /* The current value is the same as the desired one, just quit. */
        if (same_value)
                return 0;

        r = netlink_call_async(devlink->manager->genl, NULL, req, devlink_param_genl_value_set_callback,
                               devlink_destroy_callback, devlink);
        if (r < 0)
                return log_devlink_debug_errno(devlink, r, "Could not param value set message: %m");

        return 0;
}

static const DevlinkMatchSet devlink_param_matchsets[] = {
        DEVLINK_MATCH_BIT_PORT_CACHED_IFNAME | DEVLINK_MATCH_BIT_PARAM,
        DEVLINK_MATCH_BIT_DEV | DEVLINK_MATCH_BIT_PORT_INDEX | DEVLINK_MATCH_BIT_PARAM,
        DEVLINK_MATCH_BIT_DEV | DEVLINK_MATCH_BIT_PARAM,
        0,
};

const DevlinkVTable devlink_param_vtable = {
        .object_size = sizeof(DevlinkParam),
        .sections = DEVLINK_COMMON_SECTIONS "ParamMatch\0PortMatch\0Param\0",
        .matchsets = devlink_param_matchsets,
        .done = devlink_param_done,
        .config_verify = devlink_param_config_verify,
        .genl_monitor_cmd = DEVLINK_CMD_PARAM_NEW,
        .genl_enumerate_cmd = DEVLINK_CMD_PARAM_GET,
        .genl_msg_process = devlink_param_genl_msg_process,
};
