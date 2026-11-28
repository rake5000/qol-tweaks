// Native Cursor Hotkeys plugin for dk2ml.
// Hooks and frame callbacks are managed by dk2ml.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdatomic.h>
#include "../native/bindings.h"
#ifdef R5K_QOL_UI
#include "../qol_base/qol_ui.h"
#endif
#include "toggle_actions.h"
#include "autoexec_defaults.h"
#include "grenade_hotkeys.h"
#include "body_hover.h"
#include "saved_plan_guard.h"

__declspec(dllexport) const char *DESCRIPTION = "Ctrl+S suppressive fire, F Flash/9-bang, Ctrl+F Frag, G Sprint, R Reload, T movement/contact mode, V wait for clear, Shift+1-4 go codes; Ctrl+1-4 autoexecution, C clears a hovered trooper's full plan; elsewhere C/Shift+C use existing wheel cancellation.";

static uintptr_t image;
#include "port_common.h"
#include "door_settings.h"
DK2ML_PLUGIN_MANIFEST(3, "Cursor Hotkeys", "1.4.6", "Rake5000", "", 112);


static bool checked, compatible;
static unsigned autoexec_requests;
enum { AUTOEXEC_ALL_REQUEST=16u };
static uintptr_t defaults_client, defaults_map;
static uint32_t defaults_last_time;
static AutoexecDefaults auto_defaults;
static _Thread_local bool own_autoexec;




static _Atomic unsigned pending;
static ULONGLONG deadline;
static _Atomic unsigned native_button_request;
static ULONGLONG native_button_since;
static uintptr_t native_button_wheel;
static uint32_t native_button_wp;
static unsigned native_button_updates;
static HWND request_window;
static HWND hooked_window;
static WNDPROC original_proc;
static bool swallowed[256];
static SRWLOCK queue_lock = SRWLOCK_INIT;
static unsigned queued_key, dispatch_key;
static POINT queued_point,dispatch_point;
static const UINT dispatch_message = WM_APP + 0x2d2;
static const UINT autoexec_message = WM_APP + 0x2d3;

static void log_text(int level, const char *message) {
    (void)level; api->Log("%s", message);
}

// Read only a handful of known fields, and fail closed on unreadable pointers.
static bool read_at(uintptr_t address, void *out, SIZE_T count) {
    SIZE_T actual = 0;
    return address && ReadProcessMemory(GetCurrentProcess(), (void *)address,
        out, count, &actual) && actual == count;
}



typedef struct { bool wheel_open, playing, other_panel; int captures; } state;
#ifdef R5K_QOL_UI
static bool basic_113;
static uintptr_t global_gui,global_manager;
static int wheel_offset,hidden_offset,list_offset,count_offset;
static int64_t running_state;

static bool resolve_basic_hotkeys(void) {
    global_gui=(uintptr_t)api->ResolveSymbol("g_pGameGUI");
    global_manager=(uintptr_t)api->ResolveSymbol("g_pGUIManager");
    wheel_offset=api->GetFieldOffset("GameGUI","m_pWaypointMenu");
    hidden_offset=api->GetFieldOffset("GUI::Item","m_hidden");
    int capture=api->GetFieldOffset("GUIManager","m_exclusiveInputStack");
    int items=api->GetFieldOffset("List<GUI::Item *>","m_list");
    int count=api->GetFieldOffset("List<GUI::Item *>","m_elements");
    // The 1.13 game-menu code reads the top capture through these fields.
    if(!global_gui||!global_manager||wheel_offset!=8||hidden_offset!=128||
        capture!=112||items!=8||count!=16)return false;
    list_offset=capture+items;
    count_offset=capture+count;
    return api->GetGameState&&api->IsReplaying&&api->IsGameMenuOpen&&
        api->GetEnumValue("GameClient::eCGameState","CGAMESTATE_RUNNING",&running_state)==DK2ML_OK;
}

static bool get_basic_state(state *s) {
    s->playing=api->GetGameState()==running_state&&!api->IsReplaying();
    if(!s->playing)return true;
    uintptr_t gui=0,manager=0,wheel=0;
    unsigned char hidden=1;
    if(!read_at(global_gui,&gui,sizeof gui)||!gui||
        !read_at(global_manager,&manager,sizeof manager)||!manager||
        !read_at(gui+wheel_offset,&wheel,sizeof wheel)||!wheel||
        !read_at(wheel+hidden_offset,&hidden,sizeof hidden)||hidden>1)return false;
    s->wheel_open=!hidden;
    if(!s->wheel_open){
        s->other_panel=api->IsGameMenuOpen()!=0;
        return true;
    }
    uintptr_t list=0,top=0;
    if(!read_at(manager+count_offset,&s->captures,sizeof s->captures)||s->captures<1||s->captures>=100||
        !read_at(manager+list_offset,&list,sizeof list)||!list||
        !read_at(list+(s->captures-1)*sizeof top,&top,sizeof top))return false;
    s->other_panel=top!=wheel;
    return true;
}
#endif
static bool get_state(state *s) {
#ifdef R5K_QOL_UI
    if(basic_113)return get_basic_state(s);
#endif
    uintptr_t gui = 0, wheel = 0, manager = 0, client = 0;
    unsigned char hidden = 1;
    int phase = 0;
    if (!read_at(image + 0xfe71a8, &gui, 8) ||
        !read_at(gui + 8, &wheel, 8) ||
        !read_at(wheel + 0x80, &hidden, 1) || hidden > 1 ||
        !read_at(image + 0xfe7180, &manager, 8) ||
        !read_at(manager + 0x78, &s->captures, 4) ||
        !read_at(image + 0xff0ea8, &client, 8) ||
        !read_at(client + 0x290, &phase, 4)) return false;
    s->wheel_open = hidden == 0;
    s->playing = phase == 1;
    if (s->captures < 0 || s->captures >= 100) return false;
    s->other_panel = false;
    if (s->captures) {
        // The base gameplay panel may remain on the capture stack. This is
        // the same exception used by GameGUI::IsAnyMenuOpened in this build.
        uintptr_t list = 0, top = 0, base_panel = 0;
        if (!read_at(manager + 0x70, &list, 8) ||
            !read_at(list + 8 * (s->captures - 1), &top, 8) ||
            !read_at(gui + 0xf0, &base_panel, 8)) return false;
        s->other_panel = top != base_panel;
    }
    return true;
}

static bool game_foreground(HWND *window) {
    DWORD pid = 0;
    *window = GetForegroundWindow();
    if (!*window) return false;
    GetWindowThreadProcessId(*window, &pid);
    char name[128];
    return pid == GetCurrentProcessId() &&
        GetClassNameA(*window, name, sizeof(name)) &&
        strcmp(name, "Door Kickers 2") == 0;
}

// Only the Ctrl-number bindings override an existing global shortcut.
// All requests are intercepted before the original window procedure sees them.
static unsigned binding_for(unsigned key) {
#ifdef R5K_QOL_UI
    unsigned binding=qol_resolve_key(key,qol_current_modifiers());
    return binding>=QOL_SPEED_UP?0:binding;
#else
    bool control = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
    bool alt = (GetKeyState(VK_MENU) & 0x8000) != 0;
    bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
    return resolve_binding(key, shift, control, alt);
#endif
}

