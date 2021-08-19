/* SPDX-License-Identifier: LGPL-2.1+ */

#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <linux/devlink.h>
#include <linux/if.h>
#include <linux/fib_rules.h>
#include <linux/nexthop.h>

#include "sd-daemon.h"
#include "sd-netlink.h"

#include "alloc-util.h"
#include "bus-log-control-api.h"
#include "bus-polkit.h"
#include "bus-util.h"
#include "conf-parser.h"
#include "device-private.h"
#include "device-util.h"
#include "devlink-util.h"
#include "dns-domain.h"
#include "fd-util.h"
#include "fileio.h"
#include "local-addresses.h"
#include "netlink-util.h"
#include "ordered-set.h"
#include "path-lookup.h"
#include "path-util.h"
#include "set.h"
#include "signal-util.h"
#include "strv.h"
#include "sysctl-util.h"
#include "tmpfile-util.h"
#include "udev-util.h"
#include "virt.h"

#include "devlinkd-manager.h"
#include "devlink.h"

/* use 128 MB for receive socket kernel queue. */
#define RCVBUF_SIZE    (128*1024*1024)

static DevlinkKind kind_by_genl_cmd(uint8_t cmd) {
        const DevlinkVTable *vtable;
        int i;

        devlink_for_each_vtable(vtable, i) {
                if (cmd == vtable->genl_monitor_cmd) {
                        return i;
                }
        }
        return _DEVLINK_KIND_INVALID;
}

static void _manager_genl_process_message(sd_netlink *genl, sd_netlink_message *message,
                                          Manager *m, DevlinkKind kind) {
        const char *family;
        uint8_t cmd;
        int r;

        assert(genl);
        assert(message);
        assert(m);

        if (sd_netlink_message_is_error(message)) {
                r = sd_netlink_message_get_errno(message);
                if (r < 0)
                        log_warning_errno(r, "devlink netlink: Received error message, ignoring");
                return;
        }

        r = sd_genl_message_get_family_name(genl, message, &family);
        if (r < 0) {
                log_debug_errno(r, "devlink netlink: Failed to determine genl family, ignoring: %m");
                return;
        }
        if (!streq(family, DEVLINK_GENL_NAME)) {
                log_debug("devlink netlink: Received message of unexpected genl family '%s', ignoring.", family);
                return;
        }

        if (kind == _DEVLINK_KIND_INVALID) {
                r = sd_genl_message_get_command(genl, message, &cmd);
                if (r < 0) {
                        log_debug_errno(r, "devlink netlink: Failed to determine genl message command, ignoring: %m");
                        return;
                }
                log_debug("devlink netlink: Received %s(%u) message.", strna(devlink_cmd_to_string(cmd)), cmd);

                kind = kind_by_genl_cmd(cmd);
                if (kind == _DEVLINK_KIND_INVALID) {
                        log_debug("devlink netlink: Failed to lookup devlink kind.");
                        return;
                }
        }

        devlink_genl_process_message(message, m, kind);
}

static int manager_genl_process_message(sd_netlink *genl, sd_netlink_message *message,
                                        Manager *m) {
        log_debug("devlink netlink: Incoming notification message");
        _manager_genl_process_message(genl, message, m, _DEVLINK_KIND_INVALID);
        return 0;
}

static int manager_connect_genl(Manager *m) {
        int r;

        assert(m);

        r = sd_genl_socket_open(&m->genl);
        if (r < 0)
                return r;

        r = sd_netlink_increase_rxbuf(m->genl, RCVBUF_SIZE);
        if (r < 0)
                log_warning_errno(r, "Failed to increase receive buffer size for general netlink socket, ignoring: %m");

        r = sd_netlink_attach_event(m->genl, m->event, 0);
        if (r < 0)
                return r;

        r = genl_add_match(m->genl, NULL, DEVLINK_GENL_NAME, DEVLINK_GENL_MCGRP_CONFIG_NAME, 0,
                           &manager_genl_process_message, NULL, m, "devlinkd-genl_process_devlink_config");
        if (r < 0 && r != -EOPNOTSUPP)
                return r;

        return 0;
}

