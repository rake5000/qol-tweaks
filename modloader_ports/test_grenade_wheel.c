#include <assert.h>
#include <stdio.h>
#include "cursor_hotkeys.c"

static unsigned char game[0xff0eb0],client[0x105820],gui[16];
static unsigned char wheels[2][864],buttons[2][288],switches[2][152];
static unsigned char waypoint[0x228],human[1200],vtable[16],actions[2][72],item[48];
static void *action_lists[2][1];
static int page,action_clicks,switch_clicks,hidden,wrong_waypoint;

static void put_pointer(unsigned char *dst,size_t offset,const void *value) {
    memcpy(dst+offset,&value,sizeof value);
}
static void put_number(unsigned char *dst,size_t offset,int32_t value) {
    memcpy(dst+offset,&value,sizeof value);
}
static void *mock_find(void *root,const char *name) {
    if(root!=wheels[page])return NULL;
    if(strcmp(name,page?"#switch_to_target_options":"#switch_to_path_options")==0)return switches[page];
    return NULL;
}
static int mock_children(void *root,void **out,int capacity) {
    if(root!=wheels[page]||capacity<1)return 0;
    out[0]=buttons[page];
    return 1;
}
static int mock_shown(void *node) {
    return node==wheels[page]||node==switches[page]||node==buttons[page];
}
static DK2ML_Status mock_click(void *button) {
    if(button==switches[page]) {
        ++switch_clicks;page=1-page;
        put_pointer(gui,8,wheels[page]);
        if(wrong_waypoint)put_number(wheels[page],856,3);
        return DK2ML_OK;
    }
    assert(button==buttons[page]);
    ++action_clicks;
    return DK2ML_OK;
}
static void mock_show(void *node,int show) {
    assert(node==wheels[page]&&!show);
    ++hidden;
}
static void mock_log(const char *format,...) {(void)format;}
static void begin_case(int start,int first,int second,int stale) {
    page=start;switch_clicks=0;action_clicks=0;hidden=0;wrong_waypoint=stale;
    buttons[0][144]=(unsigned char)first;buttons[1][144]=(unsigned char)second;
    put_pointer(gui,8,wheels[page]);
    put_number(wheels[0],856,1);put_number(wheels[1],856,1);
}
int main(void) {
    static const DK2ML_API loader={.GuiChildren=mock_children,.GuiFind=mock_find,.GuiIsShown=mock_shown,
        .GuiClick=mock_click,.GuiShow=mock_show,.Log=mock_log};
    api=&loader;image=(uintptr_t)game;actionwp_get_type=1;
    put_pointer(game,0xfe71a8,gui);
    put_pointer(game,0xff0ea8,client);
    put_pointer(client,0x2e8+8,waypoint);put_pointer(client,0x2e8+16,human);
    put_pointer(waypoint,0,vtable);
    put_pointer(vtable,8,(void*)actionwp_get_type);
    put_number(waypoint,0x220,2);
    put_number(human,0x10,2);put_number(human,0x32c,1);human[0x333]=1;
    put_pointer(human,1072+88,item);put_number(human,1072+88+12,1);
    put_number(item,32,3);put_pointer(item,40,"Flashbang");
    for(int i=0;i<2;++i){
        switches[i][144]=1;
        action_lists[i][0]=actions[i];
        put_pointer(buttons[i],272,action_lists[i]);put_number(buttons[i],280,1);
        put_number(actions[i],8,6);put_number(actions[i],40,73);
        put_number(actions[i],64,23);put_number(actions[i],68,1);
    }
    begin_case(0,1,1,0);dispatch_grenade(BIND_FLASH);
    assert(action_clicks==1&&switch_clicks==0&&!hidden);
    begin_case(0,0,1,0);dispatch_grenade(BIND_FLASH);
    assert(action_clicks==1&&switch_clicks==1&&page==1&&!hidden);
    begin_case(1,1,0,0);put_pointer(item,40,"FOG_Frag_1x");dispatch_grenade(BIND_FRAG);
    assert(action_clicks==1&&switch_clicks==1&&page==0&&!hidden);
    begin_case(0,0,0,0);dispatch_grenade(BIND_FRAG);
    assert(!action_clicks&&switch_clicks==1&&hidden==1);
    begin_case(0,0,1,1);dispatch_grenade(BIND_FRAG);
    assert(!action_clicks&&switch_clicks==1&&!hidden);
    for(int i=0;i<2;++i)put_number(actions[i],64,17);
    begin_case(0,1,1,0);dispatch_suppression();
    assert(action_clicks==1&&switch_clicks==0&&!hidden);
    begin_case(0,0,1,0);dispatch_suppression();
    assert(action_clicks==1&&switch_clicks==1&&page==1&&!hidden);
    begin_case(1,1,0,0);dispatch_suppression();
    assert(action_clicks==1&&switch_clicks==1&&page==0&&!hidden);
    begin_case(0,0,0,0);dispatch_suppression();
    assert(!action_clicks&&switch_clicks==1&&hidden==1);
    begin_case(0,0,1,1);dispatch_suppression();
    assert(!action_clicks&&switch_clicks==1&&!hidden);
    puts("PASS: native grenade and suppressive-fire wheel priority, disabled buttons, both switch directions, no-action and stale-waypoint cases.");
    return 0;
}
