#ifndef R5K_DOOR_POSITION_ONLY_H
#define R5K_DOOR_POSITION_ONLY_H
static uintptr_t position_query_fn,position_resize_fn,position_bounds_fn,position_coarse_reserve_fn;
typedef struct {
 bool active;uintptr_t client,map;uint32_t owner,door;ULONGLONG started;
 _Alignas(16) unsigned char path[184],look[5*84];void *arrays[3];int count,staging_index;
} PositionPlan;
static PositionPlan position_request,position_buddy;
static void position_free(PositionPlan *plan) {
 for(unsigned i=0;i<3;++i)if(plan->arrays[i])HeapFree(GetProcessHeap(),0,plan->arrays[i]);
 memset(plan,0,sizeof(*plan));
}
static void position_discard(void) {position_free(&position_request);position_free(&position_buddy);}
static void position_cancel_user_clear(uint32_t owner,bool all) {
 if(position_request.active&&(all||position_request.owner==owner||position_buddy.owner==owner))position_discard();
}
static bool position_build_plan(PositionPlan *plan,uintptr_t human,const DoorGeometry *g,const float body[3],uint32_t owner) {
 unsigned char *path=plan->path;void **arrays=plan->arrays;
 if(!clone_door_path(human+0xd90,path,arrays))return false;
 memcpy(arrays[0],body,12);memset(arrays[1],0,12);
 float start[2]={body[0],body[2]};memcpy(arrays[2],start,8);
 int zero=0,one=1;memcpy(path+20,&zero,4);memcpy(path+56,&one,4);memcpy(path+80,&one,4);memcpy(path+112,&one,4);memcpy(path+120,start,8);
 unsigned char generated_actions[32*84]={0};int modified=0,added=0,generated=0;
 typedef void(*add_fn)(void*,void*,float,float,int*,int*,void*,int,int*);
 ((add_fn)path_add_fn)(path,(void*)human,g->goal[0],g->goal[1],&modified,&added,generated_actions,32,&generated);
 typedef void(*end_fn)(void*);((end_fn)path_end_fn)(path);
 uintptr_t points=0;int count=0;memcpy(&points,path+48,8);memcpy(&count,path+56,4);
 if(!points||count<2||count>8192||generated||!door_route(g,(float*)points,(unsigned)count,start))return false;
 unsigned char *look=plan->look;uint32_t type=6;
 memset(look,0,84);memcpy(look,&type,4);float target[3]={g->center[0],0,g->center[1]};
 memcpy(look+8,target,12);memcpy(look+48,&owner,4);memcpy(look+60,body,12);
 int16_t wait=500;memcpy(look+80,&wait,2);plan->count=count;return true;
}
static bool position_build(uintptr_t human,const DoorGeometry *g,const float body[3],uint32_t owner) {
 return position_build_plan(&position_request,human,g,body,owner);
}
static bool position_build_buddy(uintptr_t human,uint32_t owner,const DoorGeometry *door,const float opening[3]) {
 float body[3],target[3];DoorGeometry stage;
 position_free(&position_buddy);
 if(!read_at(human+108,body,12)||!door_buddy_staging(door,opening,body,door_buddy_spacing,&stage,target)||
    !position_build_plan(&position_buddy,human,&stage,body,owner)) {position_free(&position_buddy);return false;}
 memcpy(position_buddy.look+8,target,12);position_buddy.staging_index=position_buddy.count-1;position_buddy.owner=owner;position_buddy.active=true;
 return true;
}
// Preserve the staging anchor while appending the independent center-doorway leg.
static bool position_append_buddy(uintptr_t human,const DoorGeometry *door) {
 PositionPlan *plan=&position_buddy;int stage=plan->staging_index;
 if(!plan->active||stage<1||stage!=plan->count-1||stage>=8191)return false;
 float *original=(float*)plan->arrays[0],start[2]={original[stage*3],original[stage*3+2]};
 DoorGeometry destination=*door;
 for(unsigned i=0;i<2;++i)destination.goal[i]=door->center[i]+door->normal[i]*door->face*1.0f;
 _Alignas(16) unsigned char segment[184];void *arrays[3]={0};bool valid=false;
 if(!clone_door_path((uintptr_t)plan->path,segment,arrays))goto done;
 int one=1,zero=0;memcpy(arrays[0],original+stage*3,12);memset(arrays[1],0,12);memcpy(arrays[2],start,8);
 memcpy(segment+20,&zero,4);memcpy(segment+56,&one,4);memcpy(segment+80,&one,4);memcpy(segment+112,&one,4);memcpy(segment+120,start,8);
 unsigned char actions[32*84]={0};int modified=0,added=0,generated=0;
 typedef void(*add_fn)(void*,void*,float,float,int*,int*,void*,int,int*);
 ((add_fn)path_add_fn)(segment,(void*)human,destination.goal[0],destination.goal[1],&modified,&added,actions,32,&generated);
 typedef void(*end_fn)(void*);((end_fn)path_end_fn)(segment);
 int count=0;memcpy(&count,segment+56,4);
 if(count<2||count>8192||stage+count>8192||generated||!door_route(&destination,(float*)arrays[0],(unsigned)count,start))goto done;
 memcpy(original+(stage+1)*3,(float*)arrays[0]+3,(SIZE_T)(count-1)*12);
 memcpy((float*)plan->arrays[1]+(stage+1)*3,(float*)arrays[1]+3,(SIZE_T)(count-1)*12);
 plan->count=stage+count;memcpy(plan->path+56,&plan->count,4);memcpy(plan->path+80,&plan->count,4);
 valid=true;
 done:
 for(unsigned i=0;i<3;++i)if(arrays[i])HeapFree(GetProcessHeap(),0,arrays[i]);
 return valid;
}
// One normal Open action, anchored to the final route point, never the cursor.
static bool position_prepare_open(uint32_t owner,uint32_t door,const float center[2],const float v1[3],const float v2[3]) {
 if(!owner||!door||position_request.count<2||!position_request.arrays[0])return false;
 int index=position_request.count-1;const float *end=(const float*)position_request.arrays[0]+index*3;
 float dx=center[0]-end[0],dz=center[1]-end[2],length=hypotf(dx,dz);
 if(!isfinite(length)||length<.001f)return false;
 unsigned char *action=position_request.look+84;memset(action,0,84);
 uint32_t type=21;float facing[3]={dx/length,0,dz/length};int16_t wait=500;
 memcpy(action,&type,4);memcpy(action+4,&door,4);memcpy(action+8,facing,12);
 memcpy(action+20,v1,12);memcpy(action+32,v2,12);memcpy(action+48,&owner,4);
 // GameInput::UpdatePotentialActionWaypoint sets marker origin.y to 0.11
 // (0x131eec), independently of the ground-level route point.
 float origin[3]={end[0],.11f,end[2]};
 memcpy(action+60,origin,12);memcpy(action+72,&index,4);memcpy(action+80,&wait,2);
 return true;
}
static bool position_append_retreat(uintptr_t human,const DoorGeometry *breach) {
 int opening=position_request.count-1;
 if(opening<1||opening>=8191||!isfinite(door_sidestep_distance)||door_sidestep_distance<.1f||door_sidestep_distance>3)return false;
 float *original=(float*)position_request.arrays[0],start[2]={original[opening*3],original[opening*3+2]};
 DoorGeometry retreat=*breach;
 for(unsigned i=0;i<2;++i)retreat.goal[i]=start[i]+breach->along[i]*breach->side*door_sidestep_distance;
 // Build a separate segment so native path simplification cannot move or erase Open's anchor.
 _Alignas(16) unsigned char segment[184];void *arrays[3]={0};bool valid=false;
 if(!clone_door_path((uintptr_t)position_request.path,segment,arrays))goto done;
 int one=1,zero=0;memcpy(arrays[0],original+opening*3,12);memset(arrays[1],0,12);memcpy(arrays[2],start,8);
 memcpy(segment+20,&zero,4);memcpy(segment+56,&one,4);memcpy(segment+80,&one,4);memcpy(segment+112,&one,4);memcpy(segment+120,start,8);
 unsigned char actions[32*84]={0};int modified=0,added=0,generated=0;
 typedef void(*add_fn)(void*,void*,float,float,int*,int*,void*,int,int*);
 ((add_fn)path_add_fn)(segment,(void*)human,retreat.goal[0],retreat.goal[1],&modified,&added,actions,32,&generated);
 typedef void(*end_fn)(void*);((end_fn)path_end_fn)(segment);
 int count=0;memcpy(&count,segment+56,4);
 if(count<2||count>8192||opening+count>8192||generated||!door_route(&retreat,(float*)arrays[0],(unsigned)count,start))goto done;
 memcpy(original+(opening+1)*3,(float*)arrays[0]+3,(SIZE_T)(count-1)*12);
 memcpy((float*)position_request.arrays[1]+(opening+1)*3,(float*)arrays[1]+3,(SIZE_T)(count-1)*12);
 position_request.count=opening+count;
 memcpy(position_request.path+56,&position_request.count,4);memcpy(position_request.path+80,&position_request.count,4);
 unsigned char *pie=position_request.look+168;
 memcpy(pie,position_request.look,84);
 memcpy(pie+60,position_request.look+84+60,12);memcpy(pie+72,&opening,4);
 valid=true;
 done:
 for(unsigned i=0;i<3;++i)if(arrays[i])HeapFree(GetProcessHeap(),0,arrays[i]);
 return valid;
}
static void start_position_only(uintptr_t human,uintptr_t client,uintptr_t map,uintptr_t door,uintptr_t wheel,const float anchor[3]) {
 position_discard();float body[3];uint32_t id=0,door_id=0;
 if(!read_at(human+108,body,12)||!friendly_human(human,client,&id)||!read_at(door+16,&door_id,4)){api->GuiShow((void*)wheel,0);return;}
 float approach[2]={anchor[0],anchor[2]};DoorGeometry reference;
 if(!door_geometry(door,approach,&reference)){api->GuiShow((void*)wheel,0);return;}
 typedef uint32_t(*query_fn)(void*,bool,float,float,float*,float*);
 bool found=false;DoorGeometry chosen={0};
 // Fixed along-wall distance. Only search body clearance from the wall.
 for(int step=3;step<=13;++step) {
  DoorGeometry candidate;float clearance=step*.05f,v1[3]={0},v2[3]={0};
  if(!door_position_goal(&reference,anchor,door_breach_distance,clearance,&candidate))continue;
  if(((query_fn)position_query_fn)((void*)human,false,candidate.goal[0],candidate.goal[1],v1,v2)!=door_id)continue;
  if(position_build(human,&candidate,body,id)&&position_prepare_open(id,door_id,candidate.center,v1,v2)&&position_append_retreat(human,&candidate)) {found=true;chosen=candidate;api->Log("Door 1.4.2: destination %.3f,%.3f; wall clearance %.2f m.",candidate.goal[0],candidate.goal[1],clearance);break;}
  position_discard();
 }
 api->GuiShow((void*)wheel,0);pending=0;
 if(!found){position_discard();api->Log("Door 1.4.2: no reachable opening-valid destination; existing orders preserved.");return;}
 // Buddy selection uses current body distance, never a distant future waypoint.
 uint32_t buddy_id=nearest_door_buddy(human,client);uintptr_t buddy=0;uint32_t verified=0;
 if(buddy_id&&buddy_id<door_entity_limit&&read_at(client+0x2e8+buddy_id*8,&buddy,8)&&friendly_human(buddy,client,&verified)&&verified==buddy_id) {
  float opening[3];memcpy(opening,position_request.look+84+60,12);
  if(!position_build_buddy(buddy,buddy_id,&chosen,opening)||!position_append_buddy(buddy,&chosen)) {
   position_free(&position_buddy);api->Log("Door 1.4.2: full buddy route unavailable; buddy's existing plan preserved.");
  }
 }
 // No orders are removed until the complete replacement has passed validation.
 clear_hovered_plan(id,client);
 if(position_buddy.active)clear_hovered_plan(position_buddy.owner,client);
 position_request.client=client;position_request.map=map;position_request.owner=id;position_request.door=door_id;
 position_request.started=GetTickCount64();position_request.active=true;
 api->Log("Door 1.4.2: clear requested; waiting for old plan removal.");
}
static bool position_is_cleared(uintptr_t client,uintptr_t human,uint32_t id) {
 int count=0;if(!read_at(human+0xd90+56,&count,4)||count<0||count>1)return false;
 if(count==1){uintptr_t p=0;float point[3],body[3];if(!read_at(human+0xd90+48,&p,8)||!read_at(p,point,12)||!read_at(human+108,body,12)||hypotf(point[0]-body[0],point[2]-body[2])>.1f)return false;}
 uintptr_t head=0,wp=0,node=0,potential=0;
 if(!read_at(door_potential_slot,&potential,8))return false;
#ifdef R5K_QOL_UI
 if(basic_113){
  uintptr_t entities[waypoint_pool_limit_113];
  if(!read_at(client+entity_pool_offset_113,entities,sizeof entities))return false;
  for(unsigned i=1;i<waypoint_pool_limit_113;i++){
   uint32_t owner=0,type=0,code=0;uintptr_t table=0,kind=0,entity=entities[i];
   if(!entity||entity==potential||!read_at(entity,&table,8)||!read_at(table+8,&kind,8)||kind!=waypoint_type_113)continue;
   if(!read_at(entity+0x220,&owner,4)||!read_at(entity+0x1f0,&type,4)||!read_at(entity+0x228,&code,4))return false;
   if(owner==id&&(type||code))return false;
  }
  return true;
 }
#else
 (void)client;
#endif
 if(!read_at(image+0x7cb540,&head,8))return false;
 if(head&&!read_at(head+24,&wp,8))return false;
 for(unsigned i=0;wp&&i<4096;++i){uint32_t owner=0,type=0,code=0;
  if(!read_at(wp+0x220,&owner,4)||!read_at(wp+0x1f0,&type,4)||!read_at(wp+0x228,&code,4))return false;
  if(wp!=potential&&owner==id&&(type||code))return false;
  if(!read_at(wp+0x1d8,&node,8))return false;
  if(!node||node==head)return true;
  if(!read_at(node+24,&wp,8))return false;
 }
 return wp==0;
}
static bool position_commit_plan(uintptr_t human,const PositionPlan *plan) {
 uintptr_t live=human+0xd90,points=0,params=0,coarse=0;int count=plan->count;
 typedef void(*resize_fn)(void*,int);
 ((resize_fn)position_resize_fn)((void*)live,count);
 ((resize_fn)position_coarse_reserve_fn)((void*)(live+96),2);
 int actual=0;
 if(!read_at(live+56,&actual,4)||actual!=count||!read_at(live+48,&points,8)||!points||
    !read_at(live+72,&params,8)||!params||!read_at(live+104,&coarse,8)||!coarse)return false;
 // Copy only values into native-owned lists. Keep live rendering objects,
 // allocation ownership and synchronization metadata owned by the engine.
 memcpy((void*)points,plan->arrays[0],(SIZE_T)count*12);
 memcpy((void*)params,plan->arrays[1],(SIZE_T)count*12);
 float *p=(float*)plan->arrays[0];float ends[4]={p[0],p[2],p[(count-1)*3],p[(count-1)*3+2]};
 memcpy((void*)coarse,ends,sizeof ends);int two=2,zero=0;
 memcpy((void*)(live+112),&two,4);memcpy((void*)(live+120),ends+2,8);memcpy((void*)(live+20),&zero,4);
 typedef void(*bounds_fn)(void*,int);((bounds_fn)position_bounds_fn)((void*)live,0);
 memcpy((void*)(live+160),&zero,4);memcpy((void*)(live+164),&zero,4);
 unsigned char redraw=1;memcpy((void*)(live+168),&redraw,1);
 // Match Captured_PathDrag release: no active edit or rejected branch range.
 memset((void*)(human+0xd84),0,12);
 return true;
}
// Gate at staging, reapply pie after D, then maintain it at Wait for clear.
static bool position_prepare_sync(void) {
 uint32_t zero=0;memcpy(position_request.look+84+56,&zero,4);memcpy(position_request.look+168+56,&zero,4);
 PositionPlan *plan=&position_buddy;int stage=plan->staging_index,last=plan->count-1;
 if(!plan->active||stage<1||last<=stage||!plan->arrays[0])return false;
 const float *points=(float*)plan->arrays[0];uint32_t d=4,wait_type=7,clear=5;int16_t wait=500;
 unsigned char *ready=plan->look+84;memset(ready,0,84);
 float origin[3]={points[stage*3],.11f,points[stage*3+2]};
 memcpy(ready,&wait_type,4);memcpy(ready+48,&plan->owner,4);memcpy(ready+56,&d,4);
 memcpy(ready+60,origin,12);memcpy(ready+72,&stage,4);memcpy(ready+80,&wait,2);
 unsigned char *moving=plan->look+168;memcpy(moving,plan->look,84);
 memcpy(moving+60,origin,12);memcpy(moving+72,&stage,4);memcpy(moving+56,&zero,4);
 origin[0]=points[last*3];origin[2]=points[last*3+2];
 unsigned char *arrival=plan->look+252;memcpy(arrival,moving,84);
 memcpy(arrival+60,origin,12);memcpy(arrival+72,&last,4);
 unsigned char *wait_clear=plan->look+336;memcpy(wait_clear,ready,84);
 memcpy(wait_clear+56,&clear,4);memcpy(wait_clear+60,origin,12);memcpy(wait_clear+72,&last,4);
 // Look types keep aiming while D waits; Open itself remains ungated.
 memcpy(position_request.look+168+56,&d,4);return true;
}
static void position_order_breacher_actions(unsigned char ordered[4*84]) {
 // Submit D-gated doorway aim before ungated Open at the shared arrival index.
 // Internal preparation slots stay stable for Open target/code bookkeeping.
 memcpy(ordered,position_request.look,84);
 memcpy(ordered+84,position_request.look+168,84);
 memcpy(ordered+168,position_request.look+84,84);
 // D-gated look ends on release. Reapply an independent, ungated pie
 // after Open so the retreat has its own persistent doorway aim.
 memcpy(ordered+252,position_request.look+168,84);
 uint32_t zero=0;memcpy(ordered+252+56,&zero,4);
}
static bool position_commit_local(uintptr_t human) {return position_commit_plan(human,&position_request);}
#include "door_completion_sync.h"
static void position_only_post(void) {
 uintptr_t client=0,map=0,human=0;uint32_t id=0;HWND window;state s={0};
 if(!enabled||!game_foreground(&window)||window!=hooked_window||!get_state(&s)||!s.playing||s.other_panel||
    !read_at(door_client_slot,&client,8)||client!=position_request.client||!read_at(client+0x28,&map,8)||map!=position_request.map||
    GetTickCount64()-position_request.started>2000||!read_at(client+0x2e8+position_request.owner*8,&human,8)||
    !friendly_human(human,client,&id)||id!=position_request.owner){api->Log("Door 1.4.2: cancelled/timed out; no new plan submitted.");position_discard();return;}
 if(GetTickCount64()-position_request.started<100||!position_is_cleared(client,human,id))return;
 uintptr_t buddy=0;uint32_t buddy_id=0;
 if(position_buddy.active) {
  if(!read_at(client+0x2e8+position_buddy.owner*8,&buddy,8)||!friendly_human(buddy,client,&buddy_id)||buddy_id!=position_buddy.owner) {
   position_free(&position_buddy);buddy=0;
  }else if(!position_is_cleared(client,buddy,buddy_id))return;
 }
 // Verify both local commits before serializing paired D markers.
 if(buddy&&!position_commit_plan(buddy,&position_buddy)) {
  api->Log("Door 1.4.2: buddy local path commit failed; solo opening retained.");position_free(&position_buddy);buddy=0;
 }
 position_prepare_sync();
 if(!position_commit_local(human)){api->Log("Door 1.4.2: local path commit failed; no notification sent.");position_discard();return;}
 typedef void(*notify_fn)(void*,void*,int,int,int,void*,bool);
 unsigned char ordered[4*84];position_order_breacher_actions(ordered);
 float sync_target[3];memcpy(sync_target,position_request.look+168+8,12);
 door_sync_track(map,id,position_request.door,(float[2]){sync_target[0],sync_target[2]},position_buddy.active);
 ((notify_fn)path_notify_fn)((void*)human,(void*)(human+0xd90),0,position_request.count,4,ordered,false);
 if(buddy&&position_buddy.active) {
  ((notify_fn)path_notify_fn)((void*)buddy,(void*)(buddy+0xd90),0,position_buddy.count,5,position_buddy.look,false);
  api->Log("Door 1.4.2: buddy %u full staging/D, inset-apex movement and Wait-for-clear sequence submitted.",buddy_id);
 }
 api->Log("Door 1.4.2: breacher sequence submitted; paired D at arrival pie/staging before Open when buddy available.");position_discard();
}
static bool init_position_only(void) {
 bool current=false;
#ifdef R5K_QOL_UI
 current=basic_113;
#endif
 position_query_fn=(uintptr_t)api->ResolveSymbol("ActionWP_Client::CheckForSurroundingTargets");
 position_resize_fn=(uintptr_t)api->ResolveSymbol("Waypoints::Resize");
 position_bounds_fn=(uintptr_t)api->ResolveSymbol("Path::UpdateWaypointsBounds");
 position_coarse_reserve_fn=(uintptr_t)api->ResolveSymbol("List<Vector2>::Reserve");
 return position_query_fn==image+(current?0x9f650:0xa0bb0)&&position_resize_fn==image+(current?0x271680:0x2627a0)&&
  position_bounds_fn==image+(current?0x19c910:0x19b760)&&position_coarse_reserve_fn==image+(current?0x280e0:0x25a10)&&
  install_door_completion_sync();
}
#endif
