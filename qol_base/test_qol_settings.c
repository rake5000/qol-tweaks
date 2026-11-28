#undef NDEBUG
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <assert.h>
static unsigned simulated_mods;
static SHORT fake_key_state(int key){
 return ((key==VK_SHIFT&&(simulated_mods&1))||(key==VK_CONTROL&&(simulated_mods&2))||(key==VK_MENU&&(simulated_mods&4)))?(SHORT)0x8000:0;
}
#define GetKeyState fake_key_state
#include "qol_ui.c"
static wchar_t test_dir[MAX_PATH];
static int reset_speed_calls,registered,headers;static int section_seen[5];
static bool order_enabled=false,replay_enabled=false,name_hover=true,name_always=false,speed_enabled=false;
static float name_size=23,gap=39;
static int speed_default=80;
static int callbacks;
void qol_reset_speed_runtime(void){reset_speed_calls++;}
static const wchar_t *mock_dir(void){return test_dir;}
static void mock_log(const char *format,...){(void)format;}
static void *mock_resolve(const char *name){(void)name;return (void*)(uintptr_t)1;}
static DK2ML_Status mock_enum(const char *type,const char *name,int64_t *value){(void)type;(void)name;*value=1;return DK2ML_OK;}
static uint32_t mock_version_113(void){return 113;}
static DK2ML_Status mock_add(const DK2ML_Option *option){
 assert(option->structSize==sizeof *option);registered++;
 if(option->type==DK2ML_OPTION_HEADER){headers++;assert(strcmp(option->label,"Normal Speed Replays")!=0);for(int i=0;i<5;i++)if(strcmp(option->label,sections[i])==0)section_seen[i]++;}
 return DK2ML_OK;
}
static void observe_reset(const DK2ML_Option *option,void *user){
 (void)option;(void)user;callbacks++;
 assert(order_enabled&&replay_enabled&&name_always&&speed_enabled&&!name_hover);
 assert(name_size==10&&gap==20&&speed_default==50);
}
static void collect(const char *label,DK2ML_OptionType type,void *value){
 DK2ML_Option o={0};o.structSize=sizeof o;o.type=type;o.label=label;o.value=value;o.min=0;o.max=100;o.onChange=observe_reset;
 assert(qol_collect_option(&o)==DK2ML_OK);
}
int main(void){
 wchar_t temp[MAX_PATH],unique[MAX_PATH];assert(GetTempPathW(MAX_PATH,temp));assert(GetTempFileNameW(temp,L"r5q",0,unique));assert(DeleteFileW(unique));assert(CreateDirectoryW(unique,NULL));
 swprintf(test_dir,MAX_PATH,L"%ls\\",unique);
 DK2ML_API loader={0};loader.GetConfigDir=mock_dir;loader.Log=mock_log;loader.ResolveSymbol=mock_resolve;loader.GetEnumValue=mock_enum;loader.AddOption=mock_add;
 assert(qol_ui_prepare(&loader)==0);
 for(size_t i=0;i<COUNT(bindings);i++){assert(!qol_conflicts(bindings,COUNT(bindings),i));assert(qol_resolve_key(bindings[i].key,bindings[i].mods)==(unsigned)bindings[i].action);}
 assert(qol_resolve_key('F',0)=='F');assert(qol_resolve_key('F',QOL_CTRL)==256);assert(qol_resolve_key('F',QOL_CTRL|QOL_SHIFT)==0);
 assert(qol_resolve_key('1',QOL_SHIFT)=='1');assert(qol_resolve_key('1',QOL_CTRL)==QOL_AUTO_A);
 // Native widget change must reject a duplicate, including across sections.
 DK2ML_Option key_option={0};key_option.type=DK2ML_OPTION_KEY;
 bindings[0].key='R';binding_changed(&key_option,0);assert(bindings[0].key=='G'&&conflict_notice);
 bindings[0].key=VK_ADD;binding_changed(&key_option,0);assert(bindings[0].key=='G');
 bindings[0].key=VK_SHIFT;binding_changed(&key_option,0);assert(bindings[0].key=='G');
 bindings[0].key='K';binding_changed(&key_option,0);assert(bindings[0].key=='K'&&!conflict_notice);
 assert(qol_resolve_key('K',0)=='G'&&qol_resolve_key('G',0)==0);
 // Reload persisted bindings; defaults must stay G even after saved K.
 assert(qol_ui_prepare(&loader)==0);assert(bindings[0].key=='K'&&bindings[0].default_key=='G');
 capture=0;simulated_mods=QOL_CTRL|QOL_ALT;assert(qol_capture_key(WM_KEYDOWN,'L'));assert(bindings[0].key=='L'&&bindings[0].mods==6);
 capture=0;assert(qol_capture_key(WM_KEYDOWN,VK_ESCAPE));assert(bindings[0].key=='L'&&capture==-1);
 capture=0;assert(qol_capture_key(WM_KEYDOWN,VK_DELETE));assert(bindings[0].key==0);
 capture=1;qol_cancel_capture();assert(!qol_is_capturing());assert(!qol_capture_key(WM_KEYDOWN,'X'));
 collect("Enable additional hotkeys",DK2ML_OPTION_BOOL,&order_enabled);
 collect("Play replays at normal speed",DK2ML_OPTION_BOOL,&replay_enabled);
 collect("Show names on trooper or path hover",DK2ML_OPTION_BOOL,&name_hover);
 collect("Always show trooper names",DK2ML_OPTION_BOOL,&name_always);
 collect("Name size",DK2ML_OPTION_FLOAT,&name_size);collect("Gap below feet",DK2ML_OPTION_FLOAT,&gap);
 collect("Default slow-motion speed",DK2ML_OPTION_INT,&speed_default);collect("Enable Speed Changer",DK2ML_OPTION_BOOL,&speed_enabled);
 assert(qol_ui_register()==0);assert(headers==5);for(int i=0;i<5;i++)assert(section_seen[i]==1);
 assert(registered==5+(int)option_count+2*(int)COUNT(bindings)+1);
 int unrelated_gameplay_setting=123;reset_all(NULL,NULL);assert(unrelated_gameplay_setting==123);
 assert(callbacks==(int)option_count&&reset_speed_calls==1);
 for(size_t i=0;i<COUNT(bindings);i++)assert(bindings[i].key==bindings[i].default_key&&bindings[i].mods==bindings[i].default_mods);
 assert(GetPrivateProfileIntW(L"Bindings",L"Action71",0,binding_path)=='G');
 bindings[0].key='K';binding_changed(NULL,0);
 bindings[18].key='M';binding_changed(NULL,(void*)(uintptr_t)18);
 int before=callbacks;reset_settings(0);
 assert(bindings[0].key=='G'&&bindings[18].key=='M'&&reset_speed_calls==1&&callbacks==before+1);
 bindings[0].key='K';binding_changed(NULL,0);
 bindings[18].key='G';binding_changed(NULL,(void*)(uintptr_t)18);
 reset_settings(0);
 assert(conflict_notice&&bindings[0].key=='K'&&bindings[18].key=='G');

 // The test build exposes only the shortcuts it implements and keeps full-version preferences.
 bindings[18].key='M';binding_changed(NULL,(void*)(uintptr_t)18);
 loader.GetGameVersion=mock_version_113;
 assert(qol_ui_prepare(&loader)==0&&basic_113);
 option_count=0;registered=0;headers=0;memset(section_seen,0,sizeof section_seen);
 collect("Enable additional hotkeys",DK2ML_OPTION_BOOL,&order_enabled);
 assert(qol_ui_register()==0);
 assert(headers==3&&registered==25&&section_seen[0]==1&&section_seen[1]==1&&!section_seen[2]);
 assert(qol_basic_action('G')&&qol_basic_action('J')&&qol_basic_action('4'));
 assert(!qol_basic_action('F')&&!qol_basic_action(QOL_AUTO_A)&&!qol_basic_action(QOL_CLEAR_ALL));
 reset_settings(0);
 assert(bindings[0].key=='G'&&bindings[18].key=='M');
 puts("PASS: 1.13 exposes the basic shortcut subset and preserves unavailable bindings.");
 assert(DeleteFileW(binding_path));assert(RemoveDirectoryW(unique));
 puts("PASS: all 24 bindings, exact modifiers, cross-section conflict rejection, rebind persistence, capture/cancel/unbind, five groups and fixed reset defaults with callbacks after value restoration.");
 return 0;
}
