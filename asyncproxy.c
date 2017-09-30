/*
 * Copyright (c) 2010-2017 Sippy Software, Inc. All rights reserved.
 *
 * Warning: This computer program is protected by copyright law and
 * international treaties. Unauthorized reproduction or distribution of this
 * program, or any portion of it, may result in severe civil and criminal
 * penalties, and will be prosecuted under the maximum extent possible under
 * law.
 */

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <poll.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#if defined(PYTHON_AWARE)
#include "python2.7/Python.h"
#endif

#include "asyncproxy.h"

#define AP_STATE_INIT  0
#define AP_STATE_START 1
#define AP_STATE_RUN   2
#define AP_STATE_CEASE 3
#define AP_STATE_QUIT  4

#define DBG_LEVEL 1

#if !defined(INFTIM)
# define INFTIM (-1)
#endif

struct asyncproxy {
    int source;
    int sink;
    const char *dest;
    unsigned short portn;
    const char *bindto;
    pthread_t thread;
    pthread_mutex_t mutex;
    int state;
    int debug;
    struct sockaddr_in destaddr;
    int last_seen_alive;
    void (*transform[2])(void *, int);
    int needsjoin;
    char addrbuf[INET6_ADDRSTRLEN];
};

const struct {
    int state;
    const char *sname;
} states[] = {
    {AP_STATE_INIT, "INIT"},
    {AP_STATE_START, "START"},
    {AP_STATE_RUN, "RUN"},
    {AP_STATE_CEASE, "CEASE"},
    {AP_STATE_QUIT, "QUIT"},
    {-1, NULL}
};

#define tov(p) (void *)(p)
#define tosa(p) (struct sockaddr *)tov(p)
#define tocsa(p) (const struct sockaddr *)tov(p)

static int
asp_pton(const char *saddr, struct sockaddr_in *addr)
{

    memset(addr, '\0', sizeof(struct sockaddr_in));
    addr->sin_family = AF_INET;
#if 0
    addr->sin_len = sizeof(struct sockaddr_in);
#endif
    if (inet_pton(AF_INET, saddr, &addr->sin_addr) != 1) {
        return (-1);
    }
    return (1);
}

static int
resolve(struct sockaddr *ia, int pf, const char *host,
  const char *servname, int flags)
{
    int n;
    struct addrinfo hints, *res;

    memset(&hints, 0, sizeof(hints));
    hints.ai_flags = flags;            /* We create listening sockets */
    hints.ai_family = pf;              /* Protocol family */
    hints.ai_socktype = SOCK_STREAM;   /* TCP */

    n = getaddrinfo(host, servname, &hints, &res);
    if (n == 0) {
        /* Use the first socket address returned */
        memcpy(ia, res->ai_addr, res->ai_addrlen);
        freeaddrinfo(res);
    }

    return n;
}

static int
asp_socket_setnonblock(int fd)
{
    int flags;

    flags = fcntl(fd, F_GETFL);
    if (flags == -1)
        return (-1);
    return (fcntl(fd, F_SETFL, flags | O_NONBLOCK));
}

struct io_buf {
    unsigned char data[16 * 1024];
    int len;
};

#define BUF_FREE(ibp) (sizeof((ibp)->data) - (ibp)->len)
#define BUF_P(ibp) (&(ibp)->data[(ibp)->len])

#define NEG(idx) ((idx) ^ 1)

