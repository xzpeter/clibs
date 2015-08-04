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

#include "avl.h"
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

#define DEBUG_ENABLE

#ifdef DEBUG_ENABLE
#define debug printf
#else
#define debug
#endif

static void *avl_alloc(size_t size)
{
	void *ptr = calloc(1, size);
	assert(ptr);
	return ptr;
}

static void avl_free(void *ptr)
{
	if (ptr)
		free(ptr);
}

/* Create new AVL node and initialize */
static avl_node_t *avl_node_new(avl_node_t *parent, void *data)
{
	avl_node_t *new = avl_alloc(sizeof(*new));
	new->an_depth = 1;
	new->an_parent = parent;
	new->an_data = data;
	return new;
}

avl_tree_t *avl_create(avl_node_compare_fn cmp,
                       avl_node_data_collector destructor)
{
	avl_tree_t *tree = NULL;

	if (!cmp)
		return NULL;

	tree = avl_alloc(sizeof(*tree));
	tree->at_cmp = cmp;
	tree->at_dtor = destructor;
	tree->at_root = NULL;
}

int avl_empty(avl_tree_t *tree)
{
	assert(tree);
	if (tree->at_root)
		return 0;
	return 1;
}

#define max2(x,y) ((x)>(y)?(x):(y))

/* Replace the pointer in parent node of `node' to `new' */
static void avl_node_parent_redirect(avl_tree_t *tree, avl_node_t *node,
                                     avl_node_t *new)
{
	avl_node_t *parent = node->an_parent;
	if (!parent) {
		/* this is the root node. To replace the parent pointer, is just to
		   replace the root node of the tree */
		tree->at_root = new;
	} else if (parent->an_left == node) {
		parent->an_left = new;
	} else if (parent->an_right == node) {
		parent->an_right = new;
	} else {
		debug("FATAL: parent of %p does not contain itself\n", node);
		assert(0);
	}
	new->an_parent = parent;
}

static void avl_node_set_left(avl_node_t *node, avl_node_t *left)
{
	node->an_left = left;
	if (left)
		left->an_parent = node;
}

static void avl_node_set_right(avl_node_t *node, avl_node_t *right)
{
	node->an_right = right;
	if (right)
		right->an_parent = node;
}

static void avl_node_update_depth(avl_node_t *node)
{
	int left = 0, right = 0;
	if (node->an_left)
		left = node->an_left->an_depth;
	if (node->an_right)
		right = node->an_right->an_depth;
	node->an_depth = max2(left, right) + 1;
	debug("update node %p depth to %d\n", node, node->an_depth);
}

static void avl_node_update_depth_recursive(avl_tree_t *tree,
                                            avl_node_t *node)
{
	avl_node_t *k2 = NULL, *k3 = NULL;
	int scenario = 0;
	int left = 0, right = 0, diff = 0;

	left = right = diff = 0;
	if (node->an_left)
		left = node->an_left->an_depth;
	if (node->an_right)
		right = node->an_right->an_depth;

	/*
	 * Check whether we need to rebalance the tree. If rebalance is needed,
	 * then there are four possible scenarios:
	 *
	 * 1. new data inserted to node->an_left->an_left
	 * 2. new data inserted to node->an_left->an_right
	 * 3. new data inserted to node->an_right->an_left
	 * 4. new data inserted to node->an_right->an_right
	 *
	 * case (1) and (4) will require only single rotate, while
	 * case (2) and (3) will required double rotates
	 *
	 * In any case, we will set `scenario' to non-zero to show that we need
	 * rebalance and rotate of tree, with corresponding scenario number.
	 */
	diff = left - right;
	scenario = 0;
	if (diff == 2) {
		/* left sub-tree is heavier */
		k2 = node->an_left;
		assert(k2);
		if (!k2->an_left) {
			scenario = 2;
		} else if (!k2->an_right) {
			scenario = 1;
		} else {
			/* both childs of k2 is there */
			if (k2->an_left->an_depth > k2->an_right->an_depth) {
				scenario = 1;
			} else {
				scenario = 2;
			}
		}
	} else if (diff == -2) {
		/* right sub-tree is heavier */
		k2 = node->an_right;
		assert(k2);
		if (!k2->an_left) {
			scenario = 4;
		} else if (!k2->an_right) {
			scenario = 3;
		} else {
			if (k2->an_left->an_depth > k2->an_right->an_depth) {
				scenario = 3;
			} else {
				scenario = 4;
			}
		}
	}

	/* if we need rebalance... do it now. */
	switch (scenario) {
	case 1:
		k2 = node->an_left;
		/* now k2 will be the root of the sub-tree */
		avl_node_parent_redirect(tree, node, k2);
		avl_node_set_left(node, k2->an_right);
		avl_node_set_right(k2, node);
		/* we need to update depth in order */
		avl_node_update_depth(node);
		avl_node_update_depth(k2);
		/* update node pointer, should always points to root of current
		   subtree. */
		node = k2;
		break;
	case 2:
		k2 = node->an_left;
		k3 = k2->an_right;
		avl_node_parent_redirect(tree, node, k3);
		avl_node_set_right(k2, k3->an_left);
		avl_node_set_left(node, k3->an_right);
		avl_node_set_left(k3, k2);
		avl_node_set_right(k3, node);
		avl_node_update_depth(node);
		avl_node_update_depth(k2);
		avl_node_update_depth(k3);
		node = k3;
		break;
	case 3:
		k2 = node->an_right;
		k3 = k2->an_left;
		avl_node_parent_redirect(tree, node, k3);
		avl_node_set_right(node, k3->an_left);
		avl_node_set_left(k2, k3->an_right);
		avl_node_set_left(k3, node);
		avl_node_set_right(k3, k2);
		avl_node_update_depth(node);
		avl_node_update_depth(k2);
		avl_node_update_depth(k3);
		node = k3;
		break;
	case 4:
		k2 = node->an_right;
		avl_node_parent_redirect(tree, node, k2);
		avl_node_set_right(node, k2->an_left);
		avl_node_set_left(k2, node);
		avl_node_update_depth(node);
		avl_node_update_depth(k2);
		node = k2;
		break;
	default:
		/* no need to rebalance */
		avl_node_update_depth(node);
		break;
	}

	/* if node has parent, spread this update event to upper nodes */
	if (node->an_parent)
		avl_node_update_depth_recursive(tree, node->an_parent);
}

