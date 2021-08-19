/* SPDX-License-Identifier: LGPL-2.1-or-later */

#include "alloc-util.h"
#include "hash-funcs.h"
#include "log.h"
#include "macro.h"
#include "netlink-util.h"
#include "siphash24.h"

#include "devlink-match.h"
#include "devlink-port-cache.h"

#define _DEVLINK_PORT_INDEX_INVALID UINT32_MAX

static void devlink_match_port_index_init(DevlinkMatch *match) {
        DevlinkMatchPort *port = &match->port;

        port->index = _DEVLINK_PORT_INDEX_INVALID;
}

static bool devlink_match_port_index_check(const DevlinkMatch *match) {
        const DevlinkMatchPort *port = &match->port;

        if (port->index == _DEVLINK_PORT_INDEX_INVALID) {
                log_debug("Devlink port match index not configured.");
                return false;
        }
        return true;
}

static void devlink_match_port_index_log_prefix(char **buf, int *len, const DevlinkMatch *match) {
        const DevlinkMatchPort *port = &match->port;

        BUFFER_APPEND(*buf, *len, "index %u split %s", port->index, port->split ? "true" : "false");
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

        assert(xport->index != _DEVLINK_PORT_INDEX_INVALID);
        assert(yport->index != _DEVLINK_PORT_INDEX_INVALID);

        d = CMP(xport->index, yport->index);
        if (d)
                return d;
        return CMP(xport->split, yport->split);
}

static void devlink_port_split_genl_read(sd_netlink_message *message, DevlinkMatchPort *port) {
        uint32_t split_group;
        int r;

        r = sd_netlink_message_read_u32(message, DEVLINK_ATTR_PORT_SPLIT_GROUP, &split_group);
        if (r < 0)
                log_debug_errno(r, "devlink netlink: Message without valid port split group: %m");
        else
                port->split = true;
}

static int devlink_match_port_index_genl_read(
                sd_netlink_message *message,
                Manager *m,
                int *message_iterator,
                DevlinkMatch *match) {
        DevlinkMatchPort *port = &match->port;
        int r;

        assert(port->index == _DEVLINK_PORT_INDEX_INVALID);

        r = sd_netlink_message_read_u32(message, DEVLINK_ATTR_PORT_INDEX, &port->index);
        if (r < 0)
                return log_debug_errno(r, "devlink netlink: Message without valid port index: %m");
        devlink_port_split_genl_read(message, port);
        return 0;
}

static int devlink_match_port_genl_append(sd_netlink_message *message, const DevlinkMatch *match) {
        const DevlinkMatchPort *port = &match->port;
        int r;

        assert(port->index != _DEVLINK_PORT_INDEX_INVALID);

        r = sd_netlink_message_append_u32(message, DEVLINK_ATTR_PORT_INDEX, port->index);
        if (r < 0)
                return log_debug_errno(r, "Failed to append port index to netlink message: %m");
        return 0;
}

const DevlinkMatchVTable devlink_match_port_index_vtable = {
        .init = devlink_match_port_index_init,
        .check = devlink_match_port_index_check,
        .log_prefix = devlink_match_port_index_log_prefix,
        .hash_func = devlink_match_port_index_hash_func,
        .compare_func = devlink_match_port_index_compare_func,
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
                log_debug("Devlink port match ifname not configured.");
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
        DevlinkMatchDev *dev = &match->dev;
        int r;

        assert(!port->ifname);

        r = sd_netlink_message_read_string_strdup(message, DEVLINK_ATTR_PORT_NETDEV_NAME, &port->ifname);
        if (r < 0)
                return log_debug_errno(r, "devlink netlink: Message without valid port ifname: %m");
        devlink_port_split_genl_read(message, port);

        r = devlink_port_cache_update(m, dev->bus_name, dev->dev_name, port->index, port->ifname, port->split);
        if (r < 0)
                return log_debug_errno(r, "devlink netlink: Failed to update port cache: %m");
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
        int r;

        if (!dev->bus_name || !dev->dev_name || port->index == _DEVLINK_PORT_INDEX_INVALID)
                return log_debug_errno(SYNTHETIC_ERRNO(EINVAL), "devlink netlink: Incomplete key for cached port lookup: %m");

        assert(!port->ifname);

        /* The message for "port cached" match does not contain ifname,
         * Instead query the port cache and obtain ifname from there.
         */

        r = devlink_port_cache_query(m, dev->bus_name, dev->dev_name, port->index, &port->ifname, &port->split);
        if (r < 0)
                return log_debug_errno(r, "devlink netlink: Unable to lookup cached port: %m");
        return 0;
}

const DevlinkMatchVTable devlink_match_port_cached_ifname_vtable = {
        .free = devlink_match_port_ifname_free,
        .check = devlink_match_port_ifname_check,
        .log_prefix = devlink_match_port_ifname_log_prefix,
        .hash_func = devlink_match_port_ifname_hash_func,
        .compare_func = devlink_match_port_ifname_compare_func,
        .genl_read = devlink_match_port_cached_ifname_genl_read,
};
