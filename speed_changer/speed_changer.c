#define WIN32_LEAN_AND_MEAN
#define UNICODE
#include <windows.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdatomic.h>
#include <wchar.h>
#include "speed_toggle.h"
#ifdef R5K_QOL_UI
#include "../qol_base/qol_ui.h"
#endif
#ifdef R5K_QOL_NATIVE
#include "../enemy_autopause/sdk/include/dk2ml.h"
static const DK2ML_API *native_api;
int speed_feature_enabled=1;
static bool native_enabled=true;
static bool speed_113;
static int64_t speed_running_state;
static wchar_t native_config[MAX_PATH];
#endif


__declspec(dllexport) const char *NAME = "DK2 Simulation Speed";
__declspec(dllexport) const char *DESCRIPTION = "Numpad +/- changes simulation speed by 10 percentage points; numpad * toggles 100% and the last custom speed.";

typedef bool (*register_fn)(HMODULE, uint32_t);
typedef void (*unregister_fn)(HMODULE);
typedef void (*event_fn)(uint32_t, void *);
typedef void (*log_fn)(HMODULE, int, const char *);
extern void speed_step_stub(void);
#ifdef R5K_QOL_UI
extern void speed_step_stub_113(void);
#endif
uintptr_t speed_replay_slot, speed_resume;
float speed_selected_factor = 1.0f;
const float speed_slow_factor = 0.3f;
#ifndef R5K_QOL_UI
static HMODULE self;
#endif
#ifndef R5K_QOL_NATIVE
static unregister_fn unregister_addon;
static event_fn unregister_event;
static log_fn logger;
static bool registered, installed, rejected;
#else
static bool installed,rejected;
#include "speed_gate.h"
#endif
static uintptr_t image;
static unsigned char *target, *trampoline;
static bool slowmo_override;
static int percent = 100;
static int last_custom_percent=50;
static _Atomic int requested_default_percent=50;
static int applied_default_percent=50;
__declspec(dllexport) const char *VERSION="1.1.0";
// Settings host sets only a request; the speed thread owns remembered speed.
__declspec(dllexport) void R5K_SetDefaultSlowSpeed(int value){
    if(value>=10&&value<=90)atomic_store(&requested_default_percent,value);
}
static bool held[3];
#ifdef R5K_QOL_UI
static ULONGLONG speed_changed_at;
static bool speed_was_changed;
// A negative value highlights a recent change. Zero hides the indicator.
static _Atomic int speed_hud_value;

int qol_speed_hud_value(void){
    return atomic_load(&speed_hud_value);
}

static void update_speed_hud(HWND owner, bool active){
    (void)owner;
    ULONGLONG age=GetTickCount64()-speed_changed_at;
    int value=slowmo_override?30:percent;
    if(!active||(!slowmo_override&&percent==100&&(!speed_was_changed||age>=2000)))value=0;
    if(speed_was_changed&&age<700)value=-value;
    atomic_store(&speed_hud_value,value);
}
#else
#include "speed_hud.h"
#endif


// Exactly one eight-byte multiplication instruction; both simulation and
// physics consume the converted result immediately following this site.
static const unsigned char expected_112[8] = {0xf3,0x0f,0x59,0x83,0x10,0xe2,0x07,0x00};
static const unsigned char expected_113[] = {0x0f,0x28,0xc1,0xf3,0x0f,0x59,0xc7};
static const unsigned char *original_instruction=expected_112;
static size_t original_length=sizeof expected_112;

static void log_text(int level, const char *message) {
#ifdef R5K_QOL_NATIVE
    (void)level;if(native_api)native_api->Log("%s",message);
#else
    if (logger) logger(self, level, message);
#endif
}
static bool read_at(uintptr_t address, void *out, SIZE_T count) {
    SIZE_T actual = 0;
    return address && ReadProcessMemory(GetCurrentProcess(), (void *)address,
        out, count, &actual) && actual == count;
}
static bool is_replay(void) {
#ifdef R5K_QOL_NATIVE
    if(speed_113)return native_api->IsReplaying()!=0;
#endif
    uintptr_t stream = 0;
    return read_at(image + 0x137cdb8, &stream, sizeof(stream)) && stream;
}

