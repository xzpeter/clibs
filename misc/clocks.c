#include <time.h>
#include <stdio.h>
#include <unistd.h>
#include <assert.h>

typedef struct {
    char *name;
    clockid_t clockid;
} clock_s;

clock_s clock_list[] = {
    { "realtime", CLOCK_REALTIME },
    { "monotonic", CLOCK_MONOTONIC },
    { "boottime", CLOCK_BOOTTIME },
};

#define N_CLOCKS (sizeof(clock_list) / sizeof(clock_s))

int main(void)
{
    unsigned int count, i, ret;

    printf("%10s", "index");

    for (i = 0; i < N_CLOCKS; i++) {
        printf("%30s", clock_list[i].name);
    }
    printf("\n\n");

    for (count = 0;; count++) {
        printf("%10u", count);
        for (i = 0; i < N_CLOCKS; i++) {
            clockid_t id = clock_list[i].clockid;
            struct timespec ts;

            ret = clock_gettime(id, &ts);
            assert(ret == 0);
            printf("%23llu.%06llu", ts.tv_sec, ts.tv_nsec / 1000);
        }
        printf("\n");
        sleep(1);
    }

    return 0;
}
