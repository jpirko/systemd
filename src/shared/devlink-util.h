/* SPDX-License-Identifier: LGPL-2.1-or-later */
#pragma once

#include <netinet/in.h>
#include <linux/devlink.h>

#include "macro.h"

const char *devlink_cmd_to_string(int cmd) _const_;

typedef enum DevlinkDevESwitchMode {
        _DEVLINK_DEV_ESWITCH_MODE_LEGACY    = DEVLINK_ESWITCH_MODE_LEGACY,
        _DEVLINK_DEV_ESWITCH_MODE_SWITCHDEV = DEVLINK_ESWITCH_MODE_SWITCHDEV,
        _DEVLINK_DEV_ESWITCH_MODE_MAX,
        _DEVLINK_DEV_ESWITCH_MODE_INVALID   = -EINVAL,
} DevlinkDevESwitchMode;

const char *devlink_dev_eswitch_mode_to_string(DevlinkDevESwitchMode d) _const_;
DevlinkDevESwitchMode devlink_dev_eswitch_mode_from_string(const char *d) _pure_;

typedef enum DevlinkDevReloadAction {
        _DEVLINK_DEV_RELOAD_ACTION_DRIVER_REINIT = DEVLINK_RELOAD_ACTION_DRIVER_REINIT,
        _DEVLINK_DEV_RELOAD_ACTION_FW_ACTIVATE   = DEVLINK_RELOAD_ACTION_FW_ACTIVATE,
        _DEVLINK_DEV_RELOAD_ACTION_MAX,
        _DEVLINK_DEV_RELOAD_ACTION_INVALID       = -EINVAL,
} DevlinkDevReloadAction;

const char *devlink_dev_reload_action_to_string(DevlinkDevReloadAction d) _const_;
DevlinkDevReloadAction devlink_dev_reload_action_from_string(const char *d) _pure_;

typedef enum DevlinkDevReloadLimit {
        _DEVLINK_DEV_RELOAD_LIMIT_NO_RESET = DEVLINK_RELOAD_LIMIT_NO_RESET,
        _DEVLINK_DEV_RELOAD_LIMIT_MAX,
        _DEVLINK_DEV_RELOAD_LIMIT_INVALID  = -EINVAL,
} DevlinkDevReloadLimit;

const char *devlink_dev_reload_limit_to_string(DevlinkDevReloadLimit d) _const_;
DevlinkDevReloadLimit devlink_dev_reload_limit_from_string(const char *d) _pure_;

typedef enum DevlinkParamCMode {
        _DEVLINK_PARAM_CMODE_RUNTIME    = DEVLINK_PARAM_CMODE_RUNTIME,
        _DEVLINK_PARAM_CMODE_DRIVERINIT = DEVLINK_PARAM_CMODE_DRIVERINIT,
        _DEVLINK_PARAM_CMODE_PERMANENT  = DEVLINK_PARAM_CMODE_PERMANENT,
        _DEVLINK_PARAM_CMODE_MAX,
        _DEVLINK_PARAM_CMODE_INVALID  = -EINVAL,
} DevlinkParamCMode;

const char *devlink_param_cmode_to_string(DevlinkParamCMode d) _const_;
DevlinkParamCMode devlink_param_cmode_from_string(const char *d) _pure_;
