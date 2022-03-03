#define _GNU_SOURCE         /* See feature_test_macros(7) */
#include <stdio.h>
#include <assert.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/types.h>

int page_size;
int shmem_fd;
char *buffer;

void main(void)
{
	int ret;
	char val;

	page_size = getpagesize();
	shmem_fd = memfd_create("test", 0);
	assert(shmem_fd >= 0);

	ret = ftruncate(shmem_fd, page_size * 2);
	assert(ret == 0);

	buffer = mmap(NULL, page_size * 2, PROT_READ | PROT_WRITE,
			MAP_PRIVATE, shmem_fd, 0);
	assert(buffer != MAP_FAILED);

	/* Write private page, swap it out */
	buffer[page_size] = 1;
	madvise(buffer, page_size * 2, MADV_PAGEOUT);

	/* This should drop private buffer[page_size] already */
	ret = ftruncate(shmem_fd, page_size);
	assert(ret == 0);
	/* Recover the size */
	ret = ftruncate(shmem_fd, page_size * 2);
	assert(ret == 0);

	/* Re-read the data, it should be all zero */
	val = buffer[page_size];
	if (val == 0)
		printf("Good\n");
	else
		printf("BUG\n");
}
