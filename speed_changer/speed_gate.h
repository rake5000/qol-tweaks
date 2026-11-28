#ifndef RAKE5000_SPEED_GATE_H
#define RAKE5000_SPEED_GATE_H
// One-use permission after the exact Net_PollMessages call preceding each
// timestep. GameServer::Update can execute multiple timesteps per call.
// A fault after arming can leave at most one pending step, never a latched speed.
volatile LONG speed_update_armed;
static uintptr_t speed_gate_return;
static int speed_gate_pre(DK2ML_Regs *regs,void *user){
    (void)user;
    regs->scratch[0]=regs->stack&&regs->stack[0]==speed_gate_return;
    if(regs->scratch[0])InterlockedExchange(&speed_update_armed,0);
    return DK2ML_CALL_ORIGINAL;
}
static void speed_gate_post(DK2ML_Regs *regs,void *user){
    (void)user;
    if(regs->scratch[0])InterlockedExchange(&speed_update_armed,
        native_enabled&&installed&&!rejected&&(!speed_113||!native_api->IsReplaying())?1:0);
}
#endif
