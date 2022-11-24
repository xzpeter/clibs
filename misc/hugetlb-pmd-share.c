#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <linux/memfd.h>
#include <assert.h>
#include <pthread.h>

#define  MSIZE  (1UL << 30)     /* 1GB */
#define  PSIZE  (2UL << 20)     /* 2MB */

#define  HOLD_SEC  (1)

int pipefd[2];
void *buf;

void *do_map(int fd)
{
    unsigned char *tmpbuf, *p;
    int ret;

    ret = posix_memalign((void **)&tmpbuf, MSIZE, MSIZE);
    if (ret) {
        perror("posix_memalign() failed");
        return NULL;
    }

    tmpbuf = mmap(tmpbuf, MSIZE, PROT_READ | PROT_WRITE,
                  MAP_SHARED | MAP_FIXED, fd, 0);
    if (tmpbuf == MAP_FAILED) {
        perror("mmap() failed");
        return NULL;
    }
    printf("mmap() -> %p\n", tmpbuf);

    for (p = tmpbuf; p < tmpbuf + MSIZE; p += PSIZE) {
        *p = 1;
    }

    return tmpbuf;
}

void do_unmap(void *buf)
{
    munmap(buf, MSIZE);
}

void proc2(int fd)
{
    unsigned char c;

    buf = do_map(fd);
    if (!buf)
        return;

    read(pipefd[0], &c, 1);
    /*
     * This frees the shared pgtable page, causing use-after-free in
     * proc1_thread1 when soft walking hugetlb pgtable.
     */
    do_unmap(buf);

    printf("Proc2 quitting\n");
}

void *proc1_thread1(void *data)
{
    /*
     * Trigger follow-page on 1st 2m page.  Kernel hack patch needed to
     * withhold this procedure for easier reproduce.
     */
    madvise(buf, PSIZE, MADV_POPULATE_WRITE);
    printf("Proc1-thread1 quitting\n");
    return NULL;
}

void *proc1_thread2(void *data)
{
    unsigned char c;

    /* Wait a while until proc1_thread1() start to wait */
    sleep(0.5);
    /* Trigger pmd unshare */
    madvise(buf, PSIZE, MADV_DONTNEED);
    /* Kick off proc2 to release the pgtable */
    write(pipefd[1], &c, 1);

    printf("Proc1-thread2 quitting\n");
    return NULL;
}

void proc1(int fd)
{
    pthread_t tid1, tid2;
    int ret;

    buf = do_map(fd);
    if (!buf)
        return;

    ret = pthread_create(&tid1, NULL, proc1_thread1, NULL);
    assert(ret == 0);
    ret = pthread_create(&tid2, NULL, proc1_thread2, NULL);
    assert(ret == 0);

    /* Kick the child to share the PUD entry */
    pthread_join(tid1, NULL);
    pthread_join(tid2, NULL);

    do_unmap(buf);
}

int main(void)
{
    int fd, ret;

    fd = memfd_create("test-huge", MFD_HUGETLB | MFD_HUGE_2MB);
    if (fd < 0) {
        perror("open failed");
        return -1;
    }

    ret = ftruncate(fd, MSIZE);
    if (ret) {
        perror("ftruncate() failed");
        return -1;
    }

    ret = pipe(pipefd);
    if (ret) {
        perror("pipe() failed");
        return -1;
    }

    if (fork()) {
        proc1(fd);
    } else {
        proc2(fd);
    }

    close(pipefd[0]);
    close(pipefd[1]);
    close(fd);

    return 0;
}
