#define BIT_ULL(nr)		(1ULL << (nr))

#include <stdlib.h>

#define _err(fmt, ...)                                  \
    do {                                                \
        int ret = errno;                                \
        fprintf(stderr, "ERROR: " fmt, ##__VA_ARGS__);	\
        fprintf(stderr, " (errno=%d, line=%d)\n",       \
                ret, __LINE__);                         \
	} while (0)

#define err(fmt, ...)                           \
	do {                                        \
		_err(fmt, ##__VA_ARGS__);               \
		exit(1);                                \
	} while (0)
