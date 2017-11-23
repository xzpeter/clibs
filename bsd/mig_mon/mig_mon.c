#include <assert.h>
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
#include <errno.h>

/* whether allow client change its IP */
#define  MIG_MON_SINGLE_CLIENT       (0)
#define  MIG_MON_PORT                (12323)
#define  MIG_MON_INT_DEF             (1000)
#define  BUF_LEN                     (1024)
#define  MIG_MON_SPIKE_LOG_DEF       ("/tmp/spike.log")
#define  DEF_MM_DIRTY_SIZE           (512)

static const char *prog_name = NULL;

void usage(void)
{
    puts("");
    puts("======== VM Migration Downtime Measurement ========");
    puts("");
    puts("This is a program that could be used to measure");
    puts("VM migration down time. Please specify work mode.");
    puts("");
    puts("Example usage to measure guest server downtime (single way):");
    puts("");
    printf("1. [on guest]  start server using '%s server /tmp/spike.log'\n",
           prog_name);
    printf("   this will start server, log all spikes into spike.log.\n");
    printf("2. [on client] start client using '%s client GUEST_IP 50'\n",
           prog_name);
    printf("   this starts sending UDP packets to server, interval 50ms.\n");
    printf("3. trigger loop migration (e.g., 100 times)\n");
    printf("4. see the results on server side.\n");
    puts("");
    puts("Example usage to measure round-trip downtime:");
    puts("(This is preferred since it simulates a simplest server behavior)");
    puts("");
    printf("1. [on guest]  start server using '%s server_rr'\n",
           prog_name);
    printf("   this will start a UDP echo server.\n");
    printf("2. [on client] start client using '%s client GUEST_IP 50 spike.log'\n",
           prog_name);
    printf("   this starts sending UDP packets to server, then try to recv it.\n");
    printf("   the timeout of recv() will be 50ms.\n");
    printf("3. trigger loop migration (e.g., 100 times)\n");
    printf("4. see the results on client side.\n");
    puts("");

    puts("======== Memory Dirty Workload ========");
    puts("");
    puts("This tool can also generate dirty memory workload in different ways.");
    puts("Please see the command 'mm_dirty' for more information.");
    puts("");

    printf("usage: %s server [spike_log]\n", prog_name);
    printf("       %s client server_ip [interval_ms]\n", prog_name);
    printf("       %s server_rr\n", prog_name);
    printf("       %s client_rr server_ip [interval_ms [spike_log]]\n",
           prog_name);
    puts("");
    printf("       %s mm_dirty [mm_size in MB [dirty_rate in MB/s]]\n", prog_name);
    printf("       \t\t(default mm_size=%dMB, dirty_rate=max)\n", DEF_MM_DIRTY_SIZE);
    puts("");
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
 * State machine for the event handler. It just starts from 0 until
 * RUNNING.
 */
enum event_state {
    /* Idle, waiting for first time triggering event */
    STATE_WAIT_FIRST_TRIGGER = 0,
    /* Got first event, waiting for the 2nd one */
    STATE_WAIT_SECOND_TRIGGER = 1,
    /* Normal running state */
    STATE_RUNNING = 2,
    STATE_MAX
};

/*
 * This is a state machine to handle the incoming event. Return code
 * is the state before calling this handler.
 */
enum event_state handle_event(int spike_fd)
{
    /* Internal static variables */
    static enum event_state state = STATE_WAIT_FIRST_TRIGGER;
    static uint64_t last = 0, max_delay = 0;
    /*
     * this will store the 1st and 2nd UDP packet latency, as a
     * baseline of latency values (this is very, very possibly the
     * value that you provided as interval when you start the
     * client). This is used to define spikes, using formular:
     *
     *         spike_throttle = first_latency * 2
     */
    static uint64_t first_latency = 0, spike_throttle = 0;

    /* Temp variables */
    uint64_t cur = 0, delay = 0;
    enum event_state old_state = state;

    cur = get_msec();

    if (last) {
        /*
         * If this is not exactly the first event we got, we calculate
         * the delay.
         */
        delay = cur - last;
    }

    switch (state) {
    case STATE_WAIT_FIRST_TRIGGER:
        assert(last == 0);
        assert(max_delay == 0);
        /*
         * We need to do nothing here, just to init the "last", which
         * will be done after the switch().
         */
        state++;
        break;

    case STATE_WAIT_SECOND_TRIGGER:
        /*
         * if this is _exactly_ the 2nd packet we got, we need to note
         * this down as a baseline.
         */
        assert(first_latency == 0);
        first_latency = delay;
        printf("1st and 2nd packet latency: %lu (ms)\n", first_latency);
        spike_throttle = delay * 2;
        printf("Setting spike throttle to: %lu (ms)\n", spike_throttle);
        if (spike_fd != -1) {
            printf("Updating spike log initial timestamp\n");
            /* this -1 is meaningless, shows the init timestamp only. */
            write_spike_log(spike_fd, -1);
        }
        state++;
        break;

    case STATE_RUNNING:
        if (delay > max_delay) {
            max_delay = delay;
        }
        /*
         * if we specified spike_log, we need to log spikes into that
         * file.
         */
        if (spike_fd != -1 && delay >= spike_throttle) {
            write_spike_log(spike_fd, delay);
        }
        printf("\r                                                       ");
        printf("\r[%lu] max_delay: %lu (ms), cur: %lu (ms)", cur,
               max_delay, delay);
        fflush(stdout);
        break;

    default:
        printf("Unknown state: %d\n", state);
        exit(1);
        break;
    }

    /* update LAST */
    last = cur;

    return old_state;
}

int spike_log_open(const char *spike_log)
{
    int spike_fd = -1;

    if (spike_log) {
        spike_fd = open(spike_log, O_WRONLY | O_CREAT, 0644);
        if (spike_fd == -1) {
            perror("failed to open spike log");
            /* Silently disable spike log */
        } else {
            ftruncate(spike_fd, 0);
        }
    }

    return spike_fd;
}

/* Mig_mon callbacks. Return 0 for continue, non-zero for errors. */
typedef int (*mon_server_cbk)(int sock, int spike_fd);
typedef int (*mon_client_cbk)(int sock, int spike_fd, int interval_ms);

int mon_server_callback(int sock, int spike_fd)
{
    static in_addr_t target = -1;
    int ret;
    char buf[BUF_LEN];
    struct sockaddr_in clnt_addr = {};
    socklen_t addr_len = sizeof(clnt_addr);

    ret = recvfrom(sock, buf, BUF_LEN, 0, (struct sockaddr *)&clnt_addr,
                   &addr_len);
    if (ret == -1) {
        perror("recvfrom() error");
        return -1;
    }

    if (target == -1) {
        /* this is the first packet we recved. we should init the
           environment and remember the target client we are monitoring
           for this round. */
        printf("setting monitor target to client '%s'\n",
               inet_ntoa(clnt_addr.sin_addr));
        target = clnt_addr.sin_addr.s_addr;
        /* Should be the first time calling */
        assert(handle_event(spike_fd) == STATE_WAIT_FIRST_TRIGGER);
        return 0;
    }

#if MIG_MON_SINGLE_CLIENT
    /* this is not the first packet we received, we will only monitor
       the target client, and disgard all the other packets recved. */
    if (clnt_addr.sin_addr.s_addr != target) {
        printf("\nWARNING: another client (%s:%d) is connecting...\n",
               inet_ntoa(clnt_addr.sin_addr),
               ntohs(clnt_addr.sin_port));
        /* disgard it! */
        return 0;
    }
#endif

    handle_event(spike_fd);

    return 0;
}

/* This is actually a udp ECHO server. */
int mon_server_rr_callback(int sock, int spike_fd)
{
    int ret;
    char buf[BUF_LEN];
    struct sockaddr_in clnt_addr = {};
    socklen_t addr_len = sizeof(clnt_addr);
    uint64_t cur;

    ret = recvfrom(sock, buf, BUF_LEN, 0, (struct sockaddr *)&clnt_addr,
                   &addr_len);
    if (ret == -1) {
        perror("recvfrom() error");
        return -1;
    }

    ret = sendto(sock, buf, ret, 0, (struct sockaddr *)&clnt_addr,
                 addr_len);
    if (ret == -1) {
        perror("sendto() error");
        return -1;
    }

    cur = get_msec();

    printf("\r                                                  ");
    printf("\r[%lu] responding to client", cur);
    fflush(stdout);

    return 0;
}

/*
 * spike_log is the file path to store spikes. Spikes will be
 * stored in the form like (for each line):
 *
 * A,B
 *
 * Here, A is the timestamp in seconds. B is the latency value in
 * ms.
 */
int mon_server(const char *spike_log, mon_server_cbk server_callback)
{
    int sock = 0;
    int ret = 0;
    struct sockaddr_in svr_addr = {};
    int spike_fd = spike_log_open(spike_log);

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
        ret = server_callback(sock, spike_fd);
        if (ret) {
            break;
        }
    }

    return ret;
}

