/**
 * A mark table, used during a mark-and-sweep garbage collection cycle.
 *
 * This implementation is faster than marktable.c, but is *not*
 * copy-on-write friendly. It stores mark information directly inside objects.
 */
#ifndef _FAST_MARK_TABLE_C_
#define _FAST_MARK_TABLE_C_

static void
rb_fast_mark_table_init() {
}

static void
rb_fast_mark_table_prepare() {
}

static void
rb_fast_mark_table_finalize() {
}

static inline void
rb_fast_mark_table_add(RVALUE *object) {
	object->as.basic.flags |= FL_MARK;
}

static inline void
rb_fast_mark_table_heap_add(struct heaps_slot *hs, RVALUE *object) {
	object->as.basic.flags |= FL_MARK;
}

static inline int
rb_fast_mark_table_contains(RVALUE *object) {
	return object->as.basic.flags & FL_MARK;
}

static inline int
rb_fast_mark_table_heap_contains(struct heaps_slot *hs, RVALUE *object) {
	return object->as.basic.flags & FL_MARK;
}

static inline void
rb_fast_mark_table_remove(RVALUE *object) {
	object->as.basic.flags &= ~FL_MARK;
}

static inline void
rb_fast_mark_table_heap_remove(struct heaps_slot *hs, RVALUE *object) {
	object->as.basic.flags &= ~FL_MARK;
}

static inline void
rb_fast_mark_table_add_filename(char *filename) {
	filename[-1] = 1;
}

static inline int
rb_fast_mark_table_contains_filename(const char *filename) {
	return filename[-1];
}

static inline void
rb_fast_mark_table_remove_filename(char *filename) {
	filename[-1] = 0;
}

static void
rb_use_fast_mark_table() {
	rb_mark_table_init          = rb_fast_mark_table_init;
	rb_mark_table_prepare       = rb_fast_mark_table_prepare;
	rb_mark_table_finalize      = rb_fast_mark_table_finalize;
	rb_mark_table_add           = rb_fast_mark_table_add;
	rb_mark_table_heap_add      = rb_fast_mark_table_heap_add;
	rb_mark_table_contains      = rb_fast_mark_table_contains;
	rb_mark_table_heap_contains = rb_fast_mark_table_heap_contains;
	rb_mark_table_remove        = rb_fast_mark_table_remove;
	rb_mark_table_heap_remove   = rb_fast_mark_table_heap_remove;
	rb_mark_table_add_filename  = rb_fast_mark_table_add_filename;
	rb_mark_table_contains_filename = rb_fast_mark_table_contains_filename;
	rb_mark_table_remove_filename   = rb_fast_mark_table_remove_filename;
}

#endif /* _FAST_MARK_TABLE_C_ */