static int avl_node_insert(avl_tree_t *tree, avl_node_t *node, void *data)
{
	avl_node_compare_fn cmp = tree->at_cmp;
	int result = cmp(node->an_data, data);

	if (result == 0) {
		debug("data %p existed, insertion failed\n", data);
		return AVL_ERR_EXIST;
	} else if (result > 0) {
		if (!node->an_left) {
			avl_node_t *new = avl_node_new(node, data);
			node->an_left = new;
			debug("data %p inserted to left child of node %p\n",
				  data, node);
			avl_node_update_depth_recursive(tree, node);
			return 0;
		}
		avl_node_insert(tree, node->an_left, data);
	} else {
		/* data is bigger */
		if (!node->an_right) {
			avl_node_t *new = avl_node_new(node, data);
			node->an_right = new;
			debug("data %p inserted to right child of node %p\n",
				  data, node);
			avl_node_update_depth(node);
			avl_node_update_depth_recursive(tree, node);
			return 0;
		}
		avl_node_insert(tree, node->an_right, data);
	}
	return 0;
}

int avl_insert(avl_tree_t *tree, void *data)
{
	assert(tree);
	assert(data);
	if (avl_empty(tree)) {
		avl_node_t *new = avl_node_new(NULL, data);
		new->an_depth = 1;
		tree->at_root = new;
	} else {
		return avl_node_insert(tree, tree->at_root, data);
	}
	return 0;
}

void *avl_node_find(avl_tree_t *tree, avl_node_t *node, void *data)
{
	int result = 0;
	avl_node_compare_fn cmp = NULL;
	void *data_out = NULL;

	if (!node) {
		return NULL;
	}

	cmp = tree->at_cmp;
	result = cmp(node->an_data, data);
	if (result == 0) {
		data_out = node->an_data;
	} else if (result > 0) {
		data_out = avl_node_find(tree, node->an_left, data);
	} else {
		data_out = avl_node_find(tree, node->an_right, data);
	}

	return data_out;
}

void *avl_find(avl_tree_t *tree, void *data)
{
	assert(tree);
	if (!data) {
		return NULL;
	}
	return avl_node_find(tree, tree->at_root, data);
}

void avl_node_dump(avl_node_t *node, int level)
{
	int i = 0;

	if (!node) {
		return;
	}

	for (i = 0; i < level; i++) {
		printf("  ");
	}
	/* assuming INT */
	printf("%d\n", *(int *)node->an_data);
	avl_node_dump(node->an_left, level + 1);
	avl_node_dump(node->an_right, level + 1);
}

void avl_dump(avl_tree_t *tree)
{
	assert(tree);
	avl_node_dump(tree->at_root, 0);
}
