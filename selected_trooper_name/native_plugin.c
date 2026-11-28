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
static unsigned client_camera;
static bool enabled=true;
static bool hover_names=true,always_names=false;
static float size=14,gap=12;
static wchar_t config[MAX_PATH];
static void*(*selected_human)(void*);
static void*(*hovered_human)(void);
static int(*build_letters)(const char*,int,void*,FontVertex*,uint16_t*,uint16_t);
static void(*print_text)(const float*,const float*,const float*,void*,uint32_t,const char*);
static void*(*map_to_screen)(void*,float*,const float*);
typedef struct {
    uintptr_t human;
    char name[52];
    union {float screen[2];float world[3];} position;
} CapturedLabel;
static CapturedLabel captures[64];
static unsigned capture_count;
typedef struct {float x,y;} NameVec2;
static uintptr_t imgui_context_slot;
static void *(*viewport_draw_list)(void*,unsigned,const char*);
static void *(*measure_overlay_text)(void*,NameVec2*,float,float,float,const char*,const char*,const char**);
static void (*render_overlay_char)(void*,void*,float,NameVec2,unsigned,unsigned short);
static struct {
    unsigned status, valid, eligible, wanted, captured;
    unsigned draw, empty, inactive, client_missing, font_missing, projection_rejected, submitted;
} hover_trace;

static void trace_hover_frame(const DK2ML_Event *event,void *user){
    (void)event;(void)user;
    static ULONGLONG last;
    ULONGLONG now=GetTickCount64();
    if(now-last<2000)return;
    last=now;
    if(enabled&&(hover_trace.status||hover_trace.draw)){
        api->Log("Name hover sample (2s): status=%u valid=%u eligible=%u wanted=%u captured=%u draw=%u empty=%u inactive=%u client_missing=%u font_missing=%u projection_rejected=%u submitted=%u always=%d hover=%d",
            hover_trace.status,hover_trace.valid,hover_trace.eligible,hover_trace.wanted,hover_trace.captured,
            hover_trace.draw,hover_trace.empty,hover_trace.inactive,hover_trace.client_missing,hover_trace.font_missing,
            hover_trace.projection_rejected,hover_trace.submitted,always_names,hover_names);
    }
    memset(&hover_trace,0,sizeof hover_trace);
}

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
    uintptr_t manager=pointer(image+(names_113?0x105d440:0xff0ef0));if(!manager)return NULL;
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
static bool project_trooper(uintptr_t client,const float point[3],float *x,float *y){
    if(names_113){
        // The named 1.13 screen conversion returns GUI coordinates. Project
        // into the viewport at draw time using the client's world camera.
        const float *view=(const float*)(client+client_camera+camera_view);
        const float *projection=(const float*)(client+client_camera+camera_projection);
        float eye[4],clip[4];
        for(unsigned i=0;i<4;i++)
            eye[i]=view[i*4]*point[0]+view[i*4+1]*point[1]+view[i*4+2]*point[2]+view[i*4+3];
        for(unsigned i=0;i<4;i++)
            clip[i]=projection[i*4]*eye[0]+projection[i*4+1]*eye[1]+projection[i*4+2]*eye[2]+projection[i*4+3]*eye[3];
        if(!isfinite(clip[3])||clip[3]<=0)return false;
        *x=0.5f*(clip[0]/clip[3]+1.0f);
        *y=0.5f*(clip[1]/clip[3]+1.0f);
    }else{
        float screen[2];
        map_to_screen((void*)client,screen,point);
        const int *viewport=(const int*)(client+client_viewport);
        if(viewport[2]<=0||viewport[3]<=0)return false;
        *x=(screen[0]-viewport[0])/(float)viewport[2];
        *y=(screen[1]-viewport[1])/(float)viewport[3];
    }
    return isfinite(*x)&&isfinite(*y)&&*x>=0&&*x<=1&&*y>=0&&*y<=1;
}

