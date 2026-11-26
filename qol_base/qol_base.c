#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <wchar.h>
#include <stdio.h>
#include "../enemy_autopause/sdk/include/dk2ml.h"
#include "qol_ui.h"

DK2ML_PLUGIN_MANIFEST(3,"QOL Tweaks","0.2.16-dev","Rake5000","",112);

// Persistent per-feature API copies preserve the legacy configuration namespace.
// Do not merge translation units: their private state intentionally stays isolated.
static DK2ML_API feature_api[4];
static wchar_t feature_config[4][MAX_PATH];
static const wchar_t *config_hotkeys(void){return feature_config[0];}
static const wchar_t *config_replays(void){return feature_config[1];}
static const wchar_t *config_names(void){return feature_config[2];}
static const wchar_t *config_settings(void){return feature_config[3];}
extern int qol_hotkeys_init(const DK2ML_API *,const DK2ML_PluginInfo *);
extern int qol_replays_init(const DK2ML_API *,const DK2ML_PluginInfo *);
extern int qol_names_init(const DK2ML_API *,const DK2ML_PluginInfo *);
extern int qol_settings_init(const DK2ML_API *,const DK2ML_PluginInfo *);
extern int qol_speed_init(const DK2ML_API *,const DK2ML_PluginInfo *);

DK2ML_EXPORT int DK2ML_PluginInit(const DK2ML_API *loader,const DK2ML_PluginInfo *info){
    if(!loader||loader->apiVersion<3)return 1;
    if(qol_ui_prepare(loader))return 1;
    if(GetModuleHandleW(L"DK2SpeedChanger.addon64")){
        loader->Log("QOL Tweaks refused: legacy ReShade Speed Changer is loaded.");return 1;
    }
    const wchar_t *legacy[]={L"Rake5000CursorHotkeys",L"Rake5000NormalSpeedReplays",
        L"Rake5000SelectedTrooperName",L"Rake5000ModuleSettings"};
    // Refuse before installing hooks if a legacy copy is already loaded.
    for(unsigned i=0;i<4;i++){
        wchar_t dll[80];swprintf(dll,80,L"%ls.dll",legacy[i]);
        if(GetModuleHandleW(dll)){
            loader->Log("QOL Tweaks refused: legacy plugin %ls is also loaded. Remove duplicate installation.",dll);
            return 1;
        }
        // Also reject a sibling that would otherwise load later in discovery order.
        if(info&&info->pluginDir&&wcslen(info->pluginDir)+wcslen(dll)<MAX_PATH){
            wchar_t sibling[MAX_PATH];swprintf(sibling,MAX_PATH,L"%ls%ls",info->pluginDir,dll);
            if(GetFileAttributesW(sibling)!=INVALID_FILE_ATTRIBUTES){
                loader->Log("QOL Tweaks refused: legacy sibling %ls must be retired before activation.",dll);
                return 1;
            }
        }
    }
    const wchar_t *directory=loader->GetConfigDir();
    if(!directory||!directory[0]||wcslen(directory)>=MAX_PATH)return 1;
    wchar_t parent[MAX_PATH];wcscpy(parent,directory);
    size_t length=wcslen(parent);
    while(length&&(parent[length-1]==L'\\'||parent[length-1]==L'/'))parent[--length]=0;
    while(length&&parent[length-1]!=L'\\'&&parent[length-1]!=L'/')parent[--length]=0;
    if(!length)return 1;
    const wchar_t *(*get_config[])(void)={config_hotkeys,config_replays,config_names,config_settings};
    DK2ML_PluginInitFn initialize[]={qol_hotkeys_init,qol_replays_init,qol_names_init,qol_settings_init};
    // Complete all path preparation before any feature registers hooks.
    for(unsigned i=0;i<4;i++){
        if(wcslen(parent)+wcslen(legacy[i])+2>=MAX_PATH)return 1;
        swprintf(feature_config[i],MAX_PATH,L"%ls%ls\\",parent,legacy[i]);
        if(!CreateDirectoryW(feature_config[i],NULL)&&GetLastError()!=ERROR_ALREADY_EXISTS)return 1;
        feature_api[i]=*loader;feature_api[i].GetConfigDir=get_config[i];
        feature_api[i].AddOption=qol_collect_option;
    }
    for(unsigned i=0;i<4;i++){
        if(initialize[i](&feature_api[i],info)){
            loader->Log("QOL Tweaks initialization failed in %ls; loader must deactivate this plugin's hooks.",legacy[i]);
            return 1;
        }
    }
    if(qol_speed_init(&feature_api[3],info))return 1;
    if(qol_ui_register())return 1;
    loader->Log("QOL Tweaks ready: hotkeys including breaching, native Speed Changer, normal-speed replays, names and shared settings.");
    return 0;
}
