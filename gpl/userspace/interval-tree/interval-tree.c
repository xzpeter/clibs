/*
 * An very simplified interval tree implementation based on GTree.
 *
 * Copyright 2018 Red Hat, Inc.
 *
 * Authors:
 *  Peter Xu <peterx@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 */

#include <glib.h>
#include "interval-tree.h"

/*
 * Each element of the internal tree is an ITRange.  It is shared
 * between the key and value of the element, or we can see it a tree
 * with keys only but no values.
 */

struct ITTree {
    GTree *tree;
};

static int it_tree_compare(gconstpointer a, gconstpointer b, gpointer data)
{
    const ITRange *r1 = a, *r2 = b;

    if (r1->start > r2->end) {
        return 1;
    }

    if (r1->end < r2->start) {
        return -1;
    }

    /* Overlapped */
    return 0;
}

ITTree *it_tree_new(void)
{
    ITTree *ittree = g_new0(ITTree, 1);

    /* We don't have values actually, no need to free */
    ittree->tree = g_tree_new_full(it_tree_compare, NULL, g_free, NULL);

    return ittree;
}

ITRange *it_tree_find(ITTree *tree, ITValue start, ITValue end)
{
    ITRange range;

    g_assert(tree);

    range.start = start;
    range.end = end;

    return g_tree_lookup(tree->tree, &range);
}

ITRange *it_tree_find_value(ITTree *tree, ITValue value)
{
    return it_tree_find(tree, value, value);
}

int it_tree_insert(ITTree *tree, ITValue start, ITValue end)
{
    ITRange *range = g_new0(ITRange, 1), *overlap;
    GTree *gtree;

    g_assert(tree);
    g_assert(start <= end);

    gtree = tree->tree;

    range->start = start;
    range->end = end;

    /* We don't allow to insert range that overlaps with existings */
    if (g_tree_lookup(gtree, range)) {
        g_free(range);
        return IT_ERR_OVERLAP;
    }

    /* Merge left adjacent range */
    overlap = it_tree_find_value(tree, start - 1);
    if (overlap) {
        range->start = overlap->start;
        g_tree_remove(gtree, overlap);
    }

    /* Merge right adjacent range */
    overlap = it_tree_find_value(tree, end + 1);
    if (overlap) {
        range->end = overlap->end;
        g_tree_remove(gtree, overlap);
    }

    /* Key and value are sharing the same range data */
    g_tree_insert(gtree, range, range);

    return IT_OK;
}

static gboolean it_tree_traverse(gpointer key, gpointer value,
                                gpointer data)
{
    it_tree_iterator iterator = data;
    ITRange *range = key;
    
    g_assert(key == value);

    return iterator(range->start, range->end);
}

void it_tree_foreach(ITTree *tree, it_tree_iterator iterator)
{
    g_assert(tree && iterator);
    g_tree_foreach(tree->tree, it_tree_traverse, iterator);
}

int it_tree_remove(ITTree *tree, ITValue start, ITValue end)
{
    ITRange range = { .start = start, .end = end }, *overlap;
    GTree *gtree;

    g_assert(tree);

    gtree = tree->tree;
    while ((overlap = g_tree_lookup(gtree, &range))) {
        g_tree_remove(gtree, overlap);
    }

    return IT_OK;
}

void it_tree_destroy(ITTree *tree)
{
    g_tree_destroy(tree->tree);
    g_free(tree);
}
