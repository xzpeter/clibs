#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <inttypes.h>
#include <time.h>

static uint64_t getns(void)
{
    struct timespec tp;

    clock_gettime(CLOCK_MONOTONIC, &tp);
    return tp.tv_sec * 1000000000 + tp.tv_nsec;
}

int main(int argc, char *argv[])
{
    unsigned long i, loops, val;
    uint64_t time1, time2;

    if (argc < 2) {
        printf("usage: %s <loops>\n", argv[0]);
        return -1;
    }
    loops = strtoul(argv[1], NULL, 10);
    val = 0;
    time1 = getns();
    for (i = 1; i <= loops; i++) {
        __atomic_compare_exchange_n(&val, &val, i, false,
                                    __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
    }
    time2 = getns();
    printf("cmpxchg took %ld (ns) in %ld loops.\n", (time2-time1)/loops, loops);
    return 0;
}
