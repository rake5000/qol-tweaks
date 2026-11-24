#ifndef R5K_SUPPRESSION_HOTKEY_H
#define R5K_SUPPRESSION_HOTKEY_H
// Native action_waypoints.xml SUPPRESSION; same ActionWP type17 used by the
// existing moving-suppression module. Use the wheel's actual button and full
// click sequence, leaving native targeting and wheel cleanup to its OnClick.
static void *find_suppression_button(void *node,unsigned depth,unsigned *visited){
 if(!node||depth>12||++*visited>512||!api->GuiIsShown(node))return NULL;
 uintptr_t click=(uintptr_t)node+264,actions=0;int count=0;unsigned char accepts=0;
 if(read_at((uintptr_t)node+144,&accepts,1)&&accepts&&read_at(click+16,&count,4)&&count>0&&count<=32&&read_at(click+8,&actions,8))
  for(int i=0;i<count;++i){
   uintptr_t action=0;uint32_t kind=0,event=0,type=0;
   if(read_at(actions+i*8,&action,8)&&action&&read_at(action+8,&kind,4)&&kind==6&&
      read_at(action+40,&event,4)&&event==73&&read_at(action+64,&type,4)&&type==17)return node;
  }
 void *children[128];int n=api->GuiChildren(node,children,128);if(n<0||n>128)return NULL;
 for(int i=0;i<n;++i){void *found=find_suppression_button(children[i],depth+1,visited);if(found)return found;}
 return NULL;
}
static void dispatch_suppression(void){
 uintptr_t gui=0,wheel=0,client=0,wp=0,human=0,table=0,type=0;uint32_t wp_id=0,owner_id=0,id=0;
 if(!read_at(image+0xfe71a8,&gui,8)||!read_at(gui+8,&wheel,8)||
    !read_at(wheel+856,&wp_id,4)||!wp_id||wp_id>100000||
    !read_at(image+0xff0ea8,&client,8)||!read_at(client+0x2e8+wp_id*8,&wp,8)||!wp||
    !read_at(wp,&table,8)||!read_at(table+8,&type,8)||type!=actionwp_get_type||
    !read_at(wp+0x220,&owner_id,4)||owner_id>100000||!read_at(client+0x2e8+owner_id*8,&human,8)||
    !friendly_human(human,client,&id))return;
 unsigned visited=0;void *button=find_suppression_button((void*)wheel,0,&visited);
 if(!button){api->Log("Ctrl+S: suppressive fire unavailable; no order placed.");api->GuiShow((void*)wheel,0);return;}
 DK2ML_Status result=api->GuiClick(button);
 api->Log("Ctrl+S: native suppressive-fire click owner=%u waypoint=%u status=%d; native targeting retained.",id,wp_id,result);
}
#endif
