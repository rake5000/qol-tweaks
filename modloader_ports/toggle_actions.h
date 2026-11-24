#ifndef R5K_TOGGLE_ACTIONS_H
#define R5K_TOGGLE_ACTIONS_H
// Native wheel events: clicked=73, delete action=74, code checked=77, unchecked=78.
static inline unsigned toggle_wheel_event(unsigned binding,unsigned event,unsigned requested,
    unsigned action_type,unsigned gocode) {
    if(event==77 && ((binding>='1'&&binding<='4'&&requested==binding-'1'+1)||
        (binding=='V'&&requested==5)) && gocode==requested)return 78;
    if(event==73 && ((binding=='R'&&requested==1)||(binding=='G'&&requested==8)||(binding=='T'&&requested==11))&&
        action_type==requested)return gocode?73:74;
    return event;
}
#endif
