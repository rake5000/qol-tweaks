#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>
#include "qol_ui.h"
#include "binding_model.h"
#define COUNT(a) (sizeof(a)/sizeof((a)[0]))
#define B(label,section,action,key,mods) {label,section,action,key,mods,key,mods,key,mods}
static QolBinding bindings[]={
 B("Sprint",0,'G','G',0),B("Reload",0,'R','R',0),B("Keep moving / Stop to engage",0,'T','T',0),
 B("Wait for clear",0,'V','V',0),B("Flash / 9-bang",0,'F','F',0),B("Frag grenade",0,256,'F',QOL_CTRL),
 B("Suppressive fire",0,259,'S',QOL_CTRL),B("Clear plan / remaining path",0,'C','C',0),
 B("Delete waypoint",0,'J','C',QOL_SHIFT),B("Clear all trooper plans",0,QOL_CLEAR_ALL,'C',QOL_ALT),
 B("Assign go-code A",1,'1','1',QOL_SHIFT),B("Assign go-code B",1,'2','2',QOL_SHIFT),
 B("Assign go-code C",1,'3','3',QOL_SHIFT),B("Assign go-code D",1,'4','4',QOL_SHIFT),
 B("Toggle autoexecution A",1,QOL_AUTO_A,'1',QOL_CTRL),B("Toggle autoexecution B",1,QOL_AUTO_A+1,'2',QOL_CTRL),
 B("Toggle autoexecution C",1,QOL_AUTO_A+2,'3',QOL_CTRL),B("Toggle autoexecution D",1,QOL_AUTO_A+3,'4',QOL_CTRL),
 B("Open & center-check",2,257,'O',0),B("Open door only",2,258,'O',QOL_SHIFT),
 B("Increase game speed",3,QOL_SPEED_UP,VK_ADD,0),B("Decrease game speed",3,QOL_SPEED_DOWN,VK_SUBTRACT,0),
 B("Toggle normal / custom speed",3,QOL_SPEED_TOGGLE,VK_MULTIPLY,0),
 B("Toggle all auto-execute",1,QOL_AUTO_ALL,VK_OEM_2,0)
};
#undef B
typedef struct {DK2ML_Option option;int section;union {bool boolean;int integer;float real;} defaults;} QolOption;
static QolOption options[32];static size_t option_count;
static const DK2ML_API *api;static wchar_t binding_path[MAX_PATH];
static int capture=-1;static bool conflict_notice;
static bool basic_113;
static int scalar_float,scalar_int;
typedef struct {float x,y;} Vec2;
static bool (*button)(const char*,const Vec2*,int);
static bool (*checkbox)(const char*,bool*);
static bool (*slider)(const char*,int,void*,const void*,const void*,const char*,int);
static void (*text)(const char*,...);static void (*same_line)(float,float);
static void (*align_text)(void);
static float (*text_line_height)(void);
static const char *sections[]={"Orders & Shortcuts","Go-Codes","Open & Center-Check (Experimental)","Game Speed & Replays","Trooper Names"};
static const char *modifier_choices[]={"None","Shift","Ctrl","Ctrl + Shift","Alt","Alt + Shift","Ctrl + Alt","Ctrl + Alt + Shift"};
extern void qol_reset_speed_runtime(void);
static bool reserved_key(int key){
 return key==VK_ESCAPE||(key>0&&key<=6)||key==VK_SHIFT||key==VK_CONTROL||key==VK_MENU||
  key==VK_LSHIFT||key==VK_RSHIFT||key==VK_LCONTROL||key==VK_RCONTROL||key==VK_LMENU||key==VK_RMENU;
}

