#include <fcntl.h>
#include "libuffd.h"
#include "libpagemap.h"

int pagemap_open(void)
{
	int fd = open("/proc/self/pagemap", O_RDONLY);

	if (fd < 0)
		err("open pagemap");

	return fd;
}

uint64_t pagemap_get_entry(int fd, char *start)
{
	const unsigned long pfn = (unsigned long)start / getpagesize();
	uint64_t entry;
	int ret;

	ret = pread(fd, &entry, sizeof(entry), pfn * sizeof(entry));
	if (ret != sizeof(entry))
		err("reading pagemap failed");
	return entry;
}
