import secrets
import socket
from socket import AF_INET
import unittest
from ctypes import string_at, memmove
from threading import Thread, Semaphore
from itertools import cycle
from base64 import b64encode, b64decode

from asyncproxy.AsyncProxy import AsyncProxy, transform_res

import eddsa
from noise.connection import NoiseConnection, Keypair

class NoiseEndpoint():
    noise_params = b'Noise_KK_25519_ChaChaPoly_SHA256'
    private_key: bytes
    public_key: str

    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)
        self.private_key = secrets.token_bytes(eddsa.X25519_KEY_LEN)
        self.public_key = b64encode(self.public_key_bytes()).decode("ascii")

    def private_key_bytes(self):
        return self.private_key

    def public_key_bytes(self):
        return eddsa.x25519_base(self.private_key)

class TestServerThread(NoiseEndpoint, Thread):
    bindhost = 'localhost'
    bindport = 0
    daemon = True
    their_public: bytes

    def __init__(self, *args, **kwargs):
        s = None
        try:
            s = socket.socket()
            s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            s.bind((self.bindhost, self.bindport))
            s.listen(1)
            self.bindport = s.getsockname()[1]
            super().__init__(target=self.run_server, args=(s,), *args, **kwargs)
        except:
            if s is not None:
                s.close()
            raise

    def run_server(self, s):
        conn = None
        try:
            conn, addr = s.accept()
            print('Accepted connection from', addr)

            private_key_bytes = self.private_key_bytes()

            noise = NoiseConnection.from_name(self.noise_params)
            noise.set_as_responder()
            noise.set_keypair_from_private_bytes(Keypair.STATIC, private_key_bytes)
            noise.set_keypair_from_public_bytes(Keypair.REMOTE_STATIC, self.their_public)
            noise.start_handshake()

            # Perform handshake. Break when finished
            for action in cycle(['receive', 'send']):
                if noise.handshake_finished:
                    break
                elif action == 'send':
                    ciphertext = noise.write_message()
                    conn.sendall(ciphertext)
                elif action == 'receive':
                    data = conn.recv(2048)
                    plaintext = noise.read_message(data)
                print('Action: ', action)

            # Endless loop "echoing" received data
            while True:
                data = conn.recv(2048)
                if not data:
                    break
                received = noise.decrypt(data)
                conn.sendall(noise.encrypt(received))
        finally:
            if conn is not None:
                conn.close()
            s.close()

class NoiseProxyActive(AsyncProxy, NoiseEndpoint):
    proto: NoiseConnection
    handshake_sem: Semaphore
    their_public: bytes
    def __init__(self, their_public:str, *args):
        NoiseEndpoint.__init__(self)
        proto = NoiseConnection.from_name(self.noise_params)
        proto.set_as_initiator()
        self.proto = proto
        self.handshake_sem = Semaphore(0)
        #self.their_public = b'\0' + b64decode(their_public)[1:]
        self.their_public = b64decode(their_public)
        super().__init__(*args)

    def on_connect(self, res_p, max_len):
        private_key_bytes = self.private_key_bytes()
        self.proto.set_keypair_from_private_bytes(Keypair.STATIC, private_key_bytes)
        self.proto.set_keypair_from_public_bytes(Keypair.REMOTE_STATIC, self.their_public)
        self.proto.start_handshake()
        message = bytes(self.proto.write_message())
        tr = res_p.contents
        ptr = tr.buf
        assert(len(message) <= max_len)
        memmove(ptr, message, len(message))
        tr.len = len(message)

    def in2out(self, res_p):
        assert(self.proto.handshake_finished)
        # unpack the struct
        tr = res_p.contents
        ptr, length = tr.buf, tr.len
        # read, transform, write back
        original = string_at(ptr, length)
        # Encrypt data with Noise
        transformed = self.proto.encrypt(original)
        length = len(transformed)
        memmove(ptr, transformed, length)
        print("in2out hook: transformed", original, "to", transformed)
        tr.len = length

    def out2in(self, res_p):
        # unpack the struct
        tr = res_p.contents
        ptr, length = tr.buf, tr.len
        # read, transform, write back
        original    = string_at(ptr, length)
        if not self.proto.handshake_finished:
            self.proto.read_message(original)
            assert(self.proto.handshake_finished)
            tr.len = 0
            self.handshake_sem.release()
            return
        transformed = self.proto.decrypt(original)
        length = len(transformed)
        memmove(ptr, transformed, length)
        print("out2in hook: transformed", original, "to", transformed)
        tr.len = length

class AsyncProxy_noiseTest(unittest.TestCase):
    def test_AsyncProxy_noise(self):
        ntest_server = TestServerThread()
        # Create socket pair:
        # - client_socket: acts as the client sending data.
        client_socket, server_socket = socket.socketpair()

        # Initialize the async proxy to connect client_socket to the TestServerThread
        ntest_server.start()
        proxy_fd = NoiseProxyActive(ntest_server.public_key, server_socket.fileno(), ntest_server.bindhost, ntest_server.bindport, AF_INET, None)
        ntest_server.their_public = b64decode(proxy_fd.public_key)

        # Start the asynchronous proxy worker.
        proxy_fd.start()
        # Wait for the connect and handshake to complete
        proxy_fd.handshake_sem.acquire()

        # Send a message from client to server.
        client_message = b"Hello from Client!"
        client_socket.sendall(client_message)
        print("Client sent:", client_message.decode())

        client_received = client_socket.recv(1024)
        print("Client received:", client_received.decode())
        self.assertEqual(client_message, client_received)

        # Shutdown the proxy worker and cleanup.
        proxy_fd.join(shutdown=True)
        client_socket.close()
        server_socket.close()
        ntest_server.join()

def runme():
    unittest.main(module = __name__)

if __name__ == '__main__':
    runme()
