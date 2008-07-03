#include "config.h"
#include "defines.h"
#ifdef HAVE_STDLIB_H
	#include <stdlib.h>
#endif
#include <string.h>
#include "pointerset.h"


typedef struct _PointerSetEntry PointerSetEntry;

struct _PointerSet {
	unsigned int num_bins;
	unsigned int num_entries;
	PointerSetEntry **bins;
};

struct _PointerSetEntry {
	PointerSetElement element;
	PointerSetEntry *next;
};

/* Table of prime numbers 2^n+a, 2<=n<=30. */
static const long primes[] = {
	8 + 3,
	16 + 3,
	32 + 5,
	64 + 3,
	128 + 3,
	256 + 27,
	512 + 9,
	1024 + 9,
	2048 + 5,
	4096 + 3,
	8192 + 27,
	16384 + 43,
	32768 + 3,
	65536 + 45,
	131072 + 29,
	262144 + 3,
	524288 + 21,
	1048576 + 7,
	2097152 + 17,
	4194304 + 15,
	8388608 + 9,
	16777216 + 43,
	33554432 + 35,
	67108864 + 15,
	134217728 + 29,
	268435456 + 3,
	536870912 + 11,
	1073741824 + 85,
	0
};


/* The percentage of nonempty buckets, before increasing the number of bins. 1.0 == 100%
 * A value larger than 1.0 means that it's very likely that some buckets contain more than
 * 1 entry.
 */
#define MAX_LOAD_FACTOR 2.0
/* The default for the number of bins allocated initially. Must be a prime number. */
#define DEFAULT_TABLE_SIZE 11
/* MINSIZE is the minimum size of a dictionary. */
#define MINSIZE 8

#if SIZEOF_LONG == SIZEOF_VOIDP
	typedef unsigned long PointerInt;
#elif SIZEOF_LONG_LONG == SIZEOF_VOIDP
	typedef unsigned LONG_LONG PointerInt;
#else
	#error ---->> pointerset.c requires sizeof(void*) == sizeof(long) to be compiled. <<---
	-
#endif

#define alloc(type) (type*)malloc((unsigned)sizeof(type))
#define Calloc(n,s) (char*)calloc((n),(s))

#define HASH(element, num_bins) ((PointerInt) element) % num_bins
#define FIND_ENTRY(set, entry, element) \
	do { \
		unsigned int bin_pos = HASH(element, set->num_bins); \
		entry = (set)->bins[bin_pos]; \
		while (entry != NULL && entry->element != element) { \
			entry = entry->next; \
		} \
	} while (0)


static int
new_size(int size)
{
	int i;
	int newsize;

	for (i = 0, newsize = MINSIZE;
	     i < sizeof(primes)/sizeof(primes[0]);
	     i++, newsize <<= 1)
	{
		if (newsize > size)
			return primes[i];
	}
	/* Ran out of polynomials */
	return -1;	/* should raise exception */
}

PointerSet *
pointer_set_new()
{
	PointerSet *set;

	set = alloc(PointerSet);
	if (set != NULL) {
		set->num_entries = 0;
		set->num_bins = DEFAULT_TABLE_SIZE;
		set->bins = (PointerSetEntry **) Calloc(DEFAULT_TABLE_SIZE, sizeof(PointerSetEntry *));
	}
	return set;
}

static void
free_bin_contents(PointerSet *set)
{
	PointerSetEntry *entry, *next;
	int i;

	for(i = 0; i < set->num_bins; i++) {
		entry = set->bins[i];
		while (entry != NULL) {
			next = entry->next;
			free(entry);
			entry = next;
		}
		set->bins[i] = NULL;
	}
	set->num_entries = 0;
}

void
pointer_set_free(PointerSet *set)
{
	free_bin_contents(set);
	free(set->bins);
	free(set);
}

int
pointer_set_contains(PointerSet *set, PointerSetElement element)
{
	PointerSetEntry *entry;
	FIND_ENTRY(set, entry, element);
	return entry != NULL;
}

static void
rehash(PointerSet *set, int new_num_bins)
{
	PointerSetEntry *entry, **new_bins;
	int i;

	new_bins = (PointerSetEntry **) Calloc(new_num_bins, sizeof(PointerSetEntry *));
	for (i = 0; i < set->num_bins; i++) {
		entry = set->bins[i];
		while (entry != NULL) {
			unsigned int new_bin_pos;
			PointerSetEntry *next;

			new_bin_pos = HASH(entry->element, new_num_bins);
			next = entry->next;
			entry->next = new_bins[new_bin_pos];
			new_bins[new_bin_pos] = entry;
			entry = next;
		}
	}
	free(set->bins);
	set->num_bins = new_num_bins;
	set->bins = new_bins;
}

void
pointer_set_insert(PointerSet *set, PointerSetElement element)
{
	PointerSetEntry *entry;

	FIND_ENTRY(set, entry, element);
	if (entry == NULL) {
		unsigned int bin_pos;

		if (set->num_entries / (double) set->num_bins > MAX_LOAD_FACTOR) {
			/* Increase number of bins to the next prime number. */
			rehash(set, new_size(set->num_bins + 1));
		}

		bin_pos = HASH(element, set->num_bins);
		entry = malloc(sizeof(PointerSetEntry));
		entry->element = element;
		entry->next = set->bins[bin_pos];
		set->bins[bin_pos] = entry;
		set->num_entries++;
	}
}

void
pointer_set_delete(PointerSet *set, PointerSetElement element)
{
	unsigned int bin_pos;
	PointerSetEntry *entry, *prev;

	bin_pos = HASH(element, set->num_bins);
	entry = set->bins[bin_pos];
	prev = NULL;
	while (entry != NULL && entry->element != element) {
		prev = entry;
		entry = entry->next;
	}
	if (entry != NULL) {
		if (prev != NULL) {
			prev->next = entry->next;
		} else {
			set->bins[bin_pos] = entry->next;
		}
		free(entry);
		set->num_entries--;
		/* TODO: is it a good idea to reduce the number of bins? */
	}
}

void
pointer_set_reset(PointerSet *set)
{
	free_bin_contents(set);
	set->bins = realloc(set->bins, sizeof(PointerSetEntry *) * DEFAULT_TABLE_SIZE);
	set->num_bins = DEFAULT_TABLE_SIZE;
	memset(set->bins, 0, sizeof(PointerSetEntry *) * DEFAULT_TABLE_SIZE);
}

unsigned int
pointer_set_get_size(PointerSet *set)
{
	return set->num_entries;
}

unsigned int
pointer_set_get_capacity(PointerSet *set)
{
	return set->num_bins;
}
