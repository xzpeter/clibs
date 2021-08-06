#include <stdio.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <assert.h>
#include <inttypes.h>

#define BIT_ULL(nr)                 (1ULL << (nr))
#define PM_SWAP                       BIT_ULL(62)
#define PM_PRESENT                    BIT_ULL(63)

static int pagemap_open(void)
{
	int fd = open("/proc/self/pagemap", O_RDONLY);
	assert(fd >= 0);
	return fd;
}

static uint64_t pagemap_read_vaddr(int fd, void *vaddr)
{
	uint64_t value;
	int ret;

	ret = pread(fd, &value, sizeof(uint64_t),
		    ((uint64_t)vaddr >> 12) * sizeof(uint64_t));
	assert(ret == sizeof(uint64_t));

	return value;
}

static void dump(int fd, void *addr, const char *msg)
{
    uint64_t val = pagemap_read_vaddr(fd, addr);

    printf("%30s: present bit %d, swap bit %d\n", msg,
           !!(val & PM_PRESENT), !!(val & PM_SWAP));
}

int main(void)
{
    char *addr = mmap(NULL, 4096, PROT_READ|PROT_WRITE,
                      MAP_SHARED|MAP_ANONYMOUS, -1, 0);
    int fd;

    assert(addr && ((uint64_t)addr & 4095)==0);
    fd = pagemap_open();

    /* Fault in */
    *addr = 1;
    dump(fd, addr, "FAULT1 (expect swap==0)");

    /* Page it out */
    madvise(addr, 4096, MADV_PAGEOUT);
    dump(fd, addr, "PAGEOUT (expect swap==1)");

    /* Re-fault; test PTE marker */
    *addr = 2;
    dump(fd, addr, "FAULT2 (expect swap==0)");

    /* Truncate, test dropping page */
    madvise(addr, 4096, MADV_REMOVE);
    dump(fd, addr, "REMOVE (expect swap==0)");

    /* Below sequences test dropping of pte marker */
    *addr = 3;
    madvise(addr, 4096, MADV_PAGEOUT);
    dump(fd, addr, "PAGEOUT (expect swap==1)");
    madvise(addr, 4096, MADV_REMOVE);
    dump(fd, addr, "REMOVE (expect swap==0)");

    close(fd);
    munmap(addr, 4096);
    return 0;
}
