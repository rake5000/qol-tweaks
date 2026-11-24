#ifndef R5K_SAVED_PLAN_GUARD_H
#define R5K_SAVED_PLAN_GUARD_H
#include <stdint.h>
#include <stdbool.h>
static inline bool preserve_automatic_plan(uintptr_t caller,uintptr_t destination,uintptr_t client,
 uintptr_t map,uintptr_t protected_client,uintptr_t protected_map) {
 return protected_client&&protected_map&&client==protected_client&&map==protected_map&&
  caller==0x185777&&destination==client+0x7e280;
}
#endif
