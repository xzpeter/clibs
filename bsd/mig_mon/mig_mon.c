#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdint.h>
#include <time.h>
#include <fcntl.h>

/* whether allow client change its IP */
#define  MIG_MON_SINGLE_CLIENT       (0)
#define  MIG_MON_PORT                (12323)
#define  MIG_MON_INT_DEF             (1000)
#define  BUF_LEN                     (1024)

static const char *prog_name = NULL;

void usage(void)
{
    printf("usage: %s server [spike_log]\n", prog_name);
    printf("       %s client server_ip [interval_ms]\n", prog_name);
    puts("");
    puts("This is a program that could be used to measure");
    puts("VM migration down time. Please specify work mode.");
    puts("");
    puts("Example usage to measure guest server downtime:");
    puts("");
    printf("1. [on guest]  start server using '%s server /tmp/spike.log'\n",
           prog_name);
    printf("   this will start server, log all spikes into spike.log.\n");
    printf("2. [on client] start client using '%s client GUEST_IP 50'\n",
           prog_name);
    printf("   this starts sending UDP packets to server, interval 50ms.\n");
    printf("3. trigger loop migration (e.g., 100 times)\n");
    printf("4. see the results on server side.\n");
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

uint64_t get_timestamp(void)
{
    return (uint64_t)time(NULL);
}

void write_spike_log(int fd, uint64_t delay)
{
    char spike_buf[1024] = {0};
    int str_len = -1;
    str_len = snprintf(spike_buf, sizeof(spike_buf) - 1,
                       "%ld,%ld\n", get_timestamp(), delay);
    spike_buf[sizeof(spike_buf) - 1] = 0x00;
    write(fd, spike_buf, str_len);
    /* not flushed to make it fast */
}

/*
 * spike_log is the file path to store spikes. Spikes will be
 * stored in the form like (for each line):
 *
 * A,B
 *
 * Here, A is the timestamp in seconds. B is the latency value in
 * ms. If it is NULL, then no log is written at all (which is the
 * default behavior).
 */
int mon_server(const char *spike_log)
{
    char buf[BUF_LEN];
    int sock = 0;
    int ret = 0;
    struct sockaddr_in svr_addr, clnt_addr;
    socklen_t addr_len = sizeof(clnt_addr);
    in_addr_t target = -1;
    uint64_t last = 0, cur = 0, delay = 0, max_delay = 0;
    int spike_fd = -1;
    /*
     * this will store the 1st and 2nd UDP packet latency, as a
     * baseline of latency values (this is very, very possibly the
     * value that you provided as interval when you start the
     * client). This is used to define spikes, using formular:
     *
     *         spike_throttle = first_latency * 2
     */
    uint64_t first_latency = -1, spike_throttle = -1;

    if (spike_log) {
        spike_fd = open(spike_log, O_WRONLY | O_CREAT, 0644);
        if (spike_fd == -1) {
            perror("failed to open spike log");
            return -1;
        }
        ftruncate(spike_fd, 0);
    }

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

        /* if this is _exactly_ the 2nd packet we got, we need to
         * note this down as a baseline. */
        if (first_latency == -1) {
            first_latency = delay;
            printf("1st and 2nd packet latency: %lu (ms)\n", first_latency);
            spike_throttle = delay * 2;
            printf("Setting spike throttle to: %lu (ms)\n", spike_throttle);
            if (spike_fd != -1) {
                printf("Updating spike log initial timestamp\n");
                /* this -1 is meaningless, shows the init timestamp only. */
                write_spike_log(spike_fd, -1);
            }
        }

        /* if we specified spike_log, we need to log spikes into
         * that file. */
        if (spike_fd != -1 && delay >= spike_throttle) {
            write_spike_log(spike_fd, delay);
        }

        /* update LAST */
        last = cur;
        if (delay > max_delay)
            max_delay = delay;
        printf("\r                                                       ");
        printf("\r[%lu] max_delay: %lu (ms), last: %lu (ms)", cur,
               max_delay, delay);
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
    const char *spike_log = NULL;

    prog_name = argv[0];

    if (argc == 1) {
        usage();
        return -1;
    }

    work_mode = argv[1];
    if (!strcmp(work_mode, "server")) {
        puts("starting server mode...");
        if (argc >= 3) {
            spike_log = argv[2];
        }
        ret = mon_server(spike_log);
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
