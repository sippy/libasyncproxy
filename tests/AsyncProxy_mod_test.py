import socket
import unittest
from ctypes import memmove, string_at
from threading import Event

from asyncproxy import AsyncProxy as asyncproxy_mod


class NativeAsyncProxyTest(unittest.TestCase):
    def test_AsyncProxy2FD_callbacks(self):
        client_socket, proxy_in = socket.socketpair()
        proxy_out, server_socket = socket.socketpair()
        proxy = asyncproxy_mod.AsyncProxy2FD(proxy_in.fileno(), proxy_out.fileno())

        def in2out(res_p):
            tr = res_p.contents
            transformed = string_at(tr.buf, tr.len).upper()
            memmove(tr.buf, transformed, len(transformed))
            tr.len = len(transformed)

        def out2in(res_p):
            tr = res_p.contents
            transformed = string_at(tr.buf, tr.len)[::-1]
            memmove(tr.buf, transformed, len(transformed))
            tr.len = len(transformed)

        try:
            proxy.set_i2o(in2out)
            proxy.set_o2i(out2in)
            proxy.start()

            client_socket.sendall(b"Hello")
            self.assertEqual(b"HELLO", server_socket.recv(1024))

            server_socket.sendall(b"World")
            self.assertEqual(b"dlroW", client_socket.recv(1024))
        finally:
            proxy.join(shutdown=True)
            client_socket.close()
            proxy_in.close()
            proxy_out.close()
            server_socket.close()

    def test_AsyncProxy2FD_onestablished(self):
        client_socket, proxy_in = socket.socketpair()
        proxy_out, server_socket = socket.socketpair()
        proxy = asyncproxy_mod.AsyncProxy2FD(proxy_in.fileno(), proxy_out.fileno())
        established = Event()
        payload = b"ready"

        def onestablished(res_p, max_len):
            self.assertGreaterEqual(max_len, len(payload))
            tr = res_p.contents
            memmove(tr.buf, payload, len(payload))
            tr.len = len(payload)
            established.set()

        try:
            proxy.set_onestablished(onestablished)
            proxy.start()
            self.assertTrue(established.wait(2))
            self.assertEqual(payload, client_socket.recv(1024))
        finally:
            proxy.join(shutdown=True)
            client_socket.close()
            proxy_in.close()
            proxy_out.close()
            server_socket.close()


def runme():
    unittest.main(module=__name__)


if __name__ == '__main__':
    runme()