static void *
asyncproxy_run(void *args)
{
    int n, i, state, j, eidx, rval;
    struct asyncproxy *ap;
    struct pollfd pfds[2];
    struct io_buf bufs[2];
    ssize_t rlen;

    eidx = -1;

    ap = (struct asyncproxy *)args;
    if (ap->debug > 1) {
        fprintf(stderr, "asyncproxy_run(%p)\n", ap);
        fflush(stderr);
    }
    pthread_mutex_lock(&ap->mutex);
    if (ap->state == AP_STATE_START)
        ap->state = AP_STATE_RUN;
    pthread_mutex_unlock(&ap->mutex);

    memset(pfds, '\0', sizeof(pfds));
    memset(bufs, '\0', sizeof(bufs));
    pfds[0].fd = ap->source;
    pfds[0].events = POLLIN;
    pfds[1].fd = ap->sink;
    pfds[1].events = POLLIN;

    rval = connect(ap->sink, tocsa(&ap->destaddr), sizeof(struct sockaddr_in));
    if (rval != 0) {
        fprintf(stderr, "asyncproxy_run: connect(%d) = %d\n", ap->sink, rval);
        fflush(stderr);
        if (errno != EINPROGRESS) {
            fprintf(stderr, "asyncproxy_run: connect() failed: %s\n", strerror(errno));
            fflush(stderr);
            goto out;
        }
        pfds[1].events |= POLLOUT;
    }

    for (;;) {
        pthread_mutex_lock(&ap->mutex);
        state = ap->state;
        pthread_mutex_unlock(&ap->mutex);
        if (state != AP_STATE_RUN) {
            if (ap->debug > 2) {
                fprintf(stderr, "asyncproxy_run(%p): exit on state %d\n", ap, state);
                fflush(stderr);
            }
            break;
        }
        n = poll(pfds, 2, INFTIM);
        if (n < 0 && ap->debug > 0) {
                fprintf(stderr, "asyncproxy_run: poll() failed: %s\n", strerror(errno));
                fflush(stderr);
        }
        if (ap->debug > 3) {
            fprintf(stderr, "asyncproxy_run(%p): poll() = %d\n", ap, n);
            fflush(stderr);
        }
        if (n <= 0) {
            continue;
        }

        for (i = 0; i < 2; i++) {
            if (ap->debug > 0) {
                if (ap->debug > 3) {
                    fprintf(stderr, "asyncproxy_run(%p): pfds[%d] = {.events = %d, .revents = %d}\n", ap, i, pfds[i].events, pfds[i].revents);
                    fflush(stderr);
                }
                assert((pfds[i].revents & POLLNVAL) == 0);
            }
            if (pfds[i].revents & POLLHUP) {
                fprintf(stderr, "asyncproxy_run(%p): fd %d is gone, out\n", ap, pfds[i].fd);
                fflush(stderr);
                eidx = i;
                goto out;
            }
            j = NEG(i);
            if (pfds[i].revents & POLLIN && BUF_FREE(&bufs[i]) > 0) {
                {static int b=0; while (b);}
                rlen = recv(pfds[i].fd, BUF_P(&bufs[i]), BUF_FREE(&bufs[i]), 0);
                if (ap->debug > 2) {
                    fprintf(stderr, "asyncproxy_run(%p): received %ld bytes from %d\n", ap, rlen, pfds[i].fd);
                    fflush(stderr);
                }
                if (rlen <= 0) {
                    eidx = i;
                    goto out;
                }
                if (ap->transform[i] != NULL) {
#if defined(PYTHON_AWARE)
                    PyGILState_STATE gstate;
                    gstate = PyGILState_Ensure();
#endif
                    ap->transform[i](BUF_P(&bufs[i]), rlen);
#if defined(PYTHON_AWARE)
                    PyGILState_Release(gstate);
#endif
                }
                bufs[i].len += rlen;
                if (BUF_FREE(&bufs[i]) == 0) {
                    pfds[i].events &= ~POLLIN;
                }
                pfds[i].revents &= ~POLLIN;
            }
        }
        for (i = 0; i < 2; i++) {
            j = NEG(i);
            if (bufs[i].len > 0) {
                if (pfds[j].events & POLLOUT && (pfds[j].revents & POLLOUT) == 0)
                    continue;
                rlen = send(pfds[j].fd, bufs[i].data, bufs[i].len, 0);
                if (ap->debug > 2) {
                    fprintf(stderr, "asyncproxy_run(%p): sent %ld bytes to %d\n", ap, rlen, pfds[j].fd);
                    fflush(stderr);
                }
                if (rlen < bufs[i].len) {
                    pfds[j].events |= POLLOUT;
                }
                if (rlen <= 0)
                    continue;
                if (rlen < bufs[i].len) {
                    memmove(bufs[i].data, bufs[i].data + rlen, bufs[i].len - rlen);
                    bufs[i].len -= rlen;
                } else {
                    bufs[i].len = 0;
                    pfds[j].events &= ~POLLOUT;
                }
                pfds[j].revents &= ~POLLOUT;
                pfds[i].events |= POLLIN;
            } else if (pfds[j].events & POLLOUT && pfds[j].revents & POLLOUT) {
                pfds[j].revents &= ~POLLOUT;
                pfds[j].events &= ~POLLOUT;
                pfds[i].events |= POLLIN;
            }
        }
    }

out:
    if (ap->debug > 0 && eidx != -1) {
        j = NEG(eidx);
        assert(pfds[j].events & POLLOUT || bufs[eidx].len == 0);
    }
    pthread_mutex_lock(&ap->mutex);
    if (ap->state == AP_STATE_RUN) {
        ap->state = AP_STATE_QUIT;
        shutdown(ap->source, SHUT_RDWR);
    }
    pthread_mutex_unlock(&ap->mutex);

    if (ap->debug > 0) {
        fprintf(stderr, "cease asyncproxy_run(%p)\n", ap);
        fflush(stderr);
    }

    return (NULL);
}

