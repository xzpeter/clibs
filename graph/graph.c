#include <stdio.h>
#include <stdint.h>
#include <assert.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <sys/stat.h>
#include <unistd.h>
#include <sys/stat.h>
#include <string.h>
#include "graph.h"

/*********************************
 * Adjacency List Implementation *
 *********************************/

/* Free a whole list of links in one entry */
static void adj_link_free(Link *link)
{
    Link *next;

    while (link) {
        next = link->next;
        free(link);
        link = next;
    }
}

/* Add e2 into e1's link list */
static void adj_link_add_single(AdjList *list, Element e1, Element e2)
{
    Link *head = list->link_array[e1];
    Link *new = calloc(1, sizeof(Link));

    new->next = head;
    new->e = e2;
    list->link_array[e1] = new;
}

static void adj_link_dump(Link *link)
{
    int has = (link != NULL);

    while (link) {
        printf("%"PRIu64", ", link->e);
        link = link->next;
    }

    if (has)
        /* Erase the last ", " */
        printf("\b\b  \b\b");
}

/* Add a pair of link between element e1 and e2 */
void adj_link_add(AdjList *list, Element e1, Element e2)
{
    assert(e1 != e2);
    assert(e1 < list->size);
    assert(e2 < list->size);

    adj_link_add_single(list, e1, e2);
    adj_link_add_single(list, e2, e1);
}

/* Create an adjacency list with size */
static AdjList *adj_list_new(uint64_t size)
{
    AdjList *list = calloc(1, sizeof(AdjList));

    list->size = size;
    list->link_array = calloc(size, sizeof(Link *));

    return list;
}

/* When returned, the `list' pointer will be invalid */
void adj_list_free(AdjList *list)
{
    uint64_t i;

    for (i = 0; i < list->size; i++) {
        adj_link_free(list->link_array[i]);
    }
    free(list->link_array);
    free(list);
}

void adj_list_dump(AdjList *list)
{
    uint64_t i;

    for (i = 0; i < list->size; i++) {
        printf("%"PRIu64": ", i);
        adj_link_dump(list->link_array[i]);
        printf("\n");
    }
}

static int file_size(const char *file_name)
{
    struct stat st;

    if (stat(file_name, &st) == 0)
        return (st.st_size);
    else
        return -1;
}

/*
 * Return the uint64 value pointed by *buf and move *buf forward.  This
 * will move the *buf pointer!
 */
static void read_uint64(char **buf, uint64_t *val)
{
    char *old = *buf;

    *val = strtoul(*buf, buf, 10);
    /* Make sure buf moved */
    assert(old != *buf);
    /* Move over "\n" */
    assert(**buf == '\n');
    *buf += 1;
}

static void read_uint64_pair(char **buf, uint64_t *val1, uint64_t *val2)
{
    char *old = *buf;

    *val1 = strtoul(*buf, buf, 10);
    /* Make sure buf moved */
    assert(old != *buf);
    /* Move over "\n" */
    assert(**buf == ',');
    *buf += 1;

    old = *buf;
    *val2 = strtoul(*buf, buf, 10);
    /* Make sure buf moved */
    assert(old != *buf);
    /* Move over "\n" */
    assert(**buf == '\n');
    *buf += 1;
}

typedef void *(*InitOp)(uint64_t size);
typedef void (*AddPairOp)(void *data, Element e1, Element e2);

static void *parse_from_input(const char *input, InitOp init_op,
                              AddPairOp add_op)
{
    int fd = open(input, O_RDONLY);
    int ret, size;
    char *buf, *p;
    uint64_t val, val2;
    void *list;

    if (fd < 0)
        return NULL;

    size = file_size(input);
    p = buf = calloc(1, size + 1);

    do {
        ret = read(fd, p, size);
        assert(ret > 0);
        p += ret;
        size -= ret;
    } while (size);

    p = buf;
    read_uint64(&p, &val);
    list = init_op(val);

    while (*p) {
        read_uint64_pair(&p, &val, &val2);
        add_op(list, val, val2);
    }

    free(buf);

    return list;
}

static void *adj_list_init_op(uint64_t size)
{
    return adj_list_new(size);
}

