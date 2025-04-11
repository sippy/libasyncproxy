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
Forwarder if that fails.

## Use Cases

We use this library to allow applications to be redirected to one of several
available DB replicas and re-routed instantly if the configuration changes.

## Build and Install Python module from source code:

```
git clone https://github.com/sippy/libasyncproxy.git
pip install libasyncproxy/
```

## Usage
