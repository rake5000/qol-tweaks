#ifndef R5K_DOOR_SETTINGS_H
#define R5K_DOOR_SETTINGS_H
#include <math.h>
#include <stdlib.h>
static bool autoexecute_go_codes=true;
static _Atomic bool autoexecute_reapply;
static void autoexecute_changed(const DK2ML_Option *option,void *user) {
 (void)option;(void)user;
 if(settings_path[0])WritePrivateProfileStringW(L"GoCodes",L"AutoExecute",autoexecute_go_codes?L"1":L"0",settings_path);
 atomic_store(&autoexecute_reapply,true);
}
static float door_sidestep_distance=.30f;
static float door_buddy_spacing=.50f;
static const float door_buddy_min=.20f,door_buddy_max=1.50f;
static float door_breach_distance=1.14f;
static const float door_breach_min=.2f,door_breach_max=2.0f;
static const float door_distance_min=.1f,door_distance_max=3.0f;
#ifndef R5K_QOL_UI
static bool (*door_distance_slider)(const char *,int,void *,const void *,const void *,const char *,int);
static int door_distance_type;
static void door_breach_changed(const DK2ML_Option *option,void *user) {
 (void)option;(void)user;
 if(!isfinite(door_breach_distance))door_breach_distance=1.14f;
 door_breach_distance=fminf(door_breach_max,fmaxf(door_breach_min,door_breach_distance));
 if(settings_path[0]){wchar_t text[32];swprintf(text,32,L"%.2f",(double)door_breach_distance);WritePrivateProfileStringW(L"Door",L"BreachCenterDistance",text,settings_path);}
}
static void door_retreat_changed(const DK2ML_Option *option,void *user) {
 (void)option;(void)user;
 if(!isfinite(door_sidestep_distance))door_sidestep_distance=.30f;
 door_sidestep_distance=fminf(door_distance_max,fmaxf(door_distance_min,door_sidestep_distance));
 if(settings_path[0]){wchar_t text[32];swprintf(text,32,L"%.2f",(double)door_sidestep_distance);WritePrivateProfileStringW(L"Door",L"SidestepDistance",text,settings_path);}
}
static void door_buddy_changed(const DK2ML_Option *option,void *user) {
 (void)option;(void)user;
 if(!isfinite(door_buddy_spacing))door_buddy_spacing=.50f;
 door_buddy_spacing=fminf(door_buddy_max,fmaxf(door_buddy_min,door_buddy_spacing));
 if(settings_path[0]){wchar_t text[32];swprintf(text,32,L"%.2f",(double)door_buddy_spacing);WritePrivateProfileStringW(L"Door",L"BuddyStagingDistance",text,settings_path);}
}
static void draw_door_settings(void) {
 draw_shared_settings();
 if(imgui_checkbox("Auto-execute go-codes##cursor_hotkeys",&autoexecute_go_codes))autoexecute_changed(NULL,NULL);
 if(door_distance_slider("Breaching distance from door center##cursor_hotkeys",door_distance_type,&door_breach_distance,&door_breach_min,&door_breach_max,"%.2f m",0))door_breach_changed(NULL,NULL);
 if(door_distance_slider("Retreat distance after opening##cursor_hotkeys",door_distance_type,&door_sidestep_distance,&door_distance_min,&door_distance_max,"%.2f m",0))door_retreat_changed(NULL,NULL);
 if(door_distance_slider("Buddy staging distance##cursor_hotkeys",door_distance_type,&door_buddy_spacing,&door_buddy_min,&door_buddy_max,"%.2f m",0))door_buddy_changed(NULL,NULL);
 imgui_text("Position along the wall from door center. O positions, opens and retreats while aiming.");

}
#endif
static bool configure_door_settings(void) {
 if(settings_path[0])autoexecute_go_codes=GetPrivateProfileIntW(L"GoCodes",L"AutoExecute",1,settings_path)!=0;
 if(settings_path[0]) {
  wchar_t text[64],*end=NULL;
  GetPrivateProfileStringW(L"Door",L"SidestepDistance",L"0.30",text,64,settings_path);
  float value=wcstof(text,&end);
  if(end!=text&&!*end&&isfinite(value)&&value>=door_distance_min&&value<=door_distance_max)
   door_sidestep_distance=value;
 }
 if(settings_path[0]) {
  wchar_t text[64],*end=NULL;GetPrivateProfileStringW(L"Door",L"BreachCenterDistance",L"1.14",text,64,settings_path);
  float adjustment=wcstof(text,&end);if(end!=text&&!*end&&isfinite(adjustment)&&adjustment>=door_breach_min&&adjustment<=door_breach_max)door_breach_distance=adjustment;
 }
 if(settings_path[0]) {
  wchar_t text[64],*end=NULL;GetPrivateProfileStringW(L"Door",L"BuddyStagingDistance",L"0.50",text,64,settings_path);
  float value=wcstof(text,&end);if(end!=text&&!*end&&isfinite(value)&&value>=door_buddy_min&&value<=door_buddy_max)door_buddy_spacing=value;
 }
#ifndef R5K_QOL_UI
 door_distance_slider=(bool (*)(const char *,int,void *,const void *,const void *,const char *,int))api->ResolveSymbol("ImGui::SliderScalar");
 int64_t value=0;
 if(!door_distance_slider||api->GetEnumValue("ImGuiDataType_","ImGuiDataType_Float",&value)!=DK2ML_OK)return false;
 door_distance_type=(int)value;
#endif
 DK2ML_Option o={0};o.structSize=sizeof(o);o.type=DK2ML_OPTION_BOOL;
 o.label="Auto-execute go-codes";o.tooltip="Applies A-D autoexecution to this mission and sets the default for future missions. Individual Ctrl+1-4 toggles remain available.";
 o.value=&autoexecute_go_codes;o.onChange=autoexecute_changed;
 if(api->AddOption(&o)!=DK2ML_OK)return false;
#ifndef R5K_QOL_UI
 memset(&o,0,sizeof(o));o.structSize=sizeof(o);o.type=DK2ML_OPTION_FLOAT;
 o.label="Breaching distance from door center";o.tooltip="Along-wall distance from closed-door center. O positions, opens and retreats while aiming; saves live and applies to next O.";
 o.value=&door_breach_distance;o.min=door_breach_min;o.max=door_breach_max;o.format="%.2f m";o.onChange=door_breach_changed;
 if(api->AddOption(&o)!=DK2ML_OK)return false;
 o.label="Retreat distance after opening";o.tooltip="Additional distance along the wall from breaching position after opening. Saves live; applies to next O.";
 o.value=&door_sidestep_distance;o.min=door_distance_min;o.max=door_distance_max;o.onChange=door_retreat_changed;
 if(api->AddOption(&o)!=DK2ML_OK)return false;
 o.label="Buddy staging distance";o.tooltip="Distance out from breacher's planned opening position. Default 0.50 m. Saves live; applies to next O. Buddy selection range remains 1.50 m.";
 o.value=&door_buddy_spacing;o.min=door_buddy_min;o.max=door_buddy_max;o.onChange=door_buddy_changed;
 if(api->AddOption(&o)!=DK2ML_OK)return false;
 shared_settings.draw=draw_door_settings;
#endif
 return true;
}
#endif
