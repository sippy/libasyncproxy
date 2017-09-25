# Copyright (c) 2017 Sippy Software, Inc. All rights reserved.
#
# Warning: This computer program is protected by copyright law and
# international treaties. Unauthorized reproduction or distribution of this
# program, or any portion of it, may result in severe civil and criminal
# penalties, and will be prosecuted under the maximum extent possible under
# law.

from ctypes import cdll, c_int, c_char_p, c_ushort, c_void_p, CFUNCTYPE, \
  POINTER, pointer

_asp_data_cb = CFUNCTYPE(None, c_void_p, c_int)

_asp = cdll.LoadLibrary('libasyncproxy.so')
_asp.asyncproxy_ctor.argtypes = [c_int, c_char_p, c_ushort, c_char_p]
_asp.asyncproxy_ctor.restype = c_void_p
_asp.asyncproxy_start.argtypes = [c_void_p,]
_asp.asyncproxy_start.restype = c_int
_asp.asyncproxy_isalive.argtypes = [c_void_p,]
_asp.asyncproxy_isalive.restype = c_int
_asp.asyncproxy_dtor.argtypes = [c_void_p,]
_asp.asyncproxy_set_i2o.argtypes = [c_void_p, _asp_data_cb]
_asp.asyncproxy_set_o2i.argtypes = [c_void_p, _asp_data_cb]
_asp.asyncproxy_join.argtypes = [c_void_p,]
_asp.asyncproxy_describe.argtypes = [c_void_p,]
_asp.asyncproxy_describe.restype = c_char_p
_asp.asyncproxy_getsockname.argtypes = [c_void_p, POINTER(c_ushort)]
_asp.asyncproxy_getsockname.restype = c_char_p

class AsyncProxy(object):
    _hndl = None
    __asp = None
    in2out = None
    out2in = None

    def __init__(self, fd, dest, portn, bindto):
        dest = c_char_p(bytes(dest.encode()))
        if bindto != None:
            bindto = c_char_p(bytes(dest.encode()))
        self._hndl = _asp.asyncproxy_ctor(fd, dest, portn, bindto)
        if not bool(self._hndl):
            raise Exception('asyncproxy_ctor() failed')
        self.__asp = _asp
        if self.in2out != None:
            in2out = _asp_data_cb(self.in2out)
            self.__asp.asyncproxy_set_i2o(self._hndl, in2out)
        if self.out2in != None:
            out2in = _asp_data_cb(self.out2in)
            self.__asp.asyncproxy_set_o2i(self._hndl, out2in)

    def start(self):
        if int(self.__asp.asyncproxy_start(self._hndl)) != 0:
            raise Exception('asyncproxy_start() failed')

    def isAlive(self):
        return bool(self.__asp.asyncproxy_isalive(self._hndl))

    def join(self):
        self.__asp.asyncproxy_join(self._hndl)

    def __del__(self):
        if bool(self._hndl):
            self.__asp.asyncproxy_dtor(self._hndl)

    def _in2out(self, ptr, len):
        pass
        #print('in2out', ptr, len)

    def _out2in(self, ptr, len):
        pass
        #print('out2in', ptr, len)

    def describe(self):
        d = self.__asp.asyncproxy_describe(self._hndl)
        return d

    def getsockname(self):
        portnum = c_ushort()
        a = self.__asp.asyncproxy_getsockname(self._hndl, pointer(portnum))
        if not bool(a):
            raise Exception('asyncproxy_getsockname() failed')
        return (a, portnum.value)

if __name__ == '__main__':
    from time import sleep
    from socket import socketpair, AF_INET

    getnull = lambda: (open('/dev/null', 'r+'), open('/dev/null', 'r+'))
    getrandom = lambda: (open('/dev/urandom', 'r'), open('/dev/urandom', 'r'))

    for source in (getnull(), getrandom(), socketpair()):
        for sport in 80,:
            #source = socketpair()
            a = AsyncProxy(source[0].fileno(), 'gmail-smtp-in.l.google.com', 25, '192.168.23.52')
            print(a.isAlive(), a.getsockname())
            a.start()
            print(a.isAlive())
            b = AsyncProxy(source[1].fileno(), 'www.google.com', sport, '192.168.23.52')
            print(b.isAlive())
            b.start()
            print(b.isAlive())
            print('a=%s b=%s' % (a.describe(), b.describe()))
            while a.isAlive() or b.isAlive():
                sleep(1)
            print('a=%s b=%s' % (a.describe(), b.describe()))
            a.join()
            b.join()
