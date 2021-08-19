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

static void _manager_genl_process_message(sd_netlink *genl, sd_netlink_message *message,
                                          Manager *m, DevlinkKind kind) {
        const DevlinkMonitorCommand *monitor_cmd;
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
                const DevlinkVTable *vtable;
                int i;

                r = sd_genl_message_get_command(genl, message, &cmd);
                if (r < 0) {
                        log_debug_errno(r, "devlink netlink: Failed to determine genl message command, ignoring: %m");
                        return;
                }

                devlink_for_each_vtable(vtable, i) {
                        FOREACH_ARRAY(j, vtable->genl_monitor_cmds, vtable->genl_monitor_cmds_count) {
                                if (j->cmd == cmd) {
                                        kind = i;
                                        monitor_cmd = j;
                                        break;
                                }
                        }
                }
                if (kind == _DEVLINK_KIND_INVALID)
                        return;

                log_debug("devlink netlink: Received %s(%u) message.", strna(devlink_cmd_to_string(cmd)), cmd);
        } else {
                /* Use the first command in array for enumeration messages processing. */
                monitor_cmd = &_DEVLINK_VTABLE(kind)->genl_monitor_cmds[0];
        }

        devlink_genl_process_message(message, m, kind, monitor_cmd);
}

static int manager_genl_process_message(sd_netlink *genl, sd_netlink_message *message,
                                        Manager *m) {
        _manager_genl_process_message(genl, message, m, _DEVLINK_KIND_INVALID);
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

static int manager_enumerate_kind(Manager *m, DevlinkKind kind) {
        _cleanup_(sd_netlink_message_unrefp) sd_netlink_message *req = NULL;
        _cleanup_(sd_netlink_message_unrefp) sd_netlink_message *rep = NULL;
        int k, r;

        assert(m);
        assert(m->genl);

        r = sd_genl_message_new(m->genl, DEVLINK_GENL_NAME,
                                _DEVLINK_VTABLE(kind)->genl_enumerate_cmd, &req);
        if (r < 0)
                return r;

        r = sd_netlink_message_set_request_dump(req, true);
        if (r < 0)
                return r;

        r = sd_netlink_call(m->genl, req, 0, &rep);
        if (r < 0)
                return r;

        for (sd_netlink_message *rep_one = rep; rep_one; rep_one = sd_netlink_message_next(rep_one)) {
                k = manager_genl_enumerate_process_message(m->genl, rep_one, m, kind);
                if (k < 0 && r >= 0)
                        r = k;
        }

        return r;
}

static int manager_enumerate_internal(Manager *m, bool initial) {
        int r, i;

        devlink_for_each_kind(i) {
                if (!initial && !_DEVLINK_VTABLE(i)->genl_need_periodic_enumeration)
                        continue;
                r = manager_enumerate_kind(m, i);
                if (r < 0)
                        log_warning_errno(r, "Could not enumerate %s objects: %m", devlink_kind_to_string(i));
        }
        return 0;
}

int manager_enumerate(Manager *m) {
        return manager_enumerate_internal(m, true);
}

#define MANAGER_PERIODIC_ENUMERATION_INTERVAL (USEC_PER_SEC / 4)

static int manager_periodic_enumeration_event_callback(sd_event_source *source, usec_t usec, void *userdata) {
        Manager *m = ASSERT_PTR(userdata);
        int r;

        assert(source == m->periodic_enumeration_event_source);

        (void) manager_enumerate_internal(m, false);

        r = sd_event_source_set_time_relative(
                        m->periodic_enumeration_event_source,
                        MANAGER_PERIODIC_ENUMERATION_INTERVAL);
        if (r < 0)
                return r;
        return sd_event_source_set_enabled(m->periodic_enumeration_event_source, SD_EVENT_ONESHOT);
}

static void manager_periodic_enumeration_stop(Manager *m) {
        m->periodic_enumeration_event_source = sd_event_source_disable_unref(m->periodic_enumeration_event_source);
}

static int manager_periodic_enumeration_start(Manager *m) {
        int r;

        r = sd_event_add_time_relative(
                        m->event,
                        &m->periodic_enumeration_event_source,
                        CLOCK_MONOTONIC,
                        MANAGER_PERIODIC_ENUMERATION_INTERVAL, 0,
                        manager_periodic_enumeration_event_callback,
                        m);
        if (r < 0)
                return r;

        (void) sd_event_source_set_description(m->periodic_enumeration_event_source, "devlink-periodic-enumeration");

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

        m = new0(Manager, 1);
        if (!m)
                return -ENOMEM;

        *ret = TAKE_PTR(m);

        return 0;
}

Manager* manager_free(Manager *m) {
        if (!m)
                return NULL;

        m->devlink_objs = hashmap_free(m->devlink_objs);
        m->match_port_cache = hashmap_free(m->match_port_cache);
        m->reload = hashmap_free(m->reload);

        sd_netlink_unref(m->genl);

        manager_periodic_enumeration_stop(m);

        return mfree(m);
}

int manager_start(Manager *m) {
        assert(m);

        return manager_periodic_enumeration_start(m);
}

int manager_load_config(Manager *m) {
        return devlink_load(m, false);
}
