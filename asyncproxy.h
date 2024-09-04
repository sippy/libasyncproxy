/*
 * Copyright (c) 2010-2017 Sippy Software, Inc. All rights reserved.
 *
 * Warning: This computer program is protected by copyright law and
 * international treaties. Unauthorized reproduction or distribution of this
 * program, or any portion of it, may result in severe civil and criminal
 * penalties, and will be prosecuted under the maximum extent possible under
 * law.
 */

void * asyncproxy_ctor(int fd, const char *dest, unsigned short portn,
  int af, const char *bindto);
int asyncproxy_start(void *);
int asyncproxy_isalive(void *);
void asyncproxy_set_i2o(void *, void (*)(void *, int));
void asyncproxy_set_o2i(void *, void (*)(void *, int));
void asyncproxy_join(void *, int);
void asyncproxy_dtor(void *);
const char * asyncproxy_describe(void *);
const char * asyncproxy_getsockname(void *, unsigned short *);
void asyncproxy_setdebug(int);
