#ifndef _POINTER_SET_H_
#define _POINTER_SET_H_

typedef void * PointerSetElement;
typedef struct _PointerSet PointerSet;

PointerSet   *pointer_set_new();
void         pointer_set_free(PointerSet *set);

void         pointer_set_insert(PointerSet *set, PointerSetElement element);
void         pointer_set_delete(PointerSet *set, PointerSetElement element);
int          pointer_set_contains(PointerSet *set, PointerSetElement element);
void         pointer_set_reset(PointerSet *set);
unsigned int pointer_set_get_size(PointerSet *set);
unsigned int pointer_set_get_capacity(PointerSet *set);

#endif /* _POINTER_SET_H_ */
