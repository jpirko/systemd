/* SPDX-License-Identifier: LGPL-2.1-or-later */

#include "alloc-util.h"
#include "conf-parser.h"
#include "devlink-util.h"
#include "hash-funcs.h"
#include "log.h"
#include "macro.h"
#include "netlink-util.h"
#include "siphash24.h"

#include "devlink-match.h"

DEFINE_CONFIG_PARSE_ENUM(config_parse_devlink_param_cmode, devlink_param_cmode, DevlinkParamCMode, "Failed to parse devlink param cmode");

static void devlink_match_param_init(DevlinkMatch *match) {
        DevlinkMatchParam *param = &match->param;

        param->cmode = _DEVLINK_PARAM_CMODE_INVALID;
}

static void devlink_match_param_free(DevlinkMatch *match) {
        DevlinkMatchParam *param = &match->param;

        param->name = mfree(param->name);
}

static bool devlink_match_param_check(const DevlinkMatch *match) {
        const DevlinkMatchParam *param = &match->param;

        if (!param->name) {
                log_debug("Devlink param match name not configured.");
                return false;
        }
        if (param->cmode == _DEVLINK_PARAM_CMODE_INVALID) {
                log_debug("Devlink param match cmode not configured.");
                return false;
        }
        return true;
}

static void devlink_match_param_log_prefix(char **buf, int *len, const DevlinkMatch *match) {
        const DevlinkMatchParam *param = &match->param;

        BUFFER_APPEND(*buf, *len, "name %s cmode %s", param->name, devlink_param_cmode_to_string(param->cmode));
}

static void devlink_match_param_hash_func(const DevlinkMatch *match, struct siphash *state) {
        const DevlinkMatchParam *param = &match->param;

        string_hash_func(param->name, state);
        siphash24_compress(&param->cmode, sizeof(param->cmode), state);
}

static int devlink_match_param_compare_func(const DevlinkMatch *x, const DevlinkMatch *y) {
        const DevlinkMatchParam *xparam = &x->param;
        const DevlinkMatchParam *yparam = &y->param;
        int d;

        d = strcmp(xparam->name, yparam->name);
        if (d)
                return d;
        return CMP(xparam->cmode, yparam->cmode);
}

static int devlink_match_param_cmode_enum_read(sd_netlink_message *message, DevlinkMatchParam *param) {
        uint8_t cmode;
        int r;

        r = sd_netlink_message_read_u8(message, DEVLINK_ATTR_PARAM_VALUE_CMODE, &cmode);
        if (r < 0)
                return log_debug_errno(r, "devlink netlink: Message without valid cmode value: %m");
        if (cmode >= _DEVLINK_PARAM_CMODE_MAX)
                return log_debug_errno(r, "devlink netlink: Message without valid cmode value: %m");
        param->cmode = cmode;
        return 0;
}

static int devlink_match_param_genl_read(
                sd_netlink_message *message,
                Manager *m,
                int *message_iterator,
                DevlinkMatch *match) {
        DevlinkMatchParam *param = &match->param;
        uint16_t list_size;
        int r;

        (*message_iterator)++; /* We iterate array which starts from index 1 */

        r = sd_netlink_message_enter_container(message, DEVLINK_ATTR_PARAM);
        if (r < 0)
                return log_debug_errno(r, "devlink netlink: Message without param nest: %m");

        r = sd_netlink_message_read_string_strdup(message, DEVLINK_ATTR_PARAM_NAME, &param->name);
        if (r < 0)
                return log_debug_errno(r, "devlink netlink: Message without valid param name: %m");

        r = sd_netlink_message_enter_list(message, DEVLINK_ATTR_PARAM_VALUES_LIST, DEVLINK_ATTR_PARAM_VALUE);
        if (r < 0)
                return log_debug_errno(r, "devlink netlink: Message without valid values list nest: %m");

        r = sd_netlink_message_enter_array(message, *message_iterator);
        if (r < 0)
                return log_debug_errno(r, "devlink netlink: Message without valid values list item nest: %m");

        r = devlink_match_param_cmode_enum_read(message, param);
        if (r < 0)
                return r;

        (void) sd_netlink_message_exit_container(message);

        (void) sd_netlink_message_get_max_attribute(message, &list_size);
        if (*message_iterator >= list_size)
                *message_iterator = 0; /* Indicate the caller there is no other match. */

        (void) sd_netlink_message_exit_container(message);

        return 0;
}

static int devlink_match_param_genl_append(sd_netlink_message *message, const DevlinkMatch *match) {
        const DevlinkMatchParam *param = &match->param;
        int r;

        assert(param->name);
        assert(param->cmode != _DEVLINK_PARAM_CMODE_INVALID);

        r = sd_netlink_message_append_string(message, DEVLINK_ATTR_PARAM_NAME, param->name);
        if (r < 0)
                return log_debug_errno(r, "Failed to append param name to netlink message: %m");;

        r = sd_netlink_message_append_u8(message, DEVLINK_ATTR_PARAM_VALUE_CMODE, param->cmode);
        if (r < 0)
                return log_debug_errno(r, "Failed to append param cmode to netlink message: %m");;
        return 0;
}

const DevlinkMatchVTable devlink_match_param_vtable = {
        .init = devlink_match_param_init,
        .free = devlink_match_param_free,
        .check = devlink_match_param_check,
        .log_prefix = devlink_match_param_log_prefix,
        .hash_func = devlink_match_param_hash_func,
        .compare_func = devlink_match_param_compare_func,
        .genl_read = devlink_match_param_genl_read,
        .genl_append = devlink_match_param_genl_append,
};