// Hover-body clear uses the same per-human/action commands as ResetSavedPlan,
// never the mission-wide reset itself. Resolve IDs again during normal input update.
static bool try_toggle_order(unsigned binding);
#ifdef R5K_QOL_UI
static bool try_toggle_order_113(unsigned binding);
#endif
static void dispatch_grenade(unsigned binding);
static void dispatch_door(unsigned binding);
static void dispatch_suppression(void);
static uintptr_t waypoint_event_fn;
static unsigned toggle_trace_count;
static uintptr_t actionwp_get_type;
static void (*play_gui_click)(void);
static bool clear_button_available(unsigned binding){
    if(binding!='C'&&binding!=BIND_CLEAR_PATH)return false;
    uintptr_t gui=0,wheel=0;
    if(!read_at(image+0xfe71a8,&gui,8)||!gui||!read_at(gui+8,&wheel,8)||!wheel)return false;
    const char *name=binding=='C'?"delete_path":"delete_waypoint";
    void *button=api->GuiFind((void*)wheel,name);
    unsigned char accepts=0;
    return button&&api->GuiIsShown(button)&&read_at((uintptr_t)button+144,&accepts,1)&&accepts;
}
static LRESULT close_waypoint_wheel(WNDPROC forward,HWND window) {
    LPARAM scan=(LPARAM)MapVirtualKeyW(VK_ESCAPE,MAPVK_VK_TO_VSC)<<16;
    LRESULT result=CallWindowProcW(forward,window,WM_KEYDOWN,VK_ESCAPE,scan|1);
    CallWindowProcW(forward,window,WM_KEYUP,VK_ESCAPE,scan|0xc0000001u);
    return result;
}

static LRESULT send_toggle_key(WNDPROC forward,HWND window,UINT message,WPARAM key,LPARAM data,unsigned binding) {
    bool grenade=binding==BIND_FLASH||binding==BIND_FRAG;
    bool suppression=binding==BIND_SUPPRESSION;
#ifdef R5K_QOL_UI
    if(basic_113&&!grenade&&!suppression){
        if(message==WM_KEYDOWN&&try_toggle_order_113(binding))return close_waypoint_wheel(forward,window);
        return CallWindowProcW(forward,window,message,key,data);
    }
#endif
    bool door=binding==BIND_DOOR||binding==BIND_DOOR_ONLY;
    if(message==WM_KEYDOWN&&(grenade||door||suppression)) {
        // Do not mutate temporary native waypoints between input updates.
        // GameInput::Update refreshes them before GUIManager::UpdateInput.
        atomic_store(&native_button_request,binding);
        return 0;
    }
    if(message==WM_KEYDOWN&&try_toggle_order(binding)) {
        // Native XML's matching key action is bypassed after removal. Escape uses
        // the wheel's existing hide handler, preserving its normal input cleanup.
        return close_waypoint_wheel(forward,window);
    }
    if(message==WM_KEYDOWN&&play_gui_click&&clear_button_available(binding))play_gui_click();
    return CallWindowProcW(forward,window,message,key,data);
}
static _Atomic bool clear_all_requested;
static uintptr_t protected_plan_client,protected_plan_map;
static _Atomic uint32_t clear_body_id;
static uintptr_t hovered_human_fn,delete_path_fn,remove_visuals_fn,net_event_fn,write_bits_fn,write_varuint_fn;
static bool friendly_human(uintptr_t human,uintptr_t client,uint32_t *id) {
    uint32_t player=0,owners=0;unsigned char enabled_human=0;
    unsigned player_offset=0x10581c;
#ifdef R5K_QOL_UI
    if(basic_113)player_offset=0x10583c;
#endif
    return human&&read_at(client+player_offset,&player,4)&&player<32&&
        read_at(human+0x32c,&owners,4)&&owners==(1u<<player)&&
        read_at(human+0x333,&enabled_human,1)&&enabled_human!=0&&
        read_at(human+0x10,id,4)&&*id!=0;
}
static bool cursor_hits_body(uintptr_t client,uintptr_t human) {
    POINT mouse;RECT area;float eye[3],view[16],projection[16],low[3],high[3];
    uintptr_t camera=client+0x107274;
    if(!hooked_window||!GetCursorPos(&mouse)||!ScreenToClient(hooked_window,&mouse)||
        !GetClientRect(hooked_window,&area)||!PtInRect(&area,mouse)||area.right<1||area.bottom<1||
        !read_at(camera+24,eye,sizeof(eye))||!read_at(camera+48,view,sizeof(view))||
        !read_at(camera+176,projection,sizeof(projection))||
        !read_at(human+208,low,sizeof(low))||!read_at(human+220,high,sizeof(high))||
        !isfinite(projection[0])||!isfinite(projection[5])||projection[0]<=0||projection[5]<=0||
        fabsf(projection[15])>.001f)return false;
    float x=(2.0f*mouse.x/area.right-1)/projection[0];
    float y=(1-2.0f*mouse.y/area.bottom)/projection[5],direction[3];
    for(unsigned i=0;i<3;++i)direction[i]=view[i]*x+view[4+i]*y-view[8+i];
    return body_ray_hit(eye,direction,low,high);
}
static uint32_t body_under_cursor(void) {
    uintptr_t input=0,client=0;uint32_t slot=0,id=0;
    if(!read_at(image+0xfe8a68,&input,8)||!read_at(input+0xc,&slot,4)||slot>100000||
        !read_at(image+0xff0ea8,&client,8)||!client)return 0;
    typedef uintptr_t (*hover_fn)(void);
    // Native GetHoveredHuman resolves both ActionWP and path proxies to their
    // owner. A pie marker may cover the body, so raw entity identity is NOT a
    // body test. The independent camera ray still rejects path-only hover.
    uintptr_t human=((hover_fn)hovered_human_fn)();
    if(!friendly_human(human,client,&id)||!cursor_hits_body(client,human))return 0;
    api->Log("Cursor Hotkeys 1.4.4: C body target owner=%u raw-hover=%u.",id,slot);
    return id;
}
static bool queue_empty_plan(uintptr_t human) {
 typedef void*(*event_fn)(void*,int,int);
 typedef void(*bits_fn)(void*,uint32_t,int);
 typedef void(*var_fn)(void*,uint32_t);
 void *packet=((event_fn)net_event_fn)((void*)human,0x21,0xf);
 if(!packet)return false;
 // Exact native DeletePathFromIndexOnward opcode: replace from point0 with0 points.
 // Server OnPlayerModifedPath then calls DeleteActionWaypoints, independent of IDs.
 ((bits_fn)write_bits_fn)(packet,2,3);
 ((var_fn)write_varuint_fn)(packet,0);((var_fn)write_varuint_fn)(packet,0);
 return true;
}
static bool clear_hovered_plan(uint32_t id,uintptr_t client) {
    uintptr_t human=0;uint32_t checked_id=0;
    if(id>100000||!read_at(client+0x2e8+id*8,&human,8)||
        !friendly_human(human,client,&checked_id)||checked_id!=id)return false;
    // Gather first: removing visuals can change the intrusive action list.
    uintptr_t head=0,node=0,wp=0,waypoints[4096];unsigned count=0,scanned=0;
    bool snapshot_ok=read_at(image+0x7cb540,&head,8);
    if(snapshot_ok&&head)snapshot_ok=read_at(head+0x18,&wp,8);
    while(snapshot_ok&&wp) {
        uint32_t owner_id=0;
        if(++scanned>4096||!read_at(wp+0x220,&owner_id,4)){snapshot_ok=false;break;}
        if(owner_id==id)waypoints[count++]=wp;
        if(!read_at(wp+0x1d8,&node,8)){snapshot_ok=false;break;}
        if(!node||node==head)break;
        if(!read_at(node+0x18,&wp,8)){snapshot_ok=false;break;}
    }
    typedef void (*remove_fn)(void*);
    typedef void* (*event_fn)(void*,int,int);
    typedef void (*bits_fn)(void*,uint32_t,int);
    typedef void (*delete_fn)(void*,int,float,float);
    bool removed=false;
    for(unsigned i=0;i<count;++i) {
        uint32_t action_id=0;int ignore=0;
        if(!read_at(waypoints[i]+0x10,&action_id,4))continue;
        SIZE_T written=0;
        if(!WriteProcessMemory(GetCurrentProcess(),(void*)(waypoints[i]+0x1f0),&ignore,4,&written)||written!=4)continue;
        WriteProcessMemory(GetCurrentProcess(),(void*)(waypoints[i]+0x228),&ignore,4,&written);
        ((remove_fn)remove_visuals_fn)((void*)waypoints[i]);
        void *packet=((event_fn)net_event_fn)((void*)human,0x23,0xf);
        if(packet){((bits_fn)write_bits_fn)(packet,action_id,32);removed=true;}
    }
    int points=-1;bool have_count=read_at(human+0xd90+56,&points,4);
    ((delete_fn)delete_path_fn)((void*)human,0,0,0);
    // Native helper returns without sending anything for point counts0/1.
    // Always clear authoritative owner orders in that case, even unseen late pies.
    bool empty_sent=false;if(!have_count||points<=1)empty_sent=queue_empty_plan(human);
    api->Log("Cursor Hotkeys 1.4.4: full clear owner=%u actions=%u points=%d snapshot=%d empty-request=%d.",id,count,points,snapshot_ok,empty_sent);
    return removed||(have_count&&points>1);
}
static bool clear_every_plan(uintptr_t client,uintptr_t map) {
    int saved_size=0;
    if(!read_at(client+0xfe280,&saved_size,4)||saved_size<=0||saved_size>524288) {
        api->Log("Alt+C: no saved planning-stage plan available; orders left unchanged.");return false;
    }
    uintptr_t head=0,node=0,human=0;uint32_t ids[1024];unsigned count=0,scanned=0;
    if(!read_at(image+0x7cb820,&head,8))return false;
    if(head&&!read_at(head+0x18,&human,8))return false;
    while(human) {
        uint32_t id=0;
        if(++scanned>4096)return false;
        if(friendly_human(human,client,&id)) {
            if(count>=1024)return false;
            ids[count++]=id;
        }
        if(!read_at(human+0x1d8,&node,8))return false;
        if(!node||node==head)break;
        if(!read_at(node+0x18,&human,8))return false;
    }
    protected_plan_client=client;protected_plan_map=map;
    bool changed=false;
    for(unsigned i=0;i<count;++i)changed|=clear_hovered_plan(ids[i],client);
    api->Log("Alt+C: cleared live orders for %u friendly troops; saved plan preserved (%d bytes).",count,saved_size);
    return changed;
}
static int preserve_plan_save(DK2ML_Regs *regs,void *user) {
    (void)user;
    uintptr_t client=0,map=0;
    if(!enabled||!protected_plan_client||!regs->stack||!read_at(image+0xff0ea8,&client,8)||
        !read_at(client+0x28,&map,8))return DK2ML_CALL_ORIGINAL;
    if(client!=protected_plan_client||map!=protected_plan_map) {
        protected_plan_client=protected_plan_map=0;return DK2ML_CALL_ORIGINAL;
    }
    if(preserve_automatic_plan((uintptr_t)regs->stack[0]-image,(uintptr_t)DK2ML_Arg(regs,3),
        client,map,protected_plan_client,protected_plan_map))return DK2ML_SKIP_ORIGINAL;
    // Explicit saves to this plan are user intent; unrelated save buffers are untouched.
    if((uintptr_t)DK2ML_Arg(regs,3)==client+0x7e280)protected_plan_client=protected_plan_map=0;
    return DK2ML_CALL_ORIGINAL;
}
#ifdef R5K_QOL_UI
static uintptr_t global_client_113,waypoint_type_113;
static bool (*waypoint_event_113)(void*,unsigned,const void*);
static void (*change_waypoint_action_113)(void*,unsigned,unsigned,uint32_t);
enum { waypoint_pool_limit_113=12000 };
static unsigned go_code_unchecked_113,delete_waypoint_113;
static int wheel_id_offset_113,entity_pool_offset_113,action_offset_113,go_code_offset_113,owner_offset_113;

