#define _GNU_SOURCE
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <sys/syscall.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <linux/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <stdint.h>
#include <pthread.h>
#include <time.h>
#include <poll.h>
#include <sys/eventfd.h>
#include <linux/userfaultfd.h>
#include <inttypes.h>
#include <linux/types.h>

#include "libuffd.h"
#include "libpagemap.h"

static void run_test(int uffd, char *buffer, unsigned long size)
{
    uint64_t psize = getpagesize();
    uint64_t pages = size / psize, len = sizeof(bool) * pages;
    bool *array = malloc(len);
    unsigned long i;
    int pagemap_fd;

    if (size % psize)
        err("size not aligned to psize");

    if (uffd_wp(uffd, buffer, size))
        err("uffd_wp()");

    if (pages % 32)
        err("pages not aligned to 32");

    srandom(time(NULL));
    for (i = 0; i < len; i += 32) {
        uint32_t val = random();
        int j;

        for (j = 0; j < 32; j++) {
            bool v = val & 1;
            val >>= 1;
            array[i+j] = v;
            if (v)
                buffer[psize * (i+j)] = 2;
        }
    }

    pagemap_fd = pagemap_open();
    puts("Start verify pages...");
    for (i = 0; i < len; i++) {
        uint64_t entry = pagemap_get_entry(pagemap_fd, buffer + i * psize);
        pagemap_check_wp(entry, !array[i]);
    }
    puts("All verified!");

    close(pagemap_fd);
    free(array);
}

int main(int argc, char *argv[])
{
    unsigned long size;
    uint64_t features, required;
    char *buffer;
    char *fname;
    int uffd, fd;

    if (argc < 3) {
        printf("%s <test_file> <size>\n", argv[0]);
        return -1;
    }

    if (uffd_get_features(&features))
        err("uffd_get_features()");

    required = UFFD_FEATURE_PAGEFAULT_FLAG_WP | UFFD_FEATURE_WP_ASYNC;
    if (features & required != required)
        err("Required feature missing, current=0x%lx, required=0xlx",
            features, required);

    fname = argv[1];
    size = strtoul(argv[2], NULL, 10);

    printf("Test file: %s\n", fname);
    printf("Mem size:  %lu\n", size);

    uffd = uffd_open_with_api(UFFD_USER_MODE_ONLY, required);
    if (uffd == -1)
        err("uffd_open()");

    fd = open(fname, O_RDWR);
    if (fd == -1)
        err("open()");

    buffer = mmap(NULL, size, PROT_READ|PROT_WRITE,
                  MAP_SHARED, fd, 0);
    if (buffer == MAP_FAILED)
        err("mmap()");

    if (uffd_register(uffd, buffer, size, 0, 1, 0))
        err("uffd register");

    run_test(uffd, buffer, size);

    uffd_unregister(uffd, buffer, size);
    munmap(buffer, size);
    close(fd);
}
