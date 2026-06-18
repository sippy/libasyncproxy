import socket
import unittest
from ctypes import memmove, string_at
from threading import Event, Thread

from asyncproxy import AsyncProxy as asyncproxy_mod


class NativeAsyncProxyTest(unittest.TestCase):
    def test_AsyncProxy_callback_method_names(self):
        self.assertTrue(hasattr(asyncproxy_mod.AsyncProxy2FD, "set_on_connect"))
        self.assertTrue(hasattr(asyncproxy_mod.AsyncProxy2FD, "set_on_source_connect"))
        self.assertTrue(hasattr(asyncproxy_mod.AsyncProxy2FD, "set_on_disconnect"))
        self.assertFalse(hasattr(asyncproxy_mod.AsyncProxy2FD, "set_onconnect"))
        self.assertFalse(hasattr(asyncproxy_mod.AsyncProxy2FD, "set_onestablished"))
        self.assertFalse(hasattr(asyncproxy_mod.AsyncProxy2FD, "set_ondisconnect"))

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

    def test_AsyncProxy2FD_on_source_connect(self):
        client_socket, proxy_in = socket.socketpair()
        proxy_out, server_socket = socket.socketpair()
        proxy = asyncproxy_mod.AsyncProxy2FD(proxy_in.fileno(), proxy_out.fileno())
        established = Event()
        payload = b"ready"

        def on_source_connect(res_p, max_len):
            self.assertGreaterEqual(max_len, len(payload))
            tr = res_p.contents
            memmove(tr.buf, payload, len(payload))
            tr.len = len(payload)
            established.set()

        try:
            proxy.set_on_source_connect(on_source_connect)
            proxy.start()
            self.assertTrue(established.wait(2))
            self.assertEqual(payload, client_socket.recv(1024))
        finally:
            proxy.join(shutdown=True)
            client_socket.close()
            proxy_in.close()
            proxy_out.close()
            server_socket.close()

    def test_AsyncProxy_connect_callbacks_can_be_combined(self):
        client_socket, proxy_in = socket.socketpair()
        listen_sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        listen_sock.bind(("127.0.0.1", 0))
        listen_sock.listen(1)
        listen_addr = listen_sock.getsockname()
        received = []
        source_connected = Event()
        sink_connected = Event()
        server_done = Event()
        source_payload = b"src"
        sink_payload = b"sink"

        def server():
            conn = None
            try:
                listen_sock.settimeout(2)
                conn, _addr = listen_sock.accept()
                conn.settimeout(2)
                received.append(conn.recv(len(sink_payload)))
            finally:
                if conn is not None:
                    conn.close()
                listen_sock.close()
                server_done.set()

        server_thread = Thread(target=server)
        server_thread.daemon = True
        server_thread.start()

        proxy = asyncproxy_mod.AsyncProxy(
            proxy_in.fileno(), listen_addr[0], listen_addr[1], socket.AF_INET, None
        )

        def on_source_connect(res_p, max_len):
            self.assertGreaterEqual(max_len, len(source_payload))
            tr = res_p.contents
            memmove(tr.buf, source_payload, len(source_payload))
            tr.len = len(source_payload)
            source_connected.set()

        def on_connect(res_p, max_len):
            self.assertGreaterEqual(max_len, len(sink_payload))
            tr = res_p.contents
            memmove(tr.buf, sink_payload, len(sink_payload))
            tr.len = len(sink_payload)
            sink_connected.set()

        try:
            proxy.set_on_connect(on_connect)
            proxy.set_on_source_connect(on_source_connect)
            proxy.start()
            self.assertTrue(source_connected.wait(2))
            self.assertEqual(source_payload, client_socket.recv(1024))
            self.assertTrue(sink_connected.wait(2))
            self.assertTrue(server_done.wait(2))
            self.assertEqual(received, [sink_payload])
        finally:
            proxy.join(shutdown=True)
            client_socket.close()
            proxy_in.close()
            server_thread.join(2)


def runme():
    unittest.main(module=__name__)


if __name__ == '__main__':
    runme()
