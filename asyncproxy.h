void * asyncproxy_ctor(int fd, const char *dest, unsigned short portn,
  const char *bindto);
int asyncproxy_isalive(void *);
void asyncproxy_dtor(void *);
