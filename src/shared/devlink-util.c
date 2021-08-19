/* SPDX-License-Identifier: LGPL-2.1-or-later */

#include "devlink-util.h"
#include "string-table.h"

static const char * const devlink_cmd_table[__DEVLINK_CMD_MAX] = {
        [DEVLINK_CMD_NEW] = "new",
        [DEVLINK_CMD_PORT_NEW] = "port_new",
};

DEFINE_STRING_TABLE_LOOKUP_TO_STRING(devlink_cmd, int);

static const char* const devlink_dev_eswitch_mode_table[_DEVLINK_DEV_ESWITCH_MODE_MAX] = {
        [_DEVLINK_DEV_ESWITCH_MODE_LEGACY] = "legacy",
        [_DEVLINK_DEV_ESWITCH_MODE_SWITCHDEV] = "switchdev",
};

DEFINE_STRING_TABLE_LOOKUP(devlink_dev_eswitch_mode, DevlinkDevESwitchMode);

static const char* const devlink_dev_reload_action_table[_DEVLINK_DEV_RELOAD_ACTION_MAX] = {
        [_DEVLINK_DEV_RELOAD_ACTION_DRIVER_REINIT] = "driver_reinit",
        [_DEVLINK_DEV_RELOAD_ACTION_FW_ACTIVATE] = "fw_activate",
};

DEFINE_STRING_TABLE_LOOKUP(devlink_dev_reload_action, DevlinkDevReloadAction);

static const char* const devlink_dev_reload_limit_table[_DEVLINK_DEV_RELOAD_LIMIT_MAX] = {
        [_DEVLINK_DEV_RELOAD_LIMIT_NO_RESET] = "no_reset",
};

DEFINE_STRING_TABLE_LOOKUP(devlink_dev_reload_limit, DevlinkDevReloadLimit);

static const char* const devlink_param_cmode_table[_DEVLINK_PARAM_CMODE_MAX] = {
        [_DEVLINK_PARAM_CMODE_RUNTIME] = "runtime",
        [_DEVLINK_PARAM_CMODE_DRIVERINIT] = "driverinit",
        [_DEVLINK_PARAM_CMODE_PERMANENT] = "permanent",
};

DEFINE_STRING_TABLE_LOOKUP(devlink_param_cmode, DevlinkParamCMode);