int mon_client_callback(int sock, int spike_fd, int interval_ms)
{
    int ret;
    uint64_t cur;
    char buf[BUF_LEN] = "echo";
    int msg_len = strlen(buf);
    int int_us = interval_ms * 1000;

    ret = sendto(sock, buf, msg_len, 0, NULL, 0);
    if (ret == -1) {
        perror("sendto() failed");
        return -1;
    } else if (ret != msg_len) {
        printf("sendto() returned %d?\n", ret);
        return -1;
    }
    cur = get_msec();
    printf("\r                                                  ");
    printf("\r[%lu] sending packet to server", cur);
    fflush(stdout);
    usleep(int_us);

    return 0;
}

int socket_set_timeout(int sock, int timeout_ms)
{
    return setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO,
                      (void *)&timeout_ms, sizeof(int));
}

int mon_client_rr_callback(int sock, int spike_fd, int interval_ms)
{
    int ret;
    uint64_t cur;
    char buf[BUF_LEN] = "echo";
    int msg_len = strlen(buf);
    static int init = 0;
    static uint64_t last = 0;

    if (!init) {
        printf("Setting socket recv timeout to %d (ms)\n",
               interval_ms);
        socket_set_timeout(sock, interval_ms);
        init = 1;
    }

    cur = get_msec();

    if (last) {
        /*
         * This is not the first packet, we need to wait until we
         * reaches the interval.
         */
        int64_t delta = last + interval_ms - cur;
        if (delta > 0) {
            usleep(delta * 1000);
        }
    }

    last = get_msec();

    ret = sendto(sock, buf, msg_len, 0, NULL, 0);
    if (ret == -1) {
        perror("sendto() failed");
        return -1;
    } else if (ret != msg_len) {
        printf("sendto() returned %d?\n", ret);
        return -1;
    }

    ret = recvfrom(sock, buf, msg_len, 0, NULL, 0);
    if (ret == -1) {
        if (errno == ECONNREFUSED) {
            /*
             * This is when server is down, e.g., due to migration. So
             * this is okay.
             */
            return 0;
        } else {
            printf("recvfrom() ERRNO: %d\n", errno);
        }
    } else if (ret != msg_len) {
        printf("recvfrom() returned %d?\n", ret);
        return -1;
    }

    handle_event(spike_fd);

    return 0;
}

