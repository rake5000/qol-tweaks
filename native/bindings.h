#ifndef DK2_CURSOR_BINDINGS_H
#define DK2_CURSOR_BINDINGS_H
#include <stdbool.h>
#define BIND_CLEAR_PATH 'J'
#define BIND_FLASH 'F'
#define BIND_FRAG 256u
#define BIND_DOOR 257u
#define BIND_DOOR_ONLY 258u
#define BIND_SUPPRESSION 259u
static inline unsigned resolve_binding(unsigned key, bool shift, bool control, bool alt) {
    if (alt) return 0;
    if(key=='S'&&control&&!shift)return BIND_SUPPRESSION;
    if(key=='O'&&!control)return shift?BIND_DOOR_ONLY:BIND_DOOR;
    if(key=='F'&&!shift)return control?BIND_FRAG:BIND_FLASH;
    if (key >= '1' && key <= '4') return shift && !control ? key : 0;
    if (key == 'C' && !control) return shift ? BIND_CLEAR_PATH : 'C';
    if (shift || control) return 0;
    return (key == 'G' || key == 'R' || key == 'T' || key == 'V' || key == 'C') ? key : 0;
}
#endif
