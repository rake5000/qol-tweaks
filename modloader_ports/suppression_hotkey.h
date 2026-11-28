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
      read_at(action+40,&event,4)&&event==wheel_clicked_event&&read_at(action+64,&type,4)&&type==17)return node;
  }
 void *children[128];int n=api->GuiChildren(node,children,128);if(n<0||n>128)return NULL;
 for(int i=0;i<n;++i){void *found=find_suppression_button(children[i],depth+1,visited);if(found)return found;}
 return NULL;
}
static void dispatch_suppression(void){
 ActionWheelContext context;
 if(!read_action_wheel(&context))return;
 unsigned visited=0;
 void *button=find_suppression_button((void*)context.wheel,0,&visited);
 if(!button&&switch_action_wheel(context.wheel)){
  if(!refresh_action_wheel(&context))return;
  visited=0;
  button=find_suppression_button((void*)context.wheel,0,&visited);
 }
 if(!button){
  api->Log("Ctrl+S: suppressive fire unavailable; no order placed.");
  api->GuiShow((void*)context.wheel,0);
  return;
 }
 DK2ML_Status result=api->GuiClick(button);
 api->Log("Ctrl+S: native suppressive-fire click owner=%u waypoint=%u status=%d; native targeting retained.",context.human_id,context.waypoint_id,result);
}
#endif
