/*
 * Use this program to have a rough understanding of how brk, brk_start and
 * mmap_base works for a linear address space, and how they grows.
 */
#include <stdio.h>
#include <fcntl.h>
#include <stdbool.h>
#include <unistd.h>
#include <sys/mman.h>
#include <assert.h>

#define MAP_PATH "/proc/self/maps"

void dump_maps(void)
{
    int fd = open(MAP_PATH, O_RDONLY);
    char buf[256] = {};
    int ret;

    printf("DUMP MAPS:\n");

    while (true) {
        ret = read(fd, buf, sizeof(buf)-1);
        if (ret <= 0)
            break;
        buf[ret] = 0;
        printf("%s", buf);
    }

    puts("");
}

#define  N_1M  (1024 * 1024)
#define  N  10

int main(void)
{
    void *buf[N];
    int i;
    int perms = PROT_NONE;

    dump_maps();
    printf("sbrk(0)=%p\n", sbrk(0));

    printf("growing brk with 1M...\n");
    printf("sbrk(1M)=%p\n", sbrk(N_1M));
    dump_maps();

    for (i = 0; i < N; i++) {
        /*
         * To make it not merge with any existing mmap()s (normally RW),
         * bounce permissions between RO and NONE
         */
        printf("try to mmap() a 10M range... ");
        buf[i] = mmap(NULL, 10*N_1M, perms, MAP_PRIVATE|MAP_ANON, -1, 0);
        assert(buf[i]);
        printf("addr=%p\n", buf[i]);
        switch (perms) {
        case PROT_NONE:
            perms = PROT_READ;
            break;
        case PROT_READ:
            perms = PROT_WRITE;
            break;
        case PROT_WRITE:
            perms = PROT_NONE;
            break;
        }
    }

    dump_maps();

    for (i = 0; i < N; i++) {
        munmap(buf, 10*N_1M);
    }

    return 0;
}
