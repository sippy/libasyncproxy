/*
 * Copyright (c) 2010-2017 Sippy Software, Inc. All rights reserved.
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

#include <stddef.h>

enum ap_dest {AP_DEST_HOST = 0, AP_DEST_FD};

struct asyncproxy_ctor_args {
    int fd;
    enum ap_dest dest_type;
    union {
        struct {
            const char *dest;
            unsigned short portn;
            int af;
            const char *bindto;
        };
        int out_fd;
    };
};

struct transform_res {
    void *buf;
    size_t len;
};

enum asyncproxy_connected_flag {
    ASYNCPROXY_CONNECTED_SOURCE = 1u << 0,
    ASYNCPROXY_CONNECTED_SINK = 1u << 1,
    ASYNCPROXY_CONNECTED_BOTH = 1u << 2,
};

struct asyncproxy_cb_args {
    void *arg;
    struct transform_res res;
    size_t max_len;
    unsigned int connected_flags;
};

typedef void (*asyncproxy_data_cb)(struct asyncproxy_cb_args *);
typedef void (*asyncproxy_on_connect_cb)(struct asyncproxy_cb_args *);
typedef void (*asyncproxy_on_disconnect_cb)(void *);

union asyncproxy_cb {
    asyncproxy_data_cb data;
    asyncproxy_on_connect_cb on_connect;
    asyncproxy_on_disconnect_cb on_disconnect;
};

struct asyncproxy_cb_info {
    union asyncproxy_cb cb;
    void *cb_arg;
    unsigned int connected_flags;
};

void * asyncproxy_ctor(const struct asyncproxy_ctor_args * const);
int asyncproxy_start(void *);
int asyncproxy_isalive(void *);
void asyncproxy_set_i2o(void *, const struct asyncproxy_cb_info * const);
void asyncproxy_set_o2i(void *, const struct asyncproxy_cb_info * const);
void asyncproxy_set_on_connect(void *, const struct asyncproxy_cb_info * const);
void asyncproxy_set_on_disconnect(void *, const struct asyncproxy_cb_info * const);
void asyncproxy_join(void *, int);
void asyncproxy_dtor(void *);
const char * asyncproxy_describe(void *);
const char * asyncproxy_getsockname(void *, unsigned short *);
void asyncproxy_setdebug(int);
