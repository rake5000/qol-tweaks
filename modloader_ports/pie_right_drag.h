#ifndef R5K_PIE_RIGHT_DRAG_H
#define R5K_PIE_RIGHT_DRAG_H

static bool pie_right_drag=true,pie_patch_ready;
static bool (*pie_modifier_down)(int);
static _Thread_local bool pie_drag_started;
static _Thread_local unsigned pie_previous_modifiers;
// The decision runs immediately after the capture pre-hook on the input thread.
static unsigned char pie_capture_active __attribute__((used));
static unsigned char pie_capture_request __attribute__((used));
static unsigned char pie_capture_toggle __attribute__((used));
static uintptr_t pie_just_down __attribute__((used));
static uintptr_t pie_capture_vanilla __attribute__((used));
static uintptr_t pie_capture_change __attribute__((used));
static uintptr_t pie_capture_look_update __attribute__((used));
static uintptr_t pie_capture_cycle_113 __attribute__((used));

__attribute__((naked,used)) static void pie_capture_bridge(void) {
    __asm__ volatile(
        "cmpb $0, pie_capture_active(%rip)\n"
        "je 4f\n"
        "movzbl pie_capture_request(%rip), %edx\n"
        "testl %edx, %edx\n"
        "jz 3f\n"
        "movb $0, pie_capture_request(%rip)\n"
        "cmpb $0, pie_capture_toggle(%rip)\n"
        "je 2f\n"
        "cmpl %esi, %edx\n"
        "jne 2f\n"
        "movl $6, %edx\n"
        "2: jmp *pie_capture_change(%rip)\n"
        "3: testb %r10b, %r10b\n"
        "jnz 5f\n"
        "jmp *pie_capture_look_update(%rip)\n"
        "4: movl $3, %ecx\n"
        "call *pie_just_down(%rip)\n"
        "testb %al, %al\n"
        "jz 5f\n"
        "movl $6, %edx\n"
        "jmp 2b\n"
        "5: jmp *pie_capture_vanilla(%rip)\n");
}

__attribute__((naked)) static void pie_native_strafe_bridge(void) {
    __asm__ volatile(
        "cmpb $0, pie_capture_active(%rip)\n"
        "jne pie_capture_bridge\n"
        "movl $5, %edx\n"
        "jmp *pie_capture_change(%rip)\n");
}

__attribute__((naked)) static void pie_capture_bridge_113(void) {
    __asm__ volatile(
        "cmpb $0, pie_capture_active(%rip)\n"
        "je 4f\n"
        "movzbl pie_capture_request(%rip), %edx\n"
        "testl %edx, %edx\n"
        "jz 3f\n"
        "movb $0, pie_capture_request(%rip)\n"
        "cmpb $0, pie_capture_toggle(%rip)\n"
        "je 2f\n"
        "cmpl %r11d, %edx\n"
        "jne 2f\n"
        "movl $6, %edx\n"
        "2: jmp *pie_capture_change(%rip)\n"
        "4: movl $3, %ecx\n"
        "call *pie_just_down(%rip)\n"
        "testb %al, %al\n"
        "jz 3f\n"
        "movl $6, %edx\n"
        "jmp 2b\n"
        "3: testb %r10b, %r10b\n"
        "jnz 5f\n"
        "jmp *pie_capture_look_update(%rip)\n"
        "5: jmp *pie_capture_cycle_113(%rip)\n");
}

__attribute__((naked)) static void pie_native_strafe_bridge_113(void) {
    __asm__ volatile(
        "cmpb $0, pie_capture_active(%rip)\n"
        "jne pie_capture_bridge_113\n"
        "movl $5, %edx\n"
        "jmp *pie_capture_change(%rip)\n");
}

static void pie_input_message(UINT message) {
    if(message==WM_RBUTTONDOWN||message==WM_RBUTTONDBLCLK||message==WM_RBUTTONUP||
       message==WM_CANCELMODE||message==WM_KILLFOCUS){
        pie_drag_started=false;
        pie_capture_request=0;
    }
}

static bool pie_override_active(void) {
    return pie_patch_ready&&enabled&&pie_right_drag&&(GetKeyState(VK_RBUTTON)&0x8000);
}

static int pie_initial_pre(DK2ML_Regs *r,void *user) {
    (void)user;
    // Button2_Drag checks strafe/locked look first, then the pie modifier.
    r->scratch[0]=r->stack&&r->stack[0]==image+(basic_113?0x131d1f:0x133662)&&
        DK2ML_Arg(r,0)==3&&pie_override_active();
    return DK2ML_CALL_ORIGINAL;
}

static void pie_initial_post(DK2ML_Regs *r,void *user) {
    (void)user;
    if(r->scratch[0])r->rax=(r->rax&~UINT64_C(255))|!(r->rax&255);
}

static int pie_capture_pre(DK2ML_Regs *r,void *user) {
    (void)r;(void)user;
    pie_capture_active=pie_override_active();
    if(!pie_capture_active){
        pie_drag_started=false;
        pie_capture_request=0;
        return DK2ML_CALL_ORIGINAL;
    }
    const unsigned modifiers=(pie_modifier_down(3)?1u:0u)|(pie_modifier_down(4)?2u:0u);
    const unsigned pressed=modifiers&~pie_previous_modifiers;
    pie_previous_modifiers=modifiers;
    if(!pie_drag_started){
        pie_drag_started=true;
        pie_capture_toggle=0;
        pie_capture_request=(modifiers&2)?5:(modifiers&1)?4:6;
    }else if(pressed){
        pie_capture_toggle=1;
        pie_capture_request=(pressed&2)?5:4;
    }
    return DK2ML_CALL_ORIGINAL;
}

