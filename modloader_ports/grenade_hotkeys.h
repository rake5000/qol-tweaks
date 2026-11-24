#ifndef R5K_GRENADE_HOTKEYS_H
#define R5K_GRENADE_HOTKEYS_H
#include <stddef.h>
#include <ctype.h>
#include <stdbool.h>
#include "grenade_catalog.h"
static inline bool grenade_name_equal(const char *a,const char *b) {
 if(!a||!b)return false;
 while(*a&&*b){if(tolower((unsigned char)*a++)!=tolower((unsigned char)*b++))return false;}
 return *a==*b;
}
static inline unsigned grenade_kind(const char *name) {
 for(size_t i=0;i<sizeof(grenade_catalog)/sizeof(grenade_catalog[0]);++i)
  if(grenade_name_equal(name,grenade_catalog[i].name))return grenade_catalog[i].kind;
 return 0;
}
static inline bool grenade_candidate(unsigned desired,const char *name,int quantity,bool usable,int slot,int best_slot) {
 return usable&&quantity>0&&slot>0&&slot<27&&slot<best_slot&&grenade_kind(name)==desired;
}
#endif
