/* SPDX-License-Identifier: LGPL-2.1-or-later */

#include "alloc-util.h"
#include "devlink-util.h"
#include "hash-funcs.h"
#include "log.h"
#include "macro.h"
#include "netlink-util.h"
#include "siphash24.h"

#include "devlink-match.h"

int config_parse_devlink_dev_handle(
                const char *unit,
                const char *filename,
                unsigned line,
                const char *section,
                unsigned section_line,
                const char *lvalue,
                int ltype,
                const char *rvalue,
                void *data,
                void *userdata) {
        DevlinkMatchDev *dev = data;
        char *bus_name, *dev_name;
        const char *slash;

        assert(filename);
        assert(lvalue);
        assert(rvalue);
        assert(data);

        slash = strchr(rvalue, '/');
        if (!slash) {
                log_syntax(unit, LOG_WARNING, filename, line, 0, "Failed to parse devlink handle - does not contain '/', ignoring assignment: %s", rvalue);
                return 0;
        }
        bus_name = strndup(rvalue, slash - rvalue);
        if (!bus_name)
                return log_oom();
        dev_name = strdup(slash + 1);
        if (!dev_name) {
                free(bus_name);
                return log_oom();
        }

        if (!strlen(bus_name) ||
            !strlen(dev_name)) {
                free(bus_name);
                free(dev_name);
                log_syntax(unit, LOG_WARNING, filename, line, 0, "Failed to parse devlink handle, ignoring assignment: %s", rvalue);
                return 0;
        }

        free_and_replace(dev->bus_name, bus_name);
        free_and_replace(dev->dev_name, dev_name);

        return 0;
}

static void devlink_match_dev_free(DevlinkMatch *match) {
        DevlinkMatchDev *dev = &match->dev;

        dev->bus_name = mfree(dev->bus_name);
        dev->dev_name = mfree(dev->dev_name);
}

static bool devlink_match_dev_check(const DevlinkMatch *match) {
        const DevlinkMatchDev *dev = &match->dev;

        if (!dev->bus_name) {
                log_debug("Devlink dev match bus_name not configured.");
                return false;
        }
        if (!dev->dev_name) {
                log_debug("Devlink dev match dev_name not configured.");
                return false;
        }
        return true;
}

static void devlink_match_dev_log_prefix(char **buf, int *len, const DevlinkMatch *match) {
        const DevlinkMatchDev *dev = &match->dev;

        BUFFER_APPEND(*buf, *len, "%s/%s", dev->bus_name, dev->dev_name);
}

static void devlink_match_dev_hash_func(const DevlinkMatch *match, struct siphash *state) {
        const DevlinkMatchDev *dev = &match->dev;

        assert(dev->bus_name);
        assert(dev->dev_name);

        string_hash_func(dev->bus_name, state);
        string_hash_func(dev->dev_name, state);
}

static int devlink_match_dev_compare_func(const DevlinkMatch *x, const DevlinkMatch *y) {
        const DevlinkMatchDev *xdev = &x->dev;
        const DevlinkMatchDev *ydev = &y->dev;
        int d;

        d = strcmp(xdev->bus_name, ydev->bus_name);
        if (d)
                return d;

        return strcmp(xdev->dev_name, ydev->dev_name);
}

static int devlink_match_dev_genl_read(
                sd_netlink_message *message,
                Manager *m,
                int *message_iterator,
                DevlinkMatch *match) {
        DevlinkMatchDev *dev = &match->dev;
        int r;

        assert(!dev->bus_name);
        assert(!dev->dev_name);

        r = sd_netlink_message_read_string_strdup(message, DEVLINK_ATTR_BUS_NAME, &dev->bus_name);
        if (r < 0)
                return log_debug_errno(r, "devlink netlink: Message without valid bus name: %m");

        r = sd_netlink_message_read_string_strdup(message, DEVLINK_ATTR_DEV_NAME, &dev->dev_name);
        if (r < 0)
                return log_debug_errno(r, "devlink netlink: Message without valid device name: %m");
        return 0;
}

static int devlink_match_dev_genl_append(sd_netlink_message *message, const DevlinkMatch *match) {
        const DevlinkMatchDev *dev = &match->dev;
        int r;

        assert(dev->bus_name);
        assert(dev->dev_name);

        r = sd_netlink_message_append_string(message, DEVLINK_ATTR_BUS_NAME, dev->bus_name);
        if (r < 0)
                return log_debug_errno(r, "Failed to append dev bus_name to netlink message: %m");

        r = sd_netlink_message_append_string(message, DEVLINK_ATTR_DEV_NAME, dev->dev_name);
        if (r < 0)
                return log_debug_errno(r, "Failed to append dev dev_name to netlink message: %m");

        return 0;
}

const DevlinkMatchVTable devlink_match_dev_vtable = {
        .free = devlink_match_dev_free,
        .check = devlink_match_dev_check,
        .log_prefix = devlink_match_dev_log_prefix,
        .hash_func = devlink_match_dev_hash_func,
        .compare_func = devlink_match_dev_compare_func,
        .genl_read = devlink_match_dev_genl_read,
        .genl_append = devlink_match_dev_genl_append,
};