static void pie_save(const DK2ML_Option *option,void *user) {
    (void)option;(void)user;
    if(settings_path[0])WritePrivateProfileStringW(L"Orders",L"PieOnRightDrag",pie_right_drag?L"1":L"0",settings_path);
}

// Two entries in Captured_RClickDrag select native strafe or test the pie
// modifier. Both must reach the same mode decision when the option is on.
static const unsigned char pie_capture_original[]={
    0xba,5,0,0,0,0xe9,0xae,0,0,0,
    0xb9,3,0,0,0,0xe8,0x74,0x14,0x0f,0,
    0x84,0xc0,0x74,0x0a,0xba,6,0,0,0,0xe9,0x96,0,0,0
};
static const unsigned char pie_capture_original_113[]={
    0xba,5,0,0,0,0xeb,0x52,0xb9,3,0,0,0,0xe8,0x40,0xb7,0x0f,0,
    0x84,0xc0,0x74,7,0xba,6,0,0,0,0xeb,0x3d,0x45,0x84,0xd2,0x74,0x4b
};
static bool pie_capture_matches(void) {
    unsigned char bytes[sizeof pie_capture_original];
    size_t length=basic_113?sizeof pie_capture_original_113:sizeof pie_capture_original;
    return read_at(image+(basic_113?0x13312f:0x134ce8),bytes,length)&&
        !memcmp(bytes,basic_113?pie_capture_original_113:pie_capture_original,length);
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
    unsigned char jump[sizeof pie_capture_original]={0xff,0x25,10,0,0,0};
    unsigned char normal_entry[]={0xff,0x25,8,0,0,0};
    void *strafe=(void*)pie_native_strafe_bridge,*normal=(void*)pie_capture_bridge;
    unsigned normal_offset=10;
    size_t length=sizeof jump;
    if(basic_113){
        // Short branch layout places the second entry three bytes earlier.
        normal_offset=7;normal_entry[2]=11;
        length=sizeof pie_capture_original_113;
        strafe=(void*)pie_native_strafe_bridge_113;normal=(void*)pie_capture_bridge_113;
    }
    memcpy(jump+normal_offset,normal_entry,sizeof normal_entry);
    memcpy(jump+16,&strafe,sizeof strafe);
    memcpy(jump+24,&normal,sizeof normal);
    void *site=(void*)(image+(basic_113?0x13312f:0x134ce8));
    DWORD previous,ignored;
    if(!VirtualProtect(site,length,PAGE_EXECUTE_READWRITE,&previous)){
        api->Log("Pie on right-drag unavailable: cannot install drag decision.");
        return;
    }
    pie_capture_vanilla=image+0x134d0a;
    pie_capture_change=image+(basic_113?0x133188:0x134da0);
    pie_capture_look_update=image+(basic_113?0x13319b:0x134db2);
    pie_capture_cycle_113=image+0x133150;
    memcpy(site,jump,length);
    VirtualProtect(site,length,previous,&ignored);
    FlushInstructionCache(GetCurrentProcess(),site,length);
    pie_patch_ready=true;
}

static bool configure_pie_right_drag(void) {
    unsigned captured=basic_113?0x132f10:0x1349e0;
    unsigned down=basic_113?0x22e810:0x226100;
    unsigned just_down=basic_113?0x22e880:0x226170;
    if((uintptr_t)api->ResolveSymbol("GameInput::Captured_RClickDrag")!=image+captured||
       (uintptr_t)api->ResolveSymbol("GameInput::Button2_Drag")!=image+(basic_113?0x1319b0:0x1332e0)||
       (image==(uintptr_t)GetModuleHandleW(NULL)&&!pie_capture_matches())){
        api->Log("Pie on right-drag unavailable: unsupported drag code.");
        return true;
    }
    pie_modifier_down=(void*)api->ResolveSymbol("InputMap::IsDown");
    pie_just_down=(uintptr_t)api->ResolveSymbol("InputMap::JustDown");
    if((uintptr_t)pie_modifier_down!=image+down||
       pie_just_down!=image+just_down||
       !hook_named("InputMap::IsDown",down,pie_initial_pre,pie_initial_post)||
       !hook_named("GameInput::Captured_RClickDrag",captured,pie_capture_pre,NULL)){
        api->Log("Pie on right-drag unavailable: could not hook the native modifiers.");
        return true;
    }
    if(settings_path[0])pie_right_drag=GetPrivateProfileIntW(L"Orders",L"PieOnRightDrag",1,settings_path)!=0;
    DK2ML_Option option={0};
    option.structSize=sizeof option;
    option.type=DK2ML_OPTION_BOOL;
    option.label="Pie on right-drag by default";
    option.tooltip="Right-drag starts in pie mode. Press Shift for a regular look or Ctrl for strafe with default bindings. Press the same key again to return to pie. Left-click cycles look modes.";
    option.value=&pie_right_drag;
    option.onChange=pie_save;
    return api->AddOption(&option)==DK2ML_OK&&api->Subscribe(DK2ML_EVENT_FRAME,pie_install_frame,NULL)==DK2ML_OK;
}
#endif
