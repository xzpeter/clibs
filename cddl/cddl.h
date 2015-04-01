#ifndef __TYPES_H__
#define __TYPES_H__

#define    ASSERT          assert
#define    B_TRUE          (1)
#define    B_FALSE         (0)

#ifndef	offsetof
#define	offsetof(s, m)		((size_t)(&(((s *)0)->m)))
#endif

typedef int boolean_t;
typedef unsigned long ulong_t;

#endif
