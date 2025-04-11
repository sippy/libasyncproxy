# Copyright (c) 2017-2024 Sippy Software, Inc. All rights reserved.
#
# Warning: This computer program is protected by copyright law and
# international treaties. Unauthorized reproduction or distribution of this
# program, or any portion of it, may result in severe civil and criminal
# penalties, and will be prosecuted under the maximum extent possible under
# law.

from ctypes import cdll, c_int, c_char_p, c_ushort, c_void_p, CFUNCTYPE, \
  POINTER, pointer, Structure, Union, byref

from sysconfig import get_config_var
from site import getsitepackages
from pathlib import Path
from os.path import abspath, dirname, join as path_join

from .env import LAP_MOD_NAME

AP_DEST_HOST = 0
AP_DEST_FD = 1

class _DestStruct(Structure):
    _fields_ = [
        ("dest", c_char_p),
        ("portn", c_ushort),
        ("af", c_int),
        ("bindto", c_char_p),
    ]

class _AnonUnion(Union):
    _anonymous_ = ("dest_struct",)
    _fields_ = [
        ("dest_struct", _DestStruct),
        ("out_fd", c_int),
    ]

class asyncproxy_ctor_args(Structure):
    _anonymous_ = ("_anon_union",)
    _fields_ = [
        ("fd", c_int),
        ("dest_type", c_int),
        ("_anon_union", _AnonUnion),
    ]

_asp_data_cb = CFUNCTYPE(None, c_void_p, c_int)

_esuf = get_config_var('EXT_SUFFIX')
if not _esuf:
    _esuf = '.so'
try:
    _ROOT = str(Path(__file__).parent.absolute())
except ImportError:
    _ROOT = abspath(dirname(__file__))
#print('ROOT: ' + str(_ROOT))
modloc = getsitepackages()
modloc.insert(0, path_join(_ROOT, ".."))
for p in modloc:
    try:
        #print("Trying %s" % path_join(p, LAP_MOD_NAME + _esuf))
        _asp = cdll.LoadLibrary(path_join(p, LAP_MOD_NAME + _esuf))
    except:
        continue
    break
else:
    _asp = cdll.LoadLibrary('libasyncproxy.so')

_asp.asyncproxy_ctor.argtypes = [POINTER(asyncproxy_ctor_args)]
_asp.asyncproxy_ctor.restype = c_void_p
_asp.asyncproxy_start.argtypes = [c_void_p,]
_asp.asyncproxy_start.restype = c_int
_asp.asyncproxy_isalive.argtypes = [c_void_p,]
_asp.asyncproxy_isalive.restype = c_int
_asp.asyncproxy_dtor.argtypes = [c_void_p,]
_asp.asyncproxy_set_i2o.argtypes = [c_void_p, _asp_data_cb]
_asp.asyncproxy_set_o2i.argtypes = [c_void_p, _asp_data_cb]
_asp.asyncproxy_join.argtypes = [c_void_p, c_int]
_asp.asyncproxy_describe.argtypes = [c_void_p,]
_asp.asyncproxy_describe.restype = c_char_p
_asp.asyncproxy_getsockname.argtypes = [c_void_p, POINTER(c_ushort)]
_asp.asyncproxy_getsockname.restype = c_char_p
_asp.asyncproxy_setdebug.argtypes = [c_int,]

def setdebug(level):
    _asp.asyncproxy_setdebug(level)

class AsyncProxyBase(object):
    _hndl = None
    __asp = None
    in2out = None
    out2in = None

    def __init__(self, args:asyncproxy_ctor_args):
        self._hndl = _asp.asyncproxy_ctor(byref(args))
        if not bool(self._hndl):
            raise Exception('asyncproxy_ctor() failed')
        self.__asp = _asp
        if self.in2out is not None:
            in2out = _asp_data_cb(self.in2out)
            self.__asp.asyncproxy_set_i2o(self._hndl, in2out)
        if self.out2in is not None:
            out2in = _asp_data_cb(self.out2in)
            self.__asp.asyncproxy_set_o2i(self._hndl, out2in)

    def start(self):
        if int(self.__asp.asyncproxy_start(self._hndl)) != 0:
            raise Exception('asyncproxy_start() failed')

    def isAlive(self):
        return bool(self.__asp.asyncproxy_isalive(self._hndl))

    def join(self, shutdown=True):
        self.__asp.asyncproxy_join(self._hndl, shutdown)

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
        return (a.decode(), portnum.value)

class AsyncProxy(AsyncProxyBase):
    def __init__(self, fd, dest, portn, af, bindto):
        args = asyncproxy_ctor_args()
        args.fd = fd
        args.dest = c_char_p(bytes(dest.encode()))
        args.portn = portn
        args.af = af
        args.dest_type = AP_DEST_HOST
        if bindto is not None:
            args.bindto = c_char_p(bytes(bindto.encode()))
        super().__init__(args)

class AsyncProxy2FD(AsyncProxyBase):
    def __init__(self, fd1:int, fd2:int):
        args = asyncproxy_ctor_args()
        args.fd = fd1
        args.out_fd = fd2
        args.dest_type = AP_DEST_FD
        super().__init__(args)

if __name__ == '__main__':
    import sys
    from time import sleep
    from socket import socketpair, AF_INET

    getnull = lambda: (open('/dev/null', 'r+'), open('/dev/null', 'r+'))
    getrandom = lambda: (open('/dev/urandom', 'r'), open('/dev/urandom', 'r'))

    setdebug(2)

    dn = socketpair()
    a = AsyncProxy(dn[0].fileno(), 'gmail-smtp-in.l.google.com', 25, AF_INET, None)
    a.start()
    while a.getsockname()[1] == 0:
        print(a.isAlive(), a.getsockname())
    lclsrc = a.getsockname()[0]
    a.join()

    for source in (getnull(), getrandom(), socketpair()):
        for sport in 80, 12345:
            #source = socketpair()
            a = AsyncProxy(source[0].fileno(), 'gmail-smtp-in.l.google.com', 25, AF_INET, lclsrc)
            print(a.isAlive(), a.getsockname())
            a.start()
            print(a.isAlive())
            b = AsyncProxy(source[1].fileno(), 'www.google.com', sport, AF_INET, lclsrc)
            print(b.isAlive())
            b.start()
            print(b.isAlive())
            print('a=%s b=%s' % (a.describe(), b.describe()))
            i = 0
            while (a.isAlive() or b.isAlive()) and i < 10:
                sleep(1)
                i += 1
            print('a=%s b=%s' % (a.describe(), b.describe()))
            a.join()
            b.join()
    args = getnull()
    a = AsyncProxy2FD(*(x.fileno() for x in args))
    a.start()
    a.join(shutdown=False)
