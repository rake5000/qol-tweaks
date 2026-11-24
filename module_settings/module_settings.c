#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <wchar.h>
#include "../enemy_autopause/sdk/include/dk2ml.h"
#include "../enemy_autopause/sdk/include/rake_settings_api.h"
#ifdef R5K_QOL_UI
#include "../qol_base/qol_ui.h"
#include "../enemy_autopause/sdk/include/rake_settings_heading.h"
#endif

DK2ML_PLUGIN_MANIFEST(3,"Rake5000 Module Settings","0.1.8","Rake5000","",112);

static const DK2ML_API *api;
static bool window_open;
static int window_flags;
static bool (*imgui_begin)(const char *,bool *,int);
static void (*imgui_end)(void);
static void (*imgui_text)(const char *,...);
static void (*imgui_same_line)(float,float);
#ifndef R5K_QOL_UI
static int selected_category;
#endif
static int default_slow_percent=50,integer_type;
static wchar_t speed_config[MAX_PATH];
static bool (*imgui_slider)(const char*,int,void*,const void*,const void*,const char*,int);
#ifdef R5K_QOL_NATIVE
extern void R5K_SetDefaultSlowSpeed(int);
extern void qol_speed_draw_enabled(void);
#endif
static void apply_speed_default(void){
#ifdef R5K_QOL_NATIVE
    R5K_SetDefaultSlowSpeed(default_slow_percent);
#else
    HMODULE addon=GetModuleHandleW(L"DK2SpeedChanger.addon64");
    if(!addon)return;
    void (*set_default)(int)=(void (*)(int))GetProcAddress(addon,"R5K_SetDefaultSlowSpeed");
    if(set_default)set_default(default_slow_percent);
#endif
}
static void save_speed_default(const DK2ML_Option *option,void *user){
    (void)option;(void)user;
    if(default_slow_percent<10)default_slow_percent=10;
    if(default_slow_percent>90)default_slow_percent=90;
    wchar_t value[16];swprintf(value,16,L"%d",default_slow_percent);
    WritePrivateProfileStringW(L"Speed",L"DefaultSlowPercent",value,speed_config);
    apply_speed_default();
}
#ifndef R5K_QOL_UI
static void draw_speed_settings(bool *found){
#ifdef R5K_QOL_NATIVE
    qol_speed_draw_enabled();
#else
    HMODULE addon=GetModuleHandleW(L"DK2SpeedChanger.addon64");
    if(!addon||!GetProcAddress(addon,"R5K_SetDefaultSlowSpeed"))return;
#endif
    imgui_text("Simulation speed");int low=10,high=90;
    if(imgui_slider("Default slow-motion speed (*)",integer_type,&default_slow_percent,&low,&high,"%d%%",0))save_speed_default(NULL,NULL);
    imgui_text("* toggles 100%% and the last custom speed; this sets its starting value.");
    *found=true;
}
#endif
typedef struct {float x,y;} ImVec2;
static bool (*imgui_button_ex)(const char *,const ImVec2 *,int);
static void **imgui_context;
static int off_imgui_io,off_want_capture_mouse,off_want_capture_keyboard;
static bool captured_input;

static bool imgui_wants_game_input(void) {
    void *context=imgui_context?*imgui_context:NULL;
    if(!context)return false;
    const unsigned char *io=(const unsigned char *)context+off_imgui_io;
    bool mouse=false,keyboard=false;
    memcpy(&mouse,io+off_want_capture_mouse,sizeof mouse);
    memcpy(&keyboard,io+off_want_capture_keyboard,sizeof keyboard);
    return mouse||keyboard;
}

static void set_input_capture(bool capture) {
    if(capture==captured_input)return;
    if(api->CaptureGameInput(capture?1:0)==DK2ML_OK)captured_input=capture;
}

static void open_settings(void *item,float x,float y,void *user) {
    (void)item;(void)x;(void)y;(void)user;
    if(api->IsGameMenuOpen())window_open=!window_open;
}

