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
#include "devlink-key.h"
#include "devlink-match-port-cache.h"

/* use 128 MB for receive socket kernel queue. */
#define RCVBUF_SIZE    (128*1024*1024)

static void _manager_genl_process_message(sd_netlink *genl, sd_netlink_message *message,
                                          Manager *m, uint8_t enumerate_cmd) {
        const DevlinkMonitorCommand *monitor_cmd;
        const DevlinkVTable *vtable;
        const char *family;
        uint8_t cmd;
        int i;
        int r;

        assert(genl);
        assert(message);
        assert(m);

        if (sd_netlink_message_is_error(message)) {
                r = sd_netlink_message_get_errno(message);
                if (r < 0)
                        log_warning_errno(r, "devlink netlink: Received error message, ignoring: %m");
                return;
        }

        r = sd_genl_message_get_family_name(genl, message, &family);
        if (r < 0) {
                log_debug_errno(r, "devlink netlink: Failed to determine genl family, ignoring: %m");
                return;
        }
        if (!streq(family, DEVLINK_GENL_NAME)) {
                log_debug("devlink netlink: Received message of unexpected genl family '%s', ignoring: %m", family);
                return;
        }

        if (enumerate_cmd == DEVLINK_CMD_UNSPEC) {
                r = sd_genl_message_get_command(genl, message, &cmd);
                if (r < 0) {
                        log_debug_errno(r, "devlink netlink: Failed to determine genl message command, ignoring: %m");
                        return;
                }
        } else {
                /* In case of enumeration, assume reply cmd is the same cmd as used for enumeration.
                 * Works around buggy kernel returning wrong reply cmd for some objects. */
                cmd = enumerate_cmd;
        }

        log_debug("devlink netlink: Received %s(%u) message.", strna(devlink_cmd_to_string(cmd)), cmd);

        devlink_for_each_vtable(vtable, i) {
                FOREACH_ARRAY(j, vtable->genl_monitor_cmds, vtable->genl_monitor_cmds_count) {
                        if (j->cmd == cmd)
                                devlink_genl_process_message(message, m, i, j);
                }
        }
}

static int manager_genl_process_message(sd_netlink *genl, sd_netlink_message *message,
                                        Manager *m) {
        _manager_genl_process_message(genl, message, m, DEVLINK_CMD_UNSPEC);
        return 0;
}

static int manager_genl_enumerate_process_message(
                sd_netlink *genl,
                sd_netlink_message *message,
                Manager *m,
                uint8_t enumerate_cmd) {
        log_debug("devlink netlink: Incoming enumeration message");
        _manager_genl_process_message(genl, message, m, kind);
        return 0;
}

static int manager_enumerate_kind(Manager *m, DevlinkKind kind) {
        _cleanup_(sd_netlink_message_unrefp) sd_netlink_message *req = NULL;
        _cleanup_(sd_netlink_message_unrefp) sd_netlink_message *rep = NULL;
        uint8_t enumerate_cmd = _DEVLINK_VTABLE(kind)->genl_enumerate_cmd;
        int k, r;

        if (enumerate_cmd == DEVLINK_CMD_UNSPEC)
                return 0;

        assert(m);
        assert(m->genl);

        r = sd_genl_message_new(m->genl, DEVLINK_GENL_NAME, enumerate_cmd, &req);
        if (r < 0)
                return r;

        r = sd_netlink_message_set_request_dump(req, true);
        if (r < 0)
                return r;

        r = sd_netlink_call(m->genl, req, 0, &rep);
        if (r < 0)
                return r;

        for (sd_netlink_message *rep_one = rep; rep_one; rep_one = sd_netlink_message_next(rep_one)) {
                k = manager_genl_enumerate_process_message(m->genl, rep_one, m, enumereate_cmd);
                if (k < 0 && r >= 0)
                        r = k;
        }

        return r;
}

static int manager_enumerate_genl(Manager *m, bool initial) {
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
        return manager_enumerate_genl(m, true);
}

int manager_enumerate_genl_one(Manager *m, DevlinkKey *key) {
        _cleanup_(sd_netlink_message_unrefp) sd_netlink_message *req = NULL;
        _cleanup_(sd_netlink_message_unrefp) sd_netlink_message *rep = NULL;
        uint8_t enumerate_cmd = _DEVLINK_VTABLE(key->kind)->genl_enumerate_cmd;
        int r;

        if (enumerate_cmd == DEVLINK_CMD_UNSPEC)
                return 0;

        r = sd_genl_message_new(m->genl, DEVLINK_GENL_NAME, enumerate_cmd, &req);
        if (r < 0)
                return r;

        r = devlink_key_genl_append(req, key);
        if (r < 0)
                return r;

        r = sd_netlink_call(m->genl, req, 0, &rep);
        if (r < 0)
                return r;

        r = sd_netlink_message_get_errno(rep);
        if (r < 0)
                return r;

        return manager_genl_process_message(m->genl, rep, m);
}

