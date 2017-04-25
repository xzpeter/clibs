#include <stdio.h>
#include <sys/mman.h>

#define SIZE (8*1024*1024)

void hold(void)
{
    printf("please type any key to continue...\n");
    getchar();
}

void fill_buf(char *buf, int size)
{
    int i;

    for (i = 0; i < size; i++) {
        buf[i] = 1;
    }
}

int main(void)
{
    char *buf = mmap(NULL, SIZE, PROT_NONE,
                     MAP_ANONYMOUS | MAP_PRIVATE, 0, 0);
    char *buf2;
    int ret;

    if (buf == MAP_FAILED) {
        printf("mmap() failed\n");
        return -1;
    }

    printf("first mmap() address: %p\n", buf);

    buf = (void *)((unsigned long)buf & (-0x200000)) + 0x200000;
    printf("aligned mmap() address: %p\n", buf);
    buf2 = mmap(buf, SIZE/2, PROT_READ | PROT_WRITE,
                MAP_ANONYMOUS | MAP_PRIVATE, 0, 0);
    if (buf2 == MAP_FAILED) {
        printf("2nd mmap() failed\n");
        return -1;
    }

    printf("second mmap() address: %p\n", buf2);
    buf = buf2;

    printf("before HUGEPAGE\n");
    hold();

    ret = madvise(buf, SIZE, MADV_HUGEPAGE);
    if (ret) {
        printf("madvise() failed\n");
        return -1;
    }

    printf("before filling buf\n");
    hold();

    fill_buf(buf, SIZE/2);

    printf("before NOHUGEPAGE\n");
    hold();

    ret = madvise(buf, SIZE, MADV_NOHUGEPAGE);
    if (ret) {
        printf("madvise() failed\n");
        return -1;
    }

    printf("before leave\n");
    hold();

    munmap(buf, SIZE);

    return 0;
}
