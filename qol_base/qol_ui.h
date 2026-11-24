#ifndef RAKE_QOL_UI_H
#define RAKE_QOL_UI_H
#include "../enemy_autopause/sdk/include/dk2ml.h"
enum {QOL_CLEAR_ALL=1000,QOL_AUTO_A=1101,QOL_SPEED_UP=2001,QOL_SPEED_DOWN,QOL_SPEED_TOGGLE};
enum {QOL_SHIFT=1,QOL_CTRL=2,QOL_ALT=4};
int qol_ui_prepare(const DK2ML_API *loader);
DK2ML_Status qol_collect_option(const DK2ML_Option *option);
int qol_ui_register(void);
void qol_draw_settings(int page,void (*heading)(const char *));
void qol_draw_reset(void);
unsigned qol_resolve_key(unsigned key,unsigned modifiers);
unsigned qol_current_modifiers(void);
int qol_capture_key(unsigned message,unsigned key);
int qol_is_capturing(void);
void qol_cancel_capture(void);
int qol_speed_key_down(int index);
#endif
