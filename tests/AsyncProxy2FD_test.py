import socket
import unittest
from ctypes import string_at, memmove
from libasyncproxy.AsyncProxy import AsyncProxy2FD, transform_res

class NosyProxy(AsyncProxy2FD):
    def in2out(self, res_p):
        # unpack the struct
        tr = res_p.contents
        ptr, length = tr.buf, tr.len
        # read, transform, write back
        original = string_at(ptr, length)
        # Transform data to uppercase
        length -= 1
        transformed = original.upper()[:length]
        memmove(ptr, transformed, length)
        print("in2out hook: transformed", original, "to", transformed)
        tr.len = length

    def out2in(self, res_p):
        # unpack the struct
        tr = res_p.contents
        ptr, length = tr.buf, tr.len
        # read, transform, write back
        original    = string_at(ptr, length)
        length -= 1
        transformed = original[::-1][1:]
        memmove(ptr, transformed, length)
        print("out2in hook: transformed", original, "to", transformed)
        tr.len = length

class AsyncProxy2FDTest(unittest.TestCase):
    def test_AsyncProxy(self):
        # Create first socket pair:
        # - client_socket: acts as the client sending data.
        # - proxy_in: endpoint for the proxy from the client side.
        client_socket, proxy_in = socket.socketpair()

        # Create second socket pair:
        # - proxy_out: endpoint for the proxy on the server side.
        # - server_socket: acts as the server receiving data.
        proxy_out, server_socket = socket.socketpair()

        # Initialize the async proxy to connect the two endpoints
        proxy_fd = NosyProxy(proxy_in.fileno(), proxy_out.fileno())

        # Start the asynchronous proxy worker.
        proxy_fd.start()

        # Send a message from client to server.
        client_message = b"Hello from Client!"
        client_socket.sendall(client_message)
        print("Client sent:", client_message.decode())

        server_received = server_socket.recv(1024)
        print("Server received:", server_received.decode())
        self.assertEqual(client_message.upper()[:-1], server_received)

        # Now send a message from server back to client.
        server_message = b"Hello from Server!"
        server_socket.sendall(server_message)
        print("Server sent:", server_message.decode())

        client_received = client_socket.recv(1024)
        print("Client received:", client_received.decode())
        self.assertEqual(server_message[::-1][1:], client_received)

        # Shutdown the proxy worker and cleanup.
        proxy_fd.join(shutdown=True)
        client_socket.close()
        proxy_in.close()
        proxy_out.close()
        server_socket.close()

def runme():
    unittest.main(module = __name__)

if __name__ == '__main__':
    runme()
