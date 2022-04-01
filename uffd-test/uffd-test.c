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

typedef unsigned int bool;
#define BIT(nr)                 (1ULL << (nr))
#define UFFD_BUFFER_PAGES  (32)

#define _err(fmt, ...)                                  \
    do {                                                \
        int ret = errno;                                \
        fprintf(stderr, "ERROR: " fmt, ##__VA_ARGS__);	\
        fprintf(stderr, " (errno=%d, line=%d)\n",       \
                ret, __LINE__);                         \
	} while (0)

#define err(fmt, ...)                           \
	do {                                        \
		_err(fmt, ##__VA_ARGS__);               \
		exit(1);                                \
	} while (0)

enum {
    TEST_MISSING = 0,
    TEST_WP,
} test_name;

enum {
    MEM_ANON = 0,
    MEM_SHMEM,
    MEM_HUGETLB,
    MEM_HUGETLB_SHARED,
    MEM_TYPE_MAX,
} mem_type;

typedef enum {
    STATUS_MISSING_WAITING = 0,
    STATUS_MISSING_HANDLED,
    STATUS_WP_WAITING,
    STATUS_WP_HANDLED,
    STATUS_NO_FAULT,
} uffd_status;

struct {
    uint64_t target_addr;
    uffd_status status;
} uffd_test_ctx;

static unsigned long page_size;

static void *uffd_buffer;
static void *zero_page;
static size_t uffd_buffer_size;
static int uffd_handle;
static pthread_t uffd_thread;
static int uffd_quit = -1;
static int uffd_shmem;

const char *status[] = {
    "miss-wait",
    "miss-handled",
    "wp-wait",
    "wp-handled",
};

static void uffd_test_usage(const char *name)
{
    puts("");
    printf("usage: %s <missing|wp> [anon|shmem|hugetlb|hugetlb_shared]\n", name);
    puts("");
    puts("  missing:\tdo page miss test");
    puts("  wp:     \tdo page write-protect test");
    puts("");
    puts("The memory type parameter is optional. By default, 'anon' is used.");
    puts("");
    exit(0);
}

static inline void event_set(uint64_t addr, uffd_status status)
{
    uffd_test_ctx.target_addr = addr;
    uffd_test_ctx.status = status;
}

static inline void event_check(uint64_t addr, uffd_status cur)
{
    if (uffd_test_ctx.target_addr != addr) {
        err("%s: Unexpected fault address (0x%"PRIx64", not 0x%"PRIx64")\n",
            __func__, addr, uffd_test_ctx.target_addr);
    }

    if (uffd_test_ctx.status != cur) {
        err("%s: Unexpected uffd status (%s, rather than %s)\n",
            __func__, status[cur], status[uffd_test_ctx.status]);
    }

    printf("Event check successful on addr 0x%"PRIx64" status %s\n",
           addr, status[cur]);
}

static inline void event_check_update(uint64_t addr, uffd_status cur,
                                      uffd_status next)
{
    event_check(addr, cur);

    /* Update status to next */
    uffd_test_ctx.status = next;
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

        if (addr < (uint64_t) uffd_buffer ||
            addr >= (uint64_t) uffd_buffer + uffd_buffer_size) {
            printf("Illegal page fault address detected (0x%lx). "
                   "Possibly a kernel bug!\n", addr);
            exit(-1);
        }

        if (msg.arg.pagefault.flags & UFFD_PAGEFAULT_FLAG_WP) {
            struct uffdio_writeprotect wp = { 0 };

            event_check_update(addr, STATUS_WP_WAITING, STATUS_WP_HANDLED);

            wp.range.start = addr;
            wp.range.len = page_size;
            /* Undo write-protect, do wakeup after that */
            wp.mode = 0;

            if (ioctl(uffd, UFFDIO_WRITEPROTECT, &wp)) {
                ret = errno;
                printf("%s: Unset WP failed for address 0x%llx: %d (%s)\n",
                       __func__, addr, -ret, strerror(ret));
                continue;
            }

            printf("%s: Detected WP for page %d (0x%llx), recovered\n",
                   __func__, index, addr);
        } else {
            struct uffdio_copy copy = { 0 };

            copy.dst = addr;
            copy.src = (__u64) zero_page;
            copy.len = page_size;

            if (test_name == TEST_WP) {
                copy.mode = UFFDIO_COPY_MODE_WP;
                /* We should wait for another WP event */
                event_check_update(addr, STATUS_MISSING_WAITING,
                                   STATUS_WP_WAITING);
            } else {
                /* We simply set the event handled */
                event_check_update(addr, STATUS_MISSING_WAITING,
                                   STATUS_MISSING_HANDLED);
            }

            if (ioctl(uffd, UFFDIO_COPY, &copy)) {
                ret = errno;
                printf("%s: copy page failed for address 0x%llx: %d (%s)\n",
                       __func__, addr, -ret, strerror(ret));
                continue;
            }

            printf("%s: Detected missing page %d (0x%llx), recovered",
                   __func__, index, addr);
            if (test_name == TEST_WP) {
                printf(", wr-protected");
            }
            printf("\n");
        }

        fflush(stdout);

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

    if (mem_type == MEM_SHMEM) {
        flags |= MAP_SHARED;
        page_size = getpagesize();
    } else if (mem_type == MEM_ANON) {
        flags |= MAP_PRIVATE;
        page_size = getpagesize();
    } else if (mem_type == MEM_HUGETLB) {
        flags |= MAP_HUGETLB | MAP_HUGE_2MB | MAP_PRIVATE;
        page_size = 2UL << 20;
    } else {
        assert(mem_type == MEM_HUGETLB_SHARED);
        flags |= MAP_HUGETLB | MAP_HUGE_2MB | MAP_SHARED;
        page_size = 2UL << 20;
    }
    uffd_buffer_size = page_size * UFFD_BUFFER_PAGES;

    /* Init zero page */
    zero_page = mmap(NULL, page_size, PROT_READ | PROT_WRITE,
                     MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);


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

    printf("page size: %lu\n", page_size);
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

/*
 * For each page:
 * [0]: prefault type (<= WP_PREFAULT_MAX)
 * [1]: whether to wr-protect (0/1)
 */
unsigned char page_info[UFFD_BUFFER_PAGES][2];

/*
 * Setup the expected status for all the possible cases.  Note that when
 * filling in with STATUS_NO_FAULT it means we should never have any
 * further event triggered on this page.
 */
uffd_status wp_expected[WP_PREFAULT_MAX][2] = {
    /* prefault-none */
    { STATUS_MISSING_WAITING, STATUS_MISSING_WAITING },
    /* prefault-read */
    { STATUS_NO_FAULT, STATUS_WP_WAITING },
    /* prefault-write */
    { STATUS_NO_FAULT, STATUS_WP_WAITING },
};

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

    munmap(zero_page, page_size);
    zero_page = NULL;

    close(uffd_quit);
    uffd_quit = -1;
}

void uffd_test_loop(void)
{
    int i;
    unsigned int *ptr;
    uffd_status expected;

    for (i = 0; i < UFFD_BUFFER_PAGES; i++) {
        printf("writting to page %d\n", i);
        ptr = uffd_buffer + i * page_size;

        /* Setup what we expect to trigger */
        if (test_name == TEST_MISSING) {
            /* We always expect a MISSING event to trigger */
            expected = STATUS_MISSING_WAITING;
        } else {
            /* Slightly complicated for uffd-wp test, see the table */
            expected = wp_expected[page_info[i][0]][page_info[i][1]];
        }
        event_set((uint64_t)ptr, expected);

        *ptr = i;

        /* Let's also check what has happened.. */
        if (test_name == TEST_MISSING) {
            event_check((uint64_t)ptr, STATUS_MISSING_HANDLED);
        } else {
            /*
             * As long as it's not STATUS_NO_FAULT to be expected, we
             * expect the last status to be WP handled, because even if
             * UFFDIO_COPY with WP=1 it'll go into that stage at last.
             */
            if (expected != STATUS_NO_FAULT) {
                event_check((uint64_t)ptr, STATUS_WP_HANDLED);
            } else {
                /* If no page fault expected, it should keep as-is */
                event_check((uint64_t)ptr, STATUS_NO_FAULT);
            }
        }
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
        mem_type = MEM_SHMEM;
    } else if (!strcmp(type, "hugetlb")) {
        mem_type = MEM_HUGETLB;
    } else if (!strcmp(type, "hugetlb_shared")) {
        mem_type = MEM_HUGETLB_SHARED;
    } else if (!strcmp(type, "anon")) {
        mem_type = MEM_ANON;
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