static void adj_list_add_op(void *data, Element e1, Element e2)
{
    adj_link_add(data, e1, e2);
}

static void adj_list_add_single_op(void *data, Element e1, Element e2)
{
    adj_link_add_single(data, e1, e2);
}

AdjList *adj_list_from_input(const char *input)
{
    return parse_from_input(input, adj_list_init_op, adj_list_add_op);
}

AdjList *adj_dir_list_from_input(const char *path)
{
    /* This is single direction, use single add() */
    return parse_from_input(path, adj_list_init_op, adj_list_add_single_op);
}

/*****************************
 * Union Find Implementation *
 *****************************/

void uf_free(UnionFind *uf)
{
    uint64_t i, j, size = uf->size;
    Link *head, *next;

    /* This is only to free up the link_array[] */
    for (i = 0; i < size; i++) {
        head = uf->link_array[i];
        /*
         * Loop over each of the element, reset the uf link_array pointer
         * and also release each of the Link structure.  If head==NULL, it
         * means we've freed it before, so continue
         */
        while (head) {
            uf->link_array[head->e] = NULL;
            next = head->next;
            free(head);
            head = next;
        }
    }

    free(uf->link_array);
    free(uf);
}

/* Whether the two elements are connected? */
bool uf_connected(UnionFind *uf, Element e1, Element e2)
{
    return uf->link_array[e1] == uf->link_array[e2];
}

static void *uf_init_op(uint64_t size)
{
    UnionFind *uf = calloc(1, sizeof(UnionFind));
    uint64_t i;
    Link *link;

    uf->size = size;
    uf->unions = size;
    uf->link_array = calloc(size, sizeof(Link *));

    for (i = 0; i < size; i++) {
        link = calloc(1, sizeof(Link));
        link->e = i;
        link->next = NULL;
        uf->link_array[i] = link;
    }

    return uf;
}

static void uf_add_op(void *data, Element e1, Element e2)
{
    UnionFind *uf = data;
    Link *head, *tail, *link;

    /* If the two elements are already connected?  Nothing else to do! */
    if (uf_connected(uf, e1, e2))
        return;

    head = uf->link_array[e1];
    link = uf->link_array[e2];

    /* Find the tail of e1 link */
    tail = head;
    while (tail->next)
        tail = tail->next;

    /* Connect the two links for e1 and e2 */
    tail->next = link;

    /* Update each element in old e2's union to point to e1's head link */
    do {
        uf->link_array[link->e] = head;
        link = link->next;
    } while (link);

    /*
     * Accounting for the total unions count.  Since we just merged two,
     * total unions will decrease by 1
     */
    uf->unions--;

    /* Now they must be connected, or what? */
    assert(uf_connected(uf, e1, e2));
}

/* Create a UF with data specified in file `path' */
UnionFind *uf_from_input(const char *path)
{
    return parse_from_input(path, uf_init_op, uf_add_op);
}

/* How many unions are there? */
uint64_t uf_count(UnionFind *uf)
{
    return uf->unions;
}

/* Loop over the union elements that owns `e' */
void uf_union_foreach(UnionFind *uf, Element e, ElementOp op)
{
    Link *link = uf->link_array[e];

    do {
        op(link->e);
        link = link->next;
    } while (link);
}

/**********************
 * Depth First Search *
 **********************/

static void dfs_iter(AdjList *list, bool *marked, Element e)
{
    Link *link;

    /* If detected, stop here */
    if (marked[e]) {
        return;
    }

    marked[e] = true;
    link = list->link_array[e];
    while (link) {
        dfs_iter(list, marked, link->e);
        link = link->next;
    }
}

void dfs_foreach(AdjList *list, Element e, ElementOp op)
{
    bool *marked = calloc(list->size, sizeof(bool));
    Element i;

    /* Generate the marked array using depth-first search */
    dfs_iter(list, marked, e);

    for (i = 0; i < list->size; i++) {
        if (marked[i])
            op(i);
    }

    free(marked);
}

typedef struct {
    /* Used by both DFS and BFS */
    AdjList *list;
    bool *marked;
    Element target;

    /* Only used by DFS */
    ElementOp op;

    /* Only used in BFS */
    bool *to_search;
    bool *next_search;
    Element *edge_to;
} PathCtx;

