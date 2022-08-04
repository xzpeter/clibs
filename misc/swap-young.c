#include <stdio.h>
#include <sys/mman.h>
#include <assert.h>
#include <numaif.h>
#include <time.h>
#include <errno.h>
#include <unistd.h>
#include <stdint.h>
#include <inttypes.h>
#include <stdlib.h>
#include <stdbool.h>

#define  MEM_SIZE  (1UL << 30)

char *map;
unsigned int psize;

uint64_t get_usec(void)
{
    uint64_t val = 0;
    struct timespec t;
    int ret = clock_gettime(CLOCK_MONOTONIC, &t);
    if (ret == -1) {
        perror("clock_gettime() failed");
        /* should never happen */
        exit(-1);
    }
    val = t.tv_nsec / 1000;     /* ns -> us */
    val += t.tv_sec * 1000000;  /* s -> us */
    return val;
}

void write_once(void)
{
    char *end = map + MEM_SIZE, *start = map;

    while (start < end) {
        *start = 1;
        start += psize;
    }
}

void read_once(void)
{
    char *end = map + MEM_SIZE, *start = map;
    char v = 0;

    while (start < end) {
        v += *start;
        start += psize;
    }
}

void bind(int node)
{
    unsigned long nodemask = 0;
    long ret;

    assert(node < 2);
    nodemask |= 1UL << node;

    ret = mbind(map, MEM_SIZE, MPOL_BIND,
                (const unsigned long *)&nodemask, 8,
                MPOL_MF_STRICT | MPOL_MF_MOVE);
    if (ret) {
        perror("mbind failed");
        exit(1);
    }
}

void do_rw(bool write)
{
    uint64_t t1;
    int i;

    t1 = get_usec();
    if (write)
        write_once();
    else
        read_once();
    t1 = get_usec() - t1;
    printf("%s (node 0) took %"PRIu64" (us)\n",
            write ? "Write" : "Read", t1);
}

void do_bind(int n)
{
    uint64_t t1;

    t1 = get_usec();
    bind(n);
    t1 = get_usec() - t1;
    printf("Move to node %d took %"PRIu64 "(us)\n", n, t1);
}

int main(void)
{
    int i = 0;

    psize = getpagesize();
    map = mmap(NULL, MEM_SIZE, PROT_READ|PROT_WRITE,
                     MAP_ANONYMOUS|MAP_PRIVATE, -1, 0);
    assert(map != MAP_FAILED);
    bind(0);

    /* Write once, then read once */
    do_rw(1);
    do_rw(0);

    do_bind(1);
    do_bind(0);

    /* First read (test young bit), then write (test dirty bit) */
    for (i = 0; i < 5; i++) {
        do_rw(0);
        do_rw(1);
    }

    munmap(map, MEM_SIZE);
    return 0;
}
