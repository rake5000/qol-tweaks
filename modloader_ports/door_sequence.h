#ifndef R5K_DOOR_SEQUENCE_H
#define R5K_DOOR_SEQUENCE_H
#include "door_geometry.h"
static uintptr_t door_type_fn,path_add_fn,path_end_fn,path_notify_fn,door_gocode_fn,door_selected_fn,door_potential_fn,door_open_wheel_fn;
static uintptr_t door_client_slot,door_gui_slot,door_input_slot,door_potential_slot;
static unsigned door_entity_limit;
typedef struct {bool pending;uintptr_t client,map;uint32_t owner,door,buddy;ULONGLONG until;DoorGeometry geometry;} DoorRequest;
static DoorRequest door_request;
// A one-use continuation: close old wheel, clear the breacher, then reopen at
// the original door cursor. Never keep a wheel pointer across plan clearing.
static struct {uintptr_t client,map;uint32_t owner,door;ULONGLONG until;} door_clean;
static bool door_geometry(uintptr_t door,const float approach[2],DoorGeometry *g) {
 float origin[3],left[3],forward[3],lo[3],hi[3],scale;
 if(!read_at(door+108,origin,12)||!read_at(door+120,left,12)||!read_at(door+144,forward,12)||
    !read_at(door+184,lo,12)||!read_at(door+196,hi,12)||!read_at(door+156,&scale,4)||!isfinite(scale)||scale<=0)return false;
 float x=hi[0]-lo[0],z=hi[2]-lo[2];
 if(x<=0||z<=0)return false;
 float center[2]={origin[0]+scale*(left[0]*(hi[0]+lo[0])*.5f+forward[0]*(hi[2]+lo[2])*.5f),
 origin[2]+scale*(left[2]*(hi[0]+lo[0])*.5f+forward[2]*(hi[2]+lo[2])*.5f)};
 float along[2]={x>=z?left[0]:forward[0],x>=z?left[2]:forward[2]};
 return door_goal(g,center,along,fmaxf(x,z)*scale*.5f,approach,door_sidestep_distance);
}
static void consider_door_buddy(uintptr_t human,uintptr_t breacher,uintptr_t client,
 const float origin[3],float *best_distance,uint32_t *best) {
 uint32_t id=0;float position[3],health=0;int mode=-1;
 if(human==breacher||!friendly_human(human,client,&id)||!read_at(human+40,&health,4)||!isfinite(health)||health<=0||
    !read_at(human+3460,&mode,4)||mode!=0||!read_at(human+108,position,12))return;
 float distance=door_buddy_distance(origin,position);
 if(distance>=0&&(distance<*best_distance||(distance==*best_distance&&(!*best||id<*best)))){
  *best=id;*best_distance=distance;
 }
}
static uint32_t nearest_door_buddy(uintptr_t breacher,uintptr_t client) {
 uintptr_t head=0,node=0,human=0;float origin[3],best_distance=2.25f;uint32_t best=0;
 if(!read_at(breacher+108,origin,12))return 0;
#ifdef R5K_QOL_UI
 if(basic_113){
  uintptr_t entities[waypoint_pool_limit_113];
  if(!read_at(client+entity_pool_offset_113,entities,sizeof entities))return 0;
  for(unsigned i=1;i<waypoint_pool_limit_113;i++){
   uintptr_t table=0,type=0;
   if(entities[i]&&read_at(entities[i],&table,8)&&read_at(table+8,&type,8)&&type==human_type_113)
    consider_door_buddy(entities[i],breacher,client,origin,&best_distance,&best);
  }
  return best;
 }
#endif
 if(!read_at(image+0x7cb820,&head,8)||!head||!read_at(head+24,&human,8))return 0;
 for(unsigned scanned=0;human&&scanned<4096;++scanned) {
  consider_door_buddy(human,breacher,client,origin,&best_distance,&best);
  if(!read_at(human+0x1d8,&node,8)||!node||node==head||!read_at(node+24,&human,8))break;
 }
 return best;
}
static bool find_open_button(void *node,unsigned depth,unsigned *visited,unsigned char params[24],void **button) {
 if(!node||depth>12||++*visited>512||!api->GuiIsShown(node))return false;
 uintptr_t click=(uintptr_t)node+264,actions=0;int count=0;unsigned char accepts=0;
 if(read_at((uintptr_t)node+144,&accepts,1)&&accepts&&read_at(click+16,&count,4)&&count>0&&count<=32&&read_at(click+8,&actions,8))
  for(int i=0;i<count;++i) {
   uintptr_t a=0;uint32_t kind=0,event=0,type=0;
   if(read_at(actions+i*8,&a,8)&&a&&read_at(a+8,&kind,4)&&kind==6&&read_at(a+40,&event,4)&&event==wheel_clicked_event&&
      read_at(a+64,&type,4)&&type==21&&read_at(a+48,params,24)){*button=node;return true;}
  }
 void *children[128];int n=api->GuiChildren(node,children,128);if(n<0||n>128)return false;
 for(int i=0;i<n;++i)if(find_open_button(children[i],depth+1,visited,params,button))return true;
 return false;
}
static bool door_wheel_at_body(uintptr_t client,uintptr_t human) {
 (void)client;
 uintptr_t input=0,gui=0,wheel=0,potential=0;float origin[3];uint32_t owner=0;
 if(!read_at(door_input_slot,&input,8)||!input||!read_at(door_gui_slot,&gui,8)||!gui||
    !read_at(gui+8,&wheel,8)||!wheel||!read_at(human+108,origin,12))return false;
 api->GuiShow((void*)wheel,0);
 typedef bool(*potential_fn)(void*,void*,float*,int);
 if(!((potential_fn)door_potential_fn)((void*)input,(void*)human,origin,0)||
    !read_at(door_potential_slot,&potential,8)||!potential||!read_at(potential+0x220,&owner,4))return false;
 uint32_t id=0;if(!read_at(human+16,&id,4)||owner!=id)return false;
 // Button2_Click clears the potential default action before opening its wheel.
 uint32_t ignore=0;SIZE_T written=0;
 if(!WriteProcessMemory(GetCurrentProcess(),(void*)(potential+0x1f0),&ignore,4,&written)||written!=4)return false;
 typedef void(*open_fn)(void*,void*,int);
 ((open_fn)door_open_wheel_fn)((void*)gui,(void*)potential,0);
 return api->GuiIsShown((void*)wheel)!=0;
}
// The existing native wheel supplies the hovered path point and its owner.
// Body hits retain cursor-independent body anchoring. Path hits retain the
// engine's own snapped position/index instead of converting raw screen coordinates.
static bool door_cursor_anchor(uintptr_t human,uintptr_t wp,bool body,float anchor[3]) {
 if(body)return read_at(human+108,anchor,12)&&isfinite(anchor[0])&&isfinite(anchor[2]);
 int index=-1,count=0;
 return read_at(human+0xd90+56,&count,4)&&count>0&&count<=8192&&
  read_at(wp+0x238,&index,4)&&index>=0&&index<=count&&
  read_at(wp+0x22c,anchor,12)&&isfinite(anchor[0])&&isfinite(anchor[2]);
}
static void start_position_only(uintptr_t human,uintptr_t client,uintptr_t map,uintptr_t door,uintptr_t wheel,const float anchor[3]);
static void dispatch_door(unsigned binding) {
 uintptr_t wheel=0,client=0,map=0,wp=0,door=0,human=0,target=0,table=0,fn=0;
 uint32_t wp_id=0,door_id=0,id=0;unsigned char opened=1,locked=1,params[24];unsigned visited=0;void *button=NULL;
 float door_anchor[3]={0};
 door_request.pending=false;
 if(binding==BIND_DOOR) {
  ActionWheelContext context;
  if(!read_action_wheel(&context))return;
  client=context.client;wheel=context.wheel;wp=context.waypoint;human=context.human;
  bool body=cursor_hits_body(client,human);
  if(!door_cursor_anchor(human,wp,body,door_anchor)|| (body&&!door_wheel_at_body(client,human))) {
   api->GuiShow((void*)wheel,0);api->Log("Door 1.3.6: invalid cursor body/path context; existing orders preserved.");return;
  }
  api->Log("Door 1.3.6: availability from %s at %.3f,%.3f for trooper %u.",body?"body":"native path point",door_anchor[0],door_anchor[2],context.human_id);
 }
 ActionWheelContext context;
 if(!read_action_wheel(&context)){
  if(wheel&&api->GuiIsShown((void*)wheel))api->GuiShow((void*)wheel,0);
  return;
 }
 wheel=context.wheel;client=context.client;wp=context.waypoint;wp_id=context.waypoint_id;
 human=context.human;id=context.human_id;
 if(!read_at(wheel+848,&target,8)||!read_at(target,&door_id,4)||!door_id||door_id>=door_entity_limit||
    !read_at(client+0x28,&map,8)||!map||!read_at(client+0x2e8+door_id*8,&door,8)||!door||
    !read_at(door,&table,8)||!read_at(table+8,&fn,8)||fn!=door_type_fn||
    !read_at(door+496,&opened,1)||opened||!read_at(door+497,&locked,1)||locked) {
  api->Log("Door hotkey: no available normal-open action; no kick substituted.");
  if(wheel&&api->GuiIsShown((void*)wheel))api->GuiShow((void*)wheel,0);return;
 }
 if(binding==BIND_DOOR) {
  if(!find_open_button((void*)wheel,0,&visited,params,&button)){api->GuiShow((void*)wheel,0);return;}
  start_position_only(human,client,map,door,wheel,door_anchor);return;
 }
 if(binding==BIND_DOOR) {
  if(door_clean.owner&&GetTickCount64()<=door_clean.until&&
     (door_clean.client!=client||door_clean.map!=map||door_clean.owner!=id||door_clean.door!=door_id)) {
   memset(&door_clean,0,sizeof(door_clean));api->GuiShow((void*)wheel,0);
   api->Log("Door hotkey: context changed after clearing; rebuild cancelled.");return;
  }
  bool continuation=door_clean.client==client&&door_clean.map==map&&door_clean.owner==id&&
   door_clean.door==door_id&&GetTickCount64()<=door_clean.until;
  if(!continuation) {
   POINT point=dispatch_point;
   if(!pending||dispatch_key!=BIND_DOOR) {
    if(!GetCursorPos(&point)||!ScreenToClient(hooked_window,&point)) {api->GuiShow((void*)wheel,0);return;}
   }
   // Hide first: the old wheel's OnClose still refers to its action waypoint.
   // Clearing that action before closing would leave stale menu context.
   api->GuiShow((void*)wheel,0);
   clear_hovered_plan(id,client);
   door_clean.client=client;door_clean.map=map;door_clean.owner=id;door_clean.door=door_id;
   door_clean.until=GetTickCount64()+2000;
   AcquireSRWLockExclusive(&queue_lock);
   queued_point=point;queued_key=BIND_DOOR;pending=0;
   ReleaseSRWLockExclusive(&queue_lock);
   api->Log("Door hotkey: cleared breacher %u; rebuilding the door wheel before issuing O.",id);
   return;
  }
  memset(&door_clean,0,sizeof(door_clean));
 }
 if(!find_open_button((void*)wheel,0,&visited,params,&button)) {
  api->Log("Door hotkey: no available normal-open action after clear; no kick substituted.");
  if(api->GuiIsShown((void*)wheel))api->GuiShow((void*)wheel,0);return;
 }
 float approach[2];int points=0;
 bool have=read_at(human+0xd90+56,&points,4)&&points>0&&read_at(human+0xd90+120,approach,8);
 if(!have){float origin[3];if(!read_at(human+108,origin,12))return;approach[0]=origin[0];approach[1]=origin[2];}
 DoorGeometry geometry;bool full=binding==BIND_DOOR&&door_geometry(door,approach,&geometry);
 if(full) {
  float body[3],facing[3];SIZE_T written=0;
  if(!read_at(human+108,body,12))return;
  float dx=geometry.center[0]-body[0],dz=geometry.center[1]-body[2],length=hypotf(dx,dz);
  if(!isfinite(length)||length<.001f)return;
  facing[0]=dx/length;facing[1]=0;facing[2]=dz/length;
  if(!WriteProcessMemory(GetCurrentProcess(),(void*)(wp+0x22c),body,12,&written)||written!=12||
     !WriteProcessMemory(GetCurrentProcess(),(void*)(wp+0x1f8),facing,12,&written)||written!=12)return;
  api->Log("Door 1.1.6: body-based opening at %.3f,%.3f facing %.3f,%.3f.",body[0],body[2],facing[0],facing[2]);
 }
 uint32_t before=0,after=0;read_at(wp+0x1f0,&before,4);
 DK2ML_Status result=api->GuiClick(button);read_at(wp+0x1f0,&after,4);
 api->Log("Door hotkey: native button click status=%d door=%u waypoint=%u action before=%u after=%u.",result,door_id,wp_id,before,after);
 if(result!=DK2ML_OK)return;
 if(full) {
  door_request=(DoorRequest){.pending=true,.client=client,.map=map,.owner=id,.door=door_id,.buddy=nearest_door_buddy(human,client),.until=GetTickCount64()+3000,.geometry=geometry};
  api->Log("Door hotkey: normal open queued; waiting to append sidestep and pie for trooper %u.",id);
 }else api->Log("Door hotkey: normal open queued%s.",binding==BIND_DOOR?"; doorway geometry unavailable, no extra movement":"");
}
static bool clone_door_path(uintptr_t source,unsigned char copy[184],void *arrays[3]) {
 const unsigned offsets[3]={40,64,96},strides[3]={12,12,8};
 if(!read_at(source,copy,184))return false;
 for(unsigned i=0;i<3;++i) {
  unsigned offset=offsets[i];int count=0;uintptr_t data=0;
  memcpy(&count,copy+offset+16,4);memcpy(&data,copy+offset+8,8);
  if(count<0||count>4096||(count&&!data))return false;
  arrays[i]=HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,8192*strides[i]);if(!arrays[i])return false;
  if(count&&!read_at(data,arrays[i],(SIZE_T)count*strides[i]))return false;
  int capacity=8192;uintptr_t ptr=(uintptr_t)arrays[i];
  memcpy(copy+offset,&capacity,4);memcpy(copy+offset+8,&ptr,8);copy[offset+20]=1;
 }
 memset(copy+144,0,16); // Never share live render-geometry pointers with this temporary path.
 int max_points=8190;memcpy(copy+88,&max_points,4);
 return true;
}
#include "door_position_only.h"
typedef struct {
 _Alignas(16) unsigned char path[184],look[252];
 void *arrays[3];int first,total,action_count,opening_index;bool ready;
} DoorPlan;
static void free_door_plan(DoorPlan *plan) {
 for(unsigned i=0;i<3;++i)if(plan->arrays[i])HeapFree(GetProcessHeap(),0,plan->arrays[i]);
 memset(plan,0,sizeof(*plan));
}
static bool prepare_door_pie(uintptr_t human,int open_index,const float open_origin[3],
 const DoorGeometry *geometry,const float look_target[3],uint32_t owner,bool buddy,const float stage[3],DoorPlan *plan) {
 _Alignas(16) unsigned char segment[184],actions[33*84];
 unsigned char *path=plan->path;void **arrays=plan->arrays;
 void *route_arrays[3]={0};bool sent=false;const char *reason="path clone failed";
 int old_count=0,count=0,modified=0,added=0,generated=0;
 memset(actions,0,sizeof(actions));
 if(!clone_door_path(human+0xd90,path,arrays)||!clone_door_path(human+0xd90,segment,route_arrays))goto done;
 memcpy(&old_count,path+56,4);
 if(buddy){old_count=0;open_index=0;int zero=0;memcpy(path+20,&zero,4);memcpy(path+56,&zero,4);memcpy(path+80,&zero,4);}
 reason="open order is not at path tail";
 if(!door_tail_index(old_count,open_index))goto done;
 float *old_points=(float*)arrays[0],start[2];
 // A stationary trooper can have an opening order at index zero and no
 // movement points. Seed that index instead of rejecting every such plan.
 if(open_index==old_count) {
  reason="invalid opening anchor";
  if(!isfinite(open_origin[0])||!isfinite(open_origin[2]))goto done;
  memcpy(old_points+old_count*3,open_origin,12);
  memset((unsigned char*)arrays[1]+old_count*12,0,12);
  ++old_count;memcpy(path+56,&old_count,4);memcpy(path+80,&old_count,4);
 }
 if(!buddy) {
  // Anchor both existing and newly seeded opening points at the actual body.
  memcpy(old_points+open_index*3,open_origin,12);
 }
 start[0]=old_points[(old_count-1)*3];start[1]=old_points[(old_count-1)*3+2];
 reason="opening point is too far from door";
 if(!buddy&&hypotf(start[0]-geometry->center[0],start[1]-geometry->center[1])>
    geometry->half+1.0f)goto done;
 // Build each leg on an independent active segment so native resampling
 // cannot rewrite earlier points or the door-open order.
 int total=old_count,stage_index=0;
 float approach_point[3];
 if(!buddy&&!door_approach_point(geometry,stage,approach_point)){reason="no room for 0.1 m approach";goto done;}
 for(unsigned leg=0;leg<(buddy?2u:3u);++leg) {
  int tail=total-1;start[0]=old_points[tail*3];start[1]=old_points[tail*3+2];
  DoorGeometry leg_geometry=*geometry;
  if(leg==0){leg_geometry.goal[0]=stage[0];leg_geometry.goal[1]=stage[2];}
  else if(!buddy&&leg==1){leg_geometry.goal[0]=approach_point[0];leg_geometry.goal[1]=approach_point[2];}
  memcpy(route_arrays[0],old_points+tail*3,12);
  memset(route_arrays[1],0,12);memcpy(route_arrays[2],start,8);
  int one=1,zero=0;
  memcpy(segment+20,&zero,4);memcpy(segment+56,&one,4);
  memcpy(segment+80,&one,4);memcpy(segment+112,&one,4);memcpy(segment+120,start,8);
  generated=0;
  typedef void(*add_fn)(void*,void*,float,float,int*,int*,void*,int,int*);
  ((add_fn)path_add_fn)(segment,(void*)human,leg_geometry.goal[0],leg_geometry.goal[1],&modified,&added,actions,32,&generated);
  typedef void(*end_fn)(void*);((end_fn)path_end_fn)(segment);
  uintptr_t points=0;memcpy(&count,segment+56,4);memcpy(&points,segment+48,8);
  reason="native route produced no usable new points";
  if(count<2||count>4096||total+count-1>8192||!points)goto done;
  reason="native route requires an obstacle action";if(generated!=0)goto done;
  reason="native route crosses door face, detours, or misses goal";
  if(!door_route(&leg_geometry,(float*)points,(unsigned)count,start))goto done;
  memcpy(old_points+total*3,(float*)points+3,(SIZE_T)(count-1)*12);
  uintptr_t route_params=0;memcpy(&route_params,segment+72,8);
  reason="route parameters unavailable";if(!route_params)goto done;
  memcpy((unsigned char*)arrays[1]+total*12,(void*)(route_params+12),(SIZE_T)(count-1)*12);
  total+=count-1;
  if(leg==0){stage_index=total-1;plan->opening_index=stage_index;}
  if(!buddy&&leg==1)stage_index=total-1;
 }
 memcpy(path+56,&total,4);memcpy(path+80,&total,4);memcpy(path+120,segment+120,8);
 int index=buddy?0:stage_index;
 unsigned char *look=plan->look;uint32_t type=6;
 memset(look,0,252);memcpy(look,&type,4);memcpy(look+8,look_target,12);memcpy(look+48,&owner,4);
 memcpy(look+60,old_points+index*3,12);memcpy(look+72,&index,4);int16_t wait=500;memcpy(look+80,&wait,2);
 plan->action_count=buddy?3:2;
 if(!buddy){unsigned char *initial=look+84;memcpy(initial,look,84);int first_index=open_index;memcpy(initial+60,old_points+first_index*3,12);memcpy(initial+72,&first_index,4);}
 if(buddy) {
  // Aim while approaching; gate opening at the ready position, not before it.
  unsigned char *ready=look+84;uint32_t code=4;
  memcpy(ready,look,84);memcpy(ready+56,&code,4);
  memcpy(ready+60,old_points+stage_index*3,12);memcpy(ready+72,&stage_index,4);
  unsigned char *wait_clear=look+168;uint32_t wait_type=7,clear_code=5;int last=total-1;
  memcpy(wait_clear,&wait_type,4);memcpy(wait_clear+48,&owner,4);
  memcpy(wait_clear+56,&clear_code,4);memcpy(wait_clear+60,old_points+last*3,12);
  memcpy(wait_clear+72,&last,4);memcpy(wait_clear+80,&wait,2);
 }
 int original_count=0;read_at(human+0xd90+56,&original_count,4);
 plan->first=buddy?0:(original_count<open_index?original_count:open_index);
 plan->total=total;plan->ready=true;
 sent=true;
 done:
 if(!sent)api->Log("Door hotkey: sidestep rejected: %s (path=%d open=%d route=%d modified=%d added=%d obstacles=%d).",reason,old_count,open_index,count,modified,added,generated);
 for(unsigned i=0;i<3;++i)if(route_arrays[i])HeapFree(GetProcessHeap(),0,route_arrays[i]);
 if(!sent)free_door_plan(plan);
 return sent;
}
static void publish_door_plan(uintptr_t human,const DoorPlan *plan) {
 typedef void(*notify_fn)(void*,void*,int,int,int,void*,bool);
 ((notify_fn)path_notify_fn)((void*)human,(void*)plan->path,plan->first,plan->total-plan->first,plan->action_count,(void*)plan->look,false);
}
static bool prepare_optimal_door_plan(uintptr_t human,int index,const float body[3],uint32_t owner,DoorPlan *plan,float optimal[3],DoorGeometry *chosen) {
 const DoorGeometry *door=&door_request.geometry;DoorGeometry placement;
 if(!door_breach_goal(door,body,door_breach_distance,&placement))return false;
 float candidate[3]={placement.goal[0],body[1],placement.goal[1]};
 // Retreat remains controlled by its own existing distance setting.
 float approach[2]={candidate[0],candidate[2]};DoorGeometry retreat;
 if(!door_goal(&retreat,door->center,door->along,door->half,approach,door_sidestep_distance))return false;
 float center[3]={door->center[0],0,door->center[1]};
 if(!prepare_door_pie(human,index,body,&retreat,center,owner,false,candidate,plan))return false;
 memcpy(optimal,candidate,12);*chosen=retreat;
 api->Log("Door 1.2.3: geometric staging %.2f m beyond jamb at %.3f,%.3f.",door_breach_distance,candidate[0],candidate[2]);return true;
}
static bool append_door_pair(uintptr_t human,uintptr_t wp,int index,const float origin[3],uintptr_t client) {
 DoorPlan breacher={0},buddy={0};
 float body_anchor[3];
 if(!read_at(human+108,body_anchor,12))return false;
 float optimal[3];DoorGeometry optimized_geometry;
 if(!prepare_optimal_door_plan(human,index,body_anchor,door_request.owner,&breacher,optimal,&optimized_geometry))return false;
 door_request.geometry=optimized_geometry;
 // Publish the opening action and its matching path anchor together.
 memmove(breacher.look+84,breacher.look,168);
 if(!read_at(wp+0x1f0,breacher.look,84)){free_door_plan(&breacher);return false;}
 float aim_x=door_request.geometry.center[0]-optimal[0],aim_z=door_request.geometry.center[1]-optimal[2],aim_length=hypotf(aim_x,aim_z);
 if(aim_length<.001f){free_door_plan(&breacher);return false;}
 float opening_facing[3]={aim_x/aim_length,0,aim_z/aim_length};
 memcpy(breacher.look+8,opening_facing,12);
 memcpy(breacher.look+60,optimal,12);memcpy(breacher.look+72,&breacher.opening_index,4);breacher.action_count=3;
 // Move the existing native opening record to the validated staging index.
 SIZE_T moved=0;WriteProcessMemory(GetCurrentProcess(),(void*)(wp+0x1f8),opening_facing,12,&moved);
 WriteProcessMemory(GetCurrentProcess(),(void*)(wp+0x22c),optimal,12,&moved);
 WriteProcessMemory(GetCurrentProcess(),(void*)(wp+0x238),&breacher.opening_index,4,&moved);
 api->Log("Door 1.1.5: opening anchor corrected by %.3f m to current body position.",hypotf(origin[0]-body_anchor[0],origin[2]-body_anchor[2]));
 uintptr_t partner=0;uint32_t id=0;float position[3],breacher_position[3],health=0;
 int mode=-1;DoorGeometry geometry;float target[3],stage[3];
 bool paired=door_request.buddy&&read_at(client+0x2e8+door_request.buddy*8,&partner,8)&&
  friendly_human(partner,client,&id)&&id==door_request.buddy&&read_at(partner+40,&health,4)&&isfinite(health)&&health>0&&
  read_at(partner+3460,&mode,4)&&mode==0&&read_at(partner+108,position,12)&&read_at(human+108,breacher_position,12)&&
  door_buddy_distance(breacher_position,position)>=0&&door_buddy_goal(&door_request.geometry,position,&geometry,target)&&
  door_buddy_stage(&door_request.geometry,optimal,stage)&&door_buddy_goal(&door_request.geometry,stage,&geometry,target)&&
  prepare_door_pie(partner,0,position,&geometry,target,id,true,stage,&buddy);
 if(paired) {
  // Both complete routes exist before replacing the buddy's old plan.
  // Native go-code mutation preserves the DOOR_OPEN action and its target.
  typedef void(*code_fn)(void*,int);
  clear_hovered_plan(id,client);
  SIZE_T anchor_written=0;
  WriteProcessMemory(GetCurrentProcess(),(void*)(wp+0x22c),optimal,12,&anchor_written);
  uint32_t sync_code=4;memcpy(breacher.look+56,&sync_code,4);
  ((code_fn)door_gocode_fn)((void*)wp,4);
  publish_door_plan(partner,&buddy);
  api->Log("Door pair: breacher=%u buddy=%u, D at open/ready position; buddy ready %.2f,%.2f goal %.2f,%.2f apex %.2f,%.2f.",
   door_request.owner,id,stage[0],stage[2],geometry.goal[0],geometry.goal[1],target[0],target[2]);
 }else {
  uint32_t code=0;memcpy(&code,breacher.look+56,4);
  typedef void(*code_fn)(void*,int);((code_fn)door_gocode_fn)((void*)wp,(int)code);
  api->Log("Door pair: no usable nearest buddy within 1.5 m; retaining solo sequence.");
 }
 publish_door_plan(human,&breacher);
 free_door_plan(&buddy);free_door_plan(&breacher);
 return true;
}
static void update_door_sequence(uintptr_t client,uintptr_t map) {
 if(!door_request.pending)return;
 if(client!=door_request.client||map!=door_request.map||GetTickCount64()>door_request.until) {
  door_request.pending=false;api->Log("Door hotkey: sequence wait expired; kept normal open only.");return;
 }
 uintptr_t head=0,wp=0,node=0,human=0;uint32_t id=0;int mode=0;
 if(!read_at(client+0x2e8+door_request.owner*8,&human,8)||!friendly_human(human,client,&id)||
    !read_at(human+3460,&mode,4)||mode!=0||!read_at(image+0x7cb540,&head,8)||!head||!read_at(head+24,&wp,8))return;
 for(unsigned scanned=0;wp&&scanned<4096;++scanned) {
  uint32_t type=0,target=0,owner=0;int index=-1;
  if(!read_at(wp+0x1f0,&type,4)||!read_at(wp+0x1f4,&target,4)||!read_at(wp+0x220,&owner,4))break;
  if(type==21&&target==door_request.door&&owner==door_request.owner&&read_at(wp+0x238,&index,4)) {
   door_request.pending=false;
   float origin[3];
   if(!read_at(wp+0x22c,origin,12)||!append_door_pair(human,wp,index,origin,client))api->Log("Door hotkey: kept normal open only.");
   return;
  }
  if(!read_at(wp+0x1d8,&node,8)||!node||node==head||!read_at(node+24,&wp,8))break;
 }
}
static bool init_door_sequence(void) {
 bool current=false;
#ifdef R5K_QOL_UI
 current=basic_113;
#endif
 door_client_slot=image+(current?0x1038c40:0xff0ea8);
 door_gui_slot=image+(current?0x100ce18:0xfe71a8);
 door_input_slot=image+(current?0x100e6e0:0xfe8a68);
 door_potential_slot=image+(current?0x100bf38:0xfe6f38);
 door_entity_limit=current?12000:100001;
 door_selected_fn=(uintptr_t)api->ResolveSymbol("GameInput::GetSelectedHuman");
 door_potential_fn=(uintptr_t)api->ResolveSymbol("GameInput::UpdatePotentialActionWaypoint");
 door_open_wheel_fn=(uintptr_t)api->ResolveSymbol("GameGUI::OpenWaypointMenu");
 door_gocode_fn=(uintptr_t)api->ResolveSymbol("ActionWP_Client::OnChangeGoCode");
 door_type_fn=(uintptr_t)api->ResolveSymbol("Door_Client::GetType");
 path_add_fn=(uintptr_t)api->ResolveSymbol("Path::AddToPath");
 path_end_fn=(uintptr_t)api->ResolveSymbol("Path::AddToPath_End");
 path_notify_fn=(uintptr_t)api->ResolveSymbol("Human_Client::NotifyServerOfPathModification");
 return door_selected_fn==image+(current?0x135dc0:0x137090)&&door_potential_fn==image+(current?0x1302b0:0x131d40)&&
 door_open_wheel_fn==image+(current?0xfdaf0:0xfd3f0)&&door_gocode_fn==image+(current?0x9ba00:0x9c9d0)&&
 (current?door_type_fn==image+0xa3000:door_type_fn!=0)&&path_add_fn==image+(current?0x199f70:0x198fa0)&&
 path_end_fn==image+(current?0x199730:0x198800)&&path_notify_fn==image+(current?0xb7920:0xb9a50)&&
 api->GetTypeSize("Path")==184&&api->GetTypeSize("sActionWP")==84&&api->GetTypeSize("Waypoints::sPointParams")==12;
}
#endif