static bool dfs_path_iter(PathCtx *ctx, Element cur)
{
    Link *link;
    bool result;

    /* This node is walked already */
    if (ctx->marked[cur])
        return false;

    /* We've found it! */
    if (cur == ctx->target) {
        ctx->op(cur);
        return true;
    }

    ctx->marked[cur] = true;
    link = ctx->list->link_array[cur];
    while (link) {
        result = dfs_path_iter(ctx, link->e);
        if (result) {
            ctx->op(cur);
            return true;
        }
        link = link->next;
    }

    return false;
}

bool dfs_path(AdjList *list, Element from, Element to, ElementOp op)
{
    bool *marked = calloc(list->size, sizeof(bool));
    PathCtx ctx = {
        .list = list,
        .marked = marked,
        .target = from,
        .op = op,
    };
    bool result;

    /*
     * Start searching from "to", so that the Op will be called in reversed
     * order to form a path in the right order
     */
    result = dfs_path_iter(&ctx, to);
    free(marked);

    return result;
}

static void dfs_order_iter(PathCtx *ctx, Element cur)
{
    Link *link;

    if (ctx->marked[cur])
        return;
    ctx->marked[cur] = true;

    link = ctx->list->link_array[cur];

    while (link) {
        dfs_order_iter(ctx, link->e);
        link = link->next;
    }

    ctx->op(cur);
}

void dfs_order(AdjList *list, ElementOp op)
{
    bool *marked = calloc(list->size, sizeof(bool));
    PathCtx ctx = {
        .list = list,
        .marked = marked,
        .op = op,
    };
    Element e;

    for (e = 0; e < list->size; e++) {
        dfs_order_iter(&ctx, e);
    }
    free(marked);
}

/************************
 * Breadth First Search *
 ************************/

/*
 * Try to search all elements set in ctx->to_search array to see whether we
 * found ctx->target.  If found, then good and we're done!  If not, update
 * ctx->to_search to reflect the next round of search and redo.
 */
bool bfs_path_iter(PathCtx *ctx)
{
    Element i, next_count = 0;
    bool *to_search = ctx->to_search;
    bool *next_search = ctx->next_search;
    bool result;
    Link *link;

    for (i = 0; i < ctx->list->size; i++) {
        if (!to_search[i])
            continue;
        if (i == ctx->target)
            /* We found it! */
            return true;
    }

    /*
     * Not found in current to_search direct links, try to establish the
     * next level of to_search elements.
     */
    for (i = 0; i < ctx->list->size; i++) {
        if (!to_search[i])
            continue;
        link = ctx->list->link_array[i];
        while (link) {
            if (!ctx->marked[link->e]) {
                ctx->marked[link->e] = true;
                next_search[link->e] = true;
                ctx->edge_to[link->e] = i;
                next_count++;
            }
            link = link->next;
        }
    }

    /*
     * Copy over the next level search elements into the global one, so
     * that we can reset the temp one.
     */
    memcpy(to_search, next_search, ctx->list->size * sizeof(bool));
    memset(next_search, 0, sizeof(bool) * ctx->list->size);

    if (!next_count) {
        /* this means the two elements are NOT connected! */
        return false;
    }

    return bfs_path_iter(ctx);
}

bool bfs_path(AdjList *list, Element from, Element to, ElementOp op)
{
    bool *marked = calloc(list->size, sizeof(bool));
    bool *to_search = calloc(list->size, sizeof(bool));
    bool *next_search = calloc(list->size, sizeof(bool));
    Element *edge_to = calloc(list->size, sizeof(Element));
    PathCtx ctx = {
        .list = list,
        .marked = marked,
        .target = from,
        .to_search = to_search,
        .next_search = next_search,
        .edge_to = edge_to,
    };
    bool result;

    /*
     * Start with an array with only "to" set; that's the place where we
     * start the searching.
     */
    to_search[to] = true;
    result = bfs_path_iter(&ctx);

    if (result) {
        do {
            op(from);
            from = edge_to[from];
        } while (to != from);
        op(from);
    }

    free(marked);
    free(to_search);
    free(next_search);
    free(edge_to);

    return result;
}
