/* SPDX-License-Identifier: LGPL-2.1-or-later */

#include "alloc-util.h"
#include "hash-funcs.h"
#include "log.h"
#include "macro.h"
#include "netlink-util.h"
#include "siphash24.h"

#include "devlink-match.h"
#include "devlink-match-port.h"
#include "devlink-match-port-cache.h"

int config_parse_devlink_port_index(CONFIG_PARSER_ARGUMENTS) {
        DevlinkMatchPort *port = data;
        int r;

        r = config_parse_uint32(unit, filename, line, section, section_line, lvalue, ltype,
                                rvalue, &port->index, userdata);
        if (r < 0)
                return r;
        port->index_valid = true;
        return 0;
}

static bool devlink_match_port_index_check(const DevlinkMatch *match) {
        const DevlinkMatchPort *port = &match->port;

        if (!port->index_valid) {
                log_debug("Match index not configured.");
                return false;
        }
        return true;
}

static void devlink_match_port_index_log_prefix(char **buf, int *len, const DevlinkMatch *match) {
        const DevlinkMatchPort *port = &match->port;

        BUFFER_APPEND(*buf, *len, "port_index %u split %s", port->index, port->split ? "true" : "false");
}

static void devlink_match_port_index_hash_func(const DevlinkMatch *match, struct siphash *state) {
        const DevlinkMatchPort *port = &match->port;

        siphash24_compress(&port->index, sizeof(&port->index), state);
        siphash24_compress(&port->split, sizeof(&port->split), state);
}

static int devlink_match_port_index_compare_func(const DevlinkMatch *x, const DevlinkMatch *y) {
        const DevlinkMatchPort *xport = &x->port;
        const DevlinkMatchPort *yport = &y->port;
        int d;

        assert(xport->index_valid);
        assert(yport->index_valid);

        d = CMP(xport->index, yport->index);
        if (d)
                return d;
        return CMP(xport->split, yport->split);
}

static void devlink_match_port_index_copy_func(DevlinkMatch *dst, const DevlinkMatch *src) {
        DevlinkMatchPort *dstport = &dst->port;
        const DevlinkMatchPort *srcport = &src->port;

        assert(srcport->index_valid);

        dstport->index = srcport->index;
        dstport->index_valid = srcport->index_valid;
        dstport->split = srcport->split;
}

static int devlink_match_port_index_duplicate_func(DevlinkMatch *dst, const DevlinkMatch *src) {
        devlink_match_port_index_copy_func(dst, src);
        return 0;
}

static void devlink_port_split_genl_read(sd_netlink_message *message, DevlinkMatchPort *port) {
        uint32_t split_group;
        int r;

        r = sd_netlink_message_read_u32(message, DEVLINK_ATTR_PORT_SPLIT_GROUP, &split_group);
        if (!r)
                port->split = true;
}

static int devlink_match_port_index_genl_read(
                sd_netlink_message *message,
                Manager *m,
                int *message_iterator,
                DevlinkMatch *match) {
        DevlinkMatchPort *port = &match->port;
        int r;

        assert(!port->index_valid);

        r = sd_netlink_message_read_u32(message, DEVLINK_ATTR_PORT_INDEX, &port->index);
        if (r < 0)
                return r;
        port->index_valid = true;

        devlink_port_split_genl_read(message, port);

        return 0;
}

static int devlink_match_port_genl_append(sd_netlink_message *message, const DevlinkMatch *match) {
        const DevlinkMatchPort *port = &match->port;
        int r;

        assert(port->index_valid);

        r = sd_netlink_message_append_u32(message, DEVLINK_ATTR_PORT_INDEX, port->index);
        if (r < 0)
                return log_debug_errno(r, "Failed to append port index to netlink message: %m");
        return 0;
}

const DevlinkMatchVTable devlink_match_port_index_vtable = {
        .check = devlink_match_port_index_check,
        .log_prefix = devlink_match_port_index_log_prefix,
        .hash_func = devlink_match_port_index_hash_func,
        .compare_func = devlink_match_port_index_compare_func,
        .copy_func = devlink_match_port_index_copy_func,
        .duplicate_func = devlink_match_port_index_duplicate_func,
        .genl_read = devlink_match_port_index_genl_read,
        .genl_append = devlink_match_port_genl_append,
};

