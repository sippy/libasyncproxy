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

TCPProxy: set of high-level classes to accept and manage inbound connections
and initiate/tear-down outbound as needed, connecting them using forwarders
once established.

TCPProxyActive: a high-level active TCP forwarder. Instead of listening for
inbound TCP clients, it creates a non-blocking outbound connection to
`destaddr` and forwards that connection to `newhost:newport`.

## Python Callbacks

`AsyncProxy`, `AsyncProxy2FD`, `ForwarderFast`, `TCPProxy`, and
`TCPProxyActive` can be subclassed to provide callback methods. The native
module discovers these methods when `start()` is called.

All callbacks run in the proxy worker thread context. Callback code that reads
or writes data shared with other threads must use appropriate locking or other
synchronization.

`in2out(res_p)` is called for bytes flowing from the source side to the sink
side. `out2in(res_p)` is called for bytes flowing from the sink side to the
source side. `res_p.contents` is a transform result with `buf` and `len`.
Callbacks may rewrite bytes in place and update `len`.

`on_connect(res_p, max_len)` is called after the proxy's sink-side connection
is established. Bytes written to `res_p.contents` are queued toward the sink.
This is useful for protocols that need to send an initial client greeting or
handshake to the remote endpoint.

`on_source_connect(res_p, max_len)` is called after the source side is
connected. For `TCPProxyActive`, this means the non-blocking connection to `destaddr` has
completed. Bytes written to `res_p.contents` are queued toward the source. This
is useful when the active peer should receive an initial banner or handshake
from the proxy.

`disc_cb()` is called when the native proxy worker exits.

Callbacks may also be registered explicitly with `set_i2o()`, `set_o2i()`,
`set_on_connect()`, `set_on_source_connect()`, and `set_on_disconnect()`.

Callbacks that receive `max_len` must not write more than `max_len` bytes to
the provided buffer.

At the C API layer, connection callbacks use a single
`asyncproxy_set_on_connect()` registration. Set `cb_info.connected_flags` to
the bitmask of events the callback should receive:
`ASYNCPROXY_CONNECTED_SOURCE`, `ASYNCPROXY_CONNECTED_SINK`, or
`ASYNCPROXY_CONNECTED_BOTH`. The callback receives the event-side bit in
`args->connected_flags`; `ASYNCPROXY_CONNECTED_BOTH` is additionally set when
both endpoints are connected.

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

### asyncproxy -- `on_connect` Handshake Example

`on_connect(res_p, max_len)` can emit bytes as soon as the proxy's sink
connection is ready. The bytes are queued toward the sink before normal
source-to-sink traffic is relayed.

```python
from ctypes import memmove
from socket import AF_INET
from asyncproxy.AsyncProxy import AsyncProxy

class GreetingProxy(AsyncProxy):
    def on_connect(self, res_p, max_len):
        greeting = b"HELLO\r\n"
        assert len(greeting) <= max_len
        tr = res_p.contents
        memmove(tr.buf, greeting, len(greeting))
        tr.len = len(greeting)

# source_sock is an already-open source socket.
proxy = GreetingProxy(source_sock.fileno(), "example.com", 12345, AF_INET, None)
proxy.start()
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

### asyncproxy -- `TCPProxyActive` Example

`TCPProxyActive` starts from an outbound TCP connection instead of an inbound
listener. It connects to `destaddr` and forwards that active connection to
`newhost:newport`.

```python
from asyncproxy.TCPProxy import TCPProxyActive

# Connect actively to 127.0.0.1:9000, then forward that connection to
# www.google.com:80.
proxy = TCPProxyActive(
    destaddr=("127.0.0.1", 9000),
    newhost="www.google.com",
    newport=80,
    bindhost="127.0.0.1",
)
proxy.start()

# Use proxy.port1 / proxy.port2 for diagnostics if needed.
print("active peer port:", proxy.port1)
print("outbound local port:", proxy.port2)

proxy.shutdown()
```

To run code when the active source connection completes, subclass
`TCPProxyActive` and implement `on_source_connect(res_p, max_len)`:

```python
from ctypes import memmove
from asyncproxy.TCPProxy import TCPProxyActive

class BannerProxy(TCPProxyActive):
    def on_source_connect(self, res_p, max_len):
        banner = b"ready\n"
        assert len(banner) <= max_len
        tr = res_p.contents
        memmove(tr.buf, banner, len(banner))
        tr.len = len(banner)

proxy = BannerProxy(
    destaddr=("127.0.0.1", 9000),
    newhost="www.google.com",
    newport=80,
)
proxy.start()
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
