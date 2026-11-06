#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include "dlist.h"

bool debuglog = false;

struct value { unsigned int key, order; };

// Count comparisons as well as checking the comparator's sign contract.
static int compare(dlist *a, dlist *b, void *data)
{
	(*(size_t *) data)++;
	struct value *left = a->data, *right = b->data;
	return left->key < right->key ? -7 : left->key > right->key ? 7 : 0;
}

// Verify stable ordering, both links, node identities and subquadratic work.
static void check(unsigned int count, unsigned int pattern)
{
	if (!count) {
		assert(!dlist_sort(NULL, compare, NULL));
		return;
	}
	dlist *nodes = calloc(count, sizeof(*nodes));
	struct value *values = calloc(count, sizeof(*values));
	assert(nodes && values);
	uint32_t random = 7;
	for (unsigned int i = 0; i < count; i++) {
		random = random * 1664525u + 1013904223u;
		values[i].key = pattern == 0 ? i : pattern == 1 ? count-i : pattern == 2 ? random % 31 : 0;
		values[i].order = i;
		nodes[i].data = &values[i];
		nodes[i].prev = i ? &nodes[i-1] : NULL;
		nodes[i].next = i+1 < count ? &nodes[i+1] : NULL;
	}
	size_t comparisons = 0;
	dlist *head = dlist_sort(&nodes[count/2], compare, &comparisons), *previous = NULL;
	assert(head && !head->prev);
	unsigned int visited = 0;
	for (dlist *node = head; node; node = node->next) {
		assert(++visited <= count && node->prev == previous);
		struct value *value = node->data;
		assert(node >= nodes && node < nodes+count);
		assert(value == &values[node-nodes]);
		if (previous) {
			struct value *before = previous->data;
			assert(before->key <= value->key);
			if (before->key == value->key) assert(before->order < value->order);
		}
		previous = node;
	}
	assert(visited == count && !previous->next);
	unsigned int levels = 1;
	for (unsigned int n = count; n > 1; n >>= 1) levels++;
	assert(comparisons <= (size_t) count * levels);
	free(values);
	free(nodes);
}

int main(void)
{
	unsigned int sizes[] = {0, 1, 2, 3, 31, 128, 8192};
	for (unsigned int i = 0; i < sizeof(sizes)/sizeof(sizes[0]); i++)
		for (unsigned int pattern = 0; pattern < 4; pattern++) check(sizes[i], pattern);
	puts("Stable merge-sort ordering, links, identity and comparison bounds passed.");
	return 0;
}
