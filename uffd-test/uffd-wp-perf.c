#include <linux/userfaultfd.h>
#include <sys/syscall.h>      /* Definition of SYS_* constants */
#include <stdbool.h>
#include <unistd.h>
#include <stdint.h>
#include <inttypes.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <stdio.h>
#include <errno.h>
#include <fcntl.h>
#include <time.h>
#include <stdlib.h>

#define _err(fmt, ...)                                  \
    do {                                                \
        int ret = errno;                                \
        fprintf(stderr, "ERROR: " fmt, ##__VA_ARGS__);	\
        fprintf(stderr, " (errno=%d, line=%d)\n",       \
                ret, __LINE__);                         \
    } while (0)

#define errexit(exitcode, fmt, ...)             \
    do {                                        \
        _err(fmt, ##__VA_ARGS__);               \
        exit(exitcode);                         \
    } while (0)

#define err(fmt, ...) errexit(1, fmt, ##__VA_ARGS__)

#define UFFD_FLAGS	(O_CLOEXEC | O_NONBLOCK | UFFD_USER_MODE_ONLY)
#define SIZE        (10UL << 30)

static void wp_range(int ufd, char *start, __u64 len, bool wp)
{
    struct uffdio_writeprotect prms;

    /* Write protection page faults */
    prms.range.start = (uint64_t)start;
    prms.range.len = len;
    /* Undo write-protect, do wakeup after that */
    prms.mode = wp ? UFFDIO_WRITEPROTECT_MODE_WP : 0;

    if (ioctl(ufd, UFFDIO_WRITEPROTECT, &prms))
        err("clear WP failed: address=0x%"PRIx64, (uint64_t)start);
}

uint64_t get_usec(void)
{
    uint64_t val = 0;
    struct timespec t;
    int ret = clock_gettime(CLOCK_MONOTONIC, &t);
    if (ret == -1)
        err("clock_gettime() failed");
    val = t.tv_nsec / 1000;     /* ns -> us */
    val += t.tv_sec * 1000000;  /* s -> us */
    return val;
}

#ifndef UFFD_FEATURE_WP_UNPOPULATED
#define UFFD_FEATURE_WP_UNPOPULATED		(1<<13)
#endif

enum {
    DEFAULT = 0,
    PRE_READ,
    MADVISE,
    WP_UNPOPULATED,
    TEST_NUM,
};

const char *tests[] = {"DEFAULT", "PRE-READ", "MADVISE", "WP-UNPOPULATE"};

static void test_uffd_wp(int mode)
{
    struct uffdio_api uffdio_api;
    struct uffdio_register uffdio_register;
    int uffd = syscall(__NR_userfaultfd, UFFD_FLAGS);
    char *buffer, *p;
    uint64_t start, start1 = 0, requested = 0;

    printf("Test %s: ", tests[mode]);
    fflush(stdout);

    if (mode == WP_UNPOPULATED)
        requested = UFFD_FEATURE_WP_UNPOPULATED;

    buffer = mmap(NULL, SIZE, PROT_READ | PROT_WRITE,
                  MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
    if (buffer == MAP_FAILED)
        err("mmap()");

    uffdio_api.api = UFFD_API;
    uffdio_api.features = requested;
    if (ioctl(uffd, UFFDIO_API, &uffdio_api))
        err("UFFDIO_API");
    if (requested && !(uffdio_api.features & requested))
        err("UFFD_FEATURE_WP_UNPOPULATED not supported");

    uffdio_register.range.start = (unsigned long) buffer;
    uffdio_register.range.len = SIZE;
    uffdio_register.mode = UFFDIO_REGISTER_MODE_WP;
    if (ioctl(uffd, UFFDIO_REGISTER, &uffdio_register))
        err("UFFDIO_REGISTER");

    start = get_usec();
    if (mode == PRE_READ) {
        for (p = buffer; p < buffer + SIZE; p += 4096)
            *(volatile char *)p;
        start1 = get_usec();
    } else if (mode == MADVISE) {
        if (madvise(buffer, SIZE, MADV_POPULATE_READ))
            err("MADV_POPULATE_READ");
        start1 = get_usec();
    }
    wp_range(uffd, buffer, SIZE, true);
    if (start1 == 0)
        printf("%"PRIu64"\n", get_usec() - start);
    else
        printf("%"PRIu64" (pre-fault %"PRIu64")\n",
               get_usec() - start, start1 - start);
}

int main(void)
{
    int i;

    for (i = 0; i < TEST_NUM; i++)
        test_uffd_wp(i);

    return 0;
}