static void save_bindings(void){
 for(size_t i=0;i<COUNT(bindings);i++){
  wchar_t key[24],value[32];swprintf(key,24,L"Action%d",bindings[i].action);
  swprintf(value,32,L"%d",bindings[i].key|(bindings[i].mods<<8));
  WritePrivateProfileStringW(L"Bindings",key,value,binding_path);
 }
}
static void binding_changed(const DK2ML_Option *option,void *user){
 size_t i=(size_t)(uintptr_t)user;
 if(option&&option->type==DK2ML_OPTION_KEY&&(bindings[i].key==VK_BACK||bindings[i].key==VK_DELETE)){bindings[i].key=0;bindings[i].mods=0;}
 if(qol_conflicts(bindings,COUNT(bindings),i)||reserved_key(bindings[i].key)){
  bindings[i].key=bindings[i].last_key;bindings[i].mods=bindings[i].last_mods;
  conflict_notice=true;api->Log("QOL: duplicate or reserved binding rejected: %s",bindings[i].label);return;
 }
 bindings[i].last_key=bindings[i].key;bindings[i].last_mods=bindings[i].mods;
 conflict_notice=false;save_bindings();
}
unsigned qol_current_modifiers(void){
 return ((GetKeyState(VK_SHIFT)&0x8000)?QOL_SHIFT:0)|((GetKeyState(VK_CONTROL)&0x8000)?QOL_CTRL:0)|((GetKeyState(VK_MENU)&0x8000)?QOL_ALT:0);
}
unsigned qol_resolve_key(unsigned key,unsigned mods){return (unsigned)qol_match(bindings,COUNT(bindings),(int)key,(int)mods);}
int qol_is_capturing(void){return capture>=0;}
void qol_cancel_capture(void){capture=-1;}
int qol_capture_key(unsigned message,unsigned key){
 if(capture<0)return 0;
 if(message!=WM_KEYDOWN&&message!=WM_SYSKEYDOWN&&message!=WM_KEYUP&&message!=WM_SYSKEYUP)return 0;
 if(message==WM_KEYUP||message==WM_SYSKEYUP)return 1;
 if(key==VK_SHIFT||key==VK_CONTROL||key==VK_MENU||key==VK_LSHIFT||key==VK_RSHIFT||key==VK_LCONTROL||key==VK_RCONTROL||key==VK_LMENU||key==VK_RMENU)return 1;
 int index=capture;capture=-1;
 if(key==VK_ESCAPE)return 1;
 bindings[index].key=(key==VK_BACK||key==VK_DELETE)?0:(int)key;
 bindings[index].mods=bindings[index].key?(int)qol_current_modifiers():0;
 binding_changed(NULL,(void*)(uintptr_t)index);return 1;
}
int qol_speed_key_down(int index){
 if(capture>=0||index<0||index>2)return 0;
 QolBinding *b=&bindings[20+index];
 return b->key&&(GetAsyncKeyState(b->key)&0x8000)&&qol_current_modifiers()==(unsigned)b->mods;
}

