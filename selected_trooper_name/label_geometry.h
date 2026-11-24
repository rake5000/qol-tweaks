#ifndef LABEL_GEOMETRY_H
#define LABEL_GEOMETRY_H
#include <stdint.h>
#include <stdbool.h>
#include <math.h>
typedef struct {float p[3],uv[2];} FontVertex;
typedef struct {float p[3],uv[2];uint32_t color;} LabelVertex;
typedef struct {int blend;bool depth;unsigned char pad[3];LabelVertex q[4];uint32_t texture;} LabelQuad;
typedef struct {int capacity,pad;LabelQuad*data;int count;bool fixed;unsigned char tail[3];} LabelList;
_Static_assert(sizeof(FontVertex)==20,"font vertex ABI");
_Static_assert(sizeof(LabelQuad)==108,"status quad ABI");
_Static_assert(sizeof(LabelList)==24,"status list ABI");
static bool list_valid(const LabelList*l){return l&&l->capacity>=0&&l->capacity<65536&&l->count>=0&&l->count<=l->capacity&&(!l->capacity||l->data);}
#endif
