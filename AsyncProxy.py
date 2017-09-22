from ctypes import cdll, c_int, c_char_p, c_ushort, c_void_p

_asp = cdll.LoadLibrary('libasyncproxy.so')
_asp.asyncproxy_ctor.argtypes = [c_int, c_char_p, c_ushort, c_char_p]
_asp.asyncproxy_ctor.restype = c_void_p
_asp.asyncproxy_start.argtypes = [c_void_p,]
_asp.asyncproxy_start.restype = c_int
_asp.asyncproxy_isalive.argtypes = [c_void_p,]
_asp.asyncproxy_isalive.restype = c_int
_asp.asyncproxy_dtor.argtypes = [c_void_p,]

class AsyncProxy(object):
    _hndl = None
    __asp = None

    def __init__(self, fd, dest, portn, bindto):
        self._hndl = _asp.asyncproxy_ctor(fd, dest, portn, bindto)
        if not bool(self._hndl):
            raise Exception('asyncproxy_ctor() failed')
        self.__asp = _asp

    def start(self):
        if int(self.__asp.asyncproxy_start(self._hndl)) != 0:
            raise Exception('asyncproxy_start() failed')

    def isAlive(self):
        return bool(self.__asp.asyncproxy_isalive(self._hndl))

    def __del__(self):
        if bool(self._hndl):
            self.__asp.asyncproxy_dtor(self._hndl)

if __name__ == '__main__':
    f = open('/dev/null', 'w')
    a = AsyncProxy(f.fileno(), 'localhost', 5432, '127.0.0.1')
    print(a.isAlive())
    a.start()
    print(a.isAlive())
    #del(a)
