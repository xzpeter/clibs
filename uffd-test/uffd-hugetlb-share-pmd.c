#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <sys/syscall.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <stdint.h>
#include <pthread.h>
#include <time.h>
#include <poll.h>
#include <sys/eventfd.h>
#include <linux/userfaultfd.h>
#include <linux/mman.h>

typedef unsigned int bool;
#define BIT(nr)                 (1ULL << (nr))
/* Covers 1G, to enable hugetlb share pmd */
#define UFFD_BUFFER_PAGES  (512)

enum test_name {
    TEST_MISSING = 0,
    TEST_WP,
};

static size_t page_size;
static enum test_name test_name;

static void *uffd_buffer;
static size_t uffd_buffer_size;
static int uffd_handle;
static pthread_t uffd_thread;
static int uffd_quit = -1;
static int hugetlb_fd = -1;
static int enable_share_pmd;

static void uffd_test_usage(const char *name)
{
    puts("");
    printf("usage: %s <missing|wp> <share-pmd>\n", name);
    puts("");
    puts("  missing:\tdo page miss test");
    puts("  wp:     \tdo page write-protect test");
    puts("  share-pmd: \twhether to trigger hugetlbfs share pmd");
    puts("");
    exit(0);
}

static int uffd_handle_init(void)
{
    struct uffdio_api api_struct = { 0 };
    uint64_t ioctl_mask = BIT(_UFFDIO_REGISTER) | BIT(_UFFDIO_UNREGISTER);

    int ufd = syscall(__NR_userfaultfd, O_CLOEXEC | O_NONBLOCK);

    if (ufd == -1) {
        printf("%s: UFFD not supported", __func__);
        return -1;
    }

    api_struct.api = UFFD_API;
    /*
     * For MISSING tests, we don't need any feature bit since it's on
     * by default.
     */
    if (test_name == TEST_WP) {
        api_struct.features = UFFD_FEATURE_PAGEFAULT_FLAG_WP;
    }

    if (ioctl(ufd, UFFDIO_API, &api_struct)) {
        printf("%s: UFFDIO_API failed\n", __func__);
        return -1;
    }

    if ((api_struct.ioctls & ioctl_mask) != ioctl_mask) {
        printf("%s: Missing userfault feature\n", __func__);
        return -1;
    }

    uffd_handle = ufd;

    return 0;
}

static void *uffd_bounce_thread(void *data)
{
    int uffd = (int) (uint64_t) data;
    struct pollfd fds[2] = {
        { .fd = uffd, .events = POLLIN | POLLERR | POLLHUP },
        { .fd = uffd_quit, .events = POLLIN | POLLERR | POLLHUP },
    };
    void *zero_page = calloc(1, page_size);
    int counter_miss = 0, counter_wp = 0;

    assert(zero_page);

    printf("%s: thread created\n", __func__);

    while (1) {
        struct uffd_msg msg;
        ssize_t len;
        int ret;

        ret = poll(fds, 2, -1);
        if (ret < 0) {
            printf("%s: poll() got error: %d\n", ret);
            break;
        }

        if (fds[1].revents) {
            printf("Thread detected quit signal\n");
            break;
        }

        len = read(uffd, &msg, sizeof(msg));

        assert(len != 0);

        if (len < 0) {
            printf("%s: read() failed on uffd: %d\n", __func__, -errno);
            break;
        }

        if (msg.event != UFFD_EVENT_PAGEFAULT) {
            printf("%s: unknown message: %d\n", __func__, msg.event);
            continue;
        }

        if (test_name == TEST_WP) {
            struct uffdio_writeprotect wp = { 0 };

            if (!(msg.arg.pagefault.flags & UFFD_PAGEFAULT_FLAG_WP)) {
                printf("%s: WP flag not detected in PF flags"
                       "for address 0x%llx\n", __func__,
                       msg.arg.pagefault.address);
                continue;
            }

            wp.range.start = msg.arg.pagefault.address;
            wp.range.len = page_size;
            /* Undo write-protect, do wakeup after that */
            wp.mode = 0;

            if (ioctl(uffd, UFFDIO_WRITEPROTECT, &wp)) {
                printf("%s: Unset WP failed for address 0x%llx\n",
                       __func__, msg.arg.pagefault.address);
                exit(1);
            }

            //printf("%s: Detected WP for page 0x%llx, recovered\n",
            //       __func__, msg.arg.pagefault.address);
            counter_wp++;
        } else if (test_name == TEST_MISSING) {
            struct uffdio_copy copy = { 0 };

            /* Cannot use UFFDIO_ZEROPAGE because it won't work for hugetlbfs */
            copy.dst = msg.arg.pagefault.address;
            copy.len = page_size;
            copy.src = (__u64)zero_page;

            if (ioctl(uffd, UFFDIO_COPY, &copy)) {
                printf("%s: zero page failed for address 0x%llx: %s\n",
                       __func__, msg.arg.pagefault.address, strerror(errno));
                exit(1);
            }

            //printf("%s: Detected missing page 0x%llx, recovered\n",
            //       __func__, msg.arg.pagefault.address);

            counter_miss++;
        }
    }

    printf("%s: totally served pages: %d missing, %d wp\n",
           __func__, counter_miss, counter_wp);
    printf("%s: thread quitted\n", __func__);

    return NULL;
}

