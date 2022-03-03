#ifndef __CO_H__
#define __CO_H__

#include <ucontext.h>

#define CO_STACK_SIZE (4096)
#define CO_NAME_LEN (128)

struct coroutine_s {
    char name[CO_NAME_LEN];
    char stack[CO_STACK_SIZE];
    ucontext_t co_ctx;
    ucontext_t main_ctx;
    void (*handler)(void);
};
typedef struct coroutine_s coroutine;

coroutine *coroutine_create(const char *name);
void coroutine_run(coroutine *co, void *func);

#endif
