#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include "list.h"

typedef struct queue {
	list_t q_list;
} queue_t;

typedef struct queue_item {
	list_node_t q_item_link;
	int q_item_value;
} queue_item_t;

void *
alloc (size_t size)
{
	return calloc(1, size);
}

void
usage (void)
{
	puts("usage: list_test [int1 [int2...]]");
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
	list_create(&queue->q_list, sizeof(queue_item_t),
		    offsetof(queue_item_t, q_item_link));

	if (argc == 1)
		usage();

	index = 1;
	printf("Constructing the list...\n");
	while (index < argc) {
		value = atoi(argv[index]);
		item = alloc(sizeof(*item));
		item->q_item_value = value;
		printf("Adding value: %d\n", value);
		list_insert_tail(&queue->q_list, item);
		index++;
	}

	printf("Construction complete. Dumping list:\n");
	
	item = (queue_item_t *)list_head(&queue->q_list);
	if (!item) {
		printf("The list is empty. Stop here.\n");
		return 0;
	}

	do {
		printf("%d ", item->q_item_value);
	} while ((item = list_next(&queue->q_list, item)));

	printf("\n");

	return 0;
}
