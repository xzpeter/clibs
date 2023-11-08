#ifndef __LIBUFFD_H__
#define __LIBUFFD_H__

#define _GNU_SOURCE
#include <sys/syscall.h>
#include <linux/userfaultfd.h>
#include <fcntl.h>
#include <stdint.h>
#include <inttypes.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <assert.h>
#include <stdbool.h>

int uffd_open_dev(unsigned int flags);
int uffd_open_sys(unsigned int flags);
int uffd_open(unsigned int flags);
int uffd_get_features(uint64_t *features);
int uffd_open_with_api(unsigned int flags, uint64_t features);
int uffd_poison(int uffd, void *addr, uint64_t len);
int uffd_register(int uffd, void *addr, uint64_t len,
                  bool miss, bool wp, bool minor);
int uffd_unregister(int uffd, void *addr, uint64_t len);

#endif