static void draw_provider(const char *interface_name,bool *found) {
    const R5K_SettingsModule *module=(const R5K_SettingsModule *)api->GetInterface(
        interface_name,R5K_SETTINGS_API_VERSION,NULL);
    if(!module||module->structSize<sizeof(R5K_SettingsModule)||!module->label||!module->draw)return;
    module->draw();
    *found=true;
}

#ifdef R5K_QOL_UI
static bool (*begin_tab_bar)(const char *,int);
static void (*end_tab_bar)(void);
static bool (*begin_tab_item)(const char *,bool *,int);
static void (*separator_ex)(int,float);
static void (*spacing)(void);
static void (*dummy)(const ImVec2 *);
static int tab_no_push_id, separator_horizontal, tab_scroll;
static int visible_qol_page=-1;

static void menu_heading(const char *label) {
    spacing();
    separator_ex(separator_horizontal,1.0f);
    r5k_settings_heading(api,imgui_text,label,true);
    spacing();
}

static bool menu_tab(const char *label) {
    // NoPushId makes EndTabItem unnecessary on builds where it is inlined.
    return begin_tab_item(label,NULL,tab_no_push_id);
}

static void draw_shared_menu(void) {
    const ImVec2 minimum_width={520.0f,0.0f};
    dummy(&minimum_width);
    bool found=false, qol_selected=false;
    int qol_page=-1;
    if(begin_tab_bar("##modules",tab_scroll)){
        if(menu_tab("QOL Tweaks")){
            qol_selected=true;
            static const char *pages[]={"Controls","Go-Codes","Breaching","Speed & Replays","Names"};
            if(begin_tab_bar("##qol_pages",tab_scroll)){
                for(int i=0;i<(int)(sizeof pages/sizeof pages[0]);i++){
                    if(!menu_tab(pages[i]))continue;
                    qol_page=i;
                    if(visible_qol_page!=i)qol_cancel_capture();
                    qol_draw_settings(i,menu_heading);
                }
                end_tab_bar();
            }
        }
        if(menu_tab("Gameplay Tweaks")){
            if(begin_tab_bar("##gameplay_pages",tab_scroll)){
                if(menu_tab("NPC Control")){
                    spacing();
                    separator_ex(separator_horizontal,1.0f);
                    draw_provider("Rake5000.Settings.ControlHVTs",&found);
                }
                if(menu_tab("Movement & Combat")){
                    menu_heading("Movement");
                    draw_provider("Rake5000.Settings.SolidCharacters",&found);
                    draw_provider("Rake5000.Settings.ReloadWhileMoving",&found);
                    menu_heading("Combat");
                    draw_provider("Rake5000.Settings.FriendlyFiringLanes",&found);
                    draw_provider("Rake5000.Settings.MovingSuppression",&found);
                }
                if(menu_tab("Contact")){
                    menu_heading("Contact response");
                    draw_provider("Rake5000.Settings.PauseOnContact",&found);
                }
                end_tab_bar();
            }
            if(!found)imgui_text("No Gameplay Tweaks settings are available on this page.");
        }
        if(menu_tab("Add-ons")){
            if(begin_tab_bar("##addon_pages",tab_scroll)){
                if(menu_tab("Squad")){
                    menu_heading("Squad behaviour");
                    draw_provider("Rake5000.Settings.TrooperFollow",&found);
                }
                if(menu_tab("Display")){
                    menu_heading("Camera and overlays");
                    draw_provider("Rake5000.Settings.RTSCamera",&found);
                    draw_provider("Rake5000.Settings.ReinforcementOverlay",&found);
                }
                end_tab_bar();
            }
            if(!found)imgui_text("No add-on settings are available on this page.");
        }
        end_tab_bar();
    }
    if(qol_page<0)qol_cancel_capture();
    visible_qol_page=qol_page;
    spacing();
    separator_ex(separator_horizontal,1.0f);
    spacing();
    if(qol_selected){
        qol_draw_reset();
        imgui_same_line(0.0f,-1.0f);
    }
    const ImVec2 automatic_size={0.0f,0.0f};
    if(imgui_button_ex("Close",&automatic_size,0))window_open=false;
}
#endif

