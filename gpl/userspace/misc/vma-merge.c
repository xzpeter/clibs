#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <assert.h>
#include <stdlib.h>
#include <errno.h>

void *start;
unsigned long size;

void map_page(void)
{
    void *buffer;
    unsigned long page_size = getpagesize();

    buffer = mmap(start, page_size, PROT_READ|PROT_WRITE,
                  MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
    if (buffer == MAP_FAILED) {
        printf("Allocation failed! error=%d\n", -errno);
        exit(1);
    }
    printf("Allocated page at %p\n", buffer);

    if (!start)
        start = buffer;

    size += page_size;
    start += page_size;
}

int main(void)
{
    unsigned long i;

    for (i = 0; i < 128; i++)
        map_page();

    printf("Current PID: %d\n", getpid());
    sleep(10000);

    return 0;
}
