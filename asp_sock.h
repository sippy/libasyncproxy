struct iostats_uni {
    uint64_t nops;
    uint64_t btotal;
};

struct iostats_bi {
    struct iostats_uni in;
    struct iostats_uni out;
};

struct asp_sock {
    int fd;
    struct iostats_bi stats;
    pthread_mutex_t mutex;
};

void asp_sock_getstats(struct asp_sock *, struct iostats_bi *, int);
ssize_t asp_sock_recv(struct asp_sock *, void *buf, size_t len);
ssize_t asp_sock_send(struct asp_sock *, const void *msg, size_t len);