static int uffd_do_register(void)
{
    int uffd = uffd_handle;
    struct uffdio_register reg = { 0 };

    reg.range.start = (uint64_t) uffd_buffer;
    reg.range.len = (uint64_t) uffd_buffer_size;

    if (test_name == TEST_WP) {
        reg.mode = UFFDIO_REGISTER_MODE_WP;
    } else if (test_name == TEST_MISSING) {
        reg.mode = UFFDIO_REGISTER_MODE_MISSING;
    }

    if (ioctl(uffd, UFFDIO_REGISTER, &reg)) {
        printf("%s: UFFDIO_REGISTER failed: %d\n", __func__, -errno);
        return -1;
    }

    if (test_name == TEST_WP && !(reg.ioctls & BIT(_UFFDIO_WRITEPROTECT))) {
        printf("%s: wr-protect feature missing\n", __func__);
        return -1;
    }

    printf("uffd register completed\n");

    return 0;
}

static void trigger_share_pmd(char *addr)
{
    pid_t child;
    int i;

    child = fork();

    if (child) {
        waitpid(child, NULL, 0);
    } else {
        for (i = 0; i < UFFD_BUFFER_PAGES; i++) {
            /*
             * This will prefault the pmds, then the shared pud entry will be
             * filled in by the child.
             */
            *(addr + i * page_size) = 0;
        }
        printf("child: finished trigger share pmd\n");
        exit(0);
    }
}

static void trigger_share_pmd_mprotect(char *addr)
{
    pid_t child;
    int i;

    child = fork();

    if (child) {
        waitpid(child, NULL, 0);
    } else {
        /*
         * NOTE: this will not trigger the bug even with wp-supported
         * userfaultfd kernels because change_huge_pmd() preserve_write will
         * still be false
         */
        mprotect(addr, page_size * UFFD_BUFFER_PAGES, PROT_READ | PROT_WRITE);
        printf("child: finished trigger share pmd mprotect()\n");
        exit(0);
    }
}

