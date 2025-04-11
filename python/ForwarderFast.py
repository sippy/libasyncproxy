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

import socket

from .AsyncProxy import AsyncProxy, setdebug as AP_setdebug

class ForwarderFast(AsyncProxy):
    debug = False
    port1 = None
    _port2 = None
    dead = False
    bindhost_out = None
    source = None
    state = '__init__'

    def __init__(self, source, sink_addr, bindhost_out = None, logger = None):
        addr, port = (sink_addr[0], 0) if (sink_addr[1] == socket.AF_UNIX) else sink_addr[0]
        AsyncProxy.__init__(self, source.fileno(), addr, port, sink_addr[1], bindhost_out)
        self.source = source
        self.port1 = source.getpeername()[1]
        if self.debug:
            AP_setdebug(2)

    def start(self):
        AsyncProxy.start(self)

    def describe(self):
        s = 'Forwarder(%s) ( %s -> %s ), state = %s' % (self, self.port1, self.port2, AsyncProxy.describe(self))
        return s

    def shutdown(self):
        if self.dead:
            return
        self.dead = True
        if self.source != None:
            self.source.shutdown(socket.SHUT_RDWR)
            self.source.close()
            self.source = None

    def join(self):
        super().join(shutdown=False)

    @property
    def port2(self):
        if self._port2 == None:
            p = self.getsockname()[1]
            if p != 0:
                self._port2 = p
        return self._port2
