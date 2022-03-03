#define _GNU_SOURCE         /* See feature_test_macros(7) */
#include <unistd.h>
#include <sys/syscall.h>   /* For SYS_xxx definitions */
#include <stdio.h>
#include <linux/userfaultfd.h>
#include <unistd.h>
#include <fcntl.h>
#include <assert.h>
#include <sys/ioctl.h>
#include <stdlib.h>
#include <sys/mman.h>

#define  BUF_SIZE  (4096 * 10)

int uffd;

int main(void)
{
    int ret;
    struct uffdio_register uffdio_register;
    struct uffdio_api uffdio_api;
    void *buffer, *buffer2, *new_buffer;

    ret = posix_memalign(&buffer, 4096, BUF_SIZE);
    assert(ret == 0);
    ret = posix_memalign(&buffer2, 4096, BUF_SIZE);
    assert(ret == 0);

    uffd = syscall(__NR_userfaultfd, O_CLOEXEC);
    assert(uffd > 0);

    uffdio_api.api = UFFD_API;
    uffdio_api.features = 0;
    ret = ioctl(uffd, UFFDIO_API, &uffdio_api);
    assert(ret == 0);
    assert(uffdio_api.api == UFFD_API);

    uffdio_register.range.start = (unsigned long) buffer;
    uffdio_register.range.len = BUF_SIZE;
    uffdio_register.mode = UFFDIO_REGISTER_MODE_MISSING;

    ret = ioctl(uffd, UFFDIO_REGISTER, &uffdio_register);
    assert(ret == 0);

    new_buffer = mremap(buffer, BUF_SIZE, BUF_SIZE,
                        MREMAP_MAYMOVE | MREMAP_FIXED, buffer2);
    assert(new_buffer == buffer2);
    printf("New buffer address: %p\n", new_buffer);
    sleep(10000);

    return 0;
}