int mon_client(const char *server_ip, int interval_ms,
               const char *spike_log, mon_client_cbk client_callback)
{
    int ret = -1;
    int sock = 0;
    struct sockaddr_in addr;
    int spike_fd = spike_log_open(spike_log);

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
        ret = -1;
        goto close_sock;
    }

    ret = connect(sock, (const struct sockaddr *)&addr, sizeof(addr));
    if (ret) {
        perror("connect() failed");
        goto close_sock;
    }

    while (1) {
        ret = client_callback(sock, spike_fd, interval_ms);
        if (ret) {
            break;
        }
    }

close_sock:
    close(sock);
    return ret;
}

#define N_1M (1024 * 1024)

int mon_mm_dirty(long mm_size, long dirty_rate)
{
    char *mm_buf, *mm_ptr, *mm_end;
    long page_size = getpagesize();
    long pages_per_mb = N_1M / page_size;
    uint64_t time_iter, time_now;
    unsigned long dirtied_mb = 0;
    float speed;
    int i;

    printf("Test memory size: \t%ld (MB)\n", mm_size);
    printf("Page size: \t\t%ld (Bytes)\n", page_size);
    if (dirty_rate) {
        printf("Dirty memory rate: \t%ld (MB/s)\n", dirty_rate);
    } else {
        printf("Dirty memory rate: \tMaximum\n");
    }

    mm_buf = malloc(mm_size * N_1M);
    if (!mm_buf) {
        fprintf(stderr, "%s: malloc failed\n", __func__);
        return -1;
    }
    mm_ptr = mm_buf;
    mm_end = mm_buf + mm_size * N_1M;
    time_iter = get_msec();

    puts("+------------------------+");
    puts("|   Start Dirty Memory   |");
    puts("+------------------------+");

    while (1) {
        /* Dirty in MB unit */
        for (i = 0; i < pages_per_mb; i++) {
            *mm_ptr += 1;
            mm_ptr += page_size;
        }
        if (mm_ptr + N_1M > mm_end) {
            mm_ptr = mm_buf;
        }
        dirtied_mb++;
        if (dirty_rate && dirtied_mb >= dirty_rate) {
            /*
             * We have dirtied enough, wait for a while until we reach
             * the next second.
             */
            long sleep_ms = 1000 - get_msec() + time_iter;
            if (sleep_ms > 0) {
                usleep(sleep_ms * 1000);
            }
            while (get_msec() - time_iter < 1000);
        }
        time_now = get_msec();
        if (time_now - time_iter >= 1000) {
            speed = 1.0 * dirtied_mb / (time_now - time_iter) * 1000;
            printf("Dirty rate: %.0f (MB/s), duration: %ld (ms)\n",
                   speed, time_now - time_iter);
            time_iter = time_now;
            dirtied_mb = 0;
        }
    }

    return 0;
}

