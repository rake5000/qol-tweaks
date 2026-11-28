#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stddef.h>
#include <stdio.h>
#include <wchar.h>
#include <string.h>
#include "label_geometry.h"
#include "../enemy_autopause/sdk/include/dk2ml.h"
#include "../enemy_autopause/sdk/include/rake_settings_api.h"
DK2ML_PLUGIN_MANIFEST(3,"Selected Trooper Name","1.2.0","Rake5000","",112);
static const DK2ML_API*api;
static uintptr_t image;
static bool names_113;
static unsigned renderer_camera, camera_view, camera_projection, camera_left, client_viewport, client_player;
static bool enabled=true;
static bool hover_names=true,always_names=false;
static float size=14,gap=12;
static wchar_t config[MAX_PATH];
static void*(*selected_human)(void*);
static void*(*hovered_human)(void);
static int(*build_letters)(const char*,int,void*,FontVertex*,uint16_t*,uint16_t);
static void(*print_text)(const float*,const float*,const float*,void*,uint32_t,const char*);
static void*(*map_to_screen)(void*,float*,const float*);
typedef struct {uintptr_t human;char name[52];float screen[2];} CapturedLabel;
static CapturedLabel captures[64];
static unsigned capture_count;

static bool(*checkbox)(const char*,bool*);
static bool(*slider)(const char*,int,void*,const void*,const void*,const char*,int);
static void(*text)(const char*,...);
static int float_type;
static uintptr_t pointer(uintptr_t p){return *(uintptr_t*)p;}
static void save(const DK2ML_Option*o,void*u){
    (void)o;(void)u;wchar_t v[40];
    swprintf(v,40,L"%d",enabled);WritePrivateProfileStringW(L"Label",L"Enabled",v,config);
    WritePrivateProfileStringW(L"Label",L"BodyHover",hover_names?L"1":L"0",config);
    WritePrivateProfileStringW(L"Label",L"AlwaysShow",always_names?L"1":L"0",config);
    swprintf(v,40,L"%.1f",size);WritePrivateProfileStringW(L"Label",L"FontSize",v,config);
    swprintf(v,40,L"%.1f",gap);WritePrivateProfileStringW(L"Label",L"FeetGap",v,config);
}
static void draw_settings(void){
    text("Selected Trooper Name");
    if(checkbox("Enable trooper names",&enabled))save(NULL,NULL);
    if(checkbox("Show names on trooper or path hover",&hover_names))save(NULL,NULL);
    if(checkbox("Always show trooper names",&always_names))save(NULL,NULL);
    float low=9,high=24;
    if(slider("Name size",float_type,&size,&low,&high,"%.0f",0))save(NULL,NULL);
    low=0;high=40;
    if(slider("Gap below feet",float_type,&gap,&low,&high,"%.0f",0))save(NULL,NULL);
}
static const R5K_SettingsModule settings={sizeof(R5K_SettingsModule),"Selected Trooper Name",draw_settings};
static void*get_font(void){
    uintptr_t manager=pointer(image+(names_113?0x1038c90:0xff0ef0));if(!manager)return NULL;
    int count=*(int*)(manager+0x10);uintptr_t fonts=pointer(manager+8);
    if(count<1||count>256||!fonts)return NULL;
    for(int j=0;j<count;j++){
        uintptr_t font=pointer(fonts+j*8);if(!font)continue;
        const char*name=(void*)pointer(font+0x10);
        if(name&&!strcmp(name,"paragraph_3"))return (void*)font;
    }
    return NULL;
}
// Native hover resolves a body, waypoint or path to its owning human.
static bool hover_target_matches(uintptr_t human){return hovered_human&&(uintptr_t)hovered_human()==human;}
static bool eligible_human(uintptr_t human,uintptr_t client){
    uint32_t player=*(uint32_t*)(client+client_player);
    return player<32&&*(uint32_t*)(human+0x32c)==(1u<<player)&&*(unsigned char*)(human+0x333)&&*(float*)(human+40)>0;
}
static bool wants_name(bool selected,bool hovered){return selected||always_names||(hover_names&&hovered);}
static int before_status(DK2ML_Regs*r,void*u){(void)u;r->scratch[0]=DK2ML_Arg(r,0);r->scratch[1]=DK2ML_Arg(r,5);return 0;}
static void after_status(DK2ML_Regs*r,void*u){
    (void)u;if(!enabled)return;
    uintptr_t human=r->scratch[0],input=pointer(image+(names_113?0x100e6e0:0xfe8a68)),renderer=pointer(image+(names_113?0x1038c30:0xff0e98));
    LabelList*list=(void*)r->scratch[1];
    if(!input||!renderer||!human||!list_valid(list))return;
    uintptr_t client=pointer(image+(names_113?0x1038c40:0xff0ea8));if(!client||!eligible_human(human,client))return;
    bool selected=(uintptr_t)selected_human((void*)input)==human;
    if(!wants_name(selected,!selected&&!always_names&&hover_names&&hover_target_matches(human)))return;
    for(unsigned i=0;i<capture_count;++i)if(captures[i].human==human)return;
    if(capture_count>=64)return;
    // Human_Client::m_id is the same inline UTF-8 name used by UpdateHumanProfile.
    const char*name=(const char*)(human+0x220);int length=0;
    while(length<52&&name[length])length++;
    if(!length||length==52)return;
    float point[3];memcpy(point,(void*)(human+0x6c),sizeof(point));
    for(int k=0;k<3;k++)if(!isfinite(point[k]))return;
    float screen[2];map_to_screen((void*)client,screen,point);
    const int*viewport=(void*)(client+client_viewport);
    if(viewport[2]<=0||viewport[3]<=0)return;
    float x=(screen[0]-viewport[0])/(float)viewport[2];
    float y=(screen[1]-viewport[1])/(float)viewport[3];
    if(!isfinite(x)||!isfinite(y)||x<0||x>1||y<0||y>1)return;
    CapturedLabel*label=&captures[capture_count++];label->human=human;
    memcpy(label->name,name,(size_t)length);label->name[length]=0;
    label->screen[0]=x;label->screen[1]=y;
}
#ifdef R5K_QOL_UI
extern int qol_speed_hud_value(void);

