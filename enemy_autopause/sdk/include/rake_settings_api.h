#ifndef RAKE5000_SETTINGS_API_H
#define RAKE5000_SETTINGS_API_H

#include <stdint.h>

#define R5K_SETTINGS_API_VERSION 1u

typedef struct R5K_SettingsModule {
    uint32_t structSize;
    const char *label;
    void (*draw)(void);
} R5K_SettingsModule;


typedef struct R5K_ResettableSettingsModule {
    R5K_SettingsModule base;
    void (*reset)(void);
} R5K_ResettableSettingsModule;

static inline void r5k_reset_module(const R5K_SettingsModule *module) {
    if(!module || module->structSize<sizeof(R5K_ResettableSettingsModule))return;
    const R5K_ResettableSettingsModule *resettable=(const R5K_ResettableSettingsModule *)module;
    if(resettable->reset)resettable->reset();
}

#endif
