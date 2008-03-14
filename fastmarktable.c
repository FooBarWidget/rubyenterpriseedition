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

#define rb_fast_mark_table_add(object) (object)->as.basic.flags |= FL_MARK
#define rb_fast_mark_table_heap_add(heap, object) (object)->as.basic.flags |= FL_MARK
#define rb_fast_mark_table_contains(object) ((object)->as.basic.flags & FL_MARK)
#define rb_fast_mark_table_heap_contains(heap, object) ((object)->as.basic.flags & FL_MARK)
#define rb_fast_mark_table_remove(object) ((object)->as.basic.flags &= ~FL_MARK)
#define rb_fast_mark_table_heap_remove(heap, object) (object)->as.basic.flags &= ~FL_MARK
#define rb_fast_mark_table_add_filename(filename) (filename)[-1] = 1
#define rb_fast_mark_table_contains_filename(filename) ((filename)[-1])
#define rb_fast_mark_table_remove_filename(filename) (filename)[-1] = 0

#define rb_mark_table_init          rb_fast_mark_table_init
#define rb_mark_table_prepare       rb_fast_mark_table_prepare
#define rb_mark_table_finalize      rb_fast_mark_table_finalize
#define rb_mark_table_add           rb_fast_mark_table_add
#define rb_mark_table_heap_add      rb_fast_mark_table_heap_add
#define rb_mark_table_contains      rb_fast_mark_table_contains
#define rb_mark_table_heap_contains rb_fast_mark_table_heap_contains
#define rb_mark_table_remove        rb_fast_mark_table_remove
#define rb_mark_table_heap_remove   rb_fast_mark_table_heap_remove
#define rb_mark_table_add_filename  rb_fast_mark_table_add_filename
#define rb_mark_table_contains_filename rb_fast_mark_table_contains_filename
#define rb_mark_table_remove_filename   rb_fast_mark_table_remove_filename

#endif /* _FAST_MARK_TABLE_C_ */
