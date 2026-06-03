#ifndef APP_H
#define APP_H

#include <stdbool.h>

#include "app_config.h"

/* app_parse_args / app_set_default_config 见 app_config.h */

int app_run(const AppConfig *config);

#endif
