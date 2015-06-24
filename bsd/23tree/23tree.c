/*
 * Copyright (c) 2015, Peter Xu <xzpeter@gmail.com>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this
 * list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its contributors
 * may be used to endorse or promote products derived from this software without
 * specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <strings.h>
#include "23tree.h"

#define DEBUG_TREE23 1

#if DEBUG_TREE23
#define debug printf
#else
#define debug
#endif

#define NODE_IS_STABLE(node) (!node->data[2].key)

static void *__malloc(size_t size)
{
	void *ptr = calloc(1, size);
	assert(ptr);
	return ptr;
}

static void __free(void *ptr)
{
	if (ptr)
		free(ptr);
}

int tree23_key_cmp_func_int (tree23_key_t k1, tree23_key_t k2)
{
	int v1 = *(int *)k1;
	int v2 = *(int *)k2;

	if (v1 > v2)
		return 1;
	else if (v1 < v2)
		return -1;
	else
		return 0;
}

tree23_t *tree23_create(tree23_key_cmp_func_t cmp,
			tree23_data_handler_t destructor)
{
	tree23_t *tree = NULL;

	/* if no cmp function provided, using the default. */
	if (!cmp)
		cmp = tree23_key_cmp_func_int;

	/* destructor could be NULL */

	tree = __malloc(sizeof(*tree));
	tree->root = NULL;
	tree->cmp = cmp;
	tree->destructor = destructor;

	return tree;
}

/* create one new uninitialized node */
static tree23_node_t *node_create(tree23_t *tree, int is_leaf)
{
	tree23_node_t *node = NULL;
	assert(tree);
	node = __malloc(sizeof(*node));
	bzero(node, sizeof(*node));
	node->tree = tree;
	node->is_leaf = is_leaf;
	debug("creating node %p\n", node);
	return node;
}

/* free one node (recursively, with data) */
void node_destroy(tree23_node_t *node)
{
	tree23_data_handler_t destructor = node->tree->destructor;
	int i = 0;
	debug("destoying node %p\n", node);
	for (i = 0; i < 4; i++)
		if (node->childs[i])
			node_destroy(node->childs[i]);
	if (destructor)
		for (i = 0; i < 3; i++)
			if (node->data[i].key)
				destructor(&node->data[i]);
	__free(node);
}

void tree23_destroy(tree23_t *tree)
{
	if (tree->root)
		node_destroy(tree->root);
	__free(tree);
}

/*
 * insert key/value into one node. If hint is provided and valid (0<=hint<3),
 * then will be used directly. Otherwise, we will try to find the suitable
 * place for the pair. If this node is internal node, `new_child' should also
 * be provided. Return zero for success.
 */
static int node_insert_data(tree23_node_t *node, tree23_key_t key,
			    tree23_value_t value, int hint,
			    tree23_node_t *new_child)
{
	tree23_t *tree = NULL;
	tree23_key_cmp_func_t cmp = NULL;

	assert(node);
	assert(node->tree);
	/* this could only be node-2 or node-3 type. */
	assert(NODE_IS_STABLE(node));

	tree = node->tree;
	cmp = tree->cmp;
	
	if (hint < 0 || hint >= 3) {
		/* this is invalid hint, we should find it ourself. */
		hint = 0;
		while (hint < 3) {
			if (node->data[hint].key == NULL)
				/* end of current array */
				break;
			int result = cmp(node->data[hint].key, key);
			if (result == 0) {
				/* duplicated key */
				debug("insert fail, dup key: %p\n", key);
				return -1;
			}
			if (result > 0)
				/* we should insert key here */
				break;
			/* else, current hint is less than key */
			hint++;
		}
	}

	if (hint <= 1) {
		/* for 0,1 index, moving data[1] to [2] */
		node->data[2] = node->data[1];
		/* ONLY USED FOR INTERNAL NODE */
		node->childs[3] = node->childs[2];
	}
	if (hint == 0) {
		node->data[1] = node->data[0];
		/* ONLY USED FOR INTERNAL NODE */
		node->childs[2] = node->childs[1];
	}
	/* shifting done, just insert to right place */
	node->data[hint].key = key;
	node->data[hint].value = value;

	/* if this is a internal node, the node->childs[hint] is still
	 * missing! */
	if (!node->is_leaf) {
		assert(new_child);
		debug("insert into internal node %p, binding new child"
		      " %p at child index %d\n", node, new_child, hint+1);
		node->childs[hint+1] = new_child;
	} else
		/* leaf node insertion should have no new child */
		assert(!new_child);

	debug("inserting key %d to index %d of node %p\n",
	      *(int *)key, hint, node);

	return 0;
}

static inline void node_data_clear(tree23_data_t *data)
{
	data->key = NULL;
	data->value = NULL;
}

/* split one 4-node type into two 2-node type, with one extra data which would
 * be passed upward to parent. */