static void draw_speed_indicator(uintptr_t renderer, void *font, int value){
    char label[8];
    int length=snprintf(label,sizeof(label),"%d%%",value<0?-value:value);
    if(length<=0||length>=(int)sizeof(label))return;
    FontVertex vertices[8*4];
    uint16_t indices[8*6];
    int count=build_letters(label,length,font,vertices,indices,0);
    if(count<=0||count>length)return;

    float right=vertices[0].p[0],bottom=vertices[0].p[1];
    for(int i=1;i<count*4;i++){
        right=fmaxf(right,vertices[i].p[0]);
        bottom=fminf(bottom,vertices[i].p[1]);
    }
    const float *camera=(const float*)(renderer+0x4f8);
    float height=fabsf(camera[0xa4/4]-camera[0xa0/4]);
    float font_height=(float)*(int*)((uintptr_t)font+0x30);
    if(!isfinite(height)||height<=0||font_height<=0)return;

    // Align the bottom of the percentage digits with the timer digits.
    float scale=height*(16.0f/1440.0f)/font_height;
    float model[16]={0};
    model[0]=model[5]=scale;model[10]=model[15]=1;
    model[3]=(camera[0x98/4]+camera[0x9c/4])*0.5f+height*(106.0f/1440.0f)-right*scale;
    model[7]=fminf(camera[0xa0/4],camera[0xa4/4])+height*(38.0f/1440.0f)-bottom*scale;
    print_text(model,(void*)(renderer+0x5a8),(void*)(renderer+0x528),font,
        value<0?0xff8fdbff:0xffdbe5e7,label);
}

