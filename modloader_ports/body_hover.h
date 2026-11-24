#ifndef R5K_BODY_HOVER_H
#define R5K_BODY_HOVER_H
#include <stdbool.h>
#include <math.h>
// Positive ray segment against the actual world-space body bounds.
static inline bool body_ray_hit(const float eye[3],const float direction[3],const float low[3],const float high[3]) {
 float enter=0,leave=10000;bool extent=false,ray=false;
 for(unsigned i=0;i<3;++i) {
  if(!isfinite(eye[i])||!isfinite(direction[i])||!isfinite(low[i])||!isfinite(high[i])||high[i]<low[i]||high[i]-low[i]>10)return false;
  if(high[i]-low[i]>.01f)extent=true;
  if(fabsf(direction[i])<.000001f){if(eye[i]<low[i]||eye[i]>high[i])return false;continue;}
  ray=true;float a=(low[i]-eye[i])/direction[i],b=(high[i]-eye[i])/direction[i];
  if(a>b){float t=a;a=b;b=t;}enter=fmaxf(enter,a);leave=fminf(leave,b);
  if(enter>leave)return false;
 }
 return extent&&ray&&leave>=0;
}
#endif