static bool resolve_go_code_toggle_113(void) {
    global_client_113=(uintptr_t)api->ResolveSymbol("g_pGameClient");
    waypoint_type_113=(uintptr_t)api->ResolveSymbol("ActionWP_Client::GetType");
    waypoint_event_113=(void*)api->ResolveSymbol("GameGUI::WaypointMenu_OnEvent");
    int user_data=api->GetFieldOffset("GUI::Item","m_userData");
    int id=api->GetFieldOffset("GUI::sUserData","id");
    int action=api->GetFieldOffset("ActionWP_Client","m_action");
    int go_code=api->GetFieldOffset("sActionWP","gocode");
    int owner=api->GetFieldOffset("sActionWP","ownerId");
    entity_pool_offset_113=api->GetFieldOffset("GameCommon","m_entityPool");
    int64_t unchecked=0;
    // These layouts and the unchecked case were checked against the 1.13 handler.
    if(!global_client_113||!waypoint_type_113||!waypoint_event_113||
        user_data!=848||id!=8||action!=496||go_code!=56||owner!=48||entity_pool_offset_113!=744||
        api->GetTypeSize("GUI::sEventParams")!=24||
        api->GetEnumValue("GUI::Events::eEventType","GUI_GAME_WAYPOINT_GOCODE_UNCHECKED",&unchecked)!=DK2ML_OK||
        unchecked!=86)return false;
    wheel_id_offset_113=user_data+id;
    action_offset_113=action;
    go_code_offset_113=action+go_code;
    owner_offset_113=action+owner;
    go_code_unchecked_113=(unsigned)unchecked;
    return true;
}

static bool resolve_action_toggle_113(void) {
    int64_t remove=0,ignore=-1,none=-1;
    change_waypoint_action_113=(void*)api->ResolveSymbol("ActionWP_Client::OnChangeAction");
    if(!change_waypoint_action_113||
        api->GetEnumValue("GUI::Events::eEventType","GUI_GAME_WAYPOINT_DELETE",&remove)!=DK2ML_OK||remove!=82||
        api->GetEnumValue("eActionType","ACTION_IGNORE",&ignore)!=DK2ML_OK||ignore!=0||
        api->GetEnumValue("Inventory::eType","NONE",&none)!=DK2ML_OK||none!=0)return false;
    delete_waypoint_113=(unsigned)remove;
    return true;
}

static bool try_toggle_order_113(unsigned binding) {
    unsigned requested_code=waypoint_order_code(binding),requested_action=waypoint_order_action(binding);
    if(!go_code_unchecked_113||(!requested_code&&(!requested_action||!delete_waypoint_113)))return false;
    uintptr_t gui=0,wheel=0,client=0,waypoint=0,vtable=0,type=0,owner=0;
    uint32_t id=0,go_code=0,owner_id=0;
    if(!read_at(global_gui,&gui,sizeof gui)||!gui||
        !read_at(gui+wheel_offset,&wheel,sizeof wheel)||!wheel||
        !read_at(wheel+wheel_id_offset_113,&id,sizeof id)||!id||id>=waypoint_pool_limit_113||
        !read_at(global_client_113,&client,sizeof client)||!client||
        !read_at(client+entity_pool_offset_113+id*sizeof waypoint,&waypoint,sizeof waypoint)||!waypoint||
        !read_at(waypoint,&vtable,sizeof vtable)||!vtable||
        !read_at(vtable+sizeof type,&type,sizeof type)||type!=waypoint_type_113||
        !read_at(waypoint+go_code_offset_113,&go_code,sizeof go_code)||
        !read_at(waypoint+owner_offset_113,&owner_id,sizeof owner_id)||!owner_id||owner_id>=waypoint_pool_limit_113||
        !read_at(client+entity_pool_offset_113+owner_id*sizeof owner,&owner,sizeof owner)||!owner)return false;
    const uint64_t parameters[3]={0};
    if(requested_code)
        return go_code==requested_code&&waypoint_event_113((void*)gui,go_code_unchecked_113,parameters);
    uint32_t action=0;
    if(!read_at(waypoint+action_offset_113,&action,sizeof action)||action!=requested_action)return false;
    if(!go_code)return waypoint_event_113((void*)gui,delete_waypoint_113,parameters);
    // Use the native action update to remove only the order and retain its go-code.
    change_waypoint_action_113((void*)waypoint,0,0,0);
    return true;
}
#endif