static void after_draw_labels(DK2ML_Regs *regs,void *user){
    (void)user;
    uintptr_t renderer=regs->scratch[0];
    int speed_indicator=qol_speed_hud_value();
    if(!speed_indicator||!renderer||!*(bool*)(renderer+0x13)||api->IsGameMenuOpen())return;
    void *font=get_font();
    if(font)draw_speed_indicator(renderer,font,speed_indicator);
}
#endif

static int draw_label(DK2ML_Regs*r,void*u){
    (void)u;
    unsigned count_labels=enabled?capture_count:0;
    capture_count=0;
#ifdef R5K_QOL_UI
    r->scratch[0]=DK2ML_Arg(r,0);
#endif
    if(!count_labels||api->IsGameMenuOpen())return 0;
    uintptr_t renderer=DK2ML_Arg(r,0);if(!renderer||!*(bool*)(renderer+0x13))return 0;
    void*font=get_font();if(!font)return 0;
    for(unsigned label_index=0;label_index<count_labels;++label_index){
    const char*captured_name=captures[label_index].name;const float*captured_screen=captures[label_index].screen;
    FontVertex vertices[52*4];uint16_t indices[52*6];
    int length=(int)strlen(captured_name);
    int count=build_letters(captured_name,length,font,vertices,indices,0);
    if(count<=0||count>51)continue;
    float minx=INFINITY,maxx=-INFINITY,top=-INFINITY;
    for(int j=0;j<count*4;j++){minx=fminf(minx,vertices[j].p[0]);maxx=fmaxf(maxx,vertices[j].p[0]);top=fmaxf(top,vertices[j].p[1]);}
    float font_height=(float)*(int*)((uintptr_t)font+0x30);if(font_height<=0)continue;
    const float*bounds=(void*)(renderer+renderer_camera+camera_left);
    float x=bounds[0]+captured_screen[0]*(bounds[1]-bounds[0]);
    float y=bounds[2]+captured_screen[1]*(bounds[3]-bounds[2]);
    float scale=size/font_height;
    static const float offsets[5][2]={{-1,0},{1,0},{0,-1},{0,1},{0,0}};
    for(int pass=0;pass<5;pass++){
        float model[16]={0};model[0]=model[5]=scale;model[10]=model[15]=1;
        model[3]=x-(minx+maxx)*.5f*scale+offsets[pass][0];
        model[7]=y-gap-top*scale+offsets[pass][1];
        print_text(model,(void*)(renderer+renderer_camera+camera_projection),(void*)(renderer+renderer_camera+camera_view),font,pass==4?0xffcce3f0:0xff080808,captured_name);
    }
    }
    return 0;
}

