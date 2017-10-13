#include <sys/types.h>
#include <sys/socket.h>
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

ssize_t
asp_sock_recv(struct asp_sock *asp, void *buf, size_t len)
{
     ssize_t rlen;
     struct asp_iostats_bi tstats;
     int update_stats;

     update_stats = 0;
     pthread_mutex_lock(&asp->mutex);
     rlen = recv(asp->fd, buf, len, 0);
     if (rlen > 0) {
         asp->stats.in.nops++;
         asp->stats.in.btotal += rlen;
         if (asp->on_stats_update != NULL) {
             tstats = asp->stats;
             update_stats = 1;
         }
     }
     pthread_mutex_unlock(&asp->mutex);
     if (update_stats) {
         asp->on_stats_update(&tstats);
     }
     return (rlen);
}

ssize_t
asp_sock_send(struct asp_sock *asp, const void *msg, size_t len)
{
     ssize_t rlen;

     pthread_mutex_lock(&asp->mutex);
     rlen = send(asp->fd, msg, len, 0);
     if (rlen > 0) {
         asp->stats.out.nops++;
         asp->stats.out.btotal += rlen;
     }
     pthread_mutex_unlock(&asp->mutex);
     return (rlen);
}