static void frame(const DK2ML_Event *event,void *user) {
    (void)event;(void)user;
    apply_speed_default();
    if(window_open&&!api->IsGameMenuOpen())window_open=false;
    if(!window_open){
#ifdef R5K_QOL_UI
        qol_cancel_capture();
#endif
        set_input_capture(false);return;
    }
#ifdef R5K_QOL_UI
    set_input_capture(imgui_wants_game_input()||qol_is_capturing());
#else
    set_input_capture(imgui_wants_game_input());
#endif
    if(imgui_begin("Rake5000 Modules##shared_settings",&window_open,window_flags)) {
#ifdef R5K_QOL_UI
        draw_shared_menu();
#else
        bool found=false;
        const char *categories[]={"Controls","Movement & Combat","Squad","Display"};
        const ImVec2 automatic_size={0.0f,0.0f};
        for(unsigned i=0;i<sizeof categories/sizeof categories[0];i++){
            if(i)imgui_same_line(0.0f,-1.0f);
            char label[64];
            snprintf(label,sizeof label,selected_category==(int)i?"[%s]##category%u":"%s##category%u",categories[i],i);
            if(imgui_button_ex(label,&automatic_size,0))selected_category=(int)i;
        }
        switch(selected_category){
        case 0:
            draw_speed_settings(&found);
            draw_provider("Rake5000.Settings.CursorHotkeys",&found);
            draw_provider("Rake5000.Settings.ControlHVTs",&found);
            break;
        case 1:
            draw_provider("Rake5000.Settings.SolidCharacters",&found);
            draw_provider("Rake5000.Settings.FriendlyFiringLanes",&found);
            draw_provider("Rake5000.Settings.ReloadWhileMoving",&found);
            draw_provider("Rake5000.Settings.MovingSuppression",&found);
            break;
        case 2:
            draw_provider("Rake5000.Settings.TrooperFollow",&found);
            draw_provider("Rake5000.Settings.PauseOnContact",&found);
            break;
        case 3:
            draw_provider("Rake5000.Settings.RTSCamera",&found);
            draw_provider("Rake5000.Settings.ReinforcementOverlay",&found);
            draw_provider("Rake5000.Settings.SelectedTrooperName",&found);
            draw_provider("Rake5000.Settings.NormalSpeedReplays",&found);
            break;
        }
        if(!found)imgui_text("No settings modules in this category are currently loaded.");
        if(imgui_button_ex("Close",&automatic_size,0))window_open=false;
#endif
    }
    imgui_end();
    if(!window_open){
#ifdef R5K_QOL_UI
        qol_cancel_capture();
#endif
        set_input_capture(false);
    }
}