static bool resolve(void**out,const char*name,unsigned rva){*out=api->ResolveSymbol(name);if((uintptr_t)*out!=image+rva){api->Log("Unsupported game symbol: %s",name);return false;}return true;}
DK2ML_EXPORT int DK2ML_PluginInit(const DK2ML_API*loader,const DK2ML_PluginInfo*info){
    (void)info;api=loader;
    unsigned version=api->GetGameVersion();if(version!=112&&version!=113)return 1;
    names_113=version==113;
    void*base_symbol=api->ResolveSymbol("GameClient::UpdateCamera");if(!base_symbol)return 1;
    image=(uintptr_t)base_symbol-(names_113?0x187c20:0x187f70);
    if(!names_113&&image==(uintptr_t)GetModuleHandleW(NULL)&&memcmp((void*)(image+0xdfbe7),"\x48\x8b\x0d\x02\x13\xf1\x00",7))return 1;
    renderer_camera=names_113?0x528:0x4f8;
    camera_view=48;camera_projection=names_113?180:176;camera_left=names_113?156:152;
    client_viewport=names_113?0x107584:0x10755c;
    client_player=names_113?0x10583c:0x10581c;
    if(api->GetFieldOffset("Human_Client","m_humanId")!=0x220||
        api->GetFieldOffset("GameRenderer","m_camera")!=(int)renderer_camera||
        api->GetFieldOffset("Camera","m_matView")!=(int)camera_view||
        api->GetFieldOffset("Camera","m_matProjection")!=(int)camera_projection||
        api->GetFieldOffset("Camera","m_left")!=(int)camera_left||
        api->GetFieldOffset("GameClient","m_viewport")!=(int)client_viewport)return 1;
    if(!resolve((void**)&hovered_human,"GameInput::GetHoveredHuman",names_113?0x135e60:0x137160)||
        !resolve((void**)&selected_human,"GameInput::GetSelectedHuman",names_113?0x135dc0:0x137090)||
        !resolve((void**)&build_letters,"Render2D::BuildFontLetters",names_113?0x1aa740:0x1a87f0)||
        !resolve((void**)&print_text,"Render2D::PrintText_Immediate",names_113?0x1ab480:0x1a97d0)||
        !resolve((void**)&map_to_screen,"?ConvertMapToScreenCoords@GameClient@@QEBA?AVVector2@@VVector3@@@Z",names_113?0xfab00:0x184d70))return 1;

    checkbox=(void*)api->ResolveSymbol("ImGui::Checkbox");slider=(void*)api->ResolveSymbol("ImGui::SliderScalar");text=(void*)api->ResolveSymbol("ImGui::Text");
    int64_t type;if(!checkbox||!slider||!text||api->GetEnumValue("ImGuiDataType_","ImGuiDataType_Float",&type)!=DK2ML_OK)return 1;float_type=(int)type;
    const wchar_t*dir=api->GetConfigDir();if(!dir||wcslen(dir)+13>=MAX_PATH)return 1;swprintf(config,MAX_PATH,L"%lssettings.ini",dir);
    enabled=GetPrivateProfileIntW(L"Label",L"Enabled",1,config)!=0;
    hover_names=GetPrivateProfileIntW(L"Label",L"BodyHover",1,config)!=0;
    always_names=GetPrivateProfileIntW(L"Label",L"AlwaysShow",0,config)!=0;
    int s=GetPrivateProfileIntW(L"Label",L"FontSize",14,config),g=GetPrivateProfileIntW(L"Label",L"FeetGap",12,config);
    if(s>=9&&s<=24)size=(float)s;if(g>=0&&g<=40)gap=(float)g;
    DK2ML_Option options[5]={0};
    const char*labels[]={"Enable trooper names","Name size","Gap below feet","Show names on trooper or path hover","Always show trooper names"};void*values[]={&enabled,&size,&gap,&hover_names,&always_names};
    for(int j=0;j<5;j++){options[j].structSize=sizeof(DK2ML_Option);options[j].type=(j==1||j==2)?DK2ML_OPTION_FLOAT:DK2ML_OPTION_BOOL;options[j].label=labels[j];options[j].value=values[j];options[j].min=j==1?9:0;options[j].max=j==1?24:40;options[j].onChange=save;if(api->AddOption(options+j)!=DK2ML_OK)return 1;}
    if(api->PublishInterface("Rake5000.Settings.SelectedTrooperName",R5K_SETTINGS_API_VERSION,&settings)!=DK2ML_OK)return 1;
    void*target;if(!resolve(&target,"BatchLOSBlockerAndCoverAndROE",names_113?0x169430:0x1699c0))return 1;
    if(api->CreateSafeHook(target,before_status,after_status,NULL)!=DK2ML_OK||api->EnableHook(target)!=DK2ML_OK)return 1;
    DK2ML_PostFn after_gui=NULL;
#ifdef R5K_QOL_UI
    // Draw the speed text after the game paints the panel behind it.
    if(!names_113)after_gui=after_draw_labels;
#endif
    void*gui;if(!resolve(&gui,"GameRenderer::RenderGUIItems",names_113?0x1560e0:0x156b70)||api->CreateSafeHook(gui,draw_label,after_gui,NULL)!=DK2ML_OK||api->EnableHook(gui)!=DK2ML_OK)return 1;
    api->Log("Ready 1.2.0: selected/body-or-path-hover/all friendly trooper names, native camera-projected labels. Author Rake5000.");return 0;
}

