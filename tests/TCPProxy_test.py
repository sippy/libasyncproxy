import socket
import unittest
from ctypes import memmove
from threading import Event, Thread

from asyncproxy.ForwarderFast import ForwarderFast
from asyncproxy.TCPProxy import TCPProxy, TCPProxyActive


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
        if not self.done.is_set():
            try:
                with socket.create_connection(self.addr, timeout=0.2):
                    pass
            except OSError:
                pass
        try:
            self.sock.close()
        except OSError:
            pass


class EstablishedTCPProxyActive(TCPProxyActive):
    established = None
    payload = None

    def on_source_connect(self, res_p, max_len):
        assert self.payload is not None
        assert self.established is not None
        assert max_len >= len(self.payload)
        tr = res_p.contents
        memmove(tr.buf, self.payload, len(self.payload))
        tr.len = len(self.payload)
        self.established.set()


class TCPProxyTest(unittest.TestCase):
    def _close_server(self, server):
        server.close()
        server.join(2)
        self.assertFalse(server.is_alive())

    def _shutdown_proxy(self, proxy):
        forwarders = list(proxy.forwarders)
        proxy.shutdown()
        self.assertFalse(proxy.is_alive())
        self.assertEqual(proxy.sock.fileno(), -1)
        for forwarder in forwarders:
            self.assertFalse(forwarder.isAlive())

    def test_Forwarder_fast(self):
        self.assertIs(ForwarderFast.fast, True)

    def test_ForwarderFast_shutdown_unconnected_source(self):
        source = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.addCleanup(source.close)
        forwarder = ForwarderFast(
            source, (("127.0.0.1", 9), socket.AF_INET), source_peer_port=12345)
        forwarder.shutdown()
        forwarder.shutdown()
        self.assertTrue(forwarder.dead)
        self.assertNotEqual(source.fileno(), -1)
        self.assertEqual(source.getsockopt(socket.SOL_SOCKET, socket.SO_TYPE), socket.SOCK_STREAM)

    def test_ForwarderFast_shutdown_closed_source_stops_worker(self):
        sink = TCPServer(recv_len=None)
        sink.start()
        self.addCleanup(self._close_server, sink)
        source, client = socket.socketpair()
        self.addCleanup(source.close)
        self.addCleanup(client.close)
        forwarder = ForwarderFast(
            source, (sink.addr, socket.AF_INET), source_peer_port=12345)
        self.addCleanup(forwarder.shutdown)
        forwarder.start()
        source.close()
        self.assertTrue(sink.accepted.wait(1))
        self.assertTrue(forwarder.isAlive())

        forwarder.shutdown()
        forwarder.shutdown()
        self.assertFalse(forwarder.isAlive())
        client.settimeout(1)
        self.assertEqual(client.recv(1), b"")

    def test_ForwarderFast_port1_accepts_known_peer_port(self):
        source = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        try:
            forwarder = ForwarderFast(source, (("127.0.0.1", 9), socket.AF_INET), source_peer_port=12345)
            self.assertEqual(forwarder.port1, 12345)
            forwarder.shutdown()
        finally:
            source.close()

    def test_TCPProxy_shutdown_before_start(self):
        proxy = TCPProxy(port=0, newhost="127.0.0.1", newport=9)
        self.addCleanup(proxy.sock.close)
        self.assertIsNone(proxy.ident)
        self._shutdown_proxy(proxy)
        self._shutdown_proxy(proxy)

    def test_TCPProxy_instances_forward_independently(self):
        sink1 = TCPServer(recv_len=5)
        self.addCleanup(sink1.sock.close)
        sink2 = TCPServer(recv_len=5)
        self.addCleanup(sink2.sock.close)

        sink1.start()
        self.addCleanup(self._close_server, sink1)
        sink2.start()
        self.addCleanup(self._close_server, sink2)
        self.assertTrue(sink1.ready.wait(1))
        self.assertTrue(sink2.ready.wait(1))

        proxy1 = TCPProxy(port=0, newhost=sink1.addr[0], newport=sink1.addr[1], bindhost="127.0.0.1")
        self.addCleanup(self._shutdown_proxy, proxy1)
        proxy2 = TCPProxy(port=0, newhost=sink2.addr[0], newport=sink2.addr[1], bindhost="127.0.0.1")
        self.addCleanup(self._shutdown_proxy, proxy2)
        self.assertFalse(proxy1.is_alive())
        self.assertFalse(proxy2.is_alive())
        proxy1.start()
        proxy2.start()
        self.assertTrue(proxy1.is_alive())
        self.assertTrue(proxy2.is_alive())
        self.assertNotEqual(proxy1.ident, proxy2.ident)

        client1 = socket.create_connection(("127.0.0.1", proxy1.port), timeout=2)
        self.addCleanup(client1.close)
        client2 = socket.create_connection(("127.0.0.1", proxy2.port), timeout=2)
        self.addCleanup(client2.close)
        client1.sendall(b"one-1")
        client2.sendall(b"two-2")

        self.assertTrue(sink1.done.wait(2))
        self.assertTrue(sink2.done.wait(2))
        self.assertEqual(sink1.received, b"one-1")
        self.assertEqual(sink2.received, b"two-2")

        # Check that shutdown also handles workers that have already finished.
        client1.close()
        client2.close()
        for proxy in (proxy1, proxy2):
            for forwarder in proxy.forwarders:
                forwarder.join()
                self.assertFalse(forwarder.isAlive())

    def test_TCPProxyActive_on_source_connect_sends_bytes(self):
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