static void *find_aligned_addr(unsigned long size)
{
    void *addr;
    unsigned long ret;

    /* Find an unmapped virtual address that can be aligned to SIZE */
    addr = mmap(NULL, size * 2, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

    if (addr == MAP_FAILED) {
        printf("mmap() failed in %s: %s\n", __func__, strerror(errno));
        return NULL;
    }

    ret = ((unsigned long)addr + size) & (~(size - 1));
    munmap(addr, size * 2);

    return (void *)ret;
}

static int uffd_test_init(void)
{
    const char *fname = "/dev/hugepages/test-hugetlb-share-pmd.data";
    void *addr;

    uffd_quit = eventfd(0, 0);

    assert(uffd_buffer == NULL);

    unlink(fname);
    hugetlb_fd = open(fname, O_CREAT | O_RDWR);

    if (hugetlb_fd < 0) {
        printf("open() hugetlbfs fd failed: %s\n", strerror(errno));
        return -1;
    }

    page_size = 2UL<<20;
    printf("Test page size: %lu\n", page_size);
    uffd_buffer_size = page_size * UFFD_BUFFER_PAGES;

    addr = find_aligned_addr(uffd_buffer_size);
    if (!addr) {
        return -1;
    }

    /* Need 2MB page to trigger huge pmd sharing */
    uffd_buffer = mmap(addr, uffd_buffer_size, PROT_READ | PROT_WRITE,
                       MAP_SHARED | MAP_HUGETLB | MAP_HUGE_2MB , hugetlb_fd, 0);

    if (uffd_buffer == MAP_FAILED) {
        printf("map() failed: %s\n", strerror(errno));
        return -1;
    }

    if ((uint64_t)uffd_buffer & (uffd_buffer_size - 1)) {
        printf("mmap() returned unaligned address\n");
        return -1;
    }

    if (enable_share_pmd) {
        trigger_share_pmd(uffd_buffer);
    }

    if (uffd_handle_init()) {
        return -1;
    }

    if (uffd_do_register()) {
        return -1;
    }

    if (pthread_create(&uffd_thread, NULL,
                       uffd_bounce_thread, (void *)(uint64_t)uffd_handle)) {
        printf("pthread_create() failed: %s\n", strerror(errno));
        return -1;
    }

    printf("uffd buffer pages: %u\n", UFFD_BUFFER_PAGES);
    printf("uffd buffer size: %zu\n", uffd_buffer_size);
    printf("uffd buffer address: %p\n", uffd_buffer);

    return 0;
}

int uffd_do_write_protect(void)
{
    struct uffdio_writeprotect wp;
    int i;
    char buf;
    unsigned long *ptr;

    for (i = 0; i < UFFD_BUFFER_PAGES; i++) {
        ptr = uffd_buffer + i * page_size;
        *ptr = -1;
    }

    wp.range.start = (uint64_t) uffd_buffer;
    wp.range.len = (uint64_t) uffd_buffer_size;
    wp.mode = UFFDIO_WRITEPROTECT_MODE_WP;

    if (ioctl(uffd_handle, UFFDIO_WRITEPROTECT, &wp)) {
        printf("%s: Failed to do write protect\n", __func__);
        return -1;
    }

    printf("uffd marking write protect completed\n");

    return 0;
}

void uffd_test_stop(void)
{
    void *retval;
    uint64_t val = 1;

    /* Tell the thread to go */
    printf("Telling the thread to quit\n");
    write(uffd_quit, &val, 8);
    pthread_join(uffd_thread, &retval);

    close(uffd_handle);
    uffd_handle = 0;

    munmap(uffd_buffer, uffd_buffer_size);
    uffd_buffer = NULL;

    close(uffd_quit);
    uffd_quit = -1;
}

void uffd_test_loop(void)
{
    int i;
    unsigned int *ptr;

    for (i = 0; i < UFFD_BUFFER_PAGES; i++) {
        //printf("writting to page %d\n", i);
        ptr = uffd_buffer + i * page_size;
        *ptr = i;
    }
}

int uffd_test_wp(void)
{
    if (uffd_do_write_protect()) {
        return -1;
    }

    if (enable_share_pmd) {
        trigger_share_pmd_mprotect(uffd_buffer);
    }

    uffd_test_loop();

    return 0;
}

int uffd_test_missing(void)
{
    uffd_test_loop();

    return 0;
}

int main(int argc, char *argv[])
{
    int ret = 0;
    const char *cmd, *sharepmd;

    srand(time(NULL));

    if (argc < 3) {
        uffd_test_usage(argv[0]);
    }
    cmd = argv[1];
    sharepmd = argv[2];

    if (!strcmp(cmd, "missing")) {
        test_name = TEST_MISSING;
    } else if (!strcmp(cmd, "wp")) {
        test_name = TEST_WP;
    } else {
        uffd_test_usage(argv[0]);
    }

    enable_share_pmd = atoi(sharepmd);

    if (uffd_test_init()) {
        return -1;
    }

    switch (test_name) {
    case TEST_MISSING:
        ret = uffd_test_missing();
        break;
    case TEST_WP:
        ret = uffd_test_wp();
        break;
    }

    uffd_test_stop();

    return ret;
}
