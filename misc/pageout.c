#include <stdio.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <assert.h>
#include <inttypes.h>
#include <time.h>

int main(void)
{
    char *addr;
    int fd, i;
    uint64_t total = 0;
    int psize;

    psize = getpagesize();

    addr = mmap(NULL, psize, PROT_READ|PROT_WRITE,
                MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
    assert(addr && ((uint64_t)addr & (psize - 1)) == 0);

    *addr = 1;
    madvise(addr, psize, MADV_PAGEOUT);
    printf("swapped out, press enter to swapin...");
    getchar();
    *addr = 2;

    munmap(addr, psize);

    return 0;
}