static void draw_name_overlay(const DK2ML_Event *event,void *user){
    unsigned count=enabled?capture_count:0;
    capture_count=0;
    ++hover_trace.draw;
    if(!count){++hover_trace.empty;goto sample;}
    uintptr_t client=pointer(image+0x105d3f0),context=pointer(imgui_context_slot);
    if(!client||!context){++hover_trace.client_missing;goto sample;}
    uintptr_t font=pointer(context+15248),viewports=pointer(context+16432);
    uintptr_t viewport=viewports?pointer(viewports):0;
    if(!font||!viewport){++hover_trace.font_missing;goto sample;}
    const float *area=(const float*)(viewport+4);
    if(!isfinite(area[2])||!isfinite(area[3])||area[2]<=0||area[3]<=0)goto sample;
    void *draw=viewport_draw_list((void*)viewport,1,"##Foreground");
    if(!draw)goto sample;
    for(unsigned i=0;i<count;i++){
        const CapturedLabel *label=&captures[i];
        float x,y;
        if(!project_trooper(client,label->position.world,&x,&y)){
            ++hover_trace.projection_rejected;
            continue;
        }
        NameVec2 extent={0};
        measure_overlay_text((void*)font,&extent,size,INFINITY,0,label->name,NULL,NULL);
        NameVec2 pos={roundf(area[0]+x*area[2]-extent.x*.5f),roundf(area[1]+(1-y)*area[3]+gap)};
        static const int offsets[][2]={{-1,0},{1,0},{0,-1},{0,1},{0,0}};
        wchar_t chars[52];
        int length=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,label->name,-1,chars,52);
        for(int j=0;j+1<length;j++){
            wchar_t ch=chars[j];
            if(ch>=0xd800&&ch<=0xdbff&&j+2<length){++j;ch=0xfffd;}
            for(unsigned pass=0;pass<5;pass++){
                NameVec2 at={pos.x+offsets[pass][0],pos.y+offsets[pass][1]};
                render_overlay_char((void*)font,draw,size,at,pass==4?0xffcce3f0:0xff080808,ch);
            }
            char utf8[5]={0};
            if(WideCharToMultiByte(CP_UTF8,0,&ch,1,utf8,4,NULL,NULL)){
                NameVec2 advance={0};
                measure_overlay_text((void*)font,&advance,size,INFINITY,0,utf8,NULL,NULL);
                pos.x+=advance.x;
            }
        }
        ++hover_trace.submitted;
    }
