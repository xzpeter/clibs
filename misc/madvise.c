#include <stdio.h>
#include <sys/mman.h>
#include <errno.h>
#include <assert.h>

#define BUF_SIZE (1024 * 1024 * 1024)

void hold(const char *msg)
{
    puts(msg);
    getchar();
}

int main(void)
{
    char *buf = mmap(0, BUF_SIZE, PROT_READ | PROT_WRITE,
                     MAP_PRIVATE | MAP_ANONYMOUS, 0, 0);
    char *p, n = 0;

    assert(buf);

    hold("init array");

    for (p = buf; p < buf + BUF_SIZE; p++) {
        *p = n++;
    }

    printf("buf[1] = %d\n", buf[1]);

    hold("madvise");

    if(madvise(buf, BUF_SIZE, MADV_DONTNEED)) {
        perror("madvise() failed");
    }

    hold("before quit");

    printf("buf[1] = %d\n", buf[1]);
    buf[1] = 1;
    printf("buf[1] = %d (after write)\n", buf[1]);

    return 0;
}
