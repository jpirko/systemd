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
#include "devlink-match-health-reporter.h"

static void devlink_match_health_reporter_free(DevlinkMatch *match) {
        DevlinkMatchHealthReporter *health_reporter = &match->health_reporter;

        health_reporter->name = mfree(health_reporter->name);
}

static bool devlink_match_health_reporter_check(const DevlinkMatch *match) {
        const DevlinkMatchHealthReporter *health_reporter = &match->health_reporter;

        if (!health_reporter->name) {
                log_debug("Match name not configured.");
                return false;
        }
        return true;
}

static void devlink_match_health_reporter_log_prefix(char **buf, int *len, const DevlinkMatch *match) {
        const DevlinkMatchHealthReporter *health_reporter = &match->health_reporter;

        BUFFER_APPEND(*buf, *len, "name %s", health_reporter->name);
}

static void devlink_match_health_reporter_hash_func(const DevlinkMatch *match, struct siphash *state) {
        const DevlinkMatchHealthReporter *health_reporter = &match->health_reporter;

        assert(health_reporter->name);

        string_hash_func(health_reporter->name, state);
}

static int devlink_match_health_reporter_compare_func(const DevlinkMatch *x, const DevlinkMatch *y) {
        const DevlinkMatchHealthReporter *xhealth_reporter = &x->health_reporter;
        const DevlinkMatchHealthReporter *yhealth_reporter = &y->health_reporter;

        return strcmp(xhealth_reporter->name, yhealth_reporter->name);
}

static int devlink_match_health_reporter_genl_read(
                sd_netlink_message *message,
                Manager *m,
                DevlinkMatch *match) {
        DevlinkMatchHealthReporter *health_reporter = &match->health_reporter;
        int r;

        assert(!health_reporter->name);

        r = sd_netlink_message_enter_container(message, DEVLINK_ATTR_HEALTH_REPORTER);
        if (r < 0)
                return r;

        r = sd_netlink_message_read_string_strdup(message, DEVLINK_ATTR_HEALTH_REPORTER_NAME, &health_reporter->name);
        if (r < 0)
                return r;

        (void) sd_netlink_message_exit_container(message);

        return 0;
}

static int devlink_match_health_reporter_genl_append(sd_netlink_message *message, const DevlinkMatch *match) {
        const DevlinkMatchHealthReporter *health_reporter = &match->health_reporter;
        int r;

        assert(health_reporter->name);

        r = sd_netlink_message_append_string(message, DEVLINK_ATTR_HEALTH_REPORTER_NAME, health_reporter->name);
        if (r < 0)
                return log_debug_errno(r, "Failed to append health reporter name to netlink message: %m");;

        return 0;
}

const DevlinkMatchVTable devlink_match_health_reporter_vtable = {
        .free = devlink_match_health_reporter_free,
        .check = devlink_match_health_reporter_check,
        .log_prefix = devlink_match_health_reporter_log_prefix,
        .hash_func = devlink_match_health_reporter_hash_func,
        .compare_func = devlink_match_health_reporter_compare_func,
        .genl_read = devlink_match_health_reporter_genl_read,
        .genl_append = devlink_match_health_reporter_genl_append,
};
