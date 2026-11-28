#ifndef R5K_TOGGLE_ACTIONS_H
#define R5K_TOGGLE_ACTIONS_H
static inline unsigned waypoint_order_code(unsigned binding) {
    if(binding=='V')return 5;
    return binding>='1'&&binding<='4'?binding-'1'+1:0;
}
static inline unsigned waypoint_order_action(unsigned binding) {
    switch(binding){
    case 'R':return 1;
    case 'G':return 8;
    case 'T':return 11;
    default:return 0;
    }
}
// Native wheel events: clicked=73, delete action=74, code checked=77, unchecked=78.
static inline unsigned toggle_wheel_event(unsigned binding,unsigned event,unsigned requested,
    unsigned action_type,unsigned gocode) {
    if(event==77&&requested&&waypoint_order_code(binding)==requested&&gocode==requested)return 78;
    if(event==73&&requested&&waypoint_order_action(binding)==requested&&action_type==requested)return gocode?73:74;
    return event;
}
#endif
