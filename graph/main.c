#include <stdio.h>
#include <string.h>
#include <assert.h>
#include "graph.h"

int get_int(void)
{
    char buf[128], c, *p;
    int len = sizeof(buf)-1;

    p = buf;
    while (len--) {
        c = getchar();
        if (c == '\n')
            break;
        *p++ = c;
    }

    assert(len != 0);
    *p = '\0';

    return atoi(buf);
}

typedef int (*CmdFn)(const char *file);

static int cmd_adj_list(const char *file)
{
    AdjList *list;

    list = adj_list_from_input(file);

    if (!list) {
        printf("Failed to read input file: %s\n", file);
        return -1;
    }

    adj_list_dump(list);
    adj_list_free(list);

    return 0;
}

static void element_print_op(Element e)
{
    printf("%"PRIu64", ", e);
}

static void element_print_path_op(Element e)
{
    printf("%"PRIu64" -> ", e);
}

static int cmd_uf(const char *file)
{
    UnionFind *uf = uf_from_input(file);

    if (!uf) {
        printf("Failed to create UF: %s\n", file);
        return -1;
    }

    printf("Total unions: %"PRIu64"\n", uf_count(uf));

    while (1) {
        int val1;

        printf("Please input one element:\n");
        val1 = get_int();
        if (val1 >= uf->size) {
            printf("This element does not exist, please retry\n");
            continue;
        }
        printf("Element '%d' belongs to union: [", val1);
        uf_union_foreach(uf, val1, element_print_op);
        printf("\b\b]\n");
    }
    uf_free(uf);

    return 0;
}

static int cmd_dfs(const char *file)
{
    AdjList *list = adj_list_from_input(file);

    if (!list) {
        printf("Failed to read input file: %s\n", file);
        return -1;
    }

    while (1) {
        int val1;

        printf("Please input one element:\n");
        val1 = get_int();
        if (val1 >= list->size) {
            printf("This element does not exist, please retry\n");
            continue;
        }
        printf("Element '%d' belongs to union: [", val1);
        dfs_foreach(list, val1, element_print_op);
        printf("\b\b]\n");
    }
}

static int cmd_dfs_order(const char *file)
{
    AdjList *list = adj_dir_list_from_input(file);

    if (!list) {
        printf("Failed to read input file: %s\n", file);
        return -1;
    }

    dfs_order(list, element_print_op);
    printf("\b\b \n");
    adj_list_free(list);

    return 0;
}

typedef bool (*XfsPath)(AdjList *list, Element from, Element to, ElementOp op);

static int do_xfs_path(const char *file, XfsPath path_fn)
{
    AdjList *list = adj_list_from_input(file);

    if (!list) {
        printf("Failed to read input file: %s\n", file);
        return -1;
    }

    while (1) {
        int val1, val2;
        bool result;

        printf("Please input element FROM:\n");
        val1 = get_int();
        if (val1 >= list->size) {
            printf("This element does not exist, please retry\n");
            continue;
        }
        printf("Please input element TO:\n");
        val2 = get_int();
        if (val2 >= list->size) {
            printf("This element does not exist, please retry\n");
            continue;
        }
        printf("Path from %d to %d: ", val1, val2);
        result = path_fn(list, val1, val2, element_print_path_op);
        if (result)
            printf("\b\b\b\b   \b\b\b\n");
        else
            printf("Not found!\n");
    }
}

static int cmd_dfs_path(const char *file)
{
    return do_xfs_path(file, dfs_path);
}

static int cmd_bfs_path(const char *file)
{
    return do_xfs_path(file, bfs_path);
}

typedef struct {
    char *cmd_name;
    CmdFn cmd_fn;
} CmdEntry;

CmdEntry cmd_list[] = {
    { "adj_list", cmd_adj_list },
    { "uf", cmd_uf },
    { "dfs", cmd_dfs },
    { "dfs_path", cmd_dfs_path },
    { "dfs_order", cmd_dfs_order },
    { "bfs_path", cmd_bfs_path },
    { NULL, NULL },
};

static void dump_cmds(void)
{
    CmdEntry *entry = cmd_list;

    printf("Available commands: ");
    while (entry->cmd_name) {
        printf("%s, ", entry->cmd_name);
        entry++;
    }
    printf("\b\b \b\n");
}

int main(int argc, const char *argv[])
{
    CmdEntry *entry = cmd_list;

    if (argc != 3) {
        printf("usage: %s <cmd> <input_file>\n", argv[0]);
        dump_cmds();
        return -1;
    }

    while (entry->cmd_name) {
        if (!strcmp(entry->cmd_name, argv[1]))
            break;
        entry++;
    }

    if (entry->cmd_name) {
        return entry->cmd_fn(argv[2]);
    } else {
        printf("Unknown command '%s'\n", argv[1]);
        dump_cmds();
        return -1;
    }
}
