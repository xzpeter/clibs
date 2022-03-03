#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <assert.h>
#include "23tree.h"

static char line_buffer[1024];

void print_help(void)
{
	puts("Please input your selection:");
	puts("1. insert key/value");
	puts("2. dump tree");
	puts("3. destroy tree (create new empty one)");
	puts("4. lookup key");
	puts("5. exit");
	printf("Input: ");
}

char *read_line(void)
{
	char *p = line_buffer;
	while (1) {
		*p = getchar();
		if (*p == '\n' || (p - line_buffer) >= 1023) {
			*p = 0x00;
			break;
		}
		p++;
	}
	return line_buffer;
}

void data_destructor(tree23_data_t *data)
{
	printf("freeing key %d\n", *(int *)data->key);
	free(data->key);
	printf("freeing string '%s'\n", (char *)data->value);
	free(data->value);
}

int main(void)
{
	int ret = 0;
	tree23_t *tree = NULL;
	char *value = NULL;
	int *key_ptr = NULL;
	tree23_data_t *data = NULL;

	tree = tree23_create(NULL, data_destructor);
	assert(tree);

	while (1) {
		int selection;
		int key;

		print_help();
		selection = atoi(read_line());
		if (selection > 5 || selection < 1) {
			puts("Invalid input.");
			continue;
		}

		switch (selection) {
		case 1:
			printf("Please input key (integer for now): ");
			key = atoi(read_line());
			if (key <= 0) {
				puts("invalid key (should > 0)");
				continue;
			}
			key_ptr = malloc(sizeof(int));
			*key_ptr = key;
			printf("Please input value (one-line string): ");
			value = strdup(read_line());
			ret = tree23_insert(tree, key_ptr, value);
			if (ret) {
				puts("failed insert key/value!");
				return -1;
			}
			printf("Key %d value '%s' inserted.\n", key, value);
			break;
		case 2:
			puts("===================");
			tree23_dump(tree);
			puts("===================");
			break;
		case 3:
			tree23_destroy(tree);
			tree = tree23_create(NULL, data_destructor);
			break;
		case 4:
			printf("Please input key: ");
			key = atoi(read_line());
			data = tree23_lookup(tree, &key);
			if (data)
				printf("Found key %d, value '%s'\n",
				       key, (char *)data->value);
			else
				printf("Key %d not found.\n", key);
			break;
		case 5:
			puts("Quitting.");
			tree23_destroy(tree);
			return 0;
			/* NOT REACH */
			break;
		}
	}

	return 0;
}
