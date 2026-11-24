#include "../enemy_autopause/sdk/include/dk2ml.h"
#include "../enemy_autopause/sdk/include/rake_settings_api.h"
#include <wchar.h>
static const DK2ML_API *api;
static bool enabled = true;
static wchar_t settings_path[MAX_PATH];
static const char *shared_section;
static bool (*imgui_checkbox)(const char *,bool *);
static void (*imgui_text)(const char *,...);
static R5K_SettingsModule shared_settings;

static bool supported_image(void) {
    // Verify the version and layout through the loader so its isolated
    // initialization validator can perform these same checks.
    return api->GetGameVersion() == 112 &&
        api->GetFieldOffset("GameClient", "m_camera") == 0x107274 &&
        api->GetFieldOffset("GameClient", "m_state") == 0x7e208 &&
        (uintptr_t)api->ResolveSymbol("GameClient::UpdateCamera") == image + 0x187f70;
}
static void settings_changed(const DK2ML_Option *option, void *user) {
    (void)option; (void)user;
    if (settings_path[0])
        WritePrivateProfileStringW(L"Plugin", L"Enabled", enabled ? L"1" : L"0", settings_path);
}
static void draw_shared_settings(void) {
    imgui_text("%s",shared_section);
    if(imgui_checkbox(shared_section,&enabled))settings_changed(NULL,NULL);
}
static int configure(const DK2ML_API *loader, const char *label, const char *description) {
    api = loader;
    const char *interface_name=NULL;
    if (strcmp(label,"Enable additional hotkeys")==0) {
        interface_name="Rake5000.Settings.CursorHotkeys";shared_section="Additional Hotkeys";
    } else if (strcmp(label,"Play replays at normal speed")==0) {
        interface_name="Rake5000.Settings.NormalSpeedReplays";shared_section="Normal Speed Replays";
    } else if (strcmp(label,"Enable RTS camera controls")==0) {
        interface_name="Rake5000.Settings.RTSCamera";shared_section="RTS Camera";
    } else return 1;
    imgui_checkbox=(bool (*)(const char *,bool *))api->ResolveSymbol("ImGui::Checkbox");
    imgui_text=(void (*)(const char *,...))api->ResolveSymbol("ImGui::Text");
    if(!imgui_checkbox||!imgui_text)return 1;
    if (!supported_image()) {
        api->Log("Unsupported executable build");
        return 1;
    }
    const wchar_t *directory = api->GetConfigDir();
    if (directory && directory[0]) {
        if (wcslen(directory) + 13 >= MAX_PATH) return 1;
        swprintf(settings_path, MAX_PATH, L"%lssettings.ini", directory);
        enabled = GetPrivateProfileIntW(L"Plugin", L"Enabled", 1, settings_path) != 0;
    }
    DK2ML_Option option = {0};
    option.structSize = sizeof(option);
    option.type = DK2ML_OPTION_HEADER;
    option.label = strcmp(label, "Enable additional hotkeys") == 0 ? "Additional Hotkeys" :
        strcmp(label, "Enable RTS camera controls") == 0 ? "RTS Camera" : "Normal Speed Replays";
    if (api->AddOption(&option) != DK2ML_OK) return 1;
    option.type = DK2ML_OPTION_BOOL;
    option.label = label;
    option.tooltip = description;
    option.value = &enabled;
    option.onChange = settings_changed;
    if(api->AddOption(&option)!=DK2ML_OK)return 1;
    shared_settings=(R5K_SettingsModule){sizeof(R5K_SettingsModule),shared_section,draw_shared_settings};
    return api->PublishInterface(interface_name,R5K_SETTINGS_API_VERSION,&shared_settings)!=DK2ML_OK;
}
static bool hook_named(const char *name, uintptr_t expected_rva,
    DK2ML_PreFn pre, DK2ML_PostFn post) {
    void *target = api->ResolveSymbol(name);
    if (!target || (uintptr_t)target != image + expected_rva) return false;
    return api->CreateSafeHook(target, pre, post, NULL) == DK2ML_OK &&
        api->EnableHook(target) == DK2ML_OK;
}
