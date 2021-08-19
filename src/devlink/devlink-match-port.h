/* SPDX-License-Identifier: LGPL-2.1-or-later */
#pragma once

#include "conf-parser.h"

typedef struct DevlinkMatchPort {
        uint32_t index;
        bool index_valid;
        char *ifname;
        bool split;
} DevlinkMatchPort;

CONFIG_PARSER_PROTOTYPE(config_parse_devlink_port_index);