static int node_split(tree23_node_t *node)
{
	int ret = 0;
	tree23_node_t *new_node = NULL;

	assert(node);
	assert(!NODE_IS_STABLE(node));

	debug("splitting node %p\n", node);

	/* creating the new node with the same type (splitting) */
	new_node = node_create(node->tree, node->is_leaf);
	new_node->parent = node->parent;

	/* assume current node is N1=|A|B|C|, after split, we should got
	 * N1=|A| and N2=|C|, while passing |B| upward to parent. */
	new_node->data[0] = node->data[2];

	/* when passing B upward, we should make sure this is not root
	 * node. If so, we need to update root node. */
	if (!node->parent) {
		/* this is root node! let's create the new root node. */
		tree23_node_t *new_root_node = NULL;
		/* let us double confirm! */
		assert(node->tree->root == node);
		new_root_node = node_create(node->tree, 0);
		new_root_node->parent = NULL;
		new_root_node->data[0] = node->data[1];
		new_root_node->childs[0] = node;
		new_root_node->childs[1] = new_node;
		node->tree->root = new_root_node;

		/* also update the parent of childs */
		node->parent = new_node->parent = new_root_node;
	} else {
		/* this is non-root internal node, insert B into parent
		 * node. */
		ret = node_insert_data(node->parent, node->data[1].key,
				       node->data[1].value, -1 /* no hint */,
				       new_node);
		if (ret)
			return ret;
	}

	/* do child cleanup. for either left/right child node after split,
	 * it should only contain childs[0] and childs[1]. */
	node_data_clear(&node->data[1]);
	node_data_clear(&node->data[2]);
	new_node->childs[0] = node->childs[2];
	new_node->childs[1] = node->childs[3];
	node->childs[2] = node->childs[3] = NULL;

	/* we have finished the splitting of current node. Let us see whether
	 * the split need to be spreaded upward. */
	if (!NODE_IS_STABLE(node->parent))
		node_split(node->parent);

	return 0;
}

/* return 1 if there are duplicated key in node. */
static int node_has_duplicate_key(tree23_node_t *node, tree23_key_t key)
{
	tree23_key_cmp_func_t cmp = node->tree->cmp;
	assert(key);
	assert(NODE_IS_STABLE(node));
	if (node->data[0].key && cmp(node->data[0].key, key) == 0)
		return 1;
	if (node->data[1].key && cmp(node->data[1].key, key) == 0)
		return 1;
	return 0;
}

/* insert key/value pair into specific 2-3 tree under node. Return non-zero if
 * failed. */
static int node_insert(tree23_node_t *node, tree23_key_t key,
		       tree23_value_t value)
{
	tree23_key_cmp_func_t cmp = NULL;
	int ret = 0;
	assert(node);
	assert(NODE_IS_STABLE(node));

	if (node_has_duplicate_key(node, key)) {
		debug("key %p duplicated in node %p\n", key, node);
		return -1;
	}

	if (node->is_leaf) {
		/* if this is leaf node, just insert the data, and see whether
		 * there needs a "promote". */
		ret = node_insert_data(node, key, value, -1, NULL);
		if (ret)
			return ret;
		/* if this is not a stable state, try to split this leaf
		 * node. */
		if (!NODE_IS_STABLE(node))
			ret = node_split(node);
	} else {
		/* this is an internal node. */
		tree23_node_t *next = NULL;
		cmp = node->tree->cmp;
		if (cmp(node->data[0].key, key) > 0)
			next = node->childs[0];
		else if (node->data[1].key == NULL ||
			 cmp(node->data[1].key, key) > 0)
			next = node->childs[1];
		else
			next = node->childs[2];
		debug("looking into node %p child %p\n", node, next);
		ret = node_insert(next, key, value);
	}

	return ret;
}

/* return 1 if empty, else 0 */
int tree23_empty(tree23_t *tree)
{
	assert(tree);
	if (tree->root)
		return 0;
	else
		return 1;
}

int tree23_insert(tree23_t *tree, tree23_key_t key, tree23_value_t value)
{
	assert(tree);
	/* we could allow "value" be NULL, but not key. */
	assert(key);

	/* if the tree is empty, create the first leaf node. This should the
	 * only place that we manually create (rather than split) one node. */
	if (tree23_empty(tree)) {
		tree23_node_t *node = node_create(tree, 1);
		node->data[0].key = key;
		node->data[0].value = value;
		/* this is the root node */
		node->parent = NULL;
		tree->root = node;
		debug("creating first leaf with key %d for tree\n", *(int *)key);
		return 0;
	}

	/* then there are something.. insert into subtree. */
	return node_insert(tree->root, key, value);
}

/* assuming value is string */
static void node_dump_value(tree23_node_t *node, int index, int indent)
{
	int i = 0;
	tree23_data_t *data = &node->data[index];
	/* skip empty entry */
	if (!data->key)
		return;
	for (i = 0; i < indent; i++)
		printf("    ");
	printf("key: %d, value: %s\n", *(int *)data->key,
	       (char *)data->value);
}

/* helper function for tree23_dump(). */
static void node_dump(tree23_node_t *node, int indent)
{
	if (node->childs[0])
		node_dump(node->childs[0], indent + 1);
	node_dump_value(node, 0, indent);
	if (node->childs[1])
		node_dump(node->childs[1], indent + 1);
	node_dump_value(node, 1, indent);
	if (node->childs[2])
		node_dump(node->childs[2], indent + 1);

	/* these should not dump anything for stable nodes. */
	node_dump_value(node, 2, indent);
	if (node->childs[3])
		node_dump(node->childs[3], indent + 1);
}

void tree23_dump(tree23_t *tree)
{
	assert(tree);
	if (!tree->root)
		printf("tree is empty\n");
	else
		node_dump(tree->root, 0);
}
