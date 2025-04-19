#include <sys/types.h>
#include <sys/socket.h>
#include <errno.h>
#include <inttypes.h>
#include <pthread.h>

#include "asp_iostats.h"
#include "asp_sock.h"

void
asp_sock_getstats(struct asp_sock *asp, struct asp_iostats_bi *res, int lock)
{

     if (lock)
         pthread_mutex_lock(&asp->mutex);
     *res = asp->stats;
     if (lock)
         pthread_mutex_unlock(&asp->mutex);
}

struct recv_res
asp_sock_recv(struct asp_sock *asp, void *buf, size_t len)
{
     struct recv_res r = {0};
     struct asp_iostats_bi tstats;
     int update_stats;

     update_stats = 0;
     r.len = recv(asp->fd, buf, len, 0);
     if (r.len > 0) {
         pthread_mutex_lock(&asp->mutex);
         asp->stats.in.nops++;
         asp->stats.in.btotal += r.len;
         if (asp->on_stats_update != NULL) {
             tstats = asp->stats;
             update_stats = 1;
         } else {
             pthread_mutex_unlock(&asp->mutex);
         }
     } else {
         r.errnom = errno;
     }
     if (update_stats) {
         asp->on_stats_update(&tstats);
         pthread_mutex_unlock(&asp->mutex);
     }
     return (r);
}

ssize_t
asp_sock_send(struct asp_sock *asp, const void *msg, size_t len)
{
     ssize_t rlen;

     rlen = send(asp->fd, msg, len, 0);
     if (rlen > 0) {
         pthread_mutex_lock(&asp->mutex);
         asp->stats.out.nops++;
         asp->stats.out.btotal += rlen;
         pthread_mutex_unlock(&asp->mutex);
     }
     return (rlen);
}