#define MANAGER_PERIODIC_ENUMERATION_INTERVAL (USEC_PER_SEC / 4)

static int manager_periodic_enumeration_event_callback(sd_event_source *source, usec_t usec, void *userdata) {
        Manager *m = ASSERT_PTR(userdata);
        int r;

        assert(source == m->periodic_enumeration_event_source);

        (void) manager_enumerate_genl(m, false);

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

static int manager_setup_rtnl_filter(Manager *m) {
        struct sock_filter filter[] = {
                /* Check the packet length. */
                BPF_STMT(BPF_LD + BPF_W + BPF_LEN, 0),                                      /* A <- packet length */
                BPF_JUMP(BPF_JMP + BPF_JGE + BPF_K, sizeof(struct nlmsghdr), 1, 0),         /* A (packet length) >= sizeof(struct nlmsghdr) ? */
                BPF_STMT(BPF_RET + BPF_K, 0),                                               /* reject */
                /* Accept all messages of types RTM_NEWLINK or RTM_DELLINK. */
                BPF_STMT(BPF_LD + BPF_H + BPF_ABS, offsetof(struct nlmsghdr, nlmsg_type)),  /* A <- message type */
                BPF_JUMP(BPF_JMP + BPF_JEQ + BPF_K, htobe16(RTM_NEWLINK), 2, 0),            /* message type == RTM_NEWLINK ? */
                BPF_JUMP(BPF_JMP + BPF_JEQ + BPF_K, htobe16(RTM_DELLINK), 1, 0),            /* message type == RTM_DELLINK ? */
                BPF_STMT(BPF_RET + BPF_K, 0),                                               /* reject */
                BPF_STMT(BPF_RET + BPF_K, UINT32_MAX),                                      /* accept */
        };

        assert(m);
        assert(m->rtnl);

        return sd_netlink_attach_filter(m->rtnl, ELEMENTSOF(filter), filter);
}

static int manager_rtnl_process_link(sd_netlink *rtnl, sd_netlink_message *message, Manager *m) {
        const char *ifname;
        uint16_t type;
        int ifindex;
        int r;

        assert(rtnl);
        assert(message);
        assert(m);

        if (sd_netlink_message_is_error(message)) {
                r = sd_netlink_message_get_errno(message);
                if (r < 0)
                        log_warning_errno(r, "rtnl: Could not receive link message, ignoring: %m");

                return 0;
        }

        r = sd_netlink_message_get_type(message, &type);
        if (r < 0) {
                log_warning_errno(r, "rtnl: Could not get message type, ignoring: %m");
                return 0;

        r = sd_rtnl_message_link_get_ifindex(message, &ifindex);
        if (r < 0) {
                log_warning_errno(r, "rtnl: Could not get ifindex from link message, ignoring: %m");
                return 0;
        } else if (ifindex <= 0) {
                log_warning("rtnl: received link message with invalid ifindex %d, ignoring: %m", ifindex);
                return 0;
        }

        switch (type) {
        case RTM_NEWLINK:
                r = devlink_ifname_tracker_ifindex_update(m, ifindex, message);
                if (r < 0)
                        log_warning_errno(r, "rtnl: Could not update ifname tracker, ignoring: %m");
                return 0;
        case RTM_DELLINK:
                devlink_ifname_tracker_ifindex_remove(m, ifindex);
                return 0;
        default:
                log_warning("rtnl: Received unexpected message type %u when processing link, ignoring.", type);
                return 0;
        }
}

static int manager_connect_rtnl(Manager *m) {
        int r;

        assert(m);

        r = sd_netlink_open(&m->rtnl);
        if (r < 0)
                return r;

        r = sd_netlink_increase_rxbuf(m->rtnl, RCVBUF_SIZE);
        if (r < 0)
                log_warning_errno(r, "Failed to increase receive buffer size for RT netlink socket, ignoring: %m");

        r = sd_netlink_attach_event(m->rtnl, m->event, 0);
        if (r < 0)
                return r;

        r = netlink_add_match(m->rtnl, NULL, RTM_NEWLINK, &manager_rtnl_process_link, NULL, m, "devlinkd-rtnl_process_link");
        if (r < 0)
                return r;

        r = netlink_add_match(m->rtnl, NULL, RTM_DELLINK, &manager_rtnl_process_link, NULL, m, "devlinkd-rtnl_process_link");
        if (r < 0)
                return r;

        return manager_setup_rtnl_filter(m);
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

        r = manager_connect_rtnl(m);
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
        m->ifname_tracker_by_ifindex = hashmap_free(m->ifname_tracker_by_ifindex);
        m->ifname_tracker_by_ifname = hashmap_free(m->ifname_tracker_by_ifname);
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