DK2ML_Status qol_collect_option(const DK2ML_Option *option){
 if(option->type==DK2ML_OPTION_HEADER)return DK2ML_OK;
 if(option_count>=COUNT(options))return DK2ML_ERROR;
 QolOption *entry=&options[option_count++];entry->option=*option;const char *label=option->label;
 entry->section=0;
 if(strstr(label,"go-codes"))entry->section=1;
 if(strstr(label,"speed")||strstr(label,"Speed")||strstr(label,"replays"))entry->section=3;
 if(strstr(label,"trooper")||strstr(label,"Name size")||strstr(label,"Gap below"))entry->section=4;
 // Fixed defaults from the user's saved settings snapshot, 2026-11-17.
 if(option->type==DK2ML_OPTION_BOOL){entry->defaults.boolean=true;if(strstr(label,"on trooper or path hover"))entry->defaults.boolean=false;}
 if(option->type==DK2ML_OPTION_INT)entry->defaults.integer=50;
 if(option->type==DK2ML_OPTION_FLOAT){
  if(strstr(label,"Name size"))entry->defaults.real=10;
  else if(strstr(label,"Gap below"))entry->defaults.real=20;
  else return DK2ML_ERROR;
 }
 return DK2ML_OK;
}
static void reset_settings(int page){
 capture=-1;conflict_notice=false;
 // Apply all values in scope before callbacks that save a whole feature.
 for(size_t i=0;i<option_count;i++){
  if(page>=0&&options[i].section!=page)continue;
  DK2ML_Option *o=&options[i].option;
  if(o->type==DK2ML_OPTION_BOOL)*(bool*)o->value=options[i].defaults.boolean;
  if(o->type==DK2ML_OPTION_INT)*(int*)o->value=options[i].defaults.integer;
  if(o->type==DK2ML_OPTION_FLOAT)*(float*)o->value=options[i].defaults.real;
 }
 for(size_t i=0;i<option_count;i++){
  DK2ML_Option *o=&options[i].option;
  if((page<0||options[i].section==page)&&o->onChange)o->onChange(o,o->user);
 }
 QolBinding updated[COUNT(bindings)];
 memcpy(updated,bindings,sizeof updated);
 for(size_t i=0;i<COUNT(updated);i++){
  if(page>=0&&updated[i].section!=page)continue;
  if(basic_113&&!qol_basic_action(updated[i].action))continue;
  updated[i].key=updated[i].last_key=updated[i].default_key;
  updated[i].mods=updated[i].last_mods=updated[i].default_mods;
 }
 for(size_t i=0;i<COUNT(updated);i++)
  if(qol_conflicts(updated,COUNT(updated),i)){conflict_notice=true;break;}
 if(!conflict_notice){memcpy(bindings,updated,sizeof bindings);save_bindings();}
 if(page<0||page==3)qol_reset_speed_runtime();
}
static void reset_all(const DK2ML_Option *option,void *user){
 (void)option;(void)user;reset_settings(-1);
}
int qol_ui_prepare(const DK2ML_API *loader){
 api=loader;const wchar_t *dir=api->GetConfigDir();if(!dir||wcslen(dir)+17>=MAX_PATH)return 1;
 basic_113=api->GetGameVersion&&api->GetGameVersion()==113;
 swprintf(binding_path,MAX_PATH,L"%lsbindings.ini",dir);
 QolBinding *all=&bindings[COUNT(bindings)-1];
 SHORT hash_key=VkKeyScanW(L'#');
 all->default_key=0;
 all->default_mods=0;
 if(hash_key!=-1&&((hash_key>>8)&~7)==0&&!reserved_key(hash_key&255)){
  const int key=hash_key&255,mods=(hash_key>>8)&7;
  bool taken=false;
  for(size_t i=0;i<COUNT(bindings)-1;i++)
   if(bindings[i].default_key==key&&bindings[i].default_mods==mods)taken=true;
  if(!taken){all->default_key=key;all->default_mods=mods;}
 }
 // Validate the saved table. A newly introduced default must not replace a
 // player's existing custom binding if they already use that combination.
 bool all_saved=false;
 for(size_t i=0;i<COUNT(bindings);i++){
  wchar_t key[24];swprintf(key,24,L"Action%d",bindings[i].action);
  int packed=GetPrivateProfileIntW(L"Bindings",key,-1,binding_path);
  if(&bindings[i]==all)all_saved=packed!=-1;
  if(packed==-1)packed=bindings[i].default_key|(bindings[i].default_mods<<8);
  bindings[i].key=packed&255;bindings[i].mods=(packed>>8)&7;
 }
 if(!all_saved&&qol_conflicts(bindings,COUNT(bindings),COUNT(bindings)-1)){
  all->key=0;
  all->mods=0;
 }
 bool invalid=false;for(size_t i=0;i<COUNT(bindings);i++)if(qol_conflicts(bindings,COUNT(bindings),i)||reserved_key(bindings[i].key))invalid=true;
 for(size_t i=0;i<COUNT(bindings);i++){
  if(invalid){bindings[i].key=bindings[i].default_key;bindings[i].mods=bindings[i].default_mods;}
  bindings[i].last_key=bindings[i].key;bindings[i].last_mods=bindings[i].mods;
 }
 if(invalid)api->Log("QOL: invalid saved binding table; using defaults without overwriting the file.");
 button=(void*)api->ResolveSymbol("ImGui::ButtonEx");checkbox=(void*)api->ResolveSymbol("ImGui::Checkbox");
 slider=(void*)api->ResolveSymbol("ImGui::SliderScalar");text=(void*)api->ResolveSymbol("ImGui::Text");same_line=(void*)api->ResolveSymbol("ImGui::SameLine");
 align_text=(void*)api->ResolveSymbol("ImGui::AlignTextToFramePadding");
 text_line_height=(void*)api->ResolveSymbol("ImGui::GetTextLineHeight");
 int64_t t=0;if(api->GetEnumValue("ImGuiDataType_","ImGuiDataType_Float",&t)!=DK2ML_OK)return 1;scalar_float=(int)t;
 if(api->GetEnumValue("ImGuiDataType_","ImGuiDataType_S32",&t)!=DK2ML_OK)return 1;scalar_int=(int)t;
 return !(button&&checkbox&&slider&&text&&same_line);
}
static bool is_default_slow_speed(const DK2ML_Option *option){
 return strcmp(option->label,"Default slow-motion speed")==0;
}

