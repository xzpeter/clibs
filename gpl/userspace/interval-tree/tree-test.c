#include <stdio.h>
#include <assert.h>
#include "interval-tree.h"

gboolean iterator(ITValue start, ITValue end)
{
    printf("[%llu, %llu] ", start, end);
    return FALSE;
}

void it_tree_dump(ITTree *tree)
{
    printf("Tree: ");
    it_tree_foreach(tree, iterator);
    printf("\n");
}

int main(void)
{
    ITTree *tree;

    printf("Test inserting isolated ranges, and merge of ranges:\n");
    tree = it_tree_new();
    g_assert(it_tree_insert(tree, 10, 19) == 0);
    g_assert(it_tree_insert(tree, 40, 59) == 0);
    g_assert(it_tree_insert(tree, 80, 99) == 0);
    g_assert(it_tree_insert(tree, 20, 39) == 0);
    g_assert(it_tree_insert(tree, 25, 29) < 0);
    g_assert(it_tree_find(tree, 85, 89) != NULL);
    g_assert(it_tree_find(tree, 120, 129) == NULL);
    it_tree_dump(tree);
    it_tree_destroy(tree);

    printf("Test removing ranges:\n");
    tree = it_tree_new();
    g_assert(it_tree_insert(tree, 10, 19) == 0);
    g_assert(it_tree_insert(tree, 40, 59) == 0);
    g_assert(it_tree_insert(tree, 80, 99) == 0);
    g_assert(it_tree_remove(tree, 0, 69) == 0);
    g_assert(it_tree_remove(tree, 85, 89) == 0);
    it_tree_dump(tree);
    it_tree_destroy(tree);

    /*
    printf("Test removing ranges:\n");
    tree = it_tree_new();
    it_tree_dump(tree);
    it_tree_destroy(tree);
    */

    return 0;
}
