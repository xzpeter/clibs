#ifndef __LIBPAGEMAP_H__

#include <stdint.h>

#define PM_SOFT_DIRTY       BIT_ULL(55)
#define PM_MMAP_EXCLUSIVE   BIT_ULL(56)
#define PM_UFFD_WP          BIT_ULL(57)
#define PM_FILE             BIT_ULL(61)
#define PM_SWAP             BIT_ULL(62)
#define PM_PRESENT          BIT_ULL(63)

#define  pagemap_check_wp(value, wp) do {                       \
        if (!!(value & PM_UFFD_WP) != wp)                       \
            err("pagemap uffd-wp bit error: 0x%"PRIx64, value); \
	} while (0)

int pagemap_open(void);
uint64_t pagemap_get_entry(int fd, char *start);

#endif
