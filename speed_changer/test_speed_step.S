.intel_syntax noprefix
.text
.global test_speed_step
.global test_speed_return
test_speed_step:
    push rbx
    sub rsp, 32
    mov rbx, rcx
    movaps xmm0, xmm1
    call speed_step_stub
    add rsp, 32
    pop rbx
    ret
test_speed_return:
    ret