static bool try_toggle_order(unsigned binding) {
    unsigned event=0,requested=0;
    if(binding>='1'&&binding<='4'){event=77;requested=binding-'1'+1;}
    else if(binding=='V'){event=77;requested=5;}
    else if(binding=='R'){event=73;requested=1;}
    else if(binding=='G'){event=73;requested=8;}
    else if(binding=='T'){event=73;requested=11;}
    else return false;
    uintptr_t gui=0,wheel=0,client=0,wp=0,table=0,type_fn=0,human=0;
    uint32_t id=0,action=0,gocode=0,owner=0,checked_id=0;
    if(!enabled||!read_at(image+0xfe71a8,&gui,8)||!read_at(gui+8,&wheel,8)||
        !read_at(wheel+848+8,&id,4)||id>100000||!id||
        !read_at(image+0xff0ea8,&client,8)||!read_at(client+0x2e8+id*8,&wp,8)||!wp||
        !read_at(wp,&table,8)||!read_at(table+8,&type_fn,8)||type_fn!=actionwp_get_type||
        !read_at(wp+0x220,&owner,4)||owner>100000||
        !read_at(client+0x2e8+owner*8,&human,8)||!friendly_human(human,client,&checked_id)||
        !read_at(wp+0x1f0,&action,4)||!read_at(wp+0x228,&gocode,4)) {
        if(toggle_trace_count++<24)api->Log("Cursor toggle: key=%c context rejected; waypoint=%u owner=%u type=%llx.",binding,id,owner,(unsigned long long)(type_fn?type_fn-image:0));
        return false;
    }
    unsigned replacement=toggle_wheel_event(binding,event,requested,action,gocode);
    bool preserve_code=event==73&&gocode&&action==requested;
    if(toggle_trace_count++<24)api->Log("Cursor toggle: key=%c waypoint=%u action=%u code=%u event=%u replacement=%u.",binding,id,action,gocode,event,replacement);
    if(replacement==event&&!preserve_code)return false;
    // Native clear-code/delete-action need no item parameter. Changing an action
    // to IGNORE with a retained code uses iParam1=iParam2=0.
    unsigned char params[128]={0};
    typedef bool (*event_fn)(void*,unsigned,void*);
    ((event_fn)waypoint_event_fn)((void*)gui,replacement,params);
    api->Log("Cursor Hotkeys: toggled off %c on waypoint %u (retained code=%u).",binding,id,preserve_code?gocode:0);
    return true;
}
typedef struct {
    uintptr_t human;
    unsigned desired;
    int slot;
    void *button;
    char name[192];
    unsigned visited;
} GrenadeChoice;
static unsigned wheel_clicked_event=73;
static bool read_item_name(uintptr_t item,char name[192]) {
    uintptr_t text=0;
    if(!read_at(item+40,&text,8)||!text)return false;
    for(unsigned i=0;i<192;++i) {
        if(!read_at(text+i,name+i,1))return false;
        if(!name[i])return true;
    }
    return false;
}
static void scan_grenade_buttons(void *node,unsigned depth,GrenadeChoice *best) {
    if(!node||depth>12||++best->visited>512||!api->GuiIsShown(node))return;
    unsigned char accepts=0;int count=0;uintptr_t actions=0;
    // EVENT_CLICK=2; GUI::Item::sItemEventProperties has a List<sAction*> at +0.
    uintptr_t click=(uintptr_t)node+168+2*48;
    if(read_at((uintptr_t)node+144,&accepts,1)&&accepts&&
        read_at(click+16,&count,4)&&count>0&&count<=32&&read_at(click+8,&actions,8)) {
        for(int i=0;i<count;++i) {
            uintptr_t action=0;uint32_t kind=0,event=0,type=0;int slot=0;
            if(!read_at(actions+i*8,&action,8)||!action||!read_at(action+8,&kind,4)||kind!=6||
                !read_at(action+40,&event,4)||event!=wheel_clicked_event||!read_at(action+64,&type,4)||type!=23||
                !read_at(action+68,&slot,4)||slot<=0||slot>=27||slot>=best->slot)continue;
            uintptr_t equipment=best->human+1072+(uintptr_t)slot*88,item=0;int quantity=0;unsigned equipment_type=0;
            char name[192];
            if(!read_at(equipment,&item,8)||!item||!read_at(item+32,&equipment_type,4)||equipment_type!=3||
                !read_at(equipment+12,&quantity,4)||
                !read_item_name(item,name)||!grenade_candidate(best->desired,name,quantity,true,slot,best->slot))continue;
            best->button=node;memcpy(best->name,name,strlen(name)+1);best->slot=slot;
        }
    }
    void *children[128];int n=api->GuiChildren(node,children,128);
    if(n<0||n>128)return;
    for(int i=0;i<n;++i)scan_grenade_buttons(children[i],depth+1,best);
}
typedef struct {
    uintptr_t wheel,client,waypoint,human;
    uint32_t waypoint_id,human_id;
} ActionWheelContext;

static bool read_action_wheel(ActionWheelContext *context) {
    uintptr_t gui=0,table=0,fn=0;
    uint32_t owner=0;
    ActionWheelContext c={0};
    uintptr_t gui_slot=image+0xfe71a8,client_slot=image+0xff0ea8,type=actionwp_get_type;
    unsigned pool=0x2e8,limit=100001,owner_offset=0x220,wheel_id=856;
#ifdef R5K_QOL_UI
    if(basic_113){
        gui_slot=global_gui;client_slot=global_client_113;type=waypoint_type_113;
        pool=entity_pool_offset_113;limit=waypoint_pool_limit_113;
        owner_offset=owner_offset_113;wheel_id=wheel_id_offset_113;
    }
#endif
    if(!type||!read_at(gui_slot,&gui,8)||!gui||!read_at(gui+8,&c.wheel,8)||!c.wheel||
        !api->GuiIsShown((void*)c.wheel)||
        !read_at(c.wheel+wheel_id,&c.waypoint_id,4)||!c.waypoint_id||c.waypoint_id>=limit||
        !read_at(client_slot,&c.client,8)||!c.client||
        !read_at(c.client+pool+c.waypoint_id*8,&c.waypoint,8)||!c.waypoint||
        !read_at(c.waypoint,&table,8)||!table||!read_at(table+8,&fn,8)||fn!=type||
        !read_at(c.waypoint+owner_offset,&owner,4)||owner>=limit||
        !read_at(c.client+pool+owner*8,&c.human,8)||!friendly_human(c.human,c.client,&c.human_id))
        return false;
    *context=c;
    return true;
}

static bool switch_action_wheel(uintptr_t wheel) {
    static const char *const switches[]={"#switch_to_path_options","#switch_to_target_options"};
    for(unsigned i=0;i<sizeof(switches)/sizeof(switches[0]);++i) {
        void *button=api->GuiFind((void*)wheel,switches[i]);
        unsigned char accepts=0;
        if(button&&api->GuiIsShown(button)&&read_at((uintptr_t)button+144,&accepts,1)&&accepts)
            return api->GuiClick(button)==DK2ML_OK;
    }
    return false;
}

static bool refresh_action_wheel(ActionWheelContext *context) {
    // Switching pages can rebuild the GUI tree. Keep the original order target.
    ActionWheelContext next;
    if(!read_action_wheel(&next)||next.client!=context->client||
        next.waypoint_id!=context->waypoint_id||next.waypoint!=context->waypoint||
        next.human!=context->human||next.human_id!=context->human_id)return false;
    *context=next;
    return true;
}

static void dispatch_grenade(unsigned binding) {
    ActionWheelContext context;
    if(!read_action_wheel(&context)) {
        api->Log("Grenade hotkey: no valid friendly trooper context; no order placed.");return;
    }
    GrenadeChoice best={.human=context.human,.slot=27,.desired=binding==BIND_FRAG?2:1};
    scan_grenade_buttons((void*)context.wheel,0,&best);
    if(!best.button&&switch_action_wheel(context.wheel)) {
        if(!refresh_action_wheel(&context))return;
        best.visited=0;
        scan_grenade_buttons((void*)context.wheel,0,&best);
    }
    if(!best.button) {
        api->Log("Grenade hotkey: no available %s action; no order placed.",best.desired==1?"flash/9-bang":"frag");
        api->GuiShow((void*)context.wheel,0);
        return;
    }
    uintptr_t wp=context.waypoint;
    uint32_t before=0,after=0;read_at(wp+0x1f0,&before,4);
    DK2ML_Status result=api->GuiClick(best.button);
    read_at(wp+0x1f0,&after,4);
    api->Log("Grenade hotkey: native click %s slot=%d trooper=%u status=%d action before=%u after=%u.",best.name,best.slot,context.human_id,result,before,after);
}

#include "suppression_hotkey.h"
#include "door_sequence.h"

// Only inspect/execute this request on the native input-update boundary.
static void button_input_post(DK2ML_Regs *regs,void *user) {
    (void)regs;(void)user;
    if(position_request.active){position_only_post();return;}
    unsigned binding=atomic_load(&native_button_request);
    if(!binding){native_button_since=0;native_button_updates=0;return;}
    state s={0};HWND window=NULL;uintptr_t gui=0,wheel=0;uint32_t wp=0;
    uintptr_t gui_slot=image+0xfe71a8;
    unsigned wheel_id=856;
#ifdef R5K_QOL_UI
    if(basic_113){gui_slot=global_gui;wheel_id=wheel_id_offset_113;}
#endif
    if(!enabled||!game_foreground(&window)||window!=hooked_window||
       !get_state(&s)||!s.playing||!s.wheel_open||
       !read_at(gui_slot,&gui,8)||!read_at(gui+8,&wheel,8)||
       !read_at(wheel+wheel_id,&wp,4)||!wp) {
        atomic_store(&native_button_request,0);native_button_since=0;
        native_button_updates=0;return;
    }
    ULONGLONG now=GetTickCount64();
    if(!native_button_since||native_button_wheel!=wheel||native_button_wp!=wp) {
        native_button_since=now;native_button_wheel=wheel;
        native_button_wp=wp;native_button_updates=0;
    }
    ++native_button_updates;
    if(now-native_button_since<100||native_button_updates<3)return;
    // Never repeat a native click: that could toggle or confirm an order twice.
    atomic_store(&native_button_request,0);
    api->Log("Cursor native button: stable waypoint=%u after %llu ms / %u input updates; dispatch key=%u.",
        wp,(unsigned long long)(now-native_button_since),native_button_updates,binding);
    native_button_since=0;native_button_updates=0;
    if(binding==BIND_FLASH||binding==BIND_FRAG)dispatch_grenade(binding);
    else if(binding==BIND_SUPPRESSION)dispatch_suppression();
    else dispatch_door(binding);
    // The button's own OnClick owns Hide/OnClose, just as a manual click does.
    // Do not inject Escape or perform additional input cleanup here.
}


static bool resolve_hover_clear(void) {
    int64_t grenade_type=-1,trigger_event=-1,click_event=-1;
    if(api->GetEnumValue("EquipmentType::eType","GRENADE",&grenade_type)!=DK2ML_OK||grenade_type!=3||
        api->GetEnumValue("GUI::eAction","ACTION_TRIGGER_EVENT",&trigger_event)!=DK2ML_OK||trigger_event!=6||
        api->GetEnumValue("GUI::Item::eItemEventType","EVENT_CLICK",&click_event)!=DK2ML_OK||click_event!=2)return false;
    waypoint_event_fn=(uintptr_t)api->ResolveSymbol("GameGUI::WaypointMenu_OnEvent");
    actionwp_get_type=(uintptr_t)api->ResolveSymbol("ActionWP_Client::GetType");
    hovered_human_fn=(uintptr_t)api->ResolveSymbol("GameInput::GetHoveredHuman");
    delete_path_fn=(uintptr_t)api->ResolveSymbol("Human_Client::DeletePathFromIndexOnward");
    remove_visuals_fn=(uintptr_t)api->ResolveSymbol("ActionWP_Client::RemoveVisuals");
    net_event_fn=(uintptr_t)api->ResolveSymbol("Entity_Common::Net_AddReliableEvent");
    write_bits_fn=(uintptr_t)api->ResolveSymbol("BitPacker::WriteBits");
    write_varuint_fn=(uintptr_t)api->ResolveSymbol("BitPacker::WriteVarUInt");
    return waypoint_event_fn==image+0xe5890&&api->GetTypeSize("GUI::sEventParams")<=128&&
        api->GetTypeSize("GUI::sEventParams")>=24&&actionwp_get_type==image+0x98050&&
        api->GetFieldOffset("GUI::Item","m_userData")==848&&
        api->GetFieldOffset("GameInput","m_grenadeTargetRequested")==440&&
        api->GetFieldOffset("Door_Client","m_bOpened")==496&&api->GetFieldOffset("Door_Client","m_bLocked")==497&&
        api->GetFieldOffset("Human_Client","m_path")==3472&&
        api->GetFieldOffset("GameClient","m_savedPlan")==0x7e280&&
        api->GetFieldOffset("GameClient","m_savedPlanSize")==0xfe280&&
        api->GetFieldOffset("Entity_Common","m_minWorld")==208&&
        api->GetFieldOffset("Entity_Common","m_maxWorld")==220&&
        api->GetFieldOffset("Camera","m_matView")==48&&
        api->GetFieldOffset("Camera","m_matProjection")==176&&
        api->GetFieldOffset("GUI::Item","m_eventProperties")==168&&
        api->GetFieldOffset("GUI::Item","m_bAcceptInput")==144&&
        api->GetFieldOffset("Human_Client","m_inventory")==1072&&
        api->GetTypeSize("Equipment_Client")==88&&
        api->GetFieldOffset("Equipment_Client","quantity")==12&&
        api->GetFieldOffset("Equipment_Template","name")==40&&
        api->GetTypeSize("GUI::sAction")==80&&
        api->GetTypeSize("GUI::sEventParams")==24&&
        hovered_human_fn==image+0x137160&&delete_path_fn==image+0xb9960&&
        remove_visuals_fn==image+0x9dd40&&net_event_fn==image+0x1f90d0&&write_bits_fn==image+0x21ff70&&write_varuint_fn==image+0x2205c0&&
        api->GetFieldOffset("ActionWP_Client","m_action")==0x1f0;
}

