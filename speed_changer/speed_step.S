// Entered by a jump at GameServer::Update's timestep multiplication.
// xmm0 already contains the unscaled timestep; rbx is the server.
// Preserve all registers except the intended xmm0 result, plus flags.
.intel_syntax noprefix
.text
#ifdef R5K_QOL_UI
// Both builds copy their server pointer into rcx immediately before the patch.
#define SERVER rcx
#else
#define SERVER rbx
#endif
.global speed_step_stub
speed_step_stub:
    pushfq
    push rax
#ifdef R5K_QOL_NATIVE
    // Consume the permission exactly once. If the loader deactivates QOL,
    // its Update hook stops rearming us, even if no post callback can run.
    xor eax, eax
    xchg DWORD PTR [rip + speed_update_armed], eax
    test eax, eax
    je native_speed
    cmp DWORD PTR [rip + speed_feature_enabled], 0
    je native_speed
#endif
    cmp DWORD PTR [SERVER + 0x290], 1
    jne native_speed
    mov rax, QWORD PTR [rip + speed_replay_slot]
#ifdef R5K_QOL_UI
    test rax, rax
    je check_slow_motion
#endif
    cmp QWORD PTR [rax], 0
    jne native_speed
check_slow_motion:
    test DWORD PTR [SERVER + 0x294], 0x20000
    jne slow_speed
    mulss xmm0, DWORD PTR [rip + speed_selected_factor]
    jmp speed_done
slow_speed:
    mulss xmm0, DWORD PTR [rip + speed_slow_factor]
    jmp speed_done
native_speed:
    mulss xmm0, DWORD PTR [SERVER + 0x7e210]
speed_done:
    pop rax
    popfq
    jmp QWORD PTR [rip + speed_resume]

#ifdef R5K_QOL_UI
.global speed_step_stub_113
speed_step_stub_113:
    pushfq
    movaps xmm0, xmm1
    cmp DWORD PTR [rip + speed_update_armed], 0
    je multiply_113
    cmp DWORD PTR [rip + speed_feature_enabled], 0
    je multiply_113
    cmp DWORD PTR [r14 + 0x290], 1
    jne multiply_113
    movss xmm0, DWORD PTR [rip + speed_selected_factor]
    test DWORD PTR [r14 + 0x294], 0x20000
    je multiply_113
    movss xmm0, DWORD PTR [rip + speed_slow_factor]
multiply_113:
    mulss xmm0, xmm7
    popfq
    jmp QWORD PTR [rip + speed_resume]
#endif
