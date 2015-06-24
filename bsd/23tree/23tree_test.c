#include <stdio.h>
#include <stdlib.h>
#include "23tree.h"

int main(void)
{
	int i = 0;
	tree23_t *tree = NULL;
	int array[100];
	const char *str = "hello";

	tree = tree23_create(NULL, NULL);
	puts("tree created");

	for (i = 0; i < 50; i++) {
		array[i] = i;
		printf("trying to insert data key %d\n", i);
		if (tree23_insert(tree, (tree23_key_t)&array[i],
				  (tree23_value_t)str)) {
			printf("insert failed for %d\n", i);
			return -1;
		}
	}

	tree23_dump(tree);

	return 0;
}
