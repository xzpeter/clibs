#include <stdio.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/mman.h>
#include <assert.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <signal.h>
#include <time.h>

#define BUF_SIZE (1UL << 20)    /* 1MB */

unsigned long getns(void)
{
    struct timespec tp;

    clock_gettime(CLOCK_MONOTONIC, &tp);
    return tp.tv_sec * 1000000000 + tp.tv_nsec;
}

int main(int argc, char *argv[])
{
    pid_t *pid_list, pid;
    unsigned long start;
    int i, n, ret;
    char *area;

    if (argc <= 1)
        return -1;
    n = atoi(argv[1]);
    if (n < 10 || n > 1000)
        return -1;

    pid_list = malloc(sizeof(pid_t) * n);
    assert(pid_list);

    /* Create a shared range of pages to be mapped by all childs */
    area = mmap(NULL, BUF_SIZE, PROT_READ|PROT_WRITE,
                MAP_ANONYMOUS|MAP_SHARED, -1, 0);
    assert(area);
    /* Prefault pages */
    for (i = 0; i < BUF_SIZE; i += 4096)
        area[i] = i;

    start = getns();

    /* Trigger rmap_add(), expand rmap list */
    for (i = 0; i < n; i++) {
        pid = fork();
        if (!pid) {
            sleep(100000);
        }
        pid_list[i] = pid;
    }

    /* Shrink rmap list quickly */
    for (i = 0; i < n; i++) {
        ret = kill(pid_list[i], SIGKILL);
        assert(ret == 0);
    }

    /* Wait until umap shrink done */
    for (i = 0; i < n; i++) {
        wait(&ret);
    }

    start = getns() - start;

    printf("time used: %lu (ms)\n", start / 1000000);

    munmap(area, BUF_SIZE);
    free(pid_list);
    return 0;
}
