#include "../enemy_autopause/sdk/include/dk2ml.h"
#undef DK2ML_PLUGIN_MANIFEST
#define DK2ML_PLUGIN_MANIFEST(...)
#define DK2ML_PluginInit qol_hotkeys_init
#define DESCRIPTION qol_hotkeys_description
#include "../modloader_ports/cursor_hotkeys.c"