static bool toggle_autoexecution(unsigned code) {
    uintptr_t client = 0;
    uint32_t client_flags = 0;
    unsigned char saved_shift = 0;
#ifdef R5K_QOL_UI
    if(basic_113){
        if(code<1||code>4||!read_at(global_client_113,&client,sizeof client)||!client)return false;
        bool saved_own=own_autoexec;
        own_autoexec=true;
        // ExecuteGoCode's Shift branch tail-calls this command with the client and code.
        ((void (*)(void*,int))(image+0x189ab0))((void*)client,(int)code);
        own_autoexec=saved_own;
        return true;
    }
#endif
    if (code < 1 || code > 4 ||
        !read_at(image + 0xff0ea8, &client, sizeof(client)) ||
        !read_at(client + 0x7e210, &client_flags, sizeof(client_flags)) ||
        (client_flags & 0x80) ||
        !read_at(image + 0xff2099, &saved_shift, 1)) return false;

    // Scope Shift to this native command during the regular input update.
    unsigned char *shift_state = (unsigned char *)(image + 0xff2099);
    *shift_state = 1;
    typedef void (*execute_go_code_fn)(void *, int);
    bool saved_own=own_autoexec;own_autoexec=true;
    ((execute_go_code_fn)(image + 0x188740))((void *)client, (int)code);
    own_autoexec=saved_own;
    *shift_state = saved_shift;
    log_text(3, "Cursor Hotkeys: native go-code autoexecution toggled.");
    return true;
}

static void defaults_loaded(const DK2ML_Event *event,void *user) {
    (void)event;(void)user;defaults_reset(&auto_defaults);door_request.pending=false;
    protected_plan_client=protected_plan_map=0;atomic_store(&clear_all_requested,false);
    api->Log("Cursor Hotkeys: mission load; autoexecution defaults pending.");
}
static void saved_plan_loaded(DK2ML_Regs *regs,void *user) {
    (void)regs;(void)user;defaults_reset(&auto_defaults);door_request.pending=false;
    protected_plan_client=protected_plan_map=0;atomic_store(&clear_all_requested,false);
    api->Log("Cursor Hotkeys: saved plan loaded; autoexecution defaults pending.");
}
static int observe_go_code(DK2ML_Regs *regs,void *user) {
    (void)user;unsigned char shift=0;
#ifdef R5K_QOL_UI
    if(basic_113){
        if(enabled&&!own_autoexec)defaults_manual(&auto_defaults,(unsigned)DK2ML_Arg(regs,1));
        return DK2ML_CALL_ORIGINAL;
    }
#endif
    if(enabled&&!own_autoexec&&read_at(image+0xff2099,&shift,1)&&shift)
        defaults_manual(&auto_defaults,(unsigned)DK2ML_Arg(regs,1));
    return DK2ML_CALL_ORIGINAL;
}
static void input_post(DK2ML_Regs *regs, void *user) {
    (void)regs; (void)user;
    if (!enabled) {atomic_store(&clear_body_id,0);atomic_store(&clear_all_requested,false);return;}
    uint32_t clear_id=atomic_exchange(&clear_body_id,0);
    bool clear_all=atomic_exchange(&clear_all_requested,false);
    unsigned requests = autoexec_requests;
    autoexec_requests = 0;
    HWND window;
    state s = {0};
    uintptr_t client = 0, map = 0;
    uint32_t game_time = 0, client_flags = 0;
    uintptr_t client_slot=image+0xff0ea8;
    unsigned auto_mask_offset=0xfe2a4;
    bool legacy=true;
#ifdef R5K_QOL_UI
    if(basic_113){
        client_slot=global_client_113;
        auto_mask_offset=0xfe2c0;
        legacy=false;
    }
#endif
    if (!get_state(&s) ||
        !read_at(client_slot, &client, sizeof(client)) ||
        !read_at(client + 0x28, &map, sizeof(map)) ||
        !read_at(client + 8, &game_time, sizeof(game_time)) ||
        (legacy&&!read_at(client + 0x7e210, &client_flags, sizeof(client_flags)))) return;
    if (!s.playing || !map || (client_flags & 0x80)) {
        atomic_store(&clear_body_id,0);
        defaults_reset(&auto_defaults);
        return;
    }
    if (client != defaults_client || map != defaults_map || game_time < defaults_last_time)
        defaults_reset(&auto_defaults);
    defaults_client = client;
    defaults_map = map;
    defaults_last_time = game_time;
    if (!game_foreground(&window) || window != hooked_window ||
        s.wheel_open || s.other_panel) {atomic_store(&clear_body_id,0);return;}

    if(legacy){
        update_door_sequence(client,map);
        if(clear_all||clear_id)position_cancel_user_clear(clear_id,clear_all);
        if(clear_all){door_request.pending=false;if(clear_every_plan(client,map)&&play_gui_click)play_gui_click();}
        else if(clear_id){if(door_request.owner==clear_id)door_request.pending=false;if(clear_hovered_plan(clear_id,client)&&play_gui_click)play_gui_click();}
    }

    if(atomic_exchange(&autoexecute_reapply,false))defaults_reset(&auto_defaults);
    if(requests&AUTOEXEC_ALL_REQUEST){
        uint32_t flags=0;
        if(!read_at(client+auto_mask_offset,&flags,sizeof(flags)))return;
        requests=autoexec_all_changes(flags);
        // Keep the mission preset from overwriting this explicit choice.
        for(unsigned code=1;code<=4;++code)defaults_manual(&auto_defaults,code);
    }
    for(unsigned code=1;code<=4;++code)
        if(requests&(1u<<(code-1)))defaults_manual(&auto_defaults,code);
    if (!auto_defaults.initialized) {
        uint32_t flags=0;
        if(!read_at(client+auto_mask_offset,&flags,sizeof(flags)))return;
        unsigned target=autoexecute_go_codes?15u:0u;
        unsigned missing=defaults_missing_for(&auto_defaults,flags,target);
        for(unsigned code=1;code<=4;++code)
            if(missing&(1u<<(code-1)))toggle_autoexecution(code);
        // Recheck on the next normal input update rather than counting a call as success.
        if(!missing) {
            defaults_observe_for(&auto_defaults,flags,target);
            if(auto_defaults.initialized)api->Log("Cursor Hotkeys: mission autoexecution defaults verified (mask=%x, manual=%x).",flags,auto_defaults.manual);
        }
    }
    for (unsigned code = 1; code <= 4; ++code)
        if (requests & (1u << (code - 1))) toggle_autoexecution(code);
}

#include "window_bridge.h"

