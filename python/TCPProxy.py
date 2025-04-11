# Copyright (c) 2010-2025 Sippy Software, Inc. All rights reserved.
#
# Redistribution and use in source and binary forms, with or without modification,
# are permitted provided that the following conditions are met:
#
# 1. Redistributions of source code must retain the above copyright notice, this
# list of conditions and the following disclaimer.
#
# 2. Redistributions in binary form must reproduce the above copyright notice,
# this list of conditions and the following disclaimer in the documentation and/or
# other materials provided with the distribution.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
# ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
# WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
# DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR
# ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
# (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
# LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
# ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
# SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

import sys
from threading import Thread
import socket, os, select
import traceback
from time import sleep, strftime
from errno import EADDRINUSE, ECONNRESET, EINTR

try:
    from ctypes import ArgumentError
    from .ForwarderFast import ForwarderFast as _Forwarder
    from .Forwarder import Forwarder as _Forwarder_safe
    def Forwarder(*a, **kwa):
        try: return _Forwarder(*a, **kwa)
        except (TypeError, ArgumentError):
            return _Forwarder_safe(*a, **kwa)
except:
    from .Forwarder import Forwarder

class TCPProxyBase(Thread):
    daemon = True
    dead = False
    debug = False
    forwarders = None
    allowed_ips: tuple = None
    bindhost_out = None
    disc_cb:callable = None

    def __init__(self, port, newhost, newport = None, bindhost = '127.0.0.1', logger = None, newaf = None):
        if newaf is None:
            newaf = socket.AF_INET if (newport is not None) else socket.AF_UNIX
        self.my_pid = os.getpid()
        super().__init__()
        self.dprint(lambda: f'Redirecting: {bindhost}:{port} -> {newhost}:{newport}')
        self.newhost = newhost
        self.newport = newport
        self.newaf = newaf
        self.logger = logger
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        bindaddr = (bindhost, port)
        try:
            sock.bind(bindaddr)
        except OSError as oex:
            if oex.errno != EADDRINUSE:
                raise oex
            try:
                sock.connect(bindaddr)
                raise oex
            except ConnectionRefusedError:
                sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
                sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEPORT, 1)
                sock.bind(bindaddr)
        self.port = port if (port != 0) else sock.getsockname()[1]
        self.sock = sock
        self.forwarders = []

    def dprint(self, get_msg):
        if not self.debug: return
        sys.stderr.write(f'{get_msg()}\n')
        sys.stderr.flush()

    def spawn_forwarder(self, newsock):
        daddr = (self.newhost, self.newport) if (self.newaf != socket.AF_UNIX) else self.newhost
        try:
            fwd = Forwarder(newsock, (daddr, self.newaf), self.bindhost_out, logger = self.logger)
            self.forwarders.append(fwd)
            fwd.start()
        except Exception:
            if self.dead:
                return
            dst = f'{self.newhost}:{self.newport}' if (self.newaf != socket.AF_UNIX) else f'"{self.newhost}"'
            self.log(f'setting up redirection to {dst} failed')
            self.log('-' * 70)
            self.log(traceback.format_exc())
            self.log('-' * 70, True)
            sleep(0.01)
            return

        forwarders = []
        for fwd in self.forwarders:
            if fwd.isAlive():
                forwarders.append(fwd)
            else:
                self.dprint(lambda: f'joinning forwarder: {fwd.describe()}')
                fwd.join()
                self.dprint(lambda: f'joinning forwarder done: {fwd.describe()}')
        self.forwarders = forwarders

    def shutdown(self):
        self.dead = True
        while len(self.forwarders) > 0:
            forwarder = self.forwarders.pop()
            self.dprint(lambda: f'shutting down forwarder: {forwarder.describe()}')
            if forwarder.isAlive():
                forwarder.shutdown()
            forwarder.join()
        self.sock.close()
        self.join()

    def log(self, msg, flush = False):
        msg = 'TCPProxy[%d]: %s' % (hash(self), msg)
        if self.logger != None:
            self.logger.log(msg, flush)
        else:
            self.dprint(lambda: f'{strftime("%Y-%m-%d %H:%M:%S")}: {msg}')
            if flush:
                sys.stdout.flush()

class TCPProxyActive(TCPProxyBase):
    destaddr: tuple
    def __init__(self, destaddr, *a, **kwa):
        super().__init__(0, *a, **kwa)
        self.destaddr = destaddr

    def run(self):
        self.sock.connect(self.destaddr)
        self.spawn_forwarder(self.sock)
        self.forwarders[0].join()
        if self.disc_cb is not None:
            # pylint: disable-next=not-callable
            self.disc_cb()
            self.disc_cb = None

class TCPProxy(TCPProxyBase):
    def __init__(self, *a, **kwa):
        super().__init__(*a, **kwa)
        self.sock.listen(500)

    def access_check(self, address):
        if self.allowed_ips is None or address[0] in self.allowed_ips:  # pylint: disable=unsupported-membership-test
            return True
        return False

    def run(self):
        poller = select.poll()
        READ_ONLY = select.POLLIN | select.POLLPRI | select.POLLHUP | select.POLLERR
        poller.register(self.sock.fileno(), READ_ONLY)
        while True:
            events = poller.poll(250)
            if self.dead:
                break
            if len(events) == 0:
                continue
            fd, flag = events[0]
            if flag & select.POLLHUP:
                break
            if not flag & (select.POLLIN | select.POLLPRI):
                continue
            try:
                self.dprint(lambda: 'self.sock.accept()')
                newsock, address = self.sock.accept()
                self.dprint(lambda: f'{newsock=}, {address=}')
                if self.dead:
                    self.log('ignore connection attempt from IP %s during shutdown' % address[0])
                    newsock.shutdown(socket.SHUT_RDWR)
                    newsock.close()
                    continue
                if not self.access_check(address):
                    newsock.shutdown(socket.SHUT_RDWR)
                    newsock.close()
                    self.log('connection attempt from the unknown IP %s has been rejected' % address[0])
                    continue
            except socket.error as e:
                if self.dead:
                    return
                if e.errno == ECONNRESET:
                    # Ignore 'Connection reset by peer'
                    self.log("Ignoring 'Connection reset by peer'")
                    continue
                elif e.errno == EINTR:
                    # Ignore 'Interrupted system call'
                    self.log("Ignoring 'Interrupted system call'")
                    continue
                self.log("got socket.error exception: %s" % str(e))
                continue
            self.spawn_forwarder(newsock)
        if self.disc_cb is not None:
            # pylint: disable-next=not-callable
            self.disc_cb()
            self.disc_cb = None
