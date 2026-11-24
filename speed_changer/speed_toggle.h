#ifndef R5K_SPEED_TOGGLE_H
#define R5K_SPEED_TOGGLE_H
#include <stdbool.h>
static inline int change_speed(int current,int delta,bool toggle,int *last_custom) {
 if(toggle){
  if(current!=100){*last_custom=current;return 100;}
  return *last_custom>=10&&*last_custom<=300&&*last_custom!=100?*last_custom:100;
 }
 int next=current+delta;if(next<10)next=10;if(next>300)next=300;
 if(next!=100)*last_custom=next;else if(current!=100)*last_custom=current;
 return next;
}
#endif
