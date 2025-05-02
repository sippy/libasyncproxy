[![Build, Test & Publush](https://github.com/sippy/libasyncproxy/actions/workflows/build_and_test.yml/badge.svg)](https://github.com/sippy/libasyncproxy/actions/workflows/build_and_test.yml)

# libasyncproxy

## Introduction

The libasyncproxy is a fairy simple C library and a respective python wrapper,
which allows splicing two sockets, pipes and in general file descriptors to
relay bidirectional data in/out in a background using a worker thread (one per
connection at the moment).

Unlike system-wide facilities that might be offering similar functionality,
this library provides more control and flexibility. Allowing to connect
different kinds of underlying objects (i.e. plain file to a socket, device to
a pipe etc).

It also privides mechanism for the python code to supply a handler(s) to
monitor, record and/or alter the data being transmitted.

Last but not least, the C library can be used directly from a low-level code
for the same effect.

## History

The code was created to allow Python code implementing application-layer proxy
to manage session routing and connection, while handling all transfers outside
of confinments of the slow Python and its GIL.

## Interfaces

AsyncProxy: the lowest-level interface, dealing with raw sockets, wrapper for
libasyncproxy.

ForwarderFast: super-set of AsyncProxy with some utility methods.

Forwarder: same API and functionality as ForwarderFast, but without using
AsyncProxy C module (i.e. python thread doing i/o). Mostly for backward
compatibility when we need to break library API.

TCPProxy: set of high-level classes to accept and manage inbound connections
and initiate/tear-down outbound as needed, connecting them using forwarders
once established. Will use ForwarderFast if available, falling back to the
Forwarder if that fails to load or initialize.

## Use Cases

We use this library to allow applications to be redirected to one of several
available DB replicas and re-routed instantly if the configuration changes.

## Install Python module from PyPy:

```
pip install asyncproxy
```

## Build and Install Python module from source code:

```
git clone https://github.com/sippy/libasyncproxy.git
pip install libasyncproxy/
```

## Usage

### asyncproxy -- `AsyncProxy2FD` Example

This example shows how to set up a bidirectional relay between two socket pairs using `AsyncProxy2FD`. Data sent on one end is forwarded to the other, and vice versa.

```python
import socket
from asyncproxy.AsyncProxy import AsyncProxy2FD

# 1. Create two socket pairs:
#    - (client_socket, proxy_in): client writes to `proxy_in`
#    - (proxy_out, server_socket): proxy writes to `proxy_out`, server reads
client_socket, proxy_in     = socket.socketpair()
proxy_out,    server_socket = socket.socketpair()

# 2. Initialize and start the proxy:
proxy = AsyncProxy2FD(proxy_in.fileno(), proxy_out.fileno())
proxy.start()

# 3. Send from client → server:
client_msg = b"Hello from Client!"
client_socket.sendall(client_msg)
print("Client sent:", client_msg.decode())

server_recv = server_socket.recv(1024)
print("Server received:", server_recv.decode())

# 4. Send from server → client:
server_msg = b"Hello from Server!"
server_socket.sendall(server_msg)
print("Server sent:", server_msg.decode())

client_recv = client_socket.recv(1024)
print("Client received:", client_recv.decode())

# 5. Shutdown and cleanup:
proxy.join(shutdown=True)
for sock in (client_socket, proxy_in, proxy_out, server_socket):
    sock.close()
```

### asyncproxy -- `TCPProxy` Example

This example shows how to set up a TCP proxy accepting connections on
`localhost:8080` and forwarding it to `www.google.com:80`.

```python
import socket
from time import sleep
from asyncproxy.TCPProxy import TCPProxy

# 1. Initialize and start the proxy:
#    - Listen on local port 8080
#    - Forward all traffic to www.google.com:80
proxy = TCPProxy(port=8080, newhost='www.google.com', newport=80)
proxy.start()
print("TCPProxy running on:", proxy.sock.getsockname())

# 2. Connect via the proxy and send HTTP requests twice
for _ in (1, 2):
    with socket.create_connection(('127.0.0.1', 8080)) as s:
        print("Connected to www.google.com via TCPProxy.")
        s.sendall(b"GET / HTTP/1.0\r\nHost: www.google.com\r\n\r\n")
        resp = s.recv(256)
        print("Response received from proxy:")
        print(resp.decode('utf-8', errors='replace'))

# 3. Shutdown the proxy cleanly
proxy.shutdown()
```

### asyncproxy -- Advanced `AsyncProxy2FD` Example

This example shows how to subclass `AsyncProxy2FD` to inspect and modify data in transit using custom `in2out` and `out2in` hooks.

```python
import socket
from ctypes import string_at, memmove
from asyncproxy.AsyncProxy import AsyncProxy2FD

class NosyProxy(AsyncProxy2FD):
    def in2out(self, res_p):
        # Unpack the struct
        tr = res_p.contents
        ptr, length = tr.buf, tr.len

        # Read original bytes, transform, and write back
        original    = string_at(ptr, length)
        length     -= 1
        transformed = original.upper()[:length]
        memmove(ptr, transformed, length)
        tr.len = length

        print("in2out hook:", original, "→", transformed)

    def out2in(self, res_p):
        tr = res_p.contents
        ptr, length = tr.buf, tr.len

        original    = string_at(ptr, length)
        length     -= 1
        transformed = original[::-1][1:]
        memmove(ptr, transformed, length)
        tr.len = length

        print("out2in hook:", original, "→", transformed)

# 1. Create two socket pairs for bidirectional flow
client_socket, proxy_in       = socket.socketpair()
proxy_out,    server_socket   = socket.socketpair()

# 2. Initialize and start the custom proxy
proxy = NosyProxy(proxy_in.fileno(), proxy_out.fileno())
proxy.start()

# 3. Client → Server (uppercase transformation)
client_msg = b"Hello from Client!"
client_socket.sendall(client_msg)
print("Client sent:", client_msg.decode())

srv_recv = server_socket.recv(1024)
print("Server received:", srv_recv.decode())

# 4. Server → Client (reverse transformation)
server_msg = b"Hello from Server!"
server_socket.sendall(server_msg)
print("Server sent:", server_msg.decode())

cli_recv = client_socket.recv(1024)
print("Client received:", cli_recv.decode())

# 5. Shutdown and cleanup
proxy.join(shutdown=True)
for sock in (client_socket, proxy_in, proxy_out, server_socket):
    sock.close()
```
