#include "libuffd.h"
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

#define _err(fmt, ...)                                  \
    do {                                                \
        int ret = errno;                                \
        fprintf(stderr, "ERROR: " fmt, ##__VA_ARGS__);	\
        fprintf(stderr, " (errno=%d, @%s:%d)\n",        \
                ret, __FILE__, __LINE__);               \
    } while (0)

#define errexit(exitcode, fmt, ...)             \
    do {                                        \
        _err(fmt, ##__VA_ARGS__);               \
        exit(exitcode);                         \
    } while (0)

#define err(fmt, ...) errexit(1, fmt, ##__VA_ARGS__)

int main(void)
{
    unsigned int psize = getpagesize();
    uint64_t features;
    int uffd, ret;
    char *buffer;

    ret = uffd_get_features(&features);
    if (ret)
        err("get feature failed");

    if (!(features & UFFD_FEATURE_POISON))
        err("UFFD_FEATURE_POISON not supported");

    uffd = uffd_open_with_api(UFFD_USER_MODE_ONLY, UFFD_FEATURE_POISON);
    if (uffd < 0)
        err("uffd open failed: %s\n", strerror(-uffd));

    buffer = mmap(NULL, psize, PROT_READ|PROT_WRITE, MAP_ANON|MAP_PRIVATE,
                  -1, 0);
    if (buffer == MAP_FAILED)
        err("mmap() failed");

    ret = uffd_register(uffd, buffer, psize, false, true, false);
    if (ret)
        err("uffd register");

    ret = uffd_poison(uffd, buffer, psize);
    if (ret)
        err("uffd_poison() failed: %s", strerror(-ret));

    /* Should trigger SIGBUS */
    *buffer = 1;

    uffd_unregister(uffd, buffer, psize);
    munmap(buffer, psize);
    close(uffd);

    return 0;
}
