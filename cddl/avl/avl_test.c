#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include "avl.h"

typedef struct queue {
	avl_tree_t q_tree;
} queue_t;

typedef struct queue_item {
	avl_node_t q_item_link;
	int q_item_value;
} queue_item_t;

int
queue_compare_fn (const void *item1, const void *item2)
{
	queue_item_t *p1, *p2;
	int v1, v2;
	p1 = (queue_item_t *)item1;
	p2 = (queue_item_t *)item2;
	v1 = p1->q_item_value;
	v2 = p2->q_item_value;

	if (v1 < v2)
		return -1;
	else if (v1 > v2)
		return 1;
	if (p1 < p2)
		return -1;
	else if (p1 > p2)
		return 1;

	return 0;
}

void *
alloc (size_t size)
{
	return calloc(1, size);
}

void
usage (void)
{
	puts("usage: avl_test [int1 [int2...]]");
	exit(0);
}

int
main(int argc, char *argv[])
{
	queue_t *queue = NULL;
	queue_item_t *item = NULL;
	int index = 0, value = 0;

	queue = alloc(sizeof(*queue));
	assert(queue);
	avl_create(&queue->q_tree, &queue_compare_fn, sizeof(queue_item_t),
		   offsetof(queue_item_t, q_item_link));

	if (argc == 1)
		usage();

	index = 1;
	printf("Constructing the tree...\n");
	while (index < argc) {
		value = atoi(argv[index]);
		item = alloc(sizeof(*item));
		item->q_item_value = value;
		printf("Adding value: %d\n", value);
		avl_add(&queue->q_tree, item);
		index++;
	}

	printf("Construction complete. Dumping tree:\n");
	
	item = (queue_item_t *)avl_first(&queue->q_tree);
	if (!item) {
		printf("The tree is empty. Stop here.\n");
		return 0;
	}

	do {
		printf("%d ", item->q_item_value);
	} while ((item = AVL_NEXT(&queue->q_tree, item)));

	printf("\n");

	return 0;
}