int main(int argc, char *argv[])
{
    int ret = 0;
    int interval_ms = MIG_MON_INT_DEF;
    const char *work_mode = NULL;
    const char *server_ip = NULL;
    const char *spike_log = MIG_MON_SPIKE_LOG_DEF;

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
        ret = mon_server(spike_log, mon_server_callback);
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
        ret = mon_client(server_ip, interval_ms, NULL, mon_client_callback);
    } else if (!strcmp(work_mode, "server_rr")) {
        printf("starting server_rr...\n");
        ret = mon_server(NULL, mon_server_rr_callback);
    } else if (!strcmp(work_mode, "client_rr")) {
        if (argc < 3) {
            usage();
            return -1;
        }
        server_ip = argv[2];
        if (argc >= 4) {
            interval_ms = strtol(argv[3], NULL, 10);
        }
        if (argc >= 5) {
            spike_log = argv[4];
        }
        ret = mon_client(server_ip, interval_ms, spike_log,
                         mon_client_rr_callback);
    } else if (!strcmp(work_mode, "mm_dirty")) {
        long dirty_rate = 0, mm_size = DEF_MM_DIRTY_SIZE;
        if (argc >= 3) {
            mm_size = atol(argv[2]);
        }
        if (argc >= 4) {
            dirty_rate = atol(argv[3]);
        }
        ret = mon_mm_dirty(mm_size, dirty_rate);
    } else {
        usage();
        return -1;
    }

    return ret;
}