// A nearby relay allows a five-byte relative jump at the timestep site.
static unsigned char *allocate_near(uintptr_t address) {
    SYSTEM_INFO info;
    GetSystemInfo(&info);
    uintptr_t base = address & ~((uintptr_t)info.dwAllocationGranularity - 1);
    for (uintptr_t delta = info.dwAllocationGranularity; delta < 0x70000000;
         delta += info.dwAllocationGranularity) {
        unsigned char *p = VirtualAlloc((void *)(base + delta), 64,
            MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        if (p) return p;
    }
    return NULL;
}

// Absolute indirect jump from the nearby relay to the assembly stub.
static void jump_to(unsigned char out[14], const void *destination) {
    static const unsigned char op[6] = {0xff,0x25,0,0,0,0};
    memcpy(out, op, 6);
    uintptr_t address = (uintptr_t)destination;
    memcpy(out + 6, &address, 8);
}

static bool install(void) {
    IMAGE_DOS_HEADER dos;
    IMAGE_NT_HEADERS64 nt;
    unsigned char bytes[8];
    bool current=false;
#ifdef R5K_QOL_UI
    current=speed_113;
#endif
    image = (uintptr_t)GetModuleHandleW(NULL);
    if (!read_at(image, &dos, sizeof(dos)) || dos.e_magic != IMAGE_DOS_SIGNATURE ||
        !read_at(image + dos.e_lfanew, &nt, sizeof(nt)) ||
        nt.Signature != IMAGE_NT_SIGNATURE ||
        nt.FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64 ||
        nt.FileHeader.TimeDateStamp != (current?1795897808u:0x68cb9cfbu) ||
        nt.OptionalHeader.SizeOfImage != (current?21782528u:0x1476000u)) return false;
    target = (unsigned char *)(image + (current?0x2ebf07:0x2c3059));
    original_instruction=current?expected_113:expected_112;
    original_length=current?sizeof expected_113:sizeof expected_112;
    if (!read_at((uintptr_t)target, bytes, original_length) ||
        memcmp(bytes, original_instruction, original_length)) return false;
    trampoline = allocate_near((uintptr_t)target);
    if (!trampoline) return false;
    speed_replay_slot = current?0:image + 0x137cdb8;
    speed_resume = (uintptr_t)(target + original_length);
    const void *stub=(const void *)&speed_step_stub;
#ifdef R5K_QOL_UI
    if(current)stub=(const void *)&speed_step_stub_113;
#endif
    jump_to(trampoline, stub);
    DWORD old, ignored;
    if (!VirtualProtect(trampoline, 64, PAGE_EXECUTE_READ, &old)) goto fail;
    FlushInstructionCache(GetCurrentProcess(), trampoline, 64);
    unsigned char patch[8] = {0xe9,0,0,0,0,0x90,0x90,0x90};
    int64_t distance = (int64_t)(uintptr_t)trampoline - (int64_t)(uintptr_t)(target + 5);
    if (distance < INT32_MIN || distance > INT32_MAX) goto fail;
    int32_t relative = (int32_t)distance;
    memcpy(patch + 1, &relative, 4);
    if (!VirtualProtect(target, original_length, PAGE_EXECUTE_READWRITE, &old)) goto fail;
    memcpy(target, patch, original_length);
    VirtualProtect(target, original_length, old, &ignored);
    FlushInstructionCache(GetCurrentProcess(), target, original_length);
    installed = true;
    return true;
fail:
    VirtualFree(trampoline, 0, MEM_RELEASE);
    trampoline = NULL;
    return false;
}

static void apply_default_speed(void){
    int configured=atomic_load(&requested_default_percent);
    if(configured!=applied_default_percent){last_custom_percent=configured;applied_default_percent=configured;}
}

static void on_present(void *runtime) {
    (void)runtime;
    if (rejected) return;
    apply_default_speed();
#ifdef R5K_QOL_NATIVE
    speed_feature_enabled=native_enabled?1:0;
    if(!native_enabled)InterlockedExchange(&speed_update_armed,0);
    if(!native_enabled){
        percent=100;speed_selected_factor=1.0f;
        for(int i=0;i<3;i++){
#ifdef R5K_QOL_UI
            held[i]=qol_speed_key_down(i)!=0;
#else
            held[i]=(GetAsyncKeyState((int[]){VK_ADD,VK_SUBTRACT,VK_MULTIPLY}[i])&0x8000)!=0;
#endif
        }
        update_speed_hud(NULL,false);
        return;
    }
#endif
    if (!installed) {
        // Install only on the game's window thread, between frame updates.
        HWND window = GetForegroundWindow();
        DWORD pid = 0;
        if (!window || GetWindowThreadProcessId(window, &pid) != GetCurrentThreadId() ||
            pid != GetCurrentProcessId()) return;
        if (!install()) {
            rejected = true;
            log_text(1, "DK2 Speed: build/prologue check or hook setup failed; speed changer disabled.");
            return;
        }
        log_text(3, "DK2 Speed: simulation speed hook installed; numpad +/- and * ready.");
    }
    HWND window = GetForegroundWindow();
    DWORD pid = 0;
    GetWindowThreadProcessId(window, &pid);
    uintptr_t client = 0, gui = 0, manager = 0;
    uint32_t phase = 0;
    int captures = 0;
    bool active;
#ifdef R5K_QOL_NATIVE
    if(speed_113)active=pid==GetCurrentProcessId()&&!is_replay()&&
        native_api->GetGameState()==speed_running_state&&!native_api->IsGameMenuOpen()&&
        read_at(image+0x105d3f0,&client,sizeof client)&&client;
    else
#endif
    active = pid == GetCurrentProcessId() && !is_replay() &&
        read_at(image + 0xff0ea8, &client, 8) &&
        read_at(client + 0x290, &phase, 4) && phase == 1 &&
        read_at(image + 0xfe71a8, &gui, 8) &&
        read_at(image + 0xfe7180, &manager, 8) &&
        read_at(manager + 0x78, &captures, 4) && captures >= 0 && captures < 100;
    if (active && captures) {
        uintptr_t list = 0, top = 0, base = 0;
        active = read_at(manager + 0x70, &list, 8) &&
            read_at(list + 8 * (captures - 1), &top, 8) &&
            read_at(gui + 0xf0, &base, 8) && top == base;
    }
#ifndef R5K_QOL_UI
    const int keys[3] = {VK_ADD, VK_SUBTRACT, VK_MULTIPLY};
#endif
    for (int i = 0; i < 3; ++i) {
#ifdef R5K_QOL_UI
        bool down=qol_speed_key_down(i)!=0;
        if(active&&down&&!held[i]) {
#else
        bool down = (GetAsyncKeyState(keys[i]) & 0x8000) != 0;
        if (active && down && !held[i] &&
            !(GetAsyncKeyState(VK_CONTROL) & 0x8000) &&
            !(GetAsyncKeyState(VK_MENU) & 0x8000) &&
            !(GetAsyncKeyState(VK_SHIFT) & 0x8000)) {
#endif
            percent=change_speed(percent,i==0?10:-10,i==2,&last_custom_percent);
            speed_selected_factor = percent / 100.0f;
            speed_changed_at = GetTickCount64();
            speed_was_changed = true;
            char message[100];
            snprintf(message, sizeof(message), "DK2 Speed: simulation speed %d%%.", percent);
            log_text(3, message);
        }
        held[i] = down;
    }
    uint32_t flags = 0;
    bool override_now = active && read_at(client + 0x294, &flags, 4) && (flags & 0x20000);
    if (override_now != slowmo_override) {
        speed_changed_at = GetTickCount64();
        speed_was_changed = true;
    }
    slowmo_override = override_now;
    update_speed_hud(window, active);
}

#ifndef R5K_QOL_NATIVE
BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved) {
    if (reason == DLL_PROCESS_ATTACH) {
        self = instance;
        DisableThreadLibraryCalls(instance);
        HMODULE reshade = GetModuleHandleW(L"opengl32.dll");
        if (!reshade) return FALSE;
        register_fn reg = (register_fn)GetProcAddress(reshade, "ReShadeRegisterAddon");
        event_fn reg_event = (event_fn)GetProcAddress(reshade, "ReShadeRegisterEvent");
        unregister_addon = (unregister_fn)GetProcAddress(reshade, "ReShadeUnregisterAddon");
        unregister_event = (event_fn)GetProcAddress(reshade, "ReShadeUnregisterEvent");
        logger = (log_fn)GetProcAddress(reshade, "ReShadeLogMessage");
        if (!reg || !reg_event || !unregister_addon || !unregister_event || !reg(instance, 13)) return FALSE;
        registered = true;
        reg_event(75, (void *)&on_present);
        log_text(3, "DK2 Speed: add-on loaded.");
    } else if (reason == DLL_PROCESS_DETACH) {
        if (registered && unregister_event) unregister_event(75, (void *)&on_present);
        if (!reserved) destroy_speed_hud();
        // During process termination Windows reclaims code and allocations.
        if (installed && !reserved) {
            DWORD old, ignored;
            if (VirtualProtect(target, sizeof(expected_112), PAGE_EXECUTE_READWRITE, &old)) {
                memcpy(target, original_instruction, sizeof(expected_112));
                VirtualProtect(target, sizeof(expected_112), old, &ignored);
                FlushInstructionCache(GetCurrentProcess(), target, sizeof(expected_112));
            }
        }
        if (registered && unregister_addon) unregister_addon(instance);
        // Keep the tiny trampoline allocated on dynamic unload in case an
        // in-flight original call has not yet returned; process exit frees it.
    }
    return TRUE;
}
#else
static void native_speed_changed(const DK2ML_Option *option,void *user){
    (void)option;(void)user;
    speed_feature_enabled=native_enabled?1:0;
    if(!native_enabled)InterlockedExchange(&speed_update_armed,0);
    if(!native_enabled){percent=100;speed_selected_factor=1.0f;}
    WritePrivateProfileStringW(L"Speed",L"Enabled",native_enabled?L"1":L"0",native_config);
}
#ifdef R5K_QOL_UI
void qol_reset_speed_runtime(void){
    percent=100;speed_selected_factor=1.0f;
    last_custom_percent=atomic_load(&requested_default_percent);
    applied_default_percent=last_custom_percent;
    speed_changed_at=GetTickCount64();speed_was_changed=true;
}
#endif
void qol_speed_draw_enabled(void){
    bool (*checkbox)(const char *,bool *)=(void*)native_api->ResolveSymbol("ImGui::Checkbox");
    if(checkbox&&checkbox("Enable Speed Changer",&native_enabled))native_speed_changed(NULL,NULL);
}
static void native_frame(const DK2ML_Event *event,void *user){(void)event;(void)user;on_present(NULL);}
int qol_speed_init(const DK2ML_API *loader,const DK2ML_PluginInfo *info){
    (void)info;native_api=loader;
    unsigned version=loader->GetGameVersion();
#ifdef R5K_QOL_UI
    speed_113=version==113;
#endif
    if((version!=112&&!speed_113)||GetModuleHandleW(L"DK2SpeedChanger.addon64"))return 1;
    void *symbol=loader->ResolveSymbol("GameClient::UpdateCamera");
    if(!symbol)return 1;
    image=(uintptr_t)symbol-(speed_113?0x1920e0:0x187f70);
    if(speed_113&&(!loader->IsReplaying||!loader->IsGameMenuOpen||!loader->GetGameState||
        (uintptr_t)loader->ResolveSymbol("g_pGameClient")!=image+0x105d3f0||
        loader->GetEnumValue("GameClient::eCGameState","CGAMESTATE_RUNNING",&speed_running_state)!=DK2ML_OK))return 1;
    const wchar_t *directory=loader->GetConfigDir();
    if(!directory||wcslen(directory)+13>=MAX_PATH)return 1;
    swprintf(native_config,MAX_PATH,L"%lssettings.ini",directory);
    native_enabled=GetPrivateProfileIntW(L"Speed",L"Enabled",1,native_config)!=0;
    speed_feature_enabled=native_enabled?1:0;
#ifndef R5K_QOL_UI
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        (LPCWSTR)(uintptr_t)&qol_speed_init,&self);
#endif
    DK2ML_Option option={0};option.structSize=sizeof option;option.type=DK2ML_OPTION_BOOL;
    option.label="Enable Speed Changer";option.value=&native_enabled;option.onChange=native_speed_changed;
    if(loader->AddOption(&option)!=DK2ML_OK)return 1;
    speed_gate_return=image+0x2c3052;
    void *update=loader->ResolveSymbol(speed_113?"GameServer::Update":"GameCommon::Net_PollMessages");
    if((uintptr_t)update!=image+(speed_113?0x2ebac0:0x211f10)||loader->CreateSafeHook(update,speed_gate_pre,speed_gate_post,NULL)!=DK2ML_OK||
        loader->EnableHook(update)!=DK2ML_OK)return 1;
    if(loader->Subscribe(DK2ML_EVENT_FRAME,native_frame,NULL)!=DK2ML_OK)return 1;
    // The exact instruction/PE checks and patch execute on the game window thread,
    // never in DllMain or the offline loader validator.
    loader->Log("Native Speed Changer registered; timestep patch validation deferred to first focused frame.");
    return 0;
}
BOOL WINAPI DllMain(HINSTANCE instance,DWORD reason,LPVOID reserved){
    (void)instance;
    if(reason==DLL_PROCESS_DETACH&&!reserved){
        speed_feature_enabled=0;
#ifndef R5K_QOL_UI
        destroy_speed_hud();
#endif
        InterlockedExchange(&speed_update_armed,0);
        if(installed){DWORD old,ignored;if(VirtualProtect(target,original_length,PAGE_EXECUTE_READWRITE,&old)){
            memcpy(target,original_instruction,original_length);VirtualProtect(target,original_length,old,&ignored);
            FlushInstructionCache(GetCurrentProcess(),target,original_length);
        }}
    }
    return TRUE;
}
#endif
