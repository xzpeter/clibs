#include <stdint.h>
#include <stdio.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include <errno.h>
#include <sys/sysinfo.h>

#define BUFSIZE (2*1024*1024)

char *buffer;
volatile int start;
int pages, psize;

void *thread_fn(void *data)
{
    int i = (uintptr_t)data;
    int op = i % 3;
    int page = i % pages;

    while (!start);

    while (1) {
        switch (op) {
        case 0:
            /* Read */
            op = buffer[psize * page];
            break;
        case 1:
            /* Write */
            buffer[psize * page] = (char)op;
            break;
        case 2:
            /* Free */
            madvise(&buffer[psize * page], psize, MADV_DONTNEED);
            break;
        default:
            /* Never reach */
            exit(-1);
            break;
        }
        op += 1; op %= 3;
        page += 13; page %= pages;
    }

    return NULL;
}

int main(void)
{
    int ret, i;
    pthread_t *threads;
    int N;

    N = get_nprocs();
    threads = calloc(1, sizeof(pthread_t) * N);

    start = 0;
    psize = getpagesize();
    pages = BUFSIZE / psize;

    buffer = (char *)aligned_alloc(BUFSIZE, BUFSIZE);

    if (!buffer) {
        printf("Failed to allocate mem\n");
        return -1;
    }
    // printf("Buffer allocated: %p\n", buffer);

    ret = madvise(buffer, BUFSIZE, MADV_NOHUGEPAGE);
    if (ret) {
        printf("madvise() failed on NOHUGEPAGE, maybe thp disabled?\n");
    }

    for (i = 0; i < N; i++) {
        ret = pthread_create(&threads[i], NULL, thread_fn, (void *)(uintptr_t)i);
        if (ret) {
            printf("pthread_create() failed: %s\n", strerror(ret));
            return -1;
        }
    }

    /* Start all the threads! */
    start = 1;

    for (i = 0; i < N; i++) {
        pthread_join(threads[i], NULL);
    }

    return 0;
}
