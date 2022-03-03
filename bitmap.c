#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <inttypes.h>
#include <time.h>
#include <sys/mman.h>
#include <assert.h>

/* Bitmap for 8TB mem: 8TB / 32K = 256MB */
#define  BMAP_MB    (256)
#define  BMAP_SIZE  (BMAP_MB * (1UL << 20))

unsigned long *bitmap, *target_bitmap;

#define __X86_CASE_B	1
#define __X86_CASE_W	2
#define __X86_CASE_L	4
#define __X86_CASE_Q	8

#define __xchg_op(ptr, arg, op, lock)					\
	({								\
	        __typeof__ (*(ptr)) __ret = (arg);			\
		switch (sizeof(*(ptr))) {				\
		case __X86_CASE_B:					\
			asm volatile (lock #op "b %b0, %1\n"		\
				      : "+q" (__ret), "+m" (*(ptr))	\
				      : : "memory", "cc");		\
			break;						\
		case __X86_CASE_W:					\
			asm volatile (lock #op "w %w0, %1\n"		\
				      : "+r" (__ret), "+m" (*(ptr))	\
				      : : "memory", "cc");		\
			break;						\
		case __X86_CASE_L:					\
			asm volatile (lock #op "l %0, %1\n"		\
				      : "+r" (__ret), "+m" (*(ptr))	\
				      : : "memory", "cc");		\
			break;						\
		case __X86_CASE_Q:					\
			asm volatile (lock #op "q %q0, %1\n"		\
				      : "+r" (__ret), "+m" (*(ptr))	\
				      : : "memory", "cc");		\
			break;						\
		}							\
		__ret;							\
	})
#define xchg(ptr, v)	__xchg_op((ptr), (v), xchg, "")

unsigned long getns(void)
{
    struct timespec tp;

    clock_gettime(CLOCK_MONOTONIC, &tp);
    return tp.tv_sec * 1000000000 + tp.tv_nsec;
}

unsigned long *alloc_bitmap(void)
{
    unsigned long *bmap;

    bmap = mmap(NULL, BMAP_SIZE, PROT_READ|PROT_WRITE,
                MAP_ANONYMOUS|MAP_PRIVATE, -1, 0);
    assert(bmap);
    return bmap;
}

void init_bitmaps(void)
{
    unsigned long i;

    bitmap = alloc_bitmap();
    target_bitmap = alloc_bitmap();

    srand(getns());
    for (i = 0; i < BMAP_SIZE/sizeof(*bitmap); i++) {
        /* Initialize src bitmap with random data */
        bitmap[i] = rand();
        /* Pre-fault target bitmap to avoid page fault overhead */
        target_bitmap[i] = 0;
    }
}

void copy_bitmap(void)
{
    unsigned long i, start, end, total;

    start = getns();
    for (i = 0; i < BMAP_SIZE/sizeof(*bitmap); i++) {
#ifdef USE_ATOMIC
        target_bitmap[i] = xchg(&bitmap[i], 0);
#else
        target_bitmap[i] = bitmap[i];
#endif
    }
    end = getns();
    total = end - start;

    printf("Copy bitmap (size=%dMB) took %ld (us), each op: %ld (ns).\n",
           BMAP_MB, total / 1000, total / i);
}

void main(void)
{
    init_bitmaps();
    copy_bitmap();
}
