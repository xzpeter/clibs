#include <stdio.h>
#include <sys/ioctl.h>
#include <termios.h>

static struct termios term;

int main(void)
{
    int ret, i;

    ret = ioctl(1, TCGETS, &term);
    if (ret == 0) {
        printf("Input mode flag:\t0x%x\n", term.c_iflag);
        printf("Output mode flag:\t0x%x\n", term.c_oflag);
        printf("Control mode flag:\t0x%x\n", term.c_cflag);
        printf("Local mode flag:\t0x%x\n", term.c_lflag);
        printf("Line discipline:\t0x%x\n", term.c_line);

        printf("Control chars:\t\t");
        for (i = 0; i < NCCS; i++)
            printf("0x%x ", term.c_cc[i]);
        printf("\b\n");
        
        printf("Input speed:\t\t%d\n", term.c_ispeed);
        printf("Output speed:\t\t%d\n", term.c_ospeed);
    } else {
        perror("ioctl(TCGETS) failed");
    }

    return ret;
}
