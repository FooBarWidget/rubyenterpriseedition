#ifndef _MARK_TABLE_H_
#define _MARK_TABLE_H_

static void (*rb_mark_table_init)();
static void (*rb_mark_table_prepare)();
static void (*rb_mark_table_finalize)();
static void (*rb_mark_table_add)(RVALUE *object);
static void (*rb_mark_table_heap_add)(struct heaps_slot *hs, RVALUE *object);
static int  (*rb_mark_table_contains)(RVALUE *object);
static int  (*rb_mark_table_heap_contains)(struct heaps_slot *hs, RVALUE *object);
static void (*rb_mark_table_remove)(RVALUE *object);
static void (*rb_mark_table_heap_remove)(struct heaps_slot *hs, RVALUE *object);
static void (*rb_mark_table_add_filename)(char *filename);
static int  (*rb_mark_table_contains_filename)(const char *filename);
static void (*rb_mark_table_remove_filename)(char *filename);

#endif /* _MARK_TABLE_H_ */
