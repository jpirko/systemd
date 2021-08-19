/* SPDX-License-Identifier: LGPL-2.1-or-later */
#pragma once

#include "conf-parser.h"
#include "devlink.h"
#include "devlink-util.h"

typedef struct DevlinkDevMatch {
        char *bus_name; /* first part of the handle */
        char *dev_name; /* second part of the handle */
} DevlinkDevMatch;

typedef struct DevlinkDev {
        Devlink meta;
        DevlinkDevESwitchMode eswitch_mode;
        DevlinkDevReloadAction reload_action;
} DevlinkDev;

DEFINE_DEVLINK_CAST(DEV, DevlinkDev);

extern const DevlinkVTable devlink_dev_vtable;

CONFIG_PARSER_PROTOTYPE(config_parse_devlink_dev_eswitch_mode);
CONFIG_PARSER_PROTOTYPE(config_parse_devlink_dev_reload_action);
