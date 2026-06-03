#ifndef APP_CONFIG_H
#define APP_CONFIG_H

#include <stdbool.h>

#include "app_types.h"

void app_set_default_config(AppConfig *config);
bool app_parse_args(int argc, char **argv, AppConfig *config);
void app_print_usage(const char *exe_name);

#endif
