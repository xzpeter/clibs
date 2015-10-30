#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdint.h>
#include <time.h>

/* whether allow client change its IP */
#define  MIG_MON_SINGLE_CLIENT       (0)
#define  MIG_MON_PORT                (12323)
#define  MIG_MON_INT_DEF             (1000)
#define  BUF_LEN                     (1024)

static const char *prog_name = NULL;

void usage(void)
{
    printf("usage: %s server\n", prog_name);
    printf("       %s client server_ip [interval_ms]\n", prog_name);
    puts("");
    puts("This is a program that could be used to measure");
    puts("VM migration down time. Please specify work mode.");
}

uint64_t get_msec(void)
{
    uint64_t val = 0;
    struct timespec t;
    int ret = clock_gettime(CLOCK_MONOTONIC, &t);
    if (ret == -1) {
        perror("clock_gettime() failed");
        /* should never happen */
        exit(-1);
    }
    val = t.tv_nsec / 1000000;  /* ns -> ms */
    val += t.tv_sec * 1000;     /* s -> ms */
    return val;
}

int mon_server(void)
{
    char buf[BUF_LEN];
    int sock = 0;
    int ret = 0;
    struct sockaddr_in svr_addr, clnt_addr;
    socklen_t addr_len = sizeof(clnt_addr);
    in_addr_t target = -1;
    uint64_t last = 0, cur = 0, delay = 0, max_delay = 0;

    bzero(&svr_addr, sizeof(svr_addr));
    bzero(&clnt_addr, sizeof(clnt_addr));

    sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        perror("socket() creation failed");
        return -1;
    }

    svr_addr.sin_family = AF_INET;
    svr_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    svr_addr.sin_port = MIG_MON_PORT;

    ret = bind(sock, (struct sockaddr *)&svr_addr, sizeof(svr_addr));
    if (ret == -1) {
        perror("bind() failed");
        return -1;
    }

    printf("listening on UDP port %d...\n", MIG_MON_PORT);
#if MIG_MON_SINGLE_CLIENT
    printf("allowing single client only.\n");
#else
    printf("allowing multiple clients.\n");
#endif

    while (1) {
        ret = recvfrom(sock, buf, BUF_LEN, 0, (struct sockaddr *)&clnt_addr,
                       &addr_len);
        if (ret == -1) {
            perror("recvfrom() error");
            return -1;
        }

        /* update CURRENT */
        cur = get_msec();

        if (target == -1) {
            /* this is the first packet we recved. we should init the
               environment and remember the target client we are monitoring
               for this round. */
            printf("setting monitor target to client '%s'\n",
                   inet_ntoa(clnt_addr.sin_addr));
            target = clnt_addr.sin_addr.s_addr;
            /* also, init LAST */
            last = cur;
            max_delay = 0;
            continue;
        }

#if MIG_MON_SINGLE_CLIENT
        /* this is not the first packet we received, we will only monitor
           the target client, and disgard all the other packets recved. */
        if (clnt_addr.sin_addr.s_addr != target) {
            printf("\nWARNING: another client (%s:%d) is connecting...\n",
                   inet_ntoa(clnt_addr.sin_addr),
                   ntohs(clnt_addr.sin_port));
            /* disgard it! */
            continue;
        }
#endif

        /* this is the packet we want to measure with */
        delay = cur - last;
        /* update LAST */
        last = cur;
        if (delay > max_delay)
            max_delay = delay;
        printf("\r                                                       \r");
        printf("[%lu] max_delay: %lu (ms), last: %lu (ms)", cur, max_delay,
               delay);
        fflush(stdout);
    }
    
    return 0;
}

int mon_client(const char *server_ip, int interval_ms)
{
    char buf[BUF_LEN] = "echo";
    int ret = 0;
    int sock = 0;
    struct sockaddr_in addr;
    socklen_t addr_len = sizeof(addr);
    int msg_len = strlen(buf);
    uint64_t cur = 0;
    int int_us = interval_ms * 1000;

    bzero(&addr, sizeof(addr));

    sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock == -1) {
        perror("socket() failed");
        return -1;
    }

    addr.sin_family = AF_INET;
    addr.sin_port = MIG_MON_PORT;
    if (inet_aton(server_ip, &addr.sin_addr) != 1) {
        printf("server ip '%s' invalid\n", server_ip);
        close(sock);
        return -1;
    }

    while (1) {
        ret = sendto(sock, buf, msg_len, 0, (struct sockaddr *)&addr,
                     addr_len);
        if (ret == -1) {
            perror("sendto() failed");
            close(sock);
            return -1;
        } else if (ret != msg_len) {
            printf("sendto() returned %d?\n", ret);
            close(sock);
            return -1;
        }
        cur = get_msec();
        printf("\r                                                  ");
        printf("\r[%lu] sending packet to %s:%d", cur, server_ip,
               MIG_MON_PORT);
        fflush(stdout);
        usleep(int_us);
    }

    return 0;
}

int main(int argc, char *argv[])
{
    int ret = 0;
    int interval_ms = MIG_MON_INT_DEF;
    const char *work_mode = NULL;
    const char *server_ip = NULL;

    prog_name = argv[0];

    if (argc == 1) {
        usage();
        return -1;
    }

    work_mode = argv[1];
    if (!strcmp(work_mode, "server")) {
        puts("starting mig_mon server...");
        ret = mon_server();
    } else if (!strcmp(work_mode, "client")) {
        if (argc < 3) {
            usage();
            return -1;
        }
        server_ip = argv[2];
        if (argc >= 4) {
            interval_ms = strtol(argv[3], NULL, 10);
        }
        puts("starting client mode...");
        printf("server ip: %s, interval: %d (ms)\n", server_ip, interval_ms);
        ret = mon_client(server_ip, interval_ms);
    } else {
        usage();
        return -1;
    }

    return ret;
}
