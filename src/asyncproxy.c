/*
 * Copyright (c) 2010-2025 Sippy Software, Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this
 * list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation and/or
 * other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 * ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#if defined(PYTHON_AWARE)
#undef _POSIX_C_SOURCE
#include <Python.h>
#else
#define _POSIX_C_SOURCE 200112L
#endif

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/un.h>
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

#include "asyncproxy.h"
#include "asp_iostats.h"
#include "asp_sock.h"

#define AP_STATE_INIT  0
#define AP_STATE_START 1
#define AP_STATE_RUN   2
#define AP_STATE_CEASE 3
#define AP_STATE_QUIT  4

#if !defined(DBG_LEVEL)
#  define DBG_LEVEL 0
#endif

static int dbg_level = DBG_LEVEL;

#if !defined(INFTIM)
# define INFTIM (-1)
#endif

struct asyncproxy {
    struct asp_sock source;
    struct asp_sock sink;
    enum ap_dest dest_type;
    const char *dest;
    unsigned short portn;
    int af;
    const char *bindto;
    pthread_t thread;
    pthread_mutex_t mutex;
    int state;
    int debug;
    struct {
        union {
            struct sockaddr_in ip;
            struct sockaddr_un un;
            struct sockaddr_in6 ip6;
            struct sockaddr sa;
        };
        socklen_t alen;
    } destaddr;
    int last_seen_alive;
    void (*transform[2])(struct transform_res *);
    int needsjoin;
    char addrbuf[FILENAME_MAX];
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
asp_sock_ctor(struct asp_sock *asp, int fd)
{

    if (pthread_mutex_init(&asp->mutex, NULL) != 0)
        return (-1);
    asp->fd = fd;
    return (0);
}

static void
asp_sock_dtor(struct asp_sock *asp)
{

    pthread_mutex_destroy(&asp->mutex);
    close(asp->fd);
}

static int
asp_sock_setnonblock(int fd)
{
    int flags;

    flags = fcntl(fd, F_GETFL);
    if (flags == -1)
        return (-1);
    return (fcntl(fd, F_SETFL, flags | O_NONBLOCK));
}

struct io_buf {
    unsigned char data[16 * 1024];
    size_t len;
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
    struct asp_sock *asps[2];
    struct io_buf bufs[2];
    ssize_t rlen;

    eidx = -1;

    ap = (struct asyncproxy *)args;
    if (ap->debug > 1) {
        fprintf(stderr, "asyncproxy_run(%p)\n", (void *)ap);
        fflush(stderr);
    }
    pthread_mutex_lock(&ap->mutex);
    if (ap->state == AP_STATE_START)
        ap->state = AP_STATE_RUN;
    pthread_mutex_unlock(&ap->mutex);

    memset(pfds, '\0', sizeof(pfds));
    memset(bufs, '\0', sizeof(bufs));
    pfds[0].fd = ap->source.fd;
    pfds[0].events = POLLIN;
    asps[0] = &ap->source;
    pfds[1].fd = ap->sink.fd;
    pfds[1].events = POLLIN;
    asps[1] = &ap->sink;

    if (ap->dest_type == AP_DEST_HOST) {
        rval = connect(ap->sink.fd, &ap->destaddr.sa, ap->destaddr.alen);
        if (rval != 0) {
            if (ap->debug > 2) {
                fprintf(stderr, "asyncproxy_run: connect(%d) = %d\n", ap->sink.fd, rval);
                fflush(stderr);
            }
            if (errno != EINPROGRESS) {
                fprintf(stderr, "asyncproxy_run: connect() failed: %s\n", strerror(errno));
                fflush(stderr);
                goto out;
            }
            pfds[1].events |= POLLOUT;
        }
    }

    for (;;) {
        pthread_mutex_lock(&ap->mutex);
        state = ap->state;
        pthread_mutex_unlock(&ap->mutex);
        if (state != AP_STATE_RUN) {
            if (ap->debug > 2) {
                fprintf(stderr, "asyncproxy_run(%p): exit on state %d\n", (void *)ap, state);
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
            fprintf(stderr, "asyncproxy_run(%p): poll() = %d\n", (void *)ap, n);
            fflush(stderr);
        }
        if (n <= 0) {
            continue;
        }

        for (i = 0; i < 2; i++) {
            if (ap->debug > 0) {
                if (ap->debug > 3) {
                    fprintf(stderr, "asyncproxy_run(%p): pfds[%d] = {.events = %d, .revents = %d}\n",
                      (void *)ap, i, pfds[i].events, pfds[i].revents);
                    fflush(stderr);
                }
                assert((pfds[i].revents & POLLNVAL) == 0);
            }
            if (pfds[i].revents & POLLHUP) {
                if (ap->debug > 1) {
                    fprintf(stderr, "asyncproxy_run(%p): fd %d is gone, out\n", (void *)ap, pfds[i].fd);
                    fflush(stderr);
                }
                eidx = i;
                goto out;
            }
            j = NEG(i);
            if (pfds[i].revents & POLLIN && BUF_FREE(&bufs[i]) > 0) {
                struct recv_res r;
                r = asp_sock_recv(asps[i], BUF_P(&bufs[i]), BUF_FREE(&bufs[i]));
                if (ap->debug > 2) {
                    assert(pfds[i].fd == asps[i]->fd);
                    fprintf(stderr, "asyncproxy_run(%p): received %ld bytes from %d\n", (void *)ap, r.len, pfds[i].fd);
                    fflush(stderr);
                }
                if (r.len <= 0) {
                    if (ap->debug > 1) {
                        fprintf(stderr, "asyncproxy_run(%p): fd %d recv "
                          "failed with error %d, out\n", (void *)ap, pfds[i].fd,
                          r.errnom);
                        fflush(stderr);
                    }
                    eidx = i;
                    goto out;
                }
                pthread_mutex_lock(&ap->mutex);
                __typeof(ap->transform[i]) transform = ap->transform[i];
                pthread_mutex_unlock(&ap->mutex);
                if (transform != NULL) {
#if defined(PYTHON_AWARE)
                    PyGILState_STATE gstate;
                    gstate = PyGILState_Ensure();
#endif
                    struct transform_res tr = {BUF_P(&bufs[i]), r.len};
                    transform(&tr);
#if defined(PYTHON_AWARE)
                    PyGILState_Release(gstate);
#endif
                    if ((ssize_t)tr.len != r.len) {
                        assert(BUF_FREE(&bufs[i]) >= tr.len);
                        r.len = tr.len;
                    }
                    if (tr.buf != BUF_P(&bufs[i])) {
                        if (tr.len > 0) {
                            assert(BUF_FREE(&bufs[i]) >= tr.len);
                            memmove(BUF_P(&bufs[i]), tr.buf, tr.len);
                        }
                        r.len = tr.len;
                    } else if ((ssize_t)tr.len != r.len) {
                        assert((ssize_t)tr.len < r.len);
                        r.len = tr.len;
                    }
                }
                bufs[i].len += r.len;
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
                rlen = asp_sock_send(asps[j], bufs[i].data, bufs[i].len);
                if (ap->debug > 2) {
                    assert(pfds[j].fd == asps[j]->fd);
                    fprintf(stderr, "asyncproxy_run(%p): sent %ld bytes to %d\n", (void *)ap, rlen, pfds[j].fd);
                    fflush(stderr);
                }
                if (rlen < (ssize_t)bufs[i].len) {
                    pfds[j].events |= POLLOUT;
                }
                if (rlen <= 0)
                    continue;
                if (rlen < (ssize_t)bufs[i].len) {
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
        shutdown(ap->source.fd, SHUT_RDWR);
    }
    pthread_mutex_unlock(&ap->mutex);

    if (ap->debug > 0) {
        fprintf(stderr, "cease asyncproxy_run(%p)\n", (void *)ap);
        fflush(stderr);
    }

    return (NULL);
}

void *
asyncproxy_ctor(const struct asyncproxy_ctor_args *acap)
{
    struct asyncproxy *ap;
    char pnum[6];
    int n, fd1;

    if (dbg_level > 0) {
        if (acap->dest_type == AP_DEST_HOST) {
            fprintf(stderr, "asyncproxy_ctor(%d, %s, %d, %u, %s) = ",
              acap->fd, acap->dest, acap->portn, acap->af, acap->bindto);
        } else {
            fprintf(stderr, "asyncproxy_ctor(%d, %d) = ", acap->fd,
              acap->out_fd);
        }
        fflush(stderr);
    }

    ap = malloc(sizeof(struct asyncproxy));
    if (ap == NULL) {
        if (dbg_level > 0) {
            fprintf(stderr, "NULL\n");
            fflush(stderr);
        }
        return (NULL);
    }
    if (dbg_level > 0) {
        fprintf(stderr, "%p\n", (void *)ap);
        fflush(stderr);
    }
    memset(ap, '\0', sizeof(struct asyncproxy));
    fd1 = dup(acap->fd);
    if (fd1 == -1) {
        goto e0;
    }
    if (asp_sock_ctor(&ap->source, fd1) != 0) {
        close(fd1);
        goto e0;
    }
    ap->dest_type = acap->dest_type;
    ap->debug = dbg_level;
    ap->last_seen_alive = -1;
    ap->dest = acap->dest;
    if (acap->dest_type == AP_DEST_FD) {
        fd1 = dup(acap->out_fd);
    } else {
        ap->portn = acap->portn;
        ap->af = acap->af;
        ap->bindto = acap->bindto;

        fd1 = socket(acap->af, SOCK_STREAM, 0);
    }
    if (fd1 < 0)
        goto e1;
    if (asp_sock_ctor(&ap->sink, fd1) != 0) {
        close(fd1);
        goto e1;
    }

    if (acap->dest_type == AP_DEST_FD)
        goto finalize;

    if (acap->bindto != NULL) {
        assert (acap->af != AF_UNIX);
        struct sockaddr_in bindaddr;

        if (asp_pton(acap->bindto, &bindaddr) != 1) {
            fprintf(stderr, "asyncproxy_ctor: inet_pton() failed\n");
            goto e3;
        }
        if (bind(ap->sink.fd, tocsa(&bindaddr), sizeof(struct sockaddr_in)) != 0) {
            fprintf(stderr, "asyncproxy_ctor: bind() failed: %s\n", strerror(errno));
            goto e3;
        }
    }
    if (acap->af != AF_UNIX) {
        snprintf(pnum, sizeof(pnum), "%u", acap->portn);
        n = resolve(&ap->destaddr.sa, acap->af, acap->dest, pnum, 0);
        if (n != 0) {
            fprintf(stderr, "asyncproxy_ctor: resolve() failed: %s\n", gai_strerror(n));
            goto e3;
        }
        ap->destaddr.alen = (acap->af == AF_INET) ? sizeof(struct sockaddr_in) : sizeof(struct sockaddr_in6);
    } else {
        struct sockaddr_un *un = &ap->destaddr.un;
        size_t len = strlen(acap->dest);
        if (len >= sizeof(un->sun_path)) {
            fprintf(stderr, "asyncproxy_ctor: path too long: %s\n", acap->dest);
            goto e3;
        }
        un->sun_family = AF_UNIX;
        memcpy(un->sun_path, acap->dest, len + 1);
#if defined(HAVE_SOCKADDR_SUN_LEN)
        un->sun_len = len;
#endif
        ap->destaddr.alen = sizeof(struct sockaddr_un);
    }
finalize:
    if (asp_sock_setnonblock(ap->source.fd) == -1) {
        fprintf(stderr, "asyncproxy_ctor: asp_sock_setnonblock(ap->source.fd) failed: %s\n", strerror(errno));
        goto e3;
    }
    if (asp_sock_setnonblock(ap->sink.fd) == -1) {
        fprintf(stderr, "asyncproxy_ctor: asp_sock_setnonblock(ap->source.fd) failed: %s\n", strerror(errno));
        goto e3;
    }

    if (pthread_mutex_init(&ap->mutex, NULL) != 0) {
        fprintf(stderr, "asyncproxy_ctor: pthread_mutex_init() failed: %s\n", strerror(errno));
        goto e3;
    }

#if defined(PYTHON_AWARE) && PY_VERSION_HEX < 0x03070000
    PyEval_InitThreads();
#endif

    return (ap);
#if 0
e5:
    pthread_mutex_destroy(&ap->mutex);
#endif
e3:
    asp_sock_dtor(&ap->sink);
e1:
    asp_sock_dtor(&ap->source);
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
        fprintf(stderr, "asyncproxy_start(%p)\n", (void *)ap);
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
        fprintf(stderr, "asyncproxy_dtor(%p)\n", (void *)ap);
        fflush(stderr);
    }

    pthread_mutex_lock(&ap->mutex);
    if (ap->state == AP_STATE_START || ap->state == AP_STATE_RUN)
        ap->state = AP_STATE_CEASE;
    pthread_mutex_unlock(&ap->mutex);
    asyncproxy_join(_ap, 1);
    pthread_mutex_destroy(&ap->mutex);
    asp_sock_dtor(&ap->sink);
    asp_sock_dtor(&ap->source);
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
        fprintf(stderr, "asyncproxy_isalive(%p) = %d->%d\n", (void *)ap, ap->last_seen_alive, rval);
        fflush(stderr);
        ap->last_seen_alive = rval;
    }

    return (rval);
}

void
asyncproxy_set_i2o(void *_ap, void (*i2ofp)(struct transform_res *))
{
    struct asyncproxy *ap;

    ap = (struct asyncproxy *)_ap;

    pthread_mutex_lock(&ap->mutex);
    ap->transform[0] = i2ofp;
    pthread_mutex_unlock(&ap->mutex);
}

void
asyncproxy_set_o2i(void *_ap, void (*o2ifp)(struct transform_res *))
{
    struct asyncproxy *ap;

    ap = (struct asyncproxy *)_ap;

    pthread_mutex_lock(&ap->mutex);
    ap->transform[1] = o2ifp;
    pthread_mutex_unlock(&ap->mutex);
}

void
asyncproxy_join(void *_ap, int force)
{
    struct asyncproxy *ap;

    ap = (struct asyncproxy *)_ap;
    if (!ap->needsjoin) {
        return;
    }
    if (force != 0)
        shutdown(ap->sink.fd, SHUT_RDWR);
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
    if (ap->af == AF_UNIX)
        return ("AF_UNIX");
    snlen = sizeof(struct sockaddr_in);
    if (getsockname(ap->sink.fd, tov(&sn), &snlen) < 0)
        return (NULL);
    if (inet_ntop(ap->af, &sn.sin_addr, ap->addrbuf, sizeof(ap->addrbuf)) == NULL)
        return (NULL);

    if (portn != NULL) {
        *portn = ntohs(sn.sin_port);
    }
    return (ap->addrbuf);
}

void
asyncproxy_setdebug(int new_level)
{

    dbg_level = new_level;
}
