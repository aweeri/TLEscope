#ifndef CONFIG_H
#define CONFIG_H

#include <stdbool.h>
#include "types.h"

extern bool show_clouds;

void LoadAppConfig(const char* filename, AppConfig* config);

#endif // CONFIG_H