void *
asyncproxy_ctor(int fd, const char *dest, unsigned short portn,
  const char *bindto)
{
    struct asyncproxy *ap;
    char pnum[6];
    int n;

    ap = malloc(sizeof(struct asyncproxy));
    if (ap == NULL) {
        return (NULL);
    }
    memset(ap, '\0', sizeof(struct asyncproxy));
    ap->source = dup(fd);
    if (ap->source == -1) {
        goto e0;
    }
    ap->dest = dest;
    ap->portn = portn;
    ap->bindto = bindto;
    ap->debug = DBG_LEVEL;
    ap->last_seen_alive = -1;

    if (ap->debug > 0) {
        fprintf(stderr, "asyncproxy_ctor(%d, %s, %u, %s) = %p\n", fd, dest,
          portn, bindto, ap);
        fflush(stderr);
    }

    ap->sink = socket(AF_INET, SOCK_STREAM, 0);
    if (ap->sink < 0)
        goto e1;

    if (bindto != NULL) {
        struct sockaddr_in bindaddr;

        if (asp_pton(bindto, &bindaddr) != 1) {
            fprintf(stderr, "asyncproxy_ctor: inet_pton() failed\n");
            goto e2;
        }
        if (bind(ap->sink, tocsa(&bindaddr), sizeof(struct sockaddr_in)) != 0) {
            fprintf(stderr, "asyncproxy_ctor: bind() failed: %s\n", strerror(errno));
            goto e2;
        }
    }

    snprintf(pnum, sizeof(pnum), "%u", portn);
    n = resolve(tosa(&ap->destaddr), AF_INET, dest, pnum, 0);
    if (n != 0) {
        fprintf(stderr, "asyncproxy_ctor: resolve() failed: %s\n", gai_strerror(n));
        goto e2;
    }
    if (asp_socket_setnonblock(ap->source) == -1) {
        fprintf(stderr, "asyncproxy_ctor: asp_socket_setnonblock(ap->source) failed: %s\n", strerror(errno));
        goto e2;
    }
    if (asp_socket_setnonblock(ap->sink) == -1) {
        fprintf(stderr, "asyncproxy_ctor: asp_socket_setnonblock(ap->source) failed: %s\n", strerror(errno));
        goto e2;
    }

    if (pthread_mutex_init(&ap->mutex, NULL) != 0) {
        fprintf(stderr, "asyncproxy_ctor: pthread_mutex_init() failed: %s\n", strerror(errno));
        goto e2;
    }

#if defined(PYTHON_AWARE)
    PyEval_InitThreads();
#endif

    return (ap);
#if 0
e3:
    pthread_mutex_destroy(&ap->mutex);
#endif
e2:
    close(ap->sink);
e1:
    close(ap->source);
e0:
    free(ap);
    return (NULL);
}

