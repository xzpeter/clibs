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

#ifndef __AVL_H__
#define __AVL_H__

enum avl_errno {
    AVL_OK = 0,                 /* no error */
    AVL_ERR_EXIST = 1,          /* node exist */
};

/* Node of AVL tree. It could be internal node, or leaf node. */
struct avl_node {
    void *an_data;
    int an_depth;
    struct avl_node *an_parent;
    struct avl_node *an_left;
    struct avl_node *an_right;
};
typedef struct avl_node avl_node_t;

/* Comparing function definition between two AVL node data. Return 1 if x>y, 0
   if x==y, or -1 if x<y. */
typedef int (*avl_node_compare_fn) (void *x, void *y);
typedef void (*avl_node_data_collector) (void *x);

/* The AVL tree. It could contain zero, one or more AVL nodes. */
struct avl_tree {
    /* comparing function of node data */
    avl_node_compare_fn at_cmp;
    /* destructor of node data */
    avl_node_data_collector at_dtor;
    avl_node_t *at_root;
};
typedef struct avl_tree avl_tree_t;

/* Creating one AVL tree. `cmp' is required, `destructor' is optional. */
avl_tree_t *avl_create(avl_node_compare_fn cmp,
                       avl_node_data_collector destructor);

/* Check whether the tree is empty. Return non-zero if empty, else 0. */
int avl_empty(avl_tree_t *tree);

/* Insert item into tree */
int avl_insert(avl_tree_t *tree, void *data);

/* Find item in tree. Return the found item if exists, else return NULL. */
void *avl_find(avl_tree_t *tree, void *data);

/* DEBUG: Dump the tree with sorted order. */
void avl_dump(avl_tree_t *tree);

/* TODO: Delete existing item in the tree */
int avl_delete(avl_tree_t *tree, void *data);

/* TODO: destroy the tree */
void avl_destroy(avl_tree_t *tree);

#endif
