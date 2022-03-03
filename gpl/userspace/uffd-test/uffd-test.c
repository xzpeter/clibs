#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <sys/syscall.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
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

typedef unsigned int bool;
#define BIT(nr)                 (1ULL << (nr))
#define UFFD_BUFFER_PAGES  (8)

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
static int uffd_shmem;

static void uffd_test_usage(const char *name)
{
    puts("");
    printf("usage: %s <missing|wp> [shmem]\n", name);
    puts("");
    puts("  missing:\tdo page miss test");
    puts("  wp:     \tdo page write-protect test");
    puts("  shmem:  \tuse shmem");
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
    int served_pages = 0;
    struct pollfd fds[2] = {
        { .fd = uffd, .events = POLLIN | POLLERR | POLLHUP },
        { .fd = uffd_quit, .events = POLLIN | POLLERR | POLLHUP },
    };

    printf("%s: thread created\n", __func__);

    while (1) {
        struct uffd_msg msg;
        uint64_t addr, index;
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

        if (len == 0) {
            /* Main thread tells us to quit */
            break;
        }

        if (len < 0) {
            printf("%s: read() failed on uffd: %d\n", __func__, -errno);
            break;
        }

        if (msg.event != UFFD_EVENT_PAGEFAULT) {
            printf("%s: unknown message: %d\n", __func__, msg.event);
            continue;
        }

        addr = msg.arg.pagefault.address;
        index = (addr - (uint64_t)uffd_buffer) / page_size;

        if (msg.arg.pagefault.flags & UFFD_PAGEFAULT_FLAG_WP) {
            struct uffdio_writeprotect wp = { 0 };

            wp.range.start = addr;
            wp.range.len = page_size;
            /* Undo write-protect, do wakeup after that */
            wp.mode = 0;

            if (ioctl(uffd, UFFDIO_WRITEPROTECT, &wp)) {
                printf("%s: Unset WP failed for address 0x%llx\n",
                       __func__, addr);
                continue;
            }

            printf("%s: Detected WP for page %d (0x%llx), recovered\n",
                   __func__, index, addr);
        } else {
            struct uffdio_zeropage zero = { 0 };

            zero.range.start = addr;
            zero.range.len = page_size;

            if (ioctl(uffd, UFFDIO_ZEROPAGE, &zero)) {
                printf("%s: zero page failed for address 0x%llx\n",
                       __func__, addr);
                continue;
            }

            printf("%s: Detected missing page %d (0x%llx), recovered\n",
                   __func__, index, addr);
        }

        served_pages++;
    }

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
        reg.mode = UFFDIO_REGISTER_MODE_MISSING | UFFDIO_REGISTER_MODE_WP;
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

static int uffd_test_init(void)
{
    int flags = MAP_ANONYMOUS;

    uffd_quit = eventfd(0, 0);

    assert(uffd_buffer == NULL);

    page_size = getpagesize();
    uffd_buffer_size = page_size * UFFD_BUFFER_PAGES;

    if (uffd_shmem) {
        flags |= MAP_SHARED;
    } else {
        flags |= MAP_PRIVATE;
    }

    uffd_buffer = mmap(NULL, uffd_buffer_size, PROT_READ | PROT_WRITE,
                       flags, -1, 0);

    if (uffd_buffer == MAP_FAILED) {
        printf("map() failed: %s\n", strerror(errno));
        return -1;
    }

    if ((uint64_t)uffd_buffer & (page_size - 1)) {
        printf("mmap() returned unaligned address\n");
        return -1;
    }

    if (uffd_handle_init()) {
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

enum {
    WP_PREFAULT_NONE = 0,
    WP_PREFAULT_READ,
    WP_PREFAULT_WRITE,
    WP_PREFAULT_MAX,
};

char *wp_prefault_str[WP_PREFAULT_MAX] = {
    "no-prefault", "read-prefault", "write-prefault"
};

unsigned char page_info[UFFD_BUFFER_PAGES][2];

int uffd_do_write_protect(void)
{
    struct uffdio_writeprotect wp;
    char buf, *ptr, x;
    int i;

    for (i = 0; i < UFFD_BUFFER_PAGES; i++) {
        page_info[i][0] = random() % WP_PREFAULT_MAX;
        page_info[i][1] = random() % 2;
    }

    /*
     * Pre-fault the region randomly.  For each page, we choose one of
     * the three options:
     *
     * (1) do nothing; this will keep the PTE empty
     * (2) read once; this will trigger on-demand paging to fill in
     *     the zero page PFN into PTE
     * (3) write once; this will trigger the on-demand paging to fill
     *     in a real page PFN into PTE
     */
    for (i = 0; i < UFFD_BUFFER_PAGES; i++) {
        x = page_info[i][0];
        ptr = uffd_buffer + i * page_size;
        switch (x) {
        case WP_PREFAULT_READ:
            buf = *ptr;
            break;
        case WP_PREFAULT_WRITE:
            *ptr = 1;
            break;
        default:
            /* Do nothing */
            break;
        }
    }

    /* Register the region */
    if (uffd_do_register()) {
        return -1;
    }

    /* Randomly wr-protect some page */
    for (i = 0; i < UFFD_BUFFER_PAGES; i++) {
        ptr = uffd_buffer + i * page_size;
        if (page_info[i][1]) {
            wp.range.start = (uint64_t) ptr;
            wp.range.len = (uint64_t) page_size;
            wp.mode = UFFDIO_WRITEPROTECT_MODE_WP;

            if (ioctl(uffd_handle, UFFDIO_WRITEPROTECT, &wp)) {
                printf("%s: Failed to do write protect\n", __func__);
                return -1;
            }
        }
    }

    printf("======================\n");

    for (i = 0; i < UFFD_BUFFER_PAGES; i++) {
        x = page_info[i][0];
        printf("Page %d %s", i, wp_prefault_str[x]);
        if (page_info[i][1]) {
            printf(", wr-protected");
        }
        printf("\n");
    }

    printf("======================\n");

    /* Make sure the poll thread prints after this */
    fflush(stdout);

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
        printf("writting to page %d\n", i);
        ptr = uffd_buffer + i * page_size;
        *ptr = i;
    }
}

int uffd_test_wp(void)
{
    if (uffd_do_write_protect()) {
        return -1;
    }

    uffd_test_loop();

    return 0;
}

int uffd_test_missing(void)
{
    if (uffd_do_register()) {
        return -1;
    }

    uffd_test_loop();

    return 0;
}

int uffd_type_parse(const char *type)
{
    if (!type) {
        return 0;
    }

    if (!strcmp(type, "shmem")) {
        uffd_shmem = 1;
        printf("Using shmem\n");
    } else {
        printf("Unknown type: %s\n", type);
        return -1;
    }

    return 0;
}

int main(int argc, char *argv[])
{
    int ret = 0;
    const char *cmd, *type = NULL;

    srand(time(NULL));

    if (argc < 2) {
        uffd_test_usage(argv[0]);
    }
    cmd = argv[1];

    if (argc > 2) {
        type = argv[2];
    }

    if (!strcmp(cmd, "missing")) {
        test_name = TEST_MISSING;
    } else if (!strcmp(cmd, "wp")) {
        test_name = TEST_WP;
    } else {
        uffd_test_usage(argv[0]);
    }

    if (uffd_type_parse(type)) {
        return -1;
    }

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
