#include <stdio.h>
#include <sys/mman.h>
#include <assert.h>
#include <unistd.h>

char *buf;
int psize;

void check_incore(const char *msg)
{
    unsigned char out;
    int ret;

    ret = mincore(buf, psize, &out);
    assert(ret == 0);

    printf("%s: incore=%d\n", msg, out);
}

int main(void)
{
    int ret;

    psize = getpagesize();

    buf = mmap(NULL, psize, PROT_READ|PROT_WRITE,
               MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
    assert(buf);

    check_incore("after mmap()");
    *buf = 1;
    check_incore("after written");
    ret = madvise(buf, psize, MADV_PAGEOUT);
    check_incore("after madvise");
    *buf = 2;
    check_incore("after re-written");

    munmap(buf, psize);
    return 0;
}
