#include <stdio.h>
#include <sys/mman.h>
#include <unistd.h>
#include <fcntl.h>

#define  PMEM_PATH  "/dev/dax0.0"
#define  SIZE_1G    (1 * (1UL << 30))
#define  PMEM_SIZE  (3 * SIZE_1G)

unsigned int psize;

void faultin(char *buffer, unsigned long size)
{
    unsigned long i = 0;

    for (i = 0; i < size; i += psize)
        buffer[i] = 1;
}

void run_test(char *buffer, unsigned long size)
{
    /* dev_dax only supports aligned mprotect() / vma split */
    mprotect(buffer, SIZE_1G, PROT_READ);
    mprotect((void *)((unsigned long)buffer + SIZE_1G), SIZE_1G, PROT_NONE);
    mprotect((void *)((unsigned long)buffer + 2 * SIZE_1G), SIZE_1G, PROT_WRITE);
}

int main(void)
{
    int fd = open(PMEM_PATH, O_RDWR);
    char *buffer;

    psize = getpagesize();

    if (fd < 0) {
        printf("open(%s) failed\n", PMEM_PATH);
        return -1;
    }

    buffer = mmap(NULL, PMEM_SIZE, PROT_READ|PROT_WRITE,
                  MAP_SHARED, fd, 0);
    if (buffer == MAP_FAILED) {
        printf("mmap() failed\n");
        return -1;
    }

    printf("buffer=%p\n", buffer);
    faultin(buffer, PMEM_SIZE);
    run_test(buffer, PMEM_SIZE);

    munmap(buffer, PMEM_SIZE);
    close(fd);
    return 0;
}
