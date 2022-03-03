#define _GNU_SOURCE             /* See feature_test_macros(7) */
#include <fcntl.h>
#include <stdio.h>
#include <numaif.h>
#include <unistd.h>
#include <sys/mman.h>
#include <assert.h>
#include <stdlib.h>
#include <pthread.h>
#include <semaphore.h>
#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>

#define BSIZE (1UL << 28)       /* 64MB */
#define DELAY 30                /* Something bigger than 10 */

int page_size, npages, *status, *nodes;
pthread_t thread;
char *buffer;
void **pages;
sem_t sem;
char str[4096];
int shmem_fd;

void *evictor_thread(void *data)
{
    int i, ret;

    printf("evictor created\n");

    for (i = 0; i < npages; i++) {
        sem_wait(&sem);
        usleep(random() % DELAY);
        ret = fallocate(shmem_fd, FALLOC_FL_PUNCH_HOLE | FALLOC_FL_KEEP_SIZE,
                        i * page_size, page_size);
        assert(ret == 0);
    }

    printf("evictor quitted\n");

    return NULL;
}

void do_init(void)
{
    int i, ret;

    srand(time(NULL));

    sem_init(&sem, 0, 0);
    page_size = getpagesize();
    npages = BSIZE / page_size;

    shmem_fd = memfd_create("zap-migrate", 0);
    assert(shmem_fd >= 0);

    ret = ftruncate(shmem_fd, BSIZE);
    assert(ret == 0);

    buffer = mmap(NULL, BSIZE, PROT_READ | PROT_WRITE,
                  MAP_SHARED, shmem_fd, 0);
    assert(buffer != MAP_FAILED);

    pages = calloc(npages, sizeof(void *));
    status = calloc(npages, sizeof(int));
    nodes = calloc(npages, sizeof(int));
    assert(pages && status && nodes);

    for (i = 0; i < npages; i++) {
        *(buffer + page_size * i) = i;
    }

    ret = pthread_create(&thread, NULL, evictor_thread, NULL);
    assert(ret == 0);
}

void do_quit(void)
{
    free(pages);
    free(status);
    free(nodes);
    munmap(buffer, BSIZE);
    close(shmem_fd);
}

void move_all_to_node(int node)
{
    long ret;
    int i;

    for (i = 0; i < npages; i++) {
        pages[i] = buffer + page_size * i;
        nodes[i] = node;
        status[i] = 0;
    }

    ret = move_pages(0, npages, pages, nodes, status, MPOL_MF_MOVE);
    assert(ret == 0);

    printf("%s node=%d\n", __func__, node);
}

void move_each_to_node(int node)
{
    long ret;
    int i;

    for (i = 0; i < npages; i++) {
        pages[0] = buffer + page_size * i;
        nodes[0] = node;
        status[0] = -1;

        /* Kick the evictor */
        sem_post(&sem);
        usleep(10);
        ret = move_pages(0, 1, pages, nodes, status, MPOL_MF_MOVE);
        assert(ret == 0);
    }

    printf("%s node=%d\n", __func__, node);
}

void check(void)
{
    pid_t pid = getpid();
    int fd, ret;
    char *p, *end;

    sprintf(str, "/proc/%d/smaps_rollup", pid);
    printf("Dump file: %s\n==========\n", str);
    fd = open(str, O_RDONLY);
    assert(fd >= 0);

    while (1) {
        ret = read(fd, str, sizeof(str)-1);
        assert(ret >= 0);
        if (ret == 0)
            break;
    }

    p = strstr(str, "Pss_Shmem:");
    end = strchr(p, '\n');
    *end = 0;
    printf("%s\n", p);

    close(fd);
}

void main(void)
{
    do_init();
    move_all_to_node(0);
    move_each_to_node(1);
    /* Make sure madvise() all ran */
    pthread_join(thread, NULL);
    /* Should be "0KB" */
    check();
    do_quit();
}
