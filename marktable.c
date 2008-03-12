#include "pointerset.h"

/* A mark table for filenames and objects that are not on the heap. */
static PointerSet *mark_table = NULL;
static struct heaps_slot *last_heap = NULL;


static inline struct heaps_slot *
find_heap_slot_for_object(RVALUE *object)
{
	register int i;

	/* Look in the cache first. */
	if (last_heap != NULL && object >= last_heap->slot
	 && object < last_heap->slot + last_heap->limit) {
		return last_heap;
	}
	for (i = 0; i < heaps_used; i++) {
		struct heaps_slot *heap = &heaps[i];
		if (object >= heap->slot
		 && object < heap->slot + heap->limit) {
			/* Cache this result. According to empirical evidence, the chance is
			 * high that the next lookup will be for the same heap slot.
			 */
			last_heap = heap;
			return heap;
		}
	}
	return NULL;
}

static inline void
find_position_in_bitfield(struct heaps_slot *hs, RVALUE *object,
                          unsigned int *bitfield_index, unsigned int *bitfield_offset)
{
	unsigned int index;

	index = object - hs->slot;
	*bitfield_index = index / (sizeof(int) * 8);
	*bitfield_offset = index % (sizeof(int) * 8);
}


static void
rb_mark_table_init()
{
	mark_table = pointer_set_new();
}

static void
rb_mark_table_prepare()
{
	last_heap = NULL;
}

static void
rb_mark_table_finalize()
{
	int i;

	if (pointer_set_get_size(mark_table) > 0) {
		pointer_set_reset(mark_table);
	}
	for (i = 0; i < heaps_used; i++) {
		memset(heaps[i].marks, 0, heaps[i].marks_size * sizeof(int));
	}
}

static inline void
rb_mark_table_add(RVALUE *object)
{
	struct heaps_slot *hs;
	unsigned int bitfield_index, bitfield_offset;

	hs = find_heap_slot_for_object(object);
	if (hs != NULL) {
		find_position_in_bitfield(hs, object, &bitfield_index, &bitfield_offset);
		hs->marks[bitfield_index] |= (1 << bitfield_offset);
	} else {
		pointer_set_insert(mark_table, (void *) object);
	}
}

static inline int
rb_mark_table_contains(RVALUE *object)
{
	struct heaps_slot *hs;
	unsigned int bitfield_index, bitfield_offset;

	hs = find_heap_slot_for_object(object);
	if (hs != NULL) {
		find_position_in_bitfield(hs, object, &bitfield_index, &bitfield_offset);
		return hs->marks[bitfield_index] & (1 << bitfield_offset);
	} else {
		return pointer_set_contains(mark_table, (void *) object);
	}
}

static inline int
rb_mark_table_heap_contains(struct heaps_slot *hs, RVALUE *object)
{
	unsigned int bitfield_index, bitfield_offset;
	find_position_in_bitfield(hs, object, &bitfield_index, &bitfield_offset);
	return hs->marks[bitfield_index] & (1 << bitfield_offset);
}

static inline void
rb_mark_table_remove(RVALUE *object)
{
	struct heaps_slot *hs;
	unsigned int bitfield_index, bitfield_offset;

	hs = find_heap_slot_for_object(object);
	if (hs != NULL) {
		find_position_in_bitfield(hs, object, &bitfield_index, &bitfield_offset);
		hs->marks[bitfield_index] &= ~(1 << bitfield_offset);
	} else {
		pointer_set_delete(mark_table, (void *) object);
	}
}

static inline void
rb_mark_table_heap_remove(struct heaps_slot *hs, RVALUE *object)
{
	unsigned int bitfield_index, bitfield_offset;
	find_position_in_bitfield(hs, object, &bitfield_index, &bitfield_offset);
	hs->marks[bitfield_index] &= ~(1 << bitfield_offset);
}

static inline void
rb_mark_table_add_filename(const char *filename)
{
	pointer_set_insert(mark_table, (void *) filename);
}

static inline int
rb_mark_table_contains_filename(const char *filename)
{
	return pointer_set_contains(mark_table, (void *) filename);
}

static inline void
rb_mark_table_remove_filename(const char *filename)
{
	pointer_set_delete(mark_table, (void *) filename);
}
