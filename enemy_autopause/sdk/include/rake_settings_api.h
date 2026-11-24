#ifndef RAKE5000_SETTINGS_API_H
#define RAKE5000_SETTINGS_API_H

#include <stdint.h>

#define R5K_SETTINGS_API_VERSION 1u

typedef struct R5K_SettingsModule {
    uint32_t structSize;
    const char *label;
    void (*draw)(void);
} R5K_SettingsModule;

#endif
