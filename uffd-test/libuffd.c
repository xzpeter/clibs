#include "libuffd.h"

int uffd_open_dev(unsigned int flags)
{
    int fd, uffd;

    fd = open("/dev/userfaultfd", O_RDWR | O_CLOEXEC);
    if (fd < 0)
        return fd;
    uffd = ioctl(fd, USERFAULTFD_IOC_NEW, flags);
    close(fd);

    return uffd;
}

int uffd_open_sys(unsigned int flags)
{
#ifdef __NR_userfaultfd
    return syscall(__NR_userfaultfd, flags);
#else
    return -1;
#endif
}

int uffd_open(unsigned int flags)
{
    int uffd = uffd_open_sys(flags);

    if (uffd < 0)
        uffd = uffd_open_dev(flags);

    return uffd;
}

int uffd_get_features(uint64_t *features)
{
    struct uffdio_api uffdio_api = { .api = UFFD_API, .features = 0 };
    /*
     * This should by default work in most kernels; the feature list
     * will be the same no matter what we pass in here.
     */
    int fd = uffd_open(UFFD_USER_MODE_ONLY);

    if (fd < 0)
        /* Maybe the kernel is older than user-only mode? */
        fd = uffd_open(0);

    if (fd < 0)
        return fd;

    if (ioctl(fd, UFFDIO_API, &uffdio_api)) {
        close(fd);
        return -errno;
    }

    *features = uffdio_api.features;
    close(fd);

    return 0;
}

int uffd_open_with_api(unsigned int flags, uint64_t features)
{
    struct uffdio_api uffdio_api;
    int ret, uffd;

    uffd = uffd_open(flags);
    if (uffd < 0)
        return uffd;

    uffdio_api.api = UFFD_API;
    uffdio_api.features = features;
    if (ioctl(uffd, UFFDIO_API, &uffdio_api)) {
        ret = -errno;
        close(uffd);
        return ret;
    }

    if (uffdio_api.api != UFFD_API) {
        close(uffd);
        return -EFAULT;
    }

    return uffd;
}

int uffd_register(int uffd, void *addr, uint64_t len,
                  bool miss, bool wp, bool minor)
{
    struct uffdio_register uffdio_register = { 0 };
	uint64_t mode = 0;
	int ret = 0;

	if (miss)
		mode |= UFFDIO_REGISTER_MODE_MISSING;
	if (wp)
		mode |= UFFDIO_REGISTER_MODE_WP;
	if (minor)
		mode |= UFFDIO_REGISTER_MODE_MINOR;

	uffdio_register.range.start = (unsigned long)addr;
	uffdio_register.range.len = len;
	uffdio_register.mode = mode;

	if (ioctl(uffd, UFFDIO_REGISTER, &uffdio_register) == -1)
		ret = -errno;

	return ret;
}

int uffd_unregister(int uffd, void *addr, uint64_t len)
{
	struct uffdio_range range = { .start = (uintptr_t)addr, .len = len };
	int ret = 0;

	if (ioctl(uffd, UFFDIO_UNREGISTER, &range) == -1)
		ret = -errno;

	return ret;
}

int uffd_poison(int uffd, void *addr, uint64_t len)
{
    struct uffdio_poison args = {
        .range.start = (uintptr_t)addr,
        .range.len = len,
    };

    if (ioctl(uffd, UFFDIO_POISON, &args))
        return args.updated;

    assert(args.updated == len);

    return 0;
}