DK2ML_EXPORT int DK2ML_PluginInit(const DK2ML_API *loader,const DK2ML_PluginInfo *info) {
    (void)info;
    api=loader;
    if(!api||api->apiVersion<3)return 1;
    imgui_begin=(bool (*)(const char *,bool *,int))api->ResolveSymbol("ImGui::Begin");
    imgui_end=(void (*)(void))api->ResolveSymbol("ImGui::End");
    imgui_text=(void (*)(const char *,...))api->ResolveSymbol("ImGui::Text");
    imgui_same_line=(void (*)(float,float))api->ResolveSymbol("ImGui::SameLine");
    imgui_button_ex=(bool (*)(const char *,const ImVec2 *,int))api->ResolveSymbol("ImGui::ButtonEx");
    imgui_context=api->ResolveSymbol("GImGui");
    imgui_slider=(void*)api->ResolveSymbol("ImGui::SliderScalar");
#ifdef R5K_QOL_UI
    begin_tab_bar=(void*)api->ResolveSymbol("ImGui::BeginTabBar");
    end_tab_bar=(void*)api->ResolveSymbol("ImGui::EndTabBar");
    begin_tab_item=(void*)api->ResolveSymbol("ImGui::BeginTabItem");
    separator_ex=(void*)api->ResolveSymbol("ImGui::SeparatorEx");
    spacing=(void*)api->ResolveSymbol("ImGui::Spacing");
    dummy=(void*)api->ResolveSymbol("ImGui::Dummy");
    if(!begin_tab_bar||!end_tab_bar||!begin_tab_item||!separator_ex||!spacing||!dummy)return 2;
    int64_t flag;
    if(api->GetEnumValue("ImGuiTabItemFlags_","ImGuiTabItemFlags_NoPushId",&flag)!=DK2ML_OK)return 2;
    tab_no_push_id=(int)flag;
    if(api->GetEnumValue("ImGuiTabBarFlags_","ImGuiTabBarFlags_FittingPolicyScroll",&flag)!=DK2ML_OK)return 2;
    tab_scroll=(int)flag;
    if(api->GetEnumValue("ImGuiSeparatorFlags_","ImGuiSeparatorFlags_Horizontal",&flag)!=DK2ML_OK)return 2;
    separator_horizontal=(int)flag;
#endif
    int64_t integer_enum=0;
    if(!imgui_slider||api->GetEnumValue("ImGuiDataType_","ImGuiDataType_S32",&integer_enum)!=DK2ML_OK)return 2;
    integer_type=(int)integer_enum;
    const wchar_t *directory=api->GetConfigDir();
    if(!directory||wcslen(directory)+13>=MAX_PATH)return 2;
    swprintf(speed_config,MAX_PATH,L"%lssettings.ini",directory);
    default_slow_percent=GetPrivateProfileIntW(L"Speed",L"DefaultSlowPercent",50,speed_config);
    if(default_slow_percent<10||default_slow_percent>90)default_slow_percent=50;
    DK2ML_Option speed_option={0};speed_option.structSize=sizeof speed_option;
    speed_option.type=DK2ML_OPTION_INT;speed_option.label="Default slow-motion speed (*)";
    speed_option.tooltip="Starting custom speed for *; +/- subsequently updates the remembered speed. Requires Speed Changer 1.1.0.";
    speed_option.value=&default_slow_percent;speed_option.min=10;speed_option.max=90;speed_option.onChange=save_speed_default;
    if(api->AddOption(&speed_option)!=DK2ML_OK)return 2;
    off_imgui_io=api->GetFieldOffset("ImGuiContext","IO");
    off_want_capture_mouse=api->GetFieldOffset("ImGuiIO","WantCaptureMouse");
    off_want_capture_keyboard=api->GetFieldOffset("ImGuiIO","WantCaptureKeyboard");
    if(!imgui_begin||!imgui_end||!imgui_text||!imgui_button_ex||!imgui_context||
       !imgui_same_line||
       off_imgui_io<0||off_want_capture_mouse<0||off_want_capture_keyboard<0)return 2;
    const char *flags[]={"ImGuiWindowFlags_AlwaysAutoResize","ImGuiWindowFlags_NoSavedSettings"};
    for(unsigned i=0;i<sizeof flags/sizeof flags[0];i++) {
        int64_t value;
        if(api->GetEnumValue("ImGuiWindowFlags_",flags[i],&value)!=DK2ML_OK)return 3;
        window_flags|=(int)value;
    }
    DK2ML_MenuButton button={0};
    button.structSize=sizeof button;
    button.icon="data/textures/gui/rake5000_modules_normal.dds";
    button.iconHover="data/textures/gui/rake5000_modules_hover.dds";
    button.tooltip="Rake5000 Modules settings";
    button.onClick=open_settings;
    if(api->AddMenuButton(&button)!=DK2ML_OK||
       api->Subscribe(DK2ML_EVENT_FRAME,frame,NULL)!=DK2ML_OK)return 4;
    api->Log("Rake5000 Module Settings v0.1.8 ready; Moving Suppression in Movement & Combat");
    return 0;
}
