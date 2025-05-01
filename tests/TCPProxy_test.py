import socket
import unittest
from ctypes import memmove
from threading import Event, Thread

from asyncproxy.ForwarderFast import ForwarderFast
from asyncproxy.TCPProxy import TCPProxyActive


class TCPServer(Thread):
    daemon = True

    def __init__(self, recv_len = 0):
        super().__init__()
        self.ready = Event()
        self.accepted = Event()
        self.done = Event()
        self.stop = Event()
        self.recv_len = recv_len
        self.received = None
        self.conn = None
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.sock.bind(("127.0.0.1", 0))
        self.sock.listen(1)
        self.addr = self.sock.getsockname()

    def run(self):
        conn = None
        try:
            self.ready.set()
            self.sock.settimeout(2)
            conn, _addr = self.sock.accept()
            self.conn = conn
            self.accepted.set()
            if self.recv_len is None:
                self.stop.wait(2)
            elif self.recv_len > 0:
                conn.settimeout(2)
                self.received = conn.recv(self.recv_len)
        except (OSError, socket.timeout):
            pass
        finally:
            if conn is not None:
                conn.close()
            self.sock.close()
            self.done.set()

    def close(self):
        self.stop.set()
        try:
            if self.conn is not None:
                self.conn.close()
        except OSError:
            pass
        try:
            self.sock.close()
        except OSError:
            pass


class EstablishedTCPProxyActive(TCPProxyActive):
    established = None
    payload = None

    def onestablished(self, res_p, max_len):
        assert self.payload is not None
        assert self.established is not None
        assert max_len >= len(self.payload)
        tr = res_p.contents
        memmove(tr.buf, self.payload, len(self.payload))
        tr.len = len(self.payload)
        self.established.set()


class TCPProxyTest(unittest.TestCase):
    def test_Forwarder_fast(self):
        self.assertIs(ForwarderFast.fast, True)

    def test_ForwarderFast_shutdown_unconnected_source(self):
        forwarder = ForwarderFast.__new__(ForwarderFast)
        forwarder.dead = False
        forwarder.source = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        forwarder.shutdown()
        self.assertTrue(forwarder.dead)
        self.assertIsNone(forwarder.source)

    def test_ForwarderFast_port1_accepts_known_peer_port(self):
        source = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        try:
            forwarder = ForwarderFast(source, (("127.0.0.1", 9), socket.AF_INET), source_peer_port=12345)
            self.assertEqual(forwarder.port1, 12345)
            forwarder.shutdown()
        finally:
            source.close()

    def test_TCPProxyActive_onestablished_sends_bytes(self):
        source_server = TCPServer(recv_len=5)
        sink_server = TCPServer(recv_len=None)
        established = Event()
        payload = b"ready"

        source_server.start()
        sink_server.start()
        self.assertTrue(source_server.ready.wait(1))
        self.assertTrue(sink_server.ready.wait(1))

        proxy = EstablishedTCPProxyActive(
            destaddr=source_server.addr,
            newhost=sink_server.addr[0],
            newport=sink_server.addr[1],
            bindhost="127.0.0.1",
        )
        proxy.established = established
        proxy.payload = payload
        try:
            proxy.start()
            self.assertTrue(established.wait(2))
            self.assertTrue(source_server.done.wait(2))
            self.assertEqual(source_server.received, payload)
        finally:
            proxy.shutdown()
            source_server.close()
            sink_server.close()
            self.assertTrue(source_server.done.wait(2))
            self.assertTrue(sink_server.done.wait(2))


def runme():
    unittest.main(module=__name__)


if __name__ == '__main__':
    runme()
