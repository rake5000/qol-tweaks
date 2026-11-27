#ifndef R5K_PIE_RIGHT_DRAG_H
#define R5K_PIE_RIGHT_DRAG_H

static bool pie_right_drag=true,pie_patch_ready;
static bool (*pie_modifier_down)(int);
static unsigned char pie_capture_mode __attribute__((used));
static unsigned char pie_cycle_selected __attribute__((used));
static uintptr_t pie_capture_vanilla __attribute__((used));
static uintptr_t pie_capture_regular __attribute__((used));
static uintptr_t pie_capture_change __attribute__((used));
static uintptr_t pie_capture_look_update __attribute__((used));

// Left-click uses the native three-mode cycle. After that, keep the chosen
// action for this drag while the original routine continues updating its aim.
__attribute__((naked)) static void pie_capture_bridge(void) {
    __asm__ volatile(
        "cmpb $0, pie_capture_mode(%rip)\n"
        "je 4f\n"
        "testb %r10b, %r10b\n"
        "jnz 1f\n"
        "cmpb $0, pie_cycle_selected(%rip)\n"
        "jne 3f\n"
        "cmpb $2, pie_capture_mode(%rip)\n"
        "je 5f\n"
        "jmp 2f\n"
        "1: movb $1, pie_cycle_selected(%rip)\n"
        "jmp *pie_capture_vanilla(%rip)\n"
        "2: movl $6, %edx\n"
        "jmp *pie_capture_change(%rip)\n"
        "3: jmp *pie_capture_look_update(%rip)\n"
        "4: testb %al, %al\n"
        "jnz 2b\n"
        "jmp *pie_capture_vanilla(%rip)\n"
        "5: jmp *pie_capture_regular(%rip)\n");
}

static void pie_input_message(UINT message) {
    if(message==WM_RBUTTONDOWN||message==WM_RBUTTONDBLCLK||message==WM_RBUTTONUP||
       message==WM_CANCELMODE||message==WM_KILLFOCUS)pie_cycle_selected=0;
}

static bool pie_override_active(void) {
    return pie_patch_ready&&enabled&&pie_right_drag&&(GetKeyState(VK_RBUTTON)&0x8000);
}

static int pie_initial_pre(DK2ML_Regs *r,void *user) {
    (void)user;
    // Button2_Drag checks strafe/locked look first, then the pie modifier.
    r->scratch[0]=r->stack&&r->stack[0]==image+0x133662&&
        DK2ML_Arg(r,0)==3&&pie_override_active();
    return DK2ML_CALL_ORIGINAL;
}

static void pie_initial_post(DK2ML_Regs *r,void *user) {
    (void)user;
    if(r->scratch[0])r->rax=(r->rax&~UINT64_C(255))|!(r->rax&255);
}

static int pie_capture_pre(DK2ML_Regs *r,void *user) {
    (void)user;
    if(r->stack&&r->stack[0]==image+0x134cfc)
        pie_capture_mode=pie_override_active()?(pie_modifier_down(3)?2:1):0;
    return DK2ML_CALL_ORIGINAL;
}

static void pie_save(const DK2ML_Option *option,void *user) {
    (void)option;(void)user;
    if(settings_path[0])WritePrivateProfileStringW(L"Orders",L"PieOnRightDrag",pie_right_drag?L"1":L"0",settings_path);
}

static const unsigned char pie_capture_original[]={0x84,0xc0,0x74,0x0a,0xba,6,0,0,0,0xe9,0x96,0,0,0};
static bool pie_capture_matches(void) {
    unsigned char bytes[sizeof pie_capture_original];
    return read_at(image+0x134cfc,bytes,sizeof bytes)&&!memcmp(bytes,pie_capture_original,sizeof bytes);
}

static void pie_install_frame(const DK2ML_Event *event,void *user) {
    (void)event;(void)user;
    static bool attempted;
    if(attempted)return;
    attempted=true;
    if(!pie_capture_matches()){
        api->Log("Pie on right-drag unavailable: native drag decision changed.");
        return;
    }
    unsigned char jump[sizeof pie_capture_original]={0xff,0x25,0,0,0,0};
    void *bridge=(void*)pie_capture_bridge;
    memcpy(jump+6,&bridge,sizeof bridge);
    void *site=(void*)(image+0x134cfc);
    DWORD previous,ignored;
    if(!VirtualProtect(site,sizeof jump,PAGE_EXECUTE_READWRITE,&previous)){
        api->Log("Pie on right-drag unavailable: cannot install drag decision.");
        return;
    }
    pie_capture_vanilla=image+0x134d0a;
    pie_capture_regular=image+0x134d9b;
    pie_capture_change=image+0x134da0;
    pie_capture_look_update=image+0x134db2;
    memcpy(site,jump,sizeof jump);
    VirtualProtect(site,sizeof jump,previous,&ignored);
    FlushInstructionCache(GetCurrentProcess(),site,sizeof jump);
    pie_patch_ready=true;
}

static bool configure_pie_right_drag(void) {
    if((uintptr_t)api->ResolveSymbol("GameInput::Captured_RClickDrag")!=image+0x1349e0||
       (uintptr_t)api->ResolveSymbol("GameInput::Button2_Drag")!=image+0x1332e0||
       !pie_capture_matches()){
        api->Log("Pie on right-drag unavailable: unsupported drag code.");
        return true;
    }
    pie_modifier_down=(void*)api->ResolveSymbol("InputMap::IsDown");
    if((uintptr_t)pie_modifier_down!=image+0x226100||
       !hook_named("InputMap::IsDown",0x226100,pie_initial_pre,pie_initial_post)||
       !hook_named("InputMap::JustDown",0x226170,pie_capture_pre,NULL)){
        api->Log("Pie on right-drag unavailable: could not hook the native modifiers.");
        return true;
    }
    if(settings_path[0])pie_right_drag=GetPrivateProfileIntW(L"Orders",L"PieOnRightDrag",1,settings_path)!=0;
    DK2ML_Option option={0};
    option.structSize=sizeof option;
    option.type=DK2ML_OPTION_BOOL;
    option.label="Pie on right-drag by default";
    option.tooltip="Right-drag draws a pie. Hold the game's pie modifier (Shift by default) for a regular look. Left-click during the drag cycles look modes.";
    option.value=&pie_right_drag;
    option.onChange=pie_save;
    return api->AddOption(&option)==DK2ML_OK&&api->Subscribe(DK2ML_EVENT_FRAME,pie_install_frame,NULL)==DK2ML_OK;
}
#endif