sample:
    trace_hover_frame(event,user);
}
static int before_status(DK2ML_Regs*r,void*u){(void)u;r->scratch[0]=DK2ML_Arg(r,0);r->scratch[1]=DK2ML_Arg(r,5);return 0;}
static void after_status(DK2ML_Regs*r,void*u){
    (void)u;if(!enabled)return;
    uintptr_t human=r->scratch[0],input=pointer(image+(names_113?0x1032e90:0xfe8a68));
    LabelList*list=(void*)r->scratch[1];
    if(names_113)++hover_trace.status;
    if(!input||!human||!list_valid(list)||(!names_113&&!pointer(image+0xff0e98)))return;
    if(names_113)++hover_trace.valid;
    uintptr_t client=pointer(image+(names_113?0x105d3f0:0xff0ea8));if(!client||!eligible_human(human,client))return;
    if(names_113)++hover_trace.eligible;
    bool selected=(uintptr_t)selected_human((void*)input)==human;
    if(!wants_name(selected,!selected&&!always_names&&hover_names&&hover_target_matches(human)))return;
    if(names_113)++hover_trace.wanted;
    for(unsigned i=0;i<capture_count;++i)if(captures[i].human==human)return;
    if(capture_count>=64)return;
    // Human_Client::m_id is the same inline UTF-8 name used by UpdateHumanProfile.
    const char*name=(const char*)(human+0x220);int length=0;
    while(length<52&&name[length])length++;
    if(!length||length==52)return;
    float point[3];memcpy(point,(void*)(human+0x6c),sizeof(point));
    for(int k=0;k<3;k++)if(!isfinite(point[k]))return;
    float x=0,y=0;
    if(!names_113&&!project_trooper(client,point,&x,&y))return;
    CapturedLabel*label=&captures[capture_count++];label->human=human;
    if(names_113)++hover_trace.captured;
    memcpy(label->name,name,(size_t)length);label->name[length]=0;
    if(names_113)memcpy(label->position.world,point,sizeof point);
    else {label->position.screen[0]=x;label->position.screen[1]=y;}
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
    const float *bounds=(const float*)(renderer+renderer_camera+camera_left);
    float height=fabsf(bounds[3]-bounds[2]);
    float font_height=(float)*(int*)((uintptr_t)font+0x30);
    if(!isfinite(height)||height<=0||font_height<=0)return;

    // Align the bottom of the percentage digits with the timer digits.
    float scale=height*(16.0f/1440.0f)/font_height;
    float model[16]={0};
    model[0]=model[5]=scale;model[10]=model[15]=1;
    model[3]=(bounds[0]+bounds[1])*0.5f+height*(106.0f/1440.0f)-right*scale;
    model[7]=fminf(bounds[2],bounds[3])+height*(38.0f/1440.0f)-bottom*scale;
    print_text(model,(void*)(renderer+renderer_camera+camera_projection),(void*)(renderer+renderer_camera+camera_view),font,
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

static void draw_names(uintptr_t renderer){
    unsigned count_labels=enabled?capture_count:0;
    capture_count=0;
    if(names_113)++hover_trace.draw;
    if(!count_labels){if(names_113)++hover_trace.empty;return;}
    if(!renderer||!*(bool*)(renderer+0x13)){if(names_113)++hover_trace.inactive;return;}
    if(!names_113&&api->IsGameMenuOpen())return;
    uintptr_t client=0;
    if(names_113){
        client=pointer(image+0x105d3f0);
        if(!client){++hover_trace.client_missing;return;}
    }
    void *font=get_font();
    if(!font){if(names_113)++hover_trace.font_missing;return;}
    for(unsigned label_index=0;label_index<count_labels;++label_index){
    const CapturedLabel *label=&captures[label_index];
    const char *captured_name=label->name;
    float screen_x,screen_y;
    if(names_113){
        if(!project_trooper(client,label->position.world,&screen_x,&screen_y)){
            ++hover_trace.projection_rejected;
            continue;
        }
    }else{
        screen_x=label->position.screen[0];
        screen_y=label->position.screen[1];
    }
    FontVertex vertices[52*4];uint16_t indices[52*6];
    int length=(int)strlen(captured_name);
    int count=build_letters(captured_name,length,font,vertices,indices,0);
    if(count<=0||count>51)continue;
    float minx=INFINITY,maxx=-INFINITY,top=-INFINITY;
    for(int j=0;j<count*4;j++){minx=fminf(minx,vertices[j].p[0]);maxx=fmaxf(maxx,vertices[j].p[0]);top=fmaxf(top,vertices[j].p[1]);}
    float font_height=(float)*(int*)((uintptr_t)font+0x30);if(font_height<=0)continue;
    const float*bounds=(void*)(renderer+renderer_camera+camera_left);
    float x=bounds[0]+screen_x*(bounds[1]-bounds[0]);
    float y=bounds[2]+screen_y*(bounds[3]-bounds[2]);
    float scale=size/font_height;
    static const float offsets[5][2]={{-1,0},{1,0},{0,-1},{0,1},{0,0}};
    for(int pass=0;pass<5;pass++){
        float model[16]={0};model[0]=model[5]=scale;model[10]=model[15]=1;
        model[3]=x-(minx+maxx)*.5f*scale+offsets[pass][0];
        model[7]=y-gap-top*scale+offsets[pass][1];
        print_text(model,(void*)(renderer+renderer_camera+camera_projection),(void*)(renderer+renderer_camera+camera_view),font,pass==4?0xffcce3f0:0xff080808,captured_name);
    }
    if(names_113)++hover_trace.submitted;
    }
}

static int draw_label(DK2ML_Regs *regs,void *user){
    (void)user;
    regs->scratch[0]=DK2ML_Arg(regs,0);
    if(!names_113)draw_names(regs->scratch[0]);
    return DK2ML_CALL_ORIGINAL;
}

static void finish_draw_labels(DK2ML_Regs *regs,void *user){
#ifdef R5K_QOL_UI
    after_draw_labels(regs,user);
#else
    (void)user;
#endif
}

static bool resolve(void**out,const char*name,unsigned rva){*out=api->ResolveSymbol(name);if((uintptr_t)*out!=image+rva){api->Log("Unsupported game symbol: %s",name);return false;}return true;}
DK2ML_EXPORT int DK2ML_PluginInit(const DK2ML_API*loader,const DK2ML_PluginInfo*info){
    (void)info;api=loader;
    unsigned version=api->GetGameVersion();if(version!=112&&version!=113)return 1;
    names_113=version==113;
    void*base_symbol=api->ResolveSymbol("GameClient::UpdateCamera");if(!base_symbol)return 1;
    image=(uintptr_t)base_symbol-(names_113?0x1920e0:0x187f70);
    if(!names_113&&image==(uintptr_t)GetModuleHandleW(NULL)&&memcmp((void*)(image+0xdfbe7),"\x48\x8b\x0d\x02\x13\xf1\x00",7))return 1;
    renderer_camera=names_113?0x528:0x4f8;
    camera_view=48;camera_projection=names_113?180:176;camera_left=names_113?156:152;
    client_viewport=names_113?0x107584:0x10755c;
    client_camera=0x107294;
    client_player=names_113?0x10583c:0x10581c;
    if(api->GetFieldOffset("Human_Client","m_humanId")!=0x220||
        api->GetFieldOffset("GameRenderer","m_camera")!=(int)renderer_camera||
        api->GetFieldOffset("Camera","m_matView")!=(int)camera_view||
        api->GetFieldOffset("Camera","m_matProjection")!=(int)camera_projection||
        api->GetFieldOffset("Camera","m_left")!=(int)camera_left||
        api->GetFieldOffset("GameClient","m_viewport")!=(int)client_viewport||
        (names_113&&api->GetFieldOffset("GameClient","m_camera")!=(int)client_camera))return 1;
    if(!resolve((void**)&hovered_human,"GameInput::GetHoveredHuman",names_113?0x13e210:0x137160)||
        !resolve((void**)&selected_human,"GameInput::GetSelectedHuman",names_113?0x13e170:0x137090)||
        !resolve((void**)&build_letters,"Render2D::BuildFontLetters",names_113?0x1b3a30:0x1a87f0)||
        !resolve((void**)&print_text,"Render2D::PrintText_Immediate",names_113?0x1b5230:0x1a97d0)||
        (!names_113&&!resolve((void**)&map_to_screen,"?ConvertMapToScreenCoords@GameClient@@QEBA?AVVector2@@VVector3@@@Z",0x184d70)))return 1;

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
    void*target;if(!resolve(&target,"BatchLOSBlockerAndCoverAndROE",names_113?0x176150:0x1699c0))return 1;
    if(api->CreateSafeHook(target,before_status,after_status,NULL)!=DK2ML_OK||api->EnableHook(target)!=DK2ML_OK)return 1;
    void*gui;if(!resolve(&gui,"GameRenderer::RenderGUIItems",names_113?0x161470:0x156b70)||api->CreateSafeHook(gui,draw_label,finish_draw_labels,NULL)!=DK2ML_OK||api->EnableHook(gui)!=DK2ML_OK)return 1;
    if(names_113){
        imgui_context_slot=(uintptr_t)api->ResolveSymbol("GImGui");
        if(!imgui_context_slot||
            api->GetFieldOffset("ImGuiContext","Font")!=15248||
            api->GetFieldOffset("ImGuiContext","Viewports")!=16424||
            api->GetFieldOffset("ImGuiViewport","Pos")!=4||api->GetFieldOffset("ImGuiViewport","Size")!=12||
            !resolve((void**)&viewport_draw_list,"GetViewportDrawList",0x2fc350)||
            !resolve((void**)&measure_overlay_text,"ImFont::CalcTextSizeA",0x31f1a0)||
            !resolve((void**)&render_overlay_char,"ImFont::RenderChar",0x31f550)||
            api->Subscribe(DK2ML_EVENT_FRAME,draw_name_overlay,NULL)!=DK2ML_OK)return 1;
    }
    api->Log("Ready 1.2.0: selected/body-or-path-hover/all friendly trooper names, native camera-projected labels. Author Rake5000.");return 0;
}

