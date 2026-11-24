#include "../enemy_autopause/sdk/include/dk2ml.h"
#undef DK2ML_PLUGIN_MANIFEST
#define DK2ML_PLUGIN_MANIFEST(...)
#define DK2ML_PluginInit qol_names_init
#define DESCRIPTION qol_names_description
#include "../selected_trooper_name/native_plugin.c"