#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <linux/userfaultfd.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <stdint.h>
#include <errno.h>
#include <string.h>
#include <sys/syscall.h>
#include <linux/mman.h>
#include <inttypes.h>
#include <stdbool.h>

// Size definitions
#define PAGE_SIZE   4096
#define REGION_SIZE (2 * 1024 * 1024) // 2MB
#define OFFSET_0    0
#define OFFSET_1MB  (1024 * 1024)

// Helper to print errors
#define ERROR_EXIT(msg) do { perror(msg); exit(EXIT_FAILURE); } while (0)

// Get userfaultfd
static int userfaultfd_open() {
    int fd = syscall(SYS_userfaultfd, O_CLOEXEC | O_NONBLOCK);
    if (fd == -1) ERROR_EXIT("userfaultfd");

    struct uffdio_api uffdio_api = {
        .api = UFFD_API,   // Set the API version
        .features = 0
    };

    if (ioctl(fd, UFFDIO_API, &uffdio_api) == -1) {
        perror("UFFDIO_API");
        exit(EXIT_FAILURE);
    }

    return fd;
}

// Register memory with userfaultfd
static void register_userfaultfd(int uffd, void *addr, size_t size) {
    struct uffdio_register reg = {
        .range.start = (unsigned long) addr,
        .range.len = size,
        .mode = UFFDIO_REGISTER_MODE_MISSING
    };
    if (ioctl(uffd, UFFDIO_REGISTER, &reg) == -1)
        ERROR_EXIT("UFFDIO_REGISTER");
}

#define BIT_ULL(nr)		((unsigned long long)(1) << (nr))
#define PM_SWAP			BIT_ULL(62)
#define PM_PRESENT		BIT_ULL(63)

static uint64_t pagemap_get(void *addr) {
    uint64_t pagemap_entry;
    FILE *pagemap = fopen("/proc/self/pagemap", "rb");
    if (!pagemap) ERROR_EXIT("fopen pagemap");

    unsigned long page_index = (unsigned long) addr / PAGE_SIZE;
    fseek(pagemap, page_index * sizeof(uint64_t), SEEK_SET);
    fread(&pagemap_entry, sizeof(uint64_t), 1, pagemap);
    fclose(pagemap);
    printf("pagemap(addr=%p)=0x%"PRIx64"\n", addr, pagemap_entry);

    return pagemap_entry;
}

static bool is_page_swapped(void *addr) {
    // bit 62: swap
    return (pagemap_get(addr) & PM_SWAP);
}

int main() {
    // Step 1: mmap 2MB of anonymous private memory
    void *memory = mmap(NULL, REGION_SIZE, PROT_READ | PROT_WRITE, 
                        MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (memory == MAP_FAILED) ERROR_EXIT("mmap");

    printf("Mapped 2MB at %p\n", memory);

    if (madvise(memory, PAGE_SIZE, MADV_NOHUGEPAGE) == -1)
        ERROR_EXIT("MADV_NOHUGEPAGE");

    // Step 2: Access the first page (trigger page fault)
    printf("Triggering page fault at offset 0...\n");
    ((char *)memory)[OFFSET_0] = 'A'; // Access page at 0

    // Step 3: Create userfaultfd and register 2MB range
    int uffd = userfaultfd_open();
    register_userfaultfd(uffd, memory, REGION_SIZE);
    printf("Userfaultfd registered for the region.\n");

    // Step 4: Page it out using MADV_PAGEOUT
    printf("Triggering MADV_PAGEOUT...\n");
    if (madvise(memory, PAGE_SIZE, MADV_PAGEOUT) == -1)
        ERROR_EXIT("MADV_PAGEOUT");

    // Step 5: Verify the page is paged out
    sleep(1); // Give kernel time to evict
    if (is_page_swapped(memory))
        printf("Page at offset 0 is successfully swapped out!\n");
    else
        ERROR_EXIT("Page is still resident in RAM!\n");

    // Step 6: Move the swap entry using UFFDIO_MOVE
    struct uffdio_move move = {
        .dst = (unsigned long)(memory + OFFSET_1MB),
        .src = (unsigned long)(memory + OFFSET_0),
        .len = PAGE_SIZE,
        .mode = 0
    };
    if (ioctl(uffd, UFFDIO_MOVE, &move) == -1)
        ERROR_EXIT("UFFDIO_MOVE");

    printf("Moved swap entry from 0x0 to 1MB offset\n");

    // Step 7: Access the page at 1MB, causing a swap-in
    printf("Accessing the page at 1MB to trigger swap-in...\n");
    char val = ((char *)memory)[OFFSET_1MB];
    printf("Page at 1MB read successfully! Value: %c\n", val);

    // Cleanup
    close(uffd);
    munmap(memory, REGION_SIZE);

    return 0;
}
