#ifndef R5K_DOOR_COMPLETION_SYNC_H
#define R5K_DOOR_COMPLETION_SYNC_H
// Scope the native server barrier adjustment to paired plans published by O.
typedef struct {uint32_t owner,door;float target[2];} DoorSyncPlan;
static SRWLOCK door_sync_lock=SRWLOCK_INIT;
static DoorSyncPlan door_sync_plans[32];static uintptr_t door_sync_map;static unsigned door_sync_slot;
static void door_sync_track(uintptr_t map,uint32_t owner,uint32_t door,const float target[2],bool paired) {
 AcquireSRWLockExclusive(&door_sync_lock);
 if(map!=door_sync_map){memset(door_sync_plans,0,sizeof door_sync_plans);door_sync_map=map;door_sync_slot=0;}
 unsigned slot=32;
 for(unsigned i=0;i<32;++i)if(door_sync_plans[i].owner==owner){slot=i;break;}
 if(slot==32){if(!paired){ReleaseSRWLockExclusive(&door_sync_lock);return;}slot=door_sync_slot++%32;}
 door_sync_plans[slot]=(DoorSyncPlan){paired?owner:0,door,{target[0],target[1]}};
 ReleaseSRWLockExclusive(&door_sync_lock);
}
static bool door_sync_reorder(uintptr_t *items,int count,uintptr_t barrier,uintptr_t opening) {
 if(count<2||count>4096||!barrier||!opening||barrier==opening)return false;
 int b=-1,o=-1;for(int i=0;i<count;++i){if(items[i]==barrier)b=i;if(items[i]==opening)o=i;}
 // Original native sync inserts ahead of the D look; move only forward.
 if(b<0||o<0||b>=o)return false;
 memmove(items+b,items+b+1,(SIZE_T)(o-b)*sizeof(*items));items[o]=barrier;return true;
}
static int door_sync_pre(DK2ML_Regs *regs,void *user) {
 (void)user;regs->scratch[0]=0;
 uintptr_t human=(uintptr_t)DK2ML_Arg(regs,0),action=(uintptr_t)DK2ML_Arg(regs,1),client=0,map=0,list=0;
 uint32_t owner=0,type=0,code=0;int count=0,index=-1;float target[3];
 if(!enabled||!read_at(door_client_slot,&client,8)||!client||!read_at(client+0x28,&map,8)||
    !read_at(action+0x13c,&type,4)||type!=6||!read_at(action+0x174,&code,4)||code!=4||
    !read_at(action+0x16c,&owner,4)||!owner||!read_at(action+0x144,target,12)||!read_at(action+0x184,&index,4))return DK2ML_CALL_ORIGINAL;
 DoorSyncPlan plan={0};
 AcquireSRWLockShared(&door_sync_lock);
 if(map&&map==door_sync_map)for(unsigned i=0;i<32;++i)if(door_sync_plans[i].owner==owner&&fabsf(door_sync_plans[i].target[0]-target[0])<.001f&&fabsf(door_sync_plans[i].target[1]-target[2])<.001f){plan=door_sync_plans[i];break;}
 ReleaseSRWLockShared(&door_sync_lock);
 if(!plan.owner||!read_at(human+16,&owner,4)||owner!=plan.owner||!read_at(human+0x1538,&count,4)||count<2||count>4096||!read_at(human+0x1530,&list,8)||!list)return DK2ML_CALL_ORIGINAL;
 // Only the head D look immediately followed by its same-point normal Open.
 uintptr_t first=0,opening=0;uint32_t door=0;int open_index=-1;
 if(!read_at(list,&first,8)||first!=action||!read_at(list+8,&opening,8)||!opening||
    !read_at(opening+0x13c,&type,4)||type!=21||!read_at(opening+0x140,&door,4)||door!=plan.door||
    !read_at(opening+0x184,&open_index,4)||open_index!=index)return DK2ML_CALL_ORIGINAL;
 regs->scratch[0]=human;regs->scratch[1]=opening;regs->scratch[2]=DK2ML_Arg(regs,2);regs->scratch[3]=action;
 return DK2ML_CALL_ORIGINAL;
}
static void door_sync_post(DK2ML_Regs *regs,void *user) {
 (void)user;if(!regs->scratch[0])return;
 uintptr_t human=(uintptr_t)regs->scratch[0],list=0,barrier=0;int count=0;
 if(!read_at(human+0x1538,&count,4)||count<3||count>4096||!read_at(human+0x1530,&list,8)||!list)return;
 uintptr_t items[4096];if(!read_at(list,items,(SIZE_T)count*8))return;
 for(int i=0;i<count;++i){uint32_t type=0,group=0;float sentinel=0,source=0;
  if(read_at(items[i]+0x13c,&type,4)&&type==18&&read_at(items[i]+0x188,&group,4)&&group==(uint32_t)regs->scratch[2]&&
     read_at(items[i]+0x160,&sentinel,4)&&sentinel==3.402823466e38f&&read_at(items[i]+0x164,&source,4)) {
   uint32_t id=0;if(read_at((uintptr_t)regs->scratch[3]+16,&id,4)&&source==(float)id){barrier=items[i];break;}
  }
 }
 if(!door_sync_reorder(items,count,barrier,(uintptr_t)regs->scratch[1]))return;
 // Native creation uses the owner's current/next point index. Once moved
 // after Open, retain Open's index: otherwise the following arrival-index
 // retreat pie is behind this barrier and CanTriggerWaypoint rejects it.
 int opening_index=-1,old_index=-1;
 read_at(barrier+0x184,&old_index,4);
 if(!read_at((uintptr_t)regs->scratch[1]+0x184,&opening_index,4)||opening_index<0||opening_index>=8192)return;
 SIZE_T written=0;
 if(!WriteProcessMemory(GetCurrentProcess(),(void*)(barrier+0x184),&opening_index,4,&written)||written!=4)return;
 if(WriteProcessMemory(GetCurrentProcess(),(void*)list,items,(SIZE_T)count*8,&written)&&written==(SIZE_T)count*8)
  api->Log("Door 1.4.2: moved native completion-sync barrier after Open (group=%u, index=%d->%d).",(unsigned)regs->scratch[2],old_index,opening_index);
}
static bool install_door_completion_sync(void) {
#ifdef R5K_QOL_UI
 if(basic_113)return api->GetFieldOffset("Human_Server","m_actionWaypoints")==0x1528&&
  api->GetFieldOffset("ActionWP_Server","m_action")==0x13c&&
  hook_named("Human_Server::TryToSyncAfterWaypoint",0x2bc350,door_sync_pre,door_sync_post);
#endif
 return hook_named("Human_Server::TryToSyncAfterWaypoint",0x2ad6e0,door_sync_pre,door_sync_post);
}
#endif
