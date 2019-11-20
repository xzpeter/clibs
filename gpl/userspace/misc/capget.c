#include <sys/capability.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <unistd.h>
#include <errno.h>
#include <assert.h>

int main(int argc, char *argv[])
{
    int pid, cap, ret, count = 0;
    cap_t handle;
    cap_flag_value_t value;

    if (argc > 1) {
        pid = atoi(argv[1]);
    } else {
        pid = getpid();
    }
    
    handle = cap_get_pid(pid);

    if (!handle) {
        printf("error: cap_get_pid() failed with %d\n", errno);
        return errno;
    }

    for (cap = 0; cap < CAP_LAST_CAP; cap++) {
        ret = cap_get_flag(handle, cap, CAP_EFFECTIVE, &value);
        assert(ret == 0);
        if (value == CAP_SET) {
            count++;
            printf("Capability %d set.\n");
        } else {
            assert(value == CAP_CLEAR);
        }
    }

    if (!count) {
        printf("No capability found for PID %d\n", pid);
    }

    return 0;
}
