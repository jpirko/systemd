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
#include "devlink-match-param.h"

static void devlink_match_param_free(DevlinkMatch *match) {
        DevlinkMatchParam *param = &match->param;

        param->name = mfree(param->name);
}

static bool devlink_match_param_check(const DevlinkMatch *match) {
        const DevlinkMatchParam *param = &match->param;

        if (!param->name) {
                log_debug("Match name not configured.");
                return false;
        }
        return true;
}

static void devlink_match_param_log_prefix(char **buf, int *len, const DevlinkMatch *match) {
        const DevlinkMatchParam *param = &match->param;

        BUFFER_APPEND(*buf, *len, "name %s", param->name);
}

static void devlink_match_param_hash_func(const DevlinkMatch *match, struct siphash *state) {
        const DevlinkMatchParam *param = &match->param;

        assert(param->name);

        string_hash_func(param->name, state);
}

static int devlink_match_param_compare_func(const DevlinkMatch *x, const DevlinkMatch *y) {
        const DevlinkMatchParam *xparam = &x->param;
        const DevlinkMatchParam *yparam = &y->param;

        return strcmp(xparam->name, yparam->name);
}

static int devlink_match_param_genl_read(
                sd_netlink_message *message,
                Manager *m,
                int *message_iterator,
                DevlinkMatch *match) {
        DevlinkMatchParam *param = &match->param;
        int r;

        assert(!param->name);

        r = sd_netlink_message_enter_container(message, DEVLINK_ATTR_PARAM);
        if (r < 0)
                return r;

        r = sd_netlink_message_read_string_strdup(message, DEVLINK_ATTR_PARAM_NAME, &param->name);
        if (r < 0)
                return r;

        (void) sd_netlink_message_exit_container(message);

        return 0;
}

static int devlink_match_param_genl_append(sd_netlink_message *message, const DevlinkMatch *match) {
        const DevlinkMatchParam *param = &match->param;
        int r;

        assert(param->name);

        r = sd_netlink_message_append_string(message, DEVLINK_ATTR_PARAM_NAME, param->name);
        if (r < 0)
                return log_debug_errno(r, "Failed to append param name to netlink message: %m");;

        return 0;
}

const DevlinkMatchVTable devlink_match_param_vtable = {
        .free = devlink_match_param_free,
        .check = devlink_match_param_check,
        .log_prefix = devlink_match_param_log_prefix,
        .hash_func = devlink_match_param_hash_func,
        .compare_func = devlink_match_param_compare_func,
        .genl_read = devlink_match_param_genl_read,
        .genl_append = devlink_match_param_genl_append,
};
