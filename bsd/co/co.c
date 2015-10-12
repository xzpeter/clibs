#include <stdio.h>
#include <ucontext.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include "co.h"

union co_arg_u {
    int i[2];
    void *p;
};
typedef union co_arg_u co_arg;

static void coroutine_trampoline(int i0, int i1)
{
    coroutine *co = NULL;
    co_arg arg;
    arg.i[0] = i0;
    arg.i[1] = i1;
    co = (coroutine *)arg.p;
    printf("co: starting coroutine: %s\n", co->name);

    while (1) {
        printf("co: switching back to main\n");
        swapcontext(&co->co_ctx, &co->main_ctx);
        printf("co: running handler\n");
        co->handler();
        printf("co: finished handler\n");
    }
}

coroutine *coroutine_create(const char *name)
{
    co_arg arg;
    coroutine *co = calloc(1, sizeof(*co));
    assert(co);
    arg.p = co;
    strncpy(co->name, name, CO_NAME_LEN - 1);

    getcontext(&co->co_ctx);
    co->co_ctx.uc_stack.ss_sp = co->stack;
    co->co_ctx.uc_stack.ss_size = CO_STACK_SIZE;
    co->co_ctx.uc_stack.ss_flags = 0;
    co->co_ctx.uc_link = &co->main_ctx;
    makecontext(&co->co_ctx, (void (*)(void))coroutine_trampoline,
                2, arg.i[0], arg.i[1]);

    /* switch to coroutine immediately, and return when set up */
    swapcontext(&co->main_ctx, &co->co_ctx);

    printf("main: coroutine created\n");

    return co;
}

void coroutine_run(coroutine *co, void *func)
{
    co->handler = func;
    printf("main: trigger coroutine %s to run\n", co->name);
    swapcontext(&co->main_ctx, &co->co_ctx);
    printf("main: coroutine %s returned\n", co->name);
}
