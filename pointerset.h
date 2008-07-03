/**
 * A specialized set data structure, designed to only contain pointers.
 * It will grow and shrink dynamically.
 */
#ifndef _POINTER_SET_H_
#define _POINTER_SET_H_

typedef void * PointerSetElement;
typedef struct _PointerSet PointerSet;

/**
 * Create a new, empty pointer set.
 */
PointerSet   *pointer_set_new();

/**
 * Free the given pointer set.
 */
void         pointer_set_free(PointerSet *set);

/**
 * Insert the given pointer into the pointer set. The data that the
 * pointer pointers to is not touched, so <tt>element</tt> may even be
 * an invalid pointer.
 */
void         pointer_set_insert(PointerSet *set, PointerSetElement element);

/**
 * Remove the given pointer from the pointer set. Nothing will happen
 * if the pointer isn't already in the set.
 */
void         pointer_set_delete(PointerSet *set, PointerSetElement element);

/**
 * Check whether the given pointer is in the pointer set.
 */
int          pointer_set_contains(PointerSet *set, PointerSetElement element);

/**
 * Clear the pointer set.
 */
void         pointer_set_reset(PointerSet *set);

/**
 * Return the number of pointers in the pointer set.
 */
unsigned int pointer_set_get_size(PointerSet *set);

/**
 * Return the amount of space that is used to store the pointers in the set.
 *
 * @invariant pointer_set_get_capacity(set) >= pointer_set_get_size(set)
 */
unsigned int pointer_set_get_capacity(PointerSet *set);

#endif /* _POINTER_SET_H_ */
