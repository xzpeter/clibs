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

#ifndef __23TREE_H__
#define __23TREE_H__

/*
 * one implementation of 2-3 tree:
 * https://en.wikipedia.org/wiki/2%E2%80%933_tree
 */

/* this defines the format of key in the tree. */
typedef void *tree23_key_t;
/* this defines the format of data binded with each key. */
typedef void *tree23_value_t;

typedef struct tree23_data {
	tree23_key_t key;
	tree23_value_t value;
} tree23_data_t ;

/* this could be any handler (e.g. destructor) for data type. */
typedef void (*tree23_data_handler_t)(tree23_data_t *);
/* comparison function. Return 1 if k1>k2, -1 if k1<k2 else 0. */
typedef int (*tree23_key_cmp_func_t)(tree23_key_t k1, tree23_key_t k2);

struct tree23;
struct tree23_nodes;

/* 
 * To classify nodes by type:
 * 1. internal node: who has childs, and
 * 2. leaf node: who has no childs.
 *
 * To classify nodes by data number:
 * 1. 2-node: 1 data (might with 2 childs or none)
 * 2. 3-node: 2 data (might with 3 childs or none)
 * 3. 4-node: 3 data (might with 4 childs or none)
 * Here, only 2-node and 3-node are stable nodes. 4-node is unstable and should
 * be splitted into two stable ones.
 */
struct tree23_node {
	/* this points to the tree that this node belongs to. */
	struct tree23 *tree;
	/* set this if this node is a leaf node. */
	int is_leaf;
	/* there could be 1/2/3 data in this node. 3 data is unstable. */
	tree23_data_t data[3];
	/* only valid when this node is not leaf node. */
	struct tree23_node *childs[4];
	/* this points to the parent of this node. If this is the root node,
	 * parent will be NULL. */
	struct tree23_node *parent;
};

struct tree23 {
	/* this is the root node of the 2-3 tree. When first created, it is
	 * NULL. */
	struct tree23_node *root;
	/* the comparison function between keys. */
	tree23_key_cmp_func_t cmp;
	/* data destructor for data */
	tree23_data_handler_t destructor;
};

typedef struct tree23 tree23_t;
typedef struct tree23_node tree23_node_t;

/* create one empty 2-3 tree. When destructor is null, then data destruction
 * will be omitted during tree destruction. Setting functions with NULL to use
 * the default ones. */
tree23_t *tree23_create(tree23_key_cmp_func_t cmp,
			tree23_data_handler_t destructor);

/* TBD: destroy one 2-3 tree recursively (including data) */
void tree23_destroy(tree23_t *tree);

/* whether the tree is empty or not */
int tree23_empty(tree23_t *tree);

/* insert a new item into the tree. */
int tree23_insert(tree23_t *tree, tree23_key_t key, tree23_value_t value);

/* TBD: search a specific key. If nothing found, return NULL. */
tree23_value_t tree23_lookup(tree23_t *tree, tree23_key_t key);

/* TBD: traverse a tree (with sorted order). Handler will be called for each
 * item. */
int tree23_traverse(tree23_t *tree, tree23_data_handler_t handler);

/* dump a tree with indentation. */
void tree23_dump(tree23_t *tree);

/* TBD: remove an item from the tree */
int tree23_delete(tree23_t *tree, tree23_key_t key);

#endif