static int signal_terminate_callback(sd_event_source *s, const struct signalfd_siginfo *si, void *userdata) {
        Manager *m = userdata;

        assert(m);

        log_debug("Terminate operation initiated");

        return sd_event_exit(sd_event_source_get_event(s), 0);
}

static int signal_restart_callback(sd_event_source *s, const struct signalfd_siginfo *si, void *userdata) {
        Manager *m = userdata;

        assert(m);

        log_debug("Restart operation initiated");

        return sd_event_exit(sd_event_source_get_event(s), 0);
}

int manager_setup(Manager *m) {
        int r;

        r = sd_event_default(&m->event);
        if (r < 0)
                return r;

        assert_se(sigprocmask_many(SIG_SETMASK, NULL, SIGINT, SIGTERM, SIGUSR2, -1) >= 0);

        (void) sd_event_set_watchdog(m->event, true);
        (void) sd_event_add_signal(m->event, NULL, SIGTERM, signal_terminate_callback, m);
        (void) sd_event_add_signal(m->event, NULL, SIGINT, signal_terminate_callback, m);
        (void) sd_event_add_signal(m->event, NULL, SIGUSR2, signal_restart_callback, m);

        r = manager_connect_genl(m);
        if (r < 0)
                return r;

        return 0;
}

int manager_new(Manager **ret) {
        _cleanup_(manager_freep) Manager *m = NULL;

        m = new(Manager, 1);
        if (!m)
                return -ENOMEM;

        *m = (Manager) {
        };

        *ret = TAKE_PTR(m);

        return 0;
}

Manager* manager_free(Manager *m) {
        if (!m)
                return NULL;

        m->devlink_objs = hashmap_free(m->devlink_objs);
        m->port_cache = hashmap_free(m->port_cache);

        sd_netlink_unref(m->genl);
        return mfree(m);
}

int manager_start(Manager *m) {
        assert(m);

        return 0;
}

int manager_load_config(Manager *m) {
        int r;

        r = devlink_load(m, false);
        if (r < 0)
                return r;

        return 0;
}

static int manager_genl_enumerate_process_message(
                sd_netlink *genl,
                sd_netlink_message *message,
                Manager *m,
                DevlinkKind kind) {
        log_debug("devlink netlink: Incoming enumeration message");
        _manager_genl_process_message(genl, message, m, kind);
        return 0;
}

static int manager_enumerate_internal(
                Manager *m,
                DevlinkKind kind,
                sd_netlink_message *req,
                int (*process)(sd_netlink *, sd_netlink_message *, Manager *, DevlinkKind)) {
        _cleanup_(sd_netlink_message_unrefp) sd_netlink_message *reply = NULL;
        int k, r;

        assert(m);
        assert(req);
        assert(process);

        r = sd_netlink_message_set_request_dump(req, true);
        if (r < 0)
                return r;

        r = sd_netlink_call(m->genl, req, 0, &reply);
        if (r < 0)
                return r;

        for (sd_netlink_message *reply_one = reply; reply_one; reply_one = sd_netlink_message_next(reply_one)) {
                k = process(m->genl, reply_one, m, kind);
                if (k < 0 && r >= 0)
                        r = k;
        }

        return r;
}

static int manager_enumerate_kind(Manager *m, DevlinkKind kind) {
        _cleanup_(sd_netlink_message_unrefp) sd_netlink_message *req = NULL;
        int r;

        assert(m);
        assert(m->genl);

        r = sd_genl_message_new(m->genl, DEVLINK_GENL_NAME,
                                _DEVLINK_VTABLE(kind)->genl_enumerate_cmd, &req);
        if (r < 0)
                return r;

        return manager_enumerate_internal(m, kind, req, manager_genl_enumerate_process_message);
}

int manager_enumerate(Manager *m) {
        int r, i;

        devlink_for_each_kind(i) {
                r = manager_enumerate_kind(m, i);
                if (r < 0)
                        return log_error_errno(r, "Could not enumerate %s objects: %m", devlink_kind_to_string(i));
        }
        return 0;
}
