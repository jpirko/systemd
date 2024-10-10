/* SPDX-License-Identifier: LGPL-2.1-or-later */

#include "alloc-util.h"
#include "devlink-util.h"
#include "netlink-util.h"

#include "devlink.h"
#include "devlink-key.h"
#include "devlink-reload.h"

static int devlink_reload(Devlink *devlink) {
        _cleanup_(sd_netlink_message_unrefp) sd_netlink_message *req = NULL;
        _cleanup_(sd_netlink_message_unrefp) sd_netlink_message *rep = NULL;
        int r;

        r = devlink_genl_message_new(devlink, DEVLINK_CMD_RELOAD, &req);
        if (r < 0)
                return log_devlink_error_errno(devlink, r, "Failed to create netlink message: %m");;

        r = devlink_key_genl_append(req, &devlink->key);
        if (r < 0)
                return r;

        r = sd_netlink_message_append_u8(req, DEVLINK_ATTR_RELOAD_ACTION, DEVLINK_RELOAD_ACTION_DRIVER_REINIT);
        if (r < 0)
                return log_devlink_error_errno(devlink, r, "Failed to append reload action to netlink message: %m");;

        r = sd_netlink_call(devlink->manager->genl, req, 0, &rep);
        if (r < 0)
                return log_devlink_error_errno(devlink, r, "Could not send reload message: %m");

        r = sd_netlink_message_get_errno(rep);
        if (r < 0)
                return log_devlink_error_errno(devlink, r, "Could not be reload: %m");

        log_devlink_info(devlink, "Reload success");

        return 0;
}

static int devlink_reload_event_callback(sd_event_source *source, usec_t usec, void *userdata) {
        Devlink *devlink = ASSERT_PTR(userdata);
        DevlinkReload *reload = DEVLINK_RELOAD(devlink);

        assert(source == reload->timeout_event_source);

        (void) devlink_reload(devlink);

        return 0;
}

#define DEVLINK_RELOAD_TIMEOUT USEC_PER_SEC * 3

static int devlink_reload_queue_event(Manager *m, Devlink *devlink) {
        DevlinkReload *reload = DEVLINK_RELOAD(devlink);
        int r;

        log_devlink_info(devlink, "Scheduling reload in %lu usec", DEVLINK_RELOAD_TIMEOUT);
        if (reload->timeout_event_source) {
                r = sd_event_source_set_time_relative(reload->timeout_event_source, DEVLINK_RELOAD_TIMEOUT);
                if (r < 0)
                        return r;

                return sd_event_source_set_enabled(reload->timeout_event_source, SD_EVENT_ONESHOT);
        }

        r = sd_event_add_time_relative(
                        m->event,
                        &reload->timeout_event_source,
                        CLOCK_MONOTONIC, DEVLINK_RELOAD_TIMEOUT, 0,
                        devlink_reload_event_callback, devlink);
        if (r < 0)
                return r;

        (void) sd_event_source_set_description(reload->timeout_event_source, "devlink-reload-timeout");

        return 0;
}

int devlink_reload_queue(Manager *m, DevlinkMatch *match) {
        Devlink *devlink;
        DevlinkKey key;
        int r;

        devlink_key_init(&key, DEVLINK_KIND_RELOAD);
        devlink_key_copy_from_match(&key, match, DEVLINK_MATCH_BIT_DEV);

        devlink = devlink_get(m, &key);
        if (!devlink)
                return -ENOENT;

        r = devlink_reload_queue_event(m, devlink);
        if (r < 0)
                return log_devlink_error_errno(devlink, r, "Failed to schedule reload: %m");

        return r;
}

static int devlink_reload_genl_cmd_del_msg_process(
                Devlink *devlink,
                DevlinkKey *lookup_key,
                sd_netlink_message *message) {
        DevlinkReload *reload = DEVLINK_RELOAD(devlink);

        reload->timeout_event_source = sd_event_source_disable_unref(reload->timeout_event_source);
        return DEVLINK_MONITOR_COMMAND_RETVAL_DELETE;
}

static const DevlinkMatchSet devlink_reload_matchsets[] = {
        DEVLINK_MATCH_BIT_DEV,
        0,
};

static const DevlinkMonitorCommand devlink_reload_commands[] = {
        { DEVLINK_CMD_DEL, devlink_reload_genl_cmd_del_msg_process },
};

const DevlinkVTable devlink_reload_vtable = {
        .object_size = sizeof(DevlinkReload),
        .matchsets = devlink_reload_matchsets,
        .genl_monitor_cmds = devlink_reload_commands,
        .genl_monitor_cmds_count = ELEMENTSOF(devlink_reload_commands),
};
