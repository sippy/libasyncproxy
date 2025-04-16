# Copyright (c) 2017-2025 Sippy Software, Inc. All rights reserved.
#
# Redistribution and use in source and binary forms, with or without modification,
# are permitted provided that the following conditions are met:
#
# 1. Redistributions of source code must retain the above copyright notice, this
# list of conditions and the following disclaimer.
#
# 2. Redistributions in binary form must reproduce the above copyright notice,
# this list of conditions and the following disclaimer in the documentation and/or
# other materials provided with the distribution.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
# ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
# WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
# DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR
# ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
# (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
# LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
# ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
# SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.


from ctypes import cdll, c_int, c_char_p, c_ushort, c_void_p, CFUNCTYPE, \
  POINTER, pointer, Structure, Union, byref, c_size_t

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

class transform_res(Structure):
    _fields_ = [
        ("buf", c_void_p),
        ("len", c_size_t),
    ]

_asp_data_cb = CFUNCTYPE(None, POINTER(transform_res))

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
            self._in2out_cb = _asp_data_cb(self.in2out)
            self.__asp.asyncproxy_set_i2o(self._hndl, self._in2out_cb)
        if self.out2in is not None:
            self._out2in_cb = _asp_data_cb(self.out2in)
            self.__asp.asyncproxy_set_o2i(self._hndl, self._out2in_cb)

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