static void devlink_match_port_ifname_free(DevlinkMatch *match) {
        DevlinkMatchPort *port = &match->port;

        port->ifname = mfree(port->ifname);
}

static bool devlink_match_port_ifname_check(const DevlinkMatch *match) {
        const DevlinkMatchPort *port = &match->port;

        if (!port->ifname) {
                log_debug("Match ifname not configured.");
                return false;
        }
        return true;
}

static void devlink_match_port_ifname_log_prefix(char **buf, int *len, const DevlinkMatch *match) {
        const DevlinkMatchPort *port = &match->port;

        BUFFER_APPEND(*buf, *len, "ifname %s split %s", port->ifname, port->split ? "true" : "false");
}

static void devlink_match_port_ifname_hash_func(const DevlinkMatch *match, struct siphash *state) {
        const DevlinkMatchPort *port = &match->port;

        assert(port->ifname);

        string_hash_func(port->ifname, state);
        siphash24_compress(&port->split, sizeof(&port->split), state);
}

static int devlink_match_port_ifname_compare_func(const DevlinkMatch *x, const DevlinkMatch *y) {
        const DevlinkMatchPort *xport = &x->port;
        const DevlinkMatchPort *yport = &y->port;
        int d;

        assert(xport->ifname);
        assert(yport->ifname);

        d = strcmp(xport->ifname, yport->ifname);
        if (d)
                return d;
        return CMP(xport->split, yport->split);
}

static int devlink_match_port_ifname_genl_read(
                sd_netlink_message *message,
                Manager *m,
                int *message_iterator,
                DevlinkMatch *match) {
        DevlinkMatchPort *port = &match->port;
        uint32_t ifindex;
        int r;

        assert(!port->ifname);

        r = sd_netlink_message_read_u32(message, DEVLINK_ATTR_PORT_NETDEV_IFINDEX, &ifindex);
        if (r < 0)
                return r;

        r = sd_netlink_message_read_string_strdup(message, DEVLINK_ATTR_PORT_NETDEV_NAME, &port->ifname);
        if (r < 0)
                return r;

        devlink_port_split_genl_read(message, port);

        r = devlink_match_port_cache_update(m, match, ifindex, port->ifname, port->split);
        if (r < 0)
                return log_debug_errno(r, "Failed to update port cache: %m");

        return 0;
}

const DevlinkMatchVTable devlink_match_port_ifname_vtable = {
        .free = devlink_match_port_ifname_free,
        .check = devlink_match_port_ifname_check,
        .log_prefix = devlink_match_port_ifname_log_prefix,
        .hash_func = devlink_match_port_ifname_hash_func,
        .compare_func = devlink_match_port_ifname_compare_func,
        .genl_read = devlink_match_port_ifname_genl_read,
};

static int devlink_match_port_cached_ifname_genl_read(
                sd_netlink_message *message,
                Manager *m,
                int *message_iterator,
                DevlinkMatch *match) {
        DevlinkMatchPort *port = &match->port;
        DevlinkMatchDev *dev = &match->dev;

        if (port->ifname) {
                log_debug("Skipping cached port lookup, ifname is present.");
                return 0;
        }

        if (!dev->bus_name || !dev->dev_name || !port->index_valid)
                return -EINVAL;

        /* The message for "port cached" match does not contain ifname and
         * split info. Instead, query the port cache and obtain ifname and
         * split info from there.
         */

        return devlink_match_port_cache_query(m, match, &port->ifname, &port->split);
}

const DevlinkMatchVTable devlink_match_port_cached_ifname_vtable = {
        .free = devlink_match_port_ifname_free,
        .check = devlink_match_port_ifname_check,
        .log_prefix = devlink_match_port_ifname_log_prefix,
        .hash_func = devlink_match_port_ifname_hash_func,
        .compare_func = devlink_match_port_ifname_compare_func,
        .genl_read = devlink_match_port_cached_ifname_genl_read,
};
