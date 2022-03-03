#include <stdio.h>
#include <pthread.h>
#include <stdlib.h>

unsigned long v1, v2;

#define WRITE_ONCE(a, v) *(volatile typeof(*a) *)a = v
#define READ_ONCE(a) *(volatile typeof(*a) *)a

void *thread1(void *unused)
{
    unsigned long register count = 1;

    while (1) {
        WRITE_ONCE(&v1, count);
        WRITE_ONCE(&v2, count);
        count++;
    }
}

void *thread2(void *unused)
{
    unsigned long t1, t2;

    while (1) {
        t1 = READ_ONCE(&v1);
        t2 = READ_ONCE(&v2);
        if (t1 > t2) {
            printf("Misordering detected: t1 (%ld) > t2 (%ld)\n",
                   t1, t2);
            exit(1);
        }
    }
}

int main()
{
    pthread_t thr1, thr2;

    pthread_create(&thr1, NULL, thread1, NULL);
    pthread_create(&thr2, NULL, thread2, NULL);
    pthread_join(thr1, NULL);
    pthread_join(thr2, NULL);

    return 0;
}