int
asyncproxy_start(void *_ap)
{
    struct asyncproxy *ap;

    ap = (struct asyncproxy *)_ap;
    if (ap->debug > 0) {
        fprintf(stderr, "asyncproxy_start(%p)\n", ap);
        fflush(stderr);
    }
    pthread_mutex_lock(&ap->mutex);
    if (ap->debug > 0)
        assert(ap->state == AP_STATE_INIT);
    ap->state = AP_STATE_START;
    pthread_mutex_unlock(&ap->mutex);
    if (pthread_create(&ap->thread, NULL, asyncproxy_run, ap) != 0) {
        fprintf(stderr, "asyncproxy_start: pthread_create() failed: %s\n", strerror(errno));
        pthread_mutex_lock(&ap->mutex);
        assert(ap->state == AP_STATE_START);
        ap->state = AP_STATE_INIT;
        pthread_mutex_unlock(&ap->mutex);
        return (-1);
    }
    ap->needsjoin = 1;
    return (0);
}


void
asyncproxy_dtor(void *_ap)
{
    struct asyncproxy *ap;

    ap = (struct asyncproxy *)_ap;
    if (ap->debug > 0) {
        fprintf(stderr, "asyncproxy_dtor(%p)\n", ap);
        fflush(stderr);
    }

    pthread_mutex_lock(&ap->mutex);
    if (ap->state == AP_STATE_START || ap->state == AP_STATE_RUN)
        ap->state = AP_STATE_CEASE;
    pthread_mutex_unlock(&ap->mutex);
    asyncproxy_join(_ap);
    pthread_mutex_destroy(&ap->mutex);
    close(ap->sink);
    close(ap->source);
    free(ap);
}

int
asyncproxy_isalive(void *_ap)
{
    struct asyncproxy *ap;
    int rval;

    ap = (struct asyncproxy *)_ap;

    pthread_mutex_lock(&ap->mutex);
    rval = (ap->state == AP_STATE_START) || (ap->state == AP_STATE_RUN);
    pthread_mutex_unlock(&ap->mutex);

    if (ap->debug > 1 && ap->last_seen_alive != rval) {
        fprintf(stderr, "asyncproxy_isalive(%p) = %d->%d\n", ap, ap->last_seen_alive, rval);
        fflush(stderr);
        ap->last_seen_alive = rval;
    }

    return (rval);
}

void
asyncproxy_set_i2o(void *_ap, void (*i2ofp)(void *, int))
{
    struct asyncproxy *ap;

    ap = (struct asyncproxy *)_ap;

    pthread_mutex_lock(&ap->mutex);
    ap->transform[0] = i2ofp;
    pthread_mutex_unlock(&ap->mutex);
}

void
asyncproxy_set_o2i(void *_ap, void (*o2ifp)(void *, int))
{
    struct asyncproxy *ap;

    ap = (struct asyncproxy *)_ap;

    pthread_mutex_lock(&ap->mutex);
    ap->transform[1] = o2ifp;
    pthread_mutex_unlock(&ap->mutex);
}

void
asyncproxy_join(void *_ap)
{
    struct asyncproxy *ap;

    ap = (struct asyncproxy *)_ap;
    if (!ap->needsjoin) {
        return;
    }
    shutdown(ap->sink, SHUT_RDWR);
    pthread_join(ap->thread, NULL);
    ap->needsjoin = 0;
}

const char *
asyncproxy_describe(void *_ap)
{
    struct asyncproxy *ap;
    int state;

    ap = (struct asyncproxy *)_ap;
    pthread_mutex_lock(&ap->mutex);
    state = ap->state;
    pthread_mutex_unlock(&ap->mutex);
    return (states[state].sname);
}

const char *
asyncproxy_getsockname(void *_ap, unsigned short *portn)
{
    struct asyncproxy *ap;
    struct sockaddr_in sn;
    socklen_t snlen;

    ap = (struct asyncproxy *)_ap;
    snlen = sizeof(struct sockaddr_in);
    if (getsockname(ap->sink, tov(&sn), &snlen) < 0)
        return (NULL);
    if (inet_ntop(AF_INET, &sn.sin_addr, ap->addrbuf, sizeof(ap->addrbuf)) == NULL)
        return (NULL);

    if (portn != NULL) {
        *portn = ntohs(sn.sin_port);
    }
    return (ap->addrbuf);
}
