#include "../enemy_autopause/sdk/include/dk2ml.h"
#undef DK2ML_PLUGIN_MANIFEST
#define DK2ML_PLUGIN_MANIFEST(...)
#define DK2ML_PluginInit qol_replays_init
#define DESCRIPTION qol_replays_description
#include "../modloader_ports/normal_speed_replays.c"