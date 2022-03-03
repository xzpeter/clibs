#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/mman.h>

#define  N_TYPE_PAGES       16
#define  BUF_SIZE           (100ULL << 20)  /* 100M */

void init_buffer(char *buf)
{
    unsigned long npages = BUF_SIZE / getpagesize();
    int i;

    for (i = 0; i < npages; i++) {
        char *p = buf + i * getpagesize();
        *p = 1 + i % N_TYPE_PAGES;
    }

    printf("Buffer setup done, %lu pages for each type\n",
           npages / N_TYPE_PAGES);
}

void wait_input(const char *msg)
{
    printf("Type enter to %s ...\n", msg);
    getchar();
}

int main(void)
{
    int ret;
    char *buf;

    if (posix_memalign((void **)&buf, getpagesize(), BUF_SIZE)) {
        perror("posix_memalign failed");
        return -1;
    }

    printf("Buffer size: %lu (MB)\n", BUF_SIZE >> 20);
    printf("Type of pages: %lu\n", N_TYPE_PAGES);

    printf("Initializing buffer\n");
    init_buffer(buf);

    wait_input("setup buffer mergeable");
    if (madvise(buf, BUF_SIZE, MADV_MERGEABLE)) {
        perror("madvise() failed");
        goto out;
    }

    wait_input("stop the test");

out:
    free(buf);

    return 0;
}
