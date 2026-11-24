#ifndef RAKE_QOL_BINDING_MODEL_H
#define RAKE_QOL_BINDING_MODEL_H
#include <stddef.h>
typedef struct {const char *label;int section,action,key,mods,default_key,default_mods,last_key,last_mods;} QolBinding;
static int qol_match(const QolBinding *bindings,size_t count,int key,int mods){
 if(!key)return 0;
 for(size_t i=0;i<count;i++)if(bindings[i].key==key&&bindings[i].mods==mods)return bindings[i].action;
 return 0;
}
static int qol_conflicts(const QolBinding *bindings,size_t count,size_t changed){
 if(bindings[changed].key<0||bindings[changed].key>255||bindings[changed].mods<0||bindings[changed].mods>7)return 1;
 if(!bindings[changed].key)return 0;
 for(size_t i=0;i<count;i++)if(i!=changed&&bindings[i].key==bindings[changed].key&&bindings[i].mods==bindings[changed].mods)return 1;
 return 0;
}
#endif
