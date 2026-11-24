#include "../enemy_autopause/sdk/include/dk2ml.h"
#undef DK2ML_PLUGIN_MANIFEST
#define DK2ML_PLUGIN_MANIFEST(...)
#define DK2ML_PluginInit qol_settings_init
#define DESCRIPTION qol_settings_description
#include "../module_settings/module_settings.c"