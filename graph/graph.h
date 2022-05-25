#ifndef __GRAPH_H__
#define __GRAPH_H__

#include <inttypes.h>
#include <stdlib.h>
#include <stdbool.h>

typedef uint64_t Element;
typedef void (*ElementOp)(Element e);

/******************************
 * Adjacency List Definitions *
 ******************************/

struct _Link;
typedef struct _Link Link;

struct _Link {
    Element e;
    Link *next;
};

typedef struct {
    Link **link_array;
    /* Size of the link_array */
    uint64_t size;
} AdjList;

/* Create a new adjacency list with data specified in file `path' */
AdjList *adj_list_from_input(const char *path);
/* adjancency list with directions */
AdjList *adj_dir_list_from_input(const char *path);
/* Free an adjacancy list */
void adj_list_free(AdjList *list);
/* Add a link between two elements e1/e2 */
void adj_link_add(AdjList *list, Element e1, Element e2);
/* Dump data in one adjacency list */
void adj_list_dump(AdjList *list);

/**************************
 * Union Find Definitions *
 **************************/

typedef struct {
    Link **link_array;
    /* Size of the link_array */
    uint64_t size;
    uint64_t unions;
} UnionFind;

/* Create UF with data specified in file `path' */
UnionFind *uf_from_input(const char *path);
/* Free an union find structure */
void uf_free(UnionFind *uf);
/* How many unions are there? */
uint64_t uf_count(UnionFind *uf);
/* Whether the two elements are connected? */
bool uf_connected(UnionFind *uf, Element e1, Element e2);
/* Loop over the union elements that owns `e' */
void uf_union_foreach(UnionFind *uf, Element e, ElementOp op);

/**********************
 * Depth First Search *
 **********************/

/*
 * Search for union that element e belongs, and call the hook for each of
 * the element in the union
 */
void dfs_foreach(AdjList *list, Element e, ElementOp op);
/*
 * Find a path from element `from' to element `to'.  For each of the
 * element on the path, op() is called.  Return true if found, false if
 * `from' and `to' do not connect at all.
 */
bool dfs_path(AdjList *list, Element from, Element to, ElementOp op);

/*
 * Generate topology order of a specific adjacancy list (e.g. makefile
 * dependencies on src files)
 */
void dfs_order(AdjList *list, ElementOp op);

/************************
 * Breadth First Search *
 ************************/

/* Same as dfs_path() but find shortest path */
bool bfs_path(AdjList *list, Element from, Element to, ElementOp op);

#endif /* __GRAPH_H__ */
