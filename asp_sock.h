#pragma once

#if defined(__FreeBSD__)
#define HAVE_SOCKADDR_SUN_LEN 1
#endif

struct asp_iostats_uni;
struct asp_iostats_bi;

struct asp_sock {
    int fd;
    struct asp_iostats_bi stats;
    pthread_mutex_t mutex;
    void (*on_stats_update)(struct asp_iostats_bi *);
};

struct recv_res {
    ssize_t len;
    int errnom;
};

void asp_sock_getstats(struct asp_sock *, struct asp_iostats_bi *, int);
struct recv_res asp_sock_recv(struct asp_sock *, void *buf, size_t len);
ssize_t asp_sock_send(struct asp_sock *, const void *msg, size_t len);
