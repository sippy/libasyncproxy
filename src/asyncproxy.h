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

void * asyncproxy_ctor(const struct asyncproxy_ctor_args *);
int asyncproxy_start(void *);
int asyncproxy_isalive(void *);
void asyncproxy_set_i2o(void *, void (*)(struct transform_res *));
void asyncproxy_set_o2i(void *, void (*)(struct transform_res *));
void asyncproxy_join(void *, int);
void asyncproxy_dtor(void *);
const char * asyncproxy_describe(void *);
const char * asyncproxy_getsockname(void *, unsigned short *);
void asyncproxy_setdebug(int);
