// Entered by a jump at GameServer::Update's timestep multiplication.
// xmm0 already contains the unscaled timestep; rbx is the server.
// Preserve all registers except the intended xmm0 result, plus flags.
.intel_syntax noprefix
.text
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
    cmp DWORD PTR [rbx + 0x290], 1
    jne native_speed
    mov rax, QWORD PTR [rip + speed_replay_slot]
    cmp QWORD PTR [rax], 0
    jne native_speed
    test DWORD PTR [rbx + 0x294], 0x20000
    jne slow_speed
    mulss xmm0, DWORD PTR [rip + speed_selected_factor]
    jmp speed_done
slow_speed:
    mulss xmm0, DWORD PTR [rip + speed_slow_factor]
    jmp speed_done
native_speed:
    mulss xmm0, DWORD PTR [rbx + 0x7e210]
speed_done:
    pop rax
    popfq
    jmp QWORD PTR [rip + speed_resume]
