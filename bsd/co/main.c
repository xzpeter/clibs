#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "co.h"

void test_func(void)
{
    printf("in test func...\n");
    sleep(1);
    printf("done!\n");
}

int main(void)
{
    coroutine *co = NULL;
    co = coroutine_create("peter_co");
    coroutine_run(co, &test_func);
    coroutine_run(co, &test_func);
    return 0;
}