int qol_ui_register(void){
 for(int group=0;group<5;group++){
  if(basic_113&&group==2)continue;
  DK2ML_Option header={0};header.structSize=sizeof header;header.type=DK2ML_OPTION_HEADER;header.label=sections[group];
  if(api->AddOption(&header)!=DK2ML_OK)return 1;
  for(size_t i=0;i<option_count;i++)if(options[i].section==group){
   DK2ML_Option o=options[i].option;
   if(is_default_slow_speed(&o))continue;
   if(api->AddOption(&o)!=DK2ML_OK)return 1;
  }
  for(size_t i=0;i<COUNT(bindings);i++)if(bindings[i].section==group&&
      (!basic_113||qol_basic_action(bindings[i].action))){
   DK2ML_Option o={0};o.structSize=sizeof o;o.type=DK2ML_OPTION_KEY;o.label=bindings[i].label;
   o.value=&bindings[i].key;o.onChange=binding_changed;o.user=(void*)(uintptr_t)i;
   o.tooltip="Click to change the keyboard key; Backspace/Delete unbinds. Choose modifiers in the next row. Duplicate QOL combinations are refused.";
   if(bindings[i].action==257)o.tooltip="The sequence only works on unlocked doors. Sufficient space and unobstructed straight paths from the current positions are required for automatic waypoint placement.";
   if(api->AddOption(&o)!=DK2ML_OK)return 1;
   char label[128];snprintf(label,sizeof label,"Modifiers: %s",bindings[i].label);
   o.type=DK2ML_OPTION_CHOICE;o.label=label;o.value=&bindings[i].mods;o.choices=modifier_choices;o.choiceCount=8;
   if(api->AddOption(&o)!=DK2ML_OK)return 1;
  }
  for(size_t i=0;i<option_count;i++)if(options[i].section==group&&is_default_slow_speed(&options[i].option)){
   if(api->AddOption(&options[i].option)!=DK2ML_OK)return 1;
  }
 }
 if(basic_113){
  DK2ML_Option status={0};status.structSize=sizeof status;status.type=DK2ML_OPTION_HEADER;
  status.label="1.13 test build: waypoint shortcuts, normal-speed replays and trooper names";
  if(api->AddOption(&status)!=DK2ML_OK)return 1;
 }
 DK2ML_Option reset={0};reset.structSize=sizeof reset;reset.type=DK2ML_OPTION_BUTTON;
 reset.label="Reset QOL Tweaks to defaults";reset.tooltip="Reset only QOL Tweaks, including all key bindings, to the saved 2026-11-17 defaults.";reset.onChange=reset_all;
 return api->AddOption(&reset)!=DK2ML_OK;
}
static void key_label(const QolBinding *b,char *label,size_t size){
 char key[64]="Unbound";
 if(b->key){LONG scan=(LONG)MapVirtualKeyW((UINT)b->key,MAPVK_VK_TO_VSC)<<16;
  if(!GetKeyNameTextA(scan,key,sizeof key))snprintf(key,sizeof key,"Key %d",b->key);}
 snprintf(label,size,"%s%s%s%s",(b->mods&QOL_CTRL)?"Ctrl+":"",(b->mods&QOL_ALT)?"Alt+":"",(b->mods&QOL_SHIFT)?"Shift+":"",key);
}
static void draw_binding(size_t i){
 if(basic_113&&!qol_basic_action(bindings[i].action))return;
 float em=text_line_height ? text_line_height() : 18.0f;
 const Vec2 key_size={17.0f*em,0};
 char key[100],label[160];
 key_label(&bindings[i],key,sizeof key);
 if(align_text)align_text();
 text("%s",bindings[i].label);
 same_line(17.5f*em,-1);
 const char *caption=capture==(int)i?"Press key...":key;
 const size_t length=strlen(caption);
 // ImGui hides the text starting at ##, so separate a visible trailing #.
 const char *separator=length&&caption[length-1]=='#'?" ":"";
 snprintf(label,sizeof label,"%s%s##qbind%u",caption,separator,(unsigned)i);
 if(button(label,&key_size,0))capture=(int)i;
}

