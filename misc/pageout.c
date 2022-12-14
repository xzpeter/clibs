#include <stdio.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <assert.h>
#include <inttypes.h>
#include <time.h>
#include <sys/wait.h>

int main(int argc, char *argv[])
{
    char *addr;
    int fd, i;
    uint64_t total = 0;
    int psize;
    int test_fork = (argc > 1);
    pid_t pid;
    int child_status;

    psize = getpagesize();

    addr = mmap(NULL, psize, PROT_READ|PROT_WRITE,
                MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
    assert(addr && ((uint64_t)addr & (psize - 1)) == 0);
    printf("addr is: %p\n", addr);

    *addr = 1;
    madvise(addr, psize, MADV_PAGEOUT);
    printf("swapped out, press enter to swapin...");
    getchar();

    if (test_fork) {
        pid = fork();
        if (pid) {
            /* Test child only */
            waitpid(pid, &child_status, 0);
            if (WIFSIGNALED(child_status)) {
                printf("Child stopped by signal %d\n", WTERMSIG(child_status));
            } else {
                printf("Child didn't get any signal\n");
            }
            goto out;
        }
        printf("This is child\n");
    } else {
        printf("This is parent\n");
    }
    printf("pid=%d\n", getpid());

    /* swapin */
    if (*(volatile char *)addr != 1)
        printf("DATA CORRUPT DETECTED: %d (expect 1)\n", *addr);

out:
    munmap(addr, psize);
    return 0;
}