static LRESULT CALLBACK hotkey_proc(HWND window, UINT message, WPARAM wp, LPARAM lp) {
    WNDPROC forward = original_proc;
    if (message == autoexec_message) {
        HWND active;
        state s = {0};
        if (game_foreground(&active) && active == window &&
            get_state(&s) && s.playing && !s.wheel_open && !s.other_panel)
        {
            if (wp >= 1 && wp <= 4) autoexec_requests ^= 1u << (wp - 1);
            else if(wp==5)autoexec_requests^=AUTOEXEC_ALL_REQUEST;
        }
        return 0;
    }
    if (message == dispatch_message) {
        // Dispatch on the normal window thread and bypass our physical-key
        // interception. Recheck scope if focus or the wheel changed in between.
        HWND active;
        state s = {0};
        if (forward && game_foreground(&active) && active == window &&
            get_state(&s) && s.playing && s.wheel_open
#ifdef R5K_QOL_UI
            && (!basic_113 || (!s.other_panel && qol_ported_wheel_action((unsigned)wp)))
#endif
        ) {
            LPARAM scan = (LPARAM)MapVirtualKeyW((UINT)wp, MAPVK_VK_TO_VSC) << 16;
            send_toggle_key(forward, window, WM_KEYDOWN, wp, scan | 1,(unsigned)wp);
            if(wp!=BIND_FLASH&&wp!=BIND_FRAG&&wp!=BIND_DOOR&&wp!=BIND_DOOR_ONLY&&wp!=BIND_SUPPRESSION)
                CallWindowProcW(forward, window, WM_KEYUP, wp, scan | 0xc0000001u);
        }
        return 0;
    }
    if ((message == WM_KEYUP || message == WM_SYSKEYUP) && wp < 256 && swallowed[wp]) {
        swallowed[wp] = false;
        return 0;
    }
    if ((message == WM_KEYDOWN || message == WM_SYSKEYDOWN) && wp < 256) {
        if (swallowed[wp]) return 0; // Ignore physical key repeat.
#ifdef R5K_QOL_UI
        unsigned action=binding_for((unsigned)wp);
        if(!basic_113&&action==QOL_CLEAR_ALL) {
#else
        if(wp=='C'&&(GetKeyState(VK_MENU)&0x8000)&&
            !((GetKeyState(VK_SHIFT)|GetKeyState(VK_CONTROL))&0x8000)) {
#endif
            HWND active;state s={0};
            if(game_foreground(&active)&&active==window&&get_state(&s)&&s.playing&&
                !s.wheel_open&&!s.other_panel&&!pending&&
                !((GetAsyncKeyState(VK_LBUTTON)|GetAsyncKeyState(VK_RBUTTON))&0x8000)) {
                atomic_store(&clear_all_requested,true);swallowed[wp]=true;return 0;
            }
        }
#ifdef R5K_QOL_UI
        if((action>=QOL_AUTO_A&&action<QOL_AUTO_A+4)||action==QOL_AUTO_ALL) {
#else
        if (wp >= '1' && wp <= '4' &&
            (GetKeyState(VK_CONTROL) & 0x8000) &&
            !((GetKeyState(VK_SHIFT) | GetKeyState(VK_MENU)) & 0x8000)) {
#endif
            HWND active;
            state s = {0};
            if (game_foreground(&active) && active == window &&
                get_state(&s) && s.playing && !s.wheel_open && !s.other_panel) {
                if (!(lp & ((LPARAM)1 << 30)) &&
                    PostMessageW(window, autoexec_message,
#ifdef R5K_QOL_UI
                        action==QOL_AUTO_ALL?5:action-QOL_AUTO_A+1,
#else
                        wp-'1'+1,
#endif
                        0))
                    swallowed[wp] = true;
                return 0;
            }
        }
#ifdef R5K_QOL_UI
        if(!basic_113&&action=='C') {
#else
        if(wp=='C'&&!((GetKeyState(VK_SHIFT)|GetKeyState(VK_CONTROL)|GetKeyState(VK_MENU))&0x8000)) {
#endif
            HWND active;state s={0};
            if(game_foreground(&active)&&active==window&&get_state(&s)&&s.playing&&
                !s.wheel_open&&!s.other_panel&&!pending&&
                !((GetAsyncKeyState(VK_LBUTTON)|GetAsyncKeyState(VK_RBUTTON))&0x8000)) {
                uint32_t id=body_under_cursor();
                if(id){atomic_store(&clear_body_id,id);swallowed[wp]=true;return 0;}
            }
        }
        unsigned binding = binding_for((unsigned)wp);
#ifdef R5K_QOL_UI
        if(binding>=QOL_CLEAR_ALL||(basic_113&&!qol_ported_wheel_action(binding)))binding=0;
#endif
        if(binding==BIND_DOOR&&!(lp&((LPARAM)1<<30)))memset(&door_clean,0,sizeof(door_clean));
        HWND active;
        state s = {0};
        if (binding && game_foreground(&active) && active == window &&
            get_state(&s) && s.playing
#ifdef R5K_QOL_UI
            && (!basic_113 || !s.other_panel)
#endif
        ) {
            if ((lp & ((LPARAM)1 << 30)) && (s.wheel_open || !s.other_panel)) return 0;
            // Resolve Shift+C before dispatch so releasing Shift cannot change
            // the selected action while the wheel is being opened.
            if (s.wheel_open && binding == BIND_CLEAR_PATH) {
                if (PostMessageW(window, dispatch_message, binding, 0)) swallowed[wp] = true;
                return 0;
            }
            if (s.wheel_open) {
#ifdef R5K_QOL_UI
                LPARAM scan=(LPARAM)MapVirtualKeyW(binding,MAPVK_VK_TO_VSC)<<16;
                send_toggle_key(bridge_direct,window,WM_KEYDOWN,binding,scan|1,binding);
                if(binding<256)bridge_direct(window,WM_KEYUP,binding,scan|0xc0000001u);
                swallowed[wp]=true;return 0;
#else
                return send_toggle_key(forward,window,message,wp,lp,binding);
#endif
            }
            if (!s.other_panel &&
                !((GetAsyncKeyState(VK_LBUTTON) | GetAsyncKeyState(VK_RBUTTON)) & 0x8000)) {
                POINT point;
                RECT bounds;
                if (GetCursorPos(&point) && ScreenToClient(window, &point) &&
                    GetClientRect(window, &bounds) && PtInRect(&bounds, point)) {
                    AcquireSRWLockExclusive(&queue_lock);
                    if (!queued_key && !pending) {
                        queued_point = point;
                        queued_key = binding;
                    }
                    ReleaseSRWLockExclusive(&queue_lock);
                    swallowed[wp] = true;
                    return 0;
                }
            }
        }
    }
#ifdef R5K_QOL_UI
    // Old XML wheel shortcuts must not survive a remap. Canonical synthetic
    // dispatch above bypasses this physical-key filter.
    if((message==WM_KEYDOWN||message==WM_SYSKEYDOWN)&&wp<256){
        state old_scope={0};HWND active=NULL;
        bool xml_key=wp=='G'||wp=='R'||wp=='T'||wp=='V'||wp=='C'||wp=='J'||(wp>='1'&&wp<='4');
        if(xml_key&&game_foreground(&active)&&active==window&&get_state(&old_scope)&&old_scope.playing&&old_scope.wheel_open&&!old_scope.other_panel){swallowed[wp]=true;return 0;}
    }
#endif
    LRESULT result = CallWindowProcW(forward, window, message, wp, lp);
    if (message == WM_NCDESTROY) {
        // Restore the procedure while the original window still exists.
        hooked_window = NULL;
        original_proc = NULL;
    }
    return result;
}



static void on_present(void *runtime) {
    (void)runtime;
    if (!checked) {
        checked = true;
        compatible = true;
        log_text(compatible ? 3 : 1, compatible ?
            "DK2 Hotkeys v4: supported build." :
            "DK2 Hotkeys v4: unsupported game build; hotkeys disabled.");
    }
    if (!compatible) return;
    HWND window = NULL;
    state s = {0};
    if (!game_foreground(&window)) {
        atomic_store(&native_button_request,0);
        pending = 0;
        AcquireSRWLockExclusive(&queue_lock);
        queued_key = 0;
        ReleaseSRWLockExclusive(&queue_lock);
        return;
    }
    hooked_window = window;
    if (!get_state(&s)) return;
    if (!s.playing) {
        atomic_store(&native_button_request,0);
        pending = 0;
        AcquireSRWLockExclusive(&queue_lock);
        queued_key = 0;
        ReleaseSRWLockExclusive(&queue_lock);
        return;
    }
    ULONGLONG now = GetTickCount64();
    if (pending) {
        if (window != request_window) { pending = 0; return; }
        if (pending == 1 && s.wheel_open
#ifdef R5K_QOL_UI
            && (!basic_113 || !s.other_panel)
#endif
        ) {
            if (!PostMessageW(window, dispatch_message, dispatch_key, 0)) {
                log_text(2, "DK2 Hotkeys v4: could not queue action key.");
                pending = 0;
                return;
            }
            pending = 2;
            deadline = now + 1500;
            return;
        }
        if (pending == 2 && !s.wheel_open) {
            log_text(3, "DK2 Hotkeys v4: wheel closed after action dispatch.");
            pending = 0;
            return;
        }
        if (now >= deadline) {
            atomic_store(&native_button_request,0);
            log_text(2, pending == 1 ?
                "DK2 Hotkeys v4: no wheel opened; no action sent." :
                "DK2 Hotkeys v4: wheel stayed open; check v4 XML mod.");
            pending = 0;
        }
        return;
    }
    POINT point;
    unsigned key;
    AcquireSRWLockExclusive(&queue_lock);
    key = queued_key;
    point = queued_point;
    queued_key = 0;
    ReleaseSRWLockExclusive(&queue_lock);
    if (!key || s.wheel_open || s.other_panel) return;
    atomic_store(&native_button_request,0);
    LPARAM xy = MAKELPARAM(point.x, point.y);
    if (!PostMessageW(window, WM_RBUTTONDOWN, MK_RBUTTON, xy) ||
        !PostMessageW(window, WM_RBUTTONUP, 0, xy)) {
        log_text(2, "DK2 Hotkeys v4: could not queue right-click.");
        return;
    }
    dispatch_key = key;
    dispatch_point = point;
    request_window = window;
    pending = 1;
    deadline = now + 750;
    char message[] = "DK2 Hotkeys v4: X at cursor; waiting for wheel.";
    message[16] = key==BIND_SUPPRESSION?'S':key==BIND_FRAG?'F':(key==BIND_DOOR||key==BIND_DOOR_ONLY)?'O':(char)key;
    log_text(3, message);
}


#ifdef R5K_QOL_UI
#include "pie_right_drag.h"
#endif

static int window_pre(DK2ML_Regs *regs, void *user) {
    (void)user;
#ifdef R5K_QOL_UI
    if(qol_capture_key((unsigned)DK2ML_Arg(regs,1),(unsigned)DK2ML_Arg(regs,2))){regs->rax=0;return DK2ML_SKIP_ORIGINAL;}
#endif
    if (!enabled || forwarding_window ||
        (HWND)(uintptr_t)DK2ML_Arg(regs, 0) != hooked_window) return DK2ML_CALL_ORIGINAL;
    current_message = (UINT)DK2ML_Arg(regs, 1);
#ifdef R5K_QOL_UI
    if(!basic_113)pie_input_message(current_message);
#endif
    current_wp = (WPARAM)DK2ML_Arg(regs, 2);
    current_lp = (LPARAM)DK2ML_Arg(regs, 3);
    window_forwarded = false;
    LRESULT result = hotkey_proc(hooked_window, current_message, current_wp, current_lp);
    if (window_forwarded) return DK2ML_CALL_ORIGINAL;
    regs->rax = (uint64_t)result;
    return DK2ML_SKIP_ORIGINAL;
}
static bool install_window_bridge(void) {
    native_window_proc = (WNDPROC)api->ResolveSymbol("WndProc");
    if (!native_window_proc) return false;
    original_proc = bridge_forward;
    return api->CreateSafeHook((void *)native_window_proc, window_pre, NULL, NULL) == DK2ML_OK &&
        api->EnableHook((void *)native_window_proc) == DK2ML_OK;
}

static void frame(const DK2ML_Event *event, void *user) {
    (void)event; (void)user;
    if (enabled) on_present(NULL);
    else { pending = 0; queued_key = 0; autoexec_requests = 0; }
}
DK2ML_EXPORT int DK2ML_PluginInit(const DK2ML_API *loader, const DK2ML_PluginInfo *info) {
    (void)info;
#ifdef R5K_QOL_UI
    basic_113=loader->GetGameVersion()==113;
    if(basic_113){
        api=loader;
        uintptr_t camera=(uintptr_t)api->ResolveSymbol("GameClient::UpdateCamera");
        if(!camera)return 1;
        image=camera-0x187c20;
        if(!resolve_basic_hotkeys()){
            api->Log("Cursor Hotkeys 1.13: waypoint menu or input layout unavailable.");
            return 1;
        }
        if(resolve_go_code_toggle_113()){
            if(resolve_action_toggle_113())api->Log("Cursor Hotkeys 1.13: native waypoint toggles ready.");
            else api->Log("Cursor Hotkeys 1.13: go-code and Wait for Clear toggles ready; action removal unavailable.");
        }
        else return 1;
        int64_t clicked=0;
        if(api->GetEnumValue("GUI::Events::eEventType","GUI_GAME_WAYPOINT_CLICKED",&clicked)!=DK2ML_OK||clicked!=81||
            api->GetFieldOffset("GUI::Item","m_eventProperties")!=168||
            api->GetFieldOffset("GUI::Item","m_bAcceptInput")!=144||
            api->GetFieldOffset("GUI::sAction","eventId")!=40||api->GetTypeSize("GUI::sAction")!=80||
            api->GetFieldOffset("Human_Client","m_inventory")!=1072||
            api->GetTypeSize("Equipment_Client")!=88)return 1;
        wheel_clicked_event=(unsigned)clicked;
        // Anchor the internal autoexecution command to the verified Shift branch.
        static const unsigned char shift_branch[]={0xe9,0x1a,0x17,0,0};
        void *auto_toggle=(void*)(image+0x189ab0);
        if(global_client_113!=image+0x1038c40||
            (uintptr_t)api->ResolveSymbol("GameClient::ExecuteGoCode")!=image+0x188370||
            (image==(uintptr_t)GetModuleHandleW(NULL)&&memcmp((void*)(image+0x188391),shift_branch,sizeof shift_branch)))return 1;
        if(configure(loader,"Enable additional hotkeys","Waypoint-wheel movement, reload, go-code and path shortcuts.",true)||
            !configure_door_settings()||
            api->CreateSafeHook(auto_toggle,observe_go_code,NULL,NULL)!=DK2ML_OK||
            api->EnableHook(auto_toggle)!=DK2ML_OK||
            !hook_named("GameInput::CheckKeyboardEvents",0x12c9e0,NULL,input_post)||
            !hook_named("GameInput::Update",0x12f870,NULL,button_input_post)||
            api->Subscribe(DK2ML_EVENT_MAP_LOADED,defaults_loaded,NULL)!=DK2ML_OK||
            !install_window_bridge()||
            api->Subscribe(DK2ML_EVENT_FRAME,frame,NULL)!=DK2ML_OK)return 1;
        api->Log("Cursor Hotkeys 1.13: basic waypoint-wheel shortcuts ready.");
        return 0;
    }
#endif
    image = (uintptr_t)loader->ResolveSymbol("GameClient::UpdateCamera") - 0x187f70;
    void *click=loader->ResolveSymbol("PlayGUIClicked");
    if((uintptr_t)click==image+0x2acc0)play_gui_click=(void (*)(void))click;
    if (configure(loader, "Enable additional hotkeys", "Cursor wheel actions, cancellation and go-code autoexecution.", false) ||
        !configure_door_settings() ||
        !resolve_hover_clear() ||
        !init_door_sequence() ||
        !init_position_only() ||
        !hook_named("SavedPlan::Save",0x1d0e60,preserve_plan_save,NULL) ||
        !hook_named("SavedPlan::Load",0x1d1a90,NULL,saved_plan_loaded) ||
        !hook_named("GameClient::ExecuteGoCode",0x188740,observe_go_code,NULL) ||
        api->Subscribe(DK2ML_EVENT_MAP_LOADED,defaults_loaded,NULL)!=DK2ML_OK ||
        !install_window_bridge() ||
        !hook_named("GameInput::CheckKeyboardEvents", 0x12e2f0, NULL, input_post) ||
        !hook_named("GameInput::Update",0x131360,NULL,button_input_post) ||
        api->Subscribe(DK2ML_EVENT_FRAME, frame, NULL) != DK2ML_OK) return 1;
#ifdef R5K_QOL_UI
    if(!configure_pie_right_drag())return 1;
#endif
    api->Log("Ready: Shift+1-4 assignment, Ctrl+1-4 autoexecution, C clear path, Shift+C delete waypoint.");
    return 0;
}
