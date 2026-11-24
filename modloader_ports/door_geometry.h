#ifndef R5K_DOOR_GEOMETRY_H
#define R5K_DOOR_GEOMETRY_H
#include <math.h>
#include <stdbool.h>
typedef struct {float center[2],along[2],normal[2],half,face,side,goal[2];} DoorGeometry;
static inline bool door_goal(DoorGeometry *g,const float center[2],const float along[2],float half,const float approach[2],float distance) {
 float len=hypotf(along[0],along[1]);
 if(!isfinite(len)||len<.001f||!isfinite(half)||half<.25f||half>3.5f||!isfinite(distance)||distance<.1f||distance>3.0f)return false;
 for(unsigned i=0;i<2;++i){if(!isfinite(center[i])||!isfinite(approach[i]))return false;g->center[i]=center[i];g->along[i]=along[i]/len;}
 g->normal[0]=-g->along[1];g->normal[1]=g->along[0];g->half=half;
 float dx=approach[0]-center[0],dz=approach[1]-center[1];
 float a=dx*g->along[0]+dz*g->along[1],n=dx*g->normal[0]+dz*g->normal[1];
 if(fabsf(n)<.05f)return false;
 g->face=n<0?-1:1;g->side=a<0?-1:1;
 float clearance=fminf(.65f,fmaxf(.35f,fabsf(n)));
 for(unsigned i=0;i<2;++i)g->goal[i]=center[i]+g->along[i]*g->side*(half+distance)+g->normal[i]*g->face*clearance;
 return true;
}
// Reuse the sidestep geometry. A near-center approach chooses a consistent
// jamb rather than allowing tiny floating-point differences to flip sides.
static inline bool door_breach_goal(const DoorGeometry *door,const float body[3],float distance,DoorGeometry *g) {
 float approach[2]={body[0],body[2]};
 float along=(body[0]-door->center[0])*door->along[0]+(body[2]-door->center[1])*door->along[1];
 if(fabsf(along)<.05f)for(unsigned i=0;i<2;++i)approach[i]+=door->along[i]*(.05f-along);
 return door_goal(g,door->center,door->along,door->half,approach,distance);
}
static inline bool door_position_goal(const DoorGeometry *door,const float body[3],float distance,float clearance,DoorGeometry *g) {
 if(!isfinite(distance)||distance<.2f||distance>2||!isfinite(clearance)||clearance<.15f||clearance>.65f)return false;
 if(!door_breach_goal(door,body,.7f,g))return false;
 for(unsigned i=0;i<2;++i)g->goal[i]=g->center[i]+g->along[i]*g->side*distance+g->normal[i]*g->face*clearance;
 return true;
}
// Pair range is measured from current body positions, not future endpoints.
static inline float door_buddy_distance(const float breacher[3],const float buddy[3]) {
 float sum=0;
 for(unsigned i=0;i<3;++i){if(!isfinite(breacher[i])||!isfinite(buddy[i]))return -1;float v=breacher[i]-buddy[i];sum+=v*v;}
 return sum<=2.25f?sum:-1;
}
static inline bool door_buddy_goal(const DoorGeometry *breacher,const float origin[3],DoorGeometry *g,float target[3]) {
 float approach[2]={origin[0],origin[2]};
 if(!door_goal(g,breacher->center,breacher->along,breacher->half,approach,1.0f)||g->face!=breacher->face)return false;
 // Stand in front of the opening center, one metre out on this wall face.
 // The jamb half affects aim only; it must not move the destination sideways.
 for(unsigned i=0;i<2;++i)g->goal[i]=g->center[i]+g->normal[i]*g->face*1.0f;
 target[0]=g->center[0]+g->along[0]*g->side*g->half*.5f;
 target[1]=0;
 target[2]=g->center[1]+g->along[1]*g->side*g->half*.5f;
 return true;
}
static inline bool door_buddy_stage(const DoorGeometry *door,const float breacher[3],float stage[3]) {
 for(unsigned i=0;i<3;++i)if(!isfinite(breacher[i]))return false;
 stage[0]=breacher[0]+door->normal[0]*door->face*.7f;
 stage[1]=breacher[1];
 stage[2]=breacher[2]+door->normal[1]*door->face*.7f;
 return true;
}
// Buddy staging stays on breacher's wall face at configured spacing from the planned
// opening anchor. Aim at the inset near apex (3/4 across the door width).
static inline bool door_buddy_staging(const DoorGeometry *door,const float opening[3],const float body[3],float distance,DoorGeometry *stage,float target[3]) {
 if(!isfinite(distance)||distance<.20f||distance>1.50f)return false;
 for(unsigned i=0;i<3;++i)if(!isfinite(opening[i])||!isfinite(body[i]))return false;
 float normal=((body[0]-door->center[0])*door->normal[0]+(body[2]-door->center[1])*door->normal[1])*door->face;
 if(normal<.15f)return false;
 *stage=*door;
 for(unsigned i=0;i<2;++i)stage->goal[i]=opening[i*2]+door->normal[i]*door->face*distance;
 // Reuse the sequence's chosen breaching side; buddy's starting half
 // must not select a different apex from his planned staging position.
 float side=door->side;
 target[0]=door->center[0]+door->along[0]*side*door->half*.5f;
 target[1]=0;target[2]=door->center[1]+door->along[1]*side*door->half*.5f;
 return true;
}
static inline bool door_approach_point(const DoorGeometry *door,const float origin[3],float point[3]) {
 float x=door->center[0]-origin[0],z=door->center[1]-origin[2],length=hypotf(x,z);
 if(!isfinite(length)||length<.11f)return false;
 point[0]=origin[0]+x/length*.1f;point[1]=origin[1];point[2]=origin[2]+z/length*.1f;return true;
}
// Native opening orders may occupy the next point index before a movement
// path exists. Reject orders in the middle of a longer independent plan.
static inline bool door_tail_index(int count,int index) {
 return count>=0&&count<=4096&&index>=0&&index>=count-1&&index<=count;
}
static inline bool door_route(const DoorGeometry *g,const float *points,unsigned count,const float start[2]) {
 if(count<2||count>8192)return false;
 float total=0,last[2]={start[0],start[1]};
 for(unsigned i=0;i<count;++i){
  float x=points[i*3],z=points[i*3+2];if(!isfinite(x)||!isfinite(z))return false;
  float normal=((x-g->center[0])*g->normal[0]+(z-g->center[1])*g->normal[1])*g->face;
  if(normal<.15f)return false;
  total+=hypotf(x-last[0],z-last[1]);last[0]=x;last[1]=z;
 }
 float direct=hypotf(g->goal[0]-start[0],g->goal[1]-start[1]);
 return hypotf(last[0]-g->goal[0],last[1]-g->goal[1])<.12f&&total<=direct*1.25f+.25f;
}
#endif
