#undef NDEBUG
#include "speed_changer.c"
#include <assert.h>
int main(void){
#ifdef R5K_QOL_NATIVE
 uint64_t stack[]={123};DK2ML_Regs regs={0};regs.stack=stack;speed_gate_return=123;
 installed=false;native_enabled=true;assert(speed_gate_pre(&regs,NULL)==DK2ML_CALL_ORIGINAL);speed_gate_post(&regs,NULL);assert(speed_update_armed==0);
 installed=true;assert(speed_gate_pre(&regs,NULL)==DK2ML_CALL_ORIGINAL);assert(speed_update_armed==0);
 speed_gate_post(&regs,NULL);assert(speed_update_armed==1);
 // Each substep gets fresh permission; unrelated calls cannot rearm it.
 speed_gate_pre(&regs,NULL);assert(speed_update_armed==0);speed_gate_post(&regs,NULL);assert(speed_update_armed==1);
 speed_update_armed=0;stack[0]=456;speed_gate_pre(&regs,NULL);speed_gate_post(&regs,NULL);assert(speed_update_armed==0);stack[0]=123;
 native_enabled=false;speed_gate_pre(&regs,NULL);speed_gate_post(&regs,NULL);assert(speed_update_armed==0);
 native_enabled=true;rejected=true;speed_gate_pre(&regs,NULL);speed_gate_post(&regs,NULL);assert(speed_update_armed==0);
 rejected=false;installed=false;
#endif
 assert(last_custom_percent==50&&percent==100);
 assert(change_speed(100,0,true,&last_custom_percent)==50);
 R5K_SetDefaultSlowSpeed(35);apply_default_speed();assert(last_custom_percent==35&&percent==100);
 last_custom_percent=20;apply_default_speed();assert(last_custom_percent==20);
 R5K_SetDefaultSlowSpeed(0);apply_default_speed();assert(last_custom_percent==20);
 R5K_SetDefaultSlowSpeed(100);apply_default_speed();assert(last_custom_percent==20);
 R5K_SetDefaultSlowSpeed(75);apply_default_speed();assert(last_custom_percent==75&&percent==100);
 puts("PASS: production default setting, range guards, thread handoff and remembered custom speed");return 0;
}
