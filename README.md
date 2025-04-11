# libasyncproxy

## Introduction

The libasyncproxy is a fairy simple C library and a respective python wrapper,
which allows connecting two sockets and relay data in/out in a background
using a worker thread (one per connection at the moment).

## History

The code was created to allow Python code implementing application-layer proxy
to manage session routing and connection, while handling all transfers outside
of confinments of the slow Python and its GIL.

## Build and Install Python module from source code:

```
git clone https://github.com/sippy/libasyncproxy.git
pip install libasyncproxy/
```