static void draw_option(DK2ML_Option *o){
  bool changed=false;
  const char *label=o->label;
  if(o->type==DK2ML_OPTION_BOOL)changed=checkbox(label,o->value);
  else if(o->type==DK2ML_OPTION_FLOAT){float low=o->min,high=o->max;changed=slider(label,scalar_float,o->value,&low,&high,o->format?o->format:"%.2f",0);}
  else if(o->type==DK2ML_OPTION_INT){int low=(int)o->min,high=(int)o->max;changed=slider(label,scalar_int,o->value,&low,&high,o->format?o->format:"%d",0);}
  if(changed&&o->onChange)o->onChange(o,o->user);
  if(strcmp(label,"Play replays at normal speed")==0)text("%s",o->tooltip);
}

void qol_draw_settings(int page,void (*heading)(const char *)){
 if(page<0||page>=(int)COUNT(sections))return;
 if(basic_113){
  static const char *unavailable[]={
   "1.13 test build: waypoint-wheel shortcuts only.\nGrenade, suppression, right-drag pie and troop-plan shortcuts await porting.",
   "Go-code assignment is available. Autoexecution shortcuts await porting.",
   "Open & Center-Check and Open door only await porting to 1.13.",
   "Speed Changer awaits porting to 1.13.",
   NULL
  };
  if(unavailable[page])text("%s",unavailable[page]);
  if(page==2)return;
 }
 if(page!=0)heading(sections[page]);
 if(page==2){
  text("The sequence only works on unlocked doors.");
  text("Sufficient space and unobstructed straight paths from the\ncurrent positions are required for automatic waypoint placement.");
 }
 for(size_t i=0;i<option_count;i++)if(options[i].section==page&&!is_default_slow_speed(&options[i].option))
  draw_option(&options[i].option);
 if(page==0){
  heading("Movement");
  draw_binding(0);draw_binding(2);
  heading("Weapons");
  draw_binding(1);draw_binding(4);draw_binding(5);draw_binding(6);
  heading("Plans");
  draw_binding(3);draw_binding(7);draw_binding(8);draw_binding(9);
 }else{
  for(size_t i=0;i<COUNT(bindings);i++)if(bindings[i].section==page)draw_binding(i);
 }
 for(size_t i=0;i<option_count;i++)if(options[i].section==page&&is_default_slow_speed(&options[i].option))
  draw_option(&options[i].option);
 if(capture>=0)text("Press a key with optional Ctrl/Shift/Alt. Esc cancels; Backspace/Delete clears.");
 if(conflict_notice)text("That combination is already assigned or reserved. Previous binding kept.");
}
void qol_draw_reset(int page){
 if(basic_113&&page==2)return;
 const Vec2 automatic={0,0};
 if(button("Reset this tab",&automatic,0))reset_settings(page);
}
