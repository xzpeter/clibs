#include <stdio.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <assert.h>
#include <inttypes.h>
#include <time.h>

#define BIT_ULL(nr)                 (1ULL << (nr))
#define PM_SWAP                       BIT_ULL(62)
#define PM_PRESENT                    BIT_ULL(63)

#define PAGE_NUM 32*(1UL<<10) /* 32K pages -> 64MB bytes */
#define PERF_RUNS 5

static int pagemap_fd;
static int psize;

static uint64_t getns(void)
{
    struct timespec tp;

    clock_gettime(CLOCK_MONOTONIC, &tp);
    return tp.tv_sec * 1000000000 + tp.tv_nsec;
}

static void pagemap_open(void)
{
    pagemap_fd = open("/proc/self/pagemap", O_RDONLY);
    assert(pagemap_fd >= 0);
}

static uint64_t pagemap_read_vaddr(void *vaddr)
{
    uint64_t value;
    int ret;

    ret = pread(pagemap_fd, &value, sizeof(uint64_t),
                ((uint64_t)vaddr >> 12) * sizeof(uint64_t));
    assert(ret == sizeof(uint64_t));

    return value;
}

static void dump(void *addr, const char *msg)
{
    uint64_t val = pagemap_read_vaddr(addr);

    printf("%30s: present bit %d, swap bit %d\n", msg,
           !!(val & PM_PRESENT), !!(val & PM_SWAP));
}

static void test_func(char *addr)
{
    /* Fault in */
    *addr = 1;
    dump(addr, "FAULT1 (expect swap==0)");

    /* Page it out */
    madvise(addr, 4096, MADV_PAGEOUT);
    dump(addr, "PAGEOUT (expect swap==1)");

    /* Re-fault; test PTE marker */
    *addr = 2;
    dump(addr, "FAULT2 (expect swap==0)");

    /* Truncate, test dropping page */
    madvise(addr, 4096, MADV_REMOVE);
    dump(addr, "REMOVE (expect swap==0)");

    /* Below sequences test dropping of pte marker */
    *addr = 3;
    madvise(addr, 4096, MADV_PAGEOUT);
    dump(addr, "PAGEOUT (expect swap==1)");
    madvise(addr, 4096, MADV_REMOVE);
    dump(addr, "REMOVE (expect swap==0)");
}

static uint64_t test_perf(char *addr)
{
    uint64_t record;
    char *p;
    int i;

    /* Fault in all pages */
    for (i = 0, p = addr; i < PAGE_NUM; i++) {
        *p = i;
        p += psize;
    }
    /* Page out all pages */
    madvise(addr, psize * PAGE_NUM, MADV_PAGEOUT);
    record = getns();
    /* Quickly fault back all pages */
    for (i = 0, p = addr; i < PAGE_NUM; i++) {
        *p = i;
        p += psize;
    }
    record = getns() - record;
    record /= 1000;
    printf("Total time used: %lu (us)\n", record);
    return record;
}

int main(void)
{
    char *addr;
    int fd, i;
    uint64_t total = 0;

    psize = getpagesize();

    addr = mmap(NULL, psize * PAGE_NUM, PROT_READ|PROT_WRITE,
                MAP_SHARED|MAP_ANONYMOUS, -1, 0);
    assert(addr && ((uint64_t)addr & (psize - 1)) == 0);

    pagemap_open();

    printf("prepare func test\n=============\n");
    test_func(addr);
    printf("prepare perf test...\n=============\n");
    for (i = 0; i < PERF_RUNS; i++) {
        total += test_perf(addr);
    }
    printf("Average swap-in time: %lu\n", total / PERF_RUNS);

    close(pagemap_fd);
    munmap(addr, 4096);

    return 0;
}
