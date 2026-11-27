#ifndef R5K_AUTOEXEC_DEFAULTS_H
#define R5K_AUTOEXEC_DEFAULTS_H
#include <stdbool.h>
static inline unsigned autoexec_all_changes(unsigned flags){
    const unsigned all=15u;
    return (flags&all)==all?all:all&~flags;
}
typedef struct { bool initialized; unsigned manual; } AutoexecDefaults;
static inline void defaults_reset(AutoexecDefaults *s){s->initialized=false;s->manual=0;}
static inline void defaults_manual(AutoexecDefaults *s,unsigned code){if(code>=1&&code<=4)s->manual|=1u<<(code-1);}
static inline unsigned defaults_missing_for(const AutoexecDefaults *s,unsigned flags,unsigned target){return s->initialized?0:(15u&~s->manual&(flags^target));}
static inline void defaults_observe_for(AutoexecDefaults *s,unsigned flags,unsigned target){if(!defaults_missing_for(s,flags,target))s->initialized=true;}
static inline unsigned defaults_missing(const AutoexecDefaults *s,unsigned flags){return s->initialized?0:(15u&~s->manual&~flags);}
static inline void defaults_observe(AutoexecDefaults *s,unsigned flags){if(!defaults_missing(s,flags))s->initialized=true;}
#endif
