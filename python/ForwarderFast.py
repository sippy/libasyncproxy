# Copyright (c) 2010-2017 Sippy Software, Inc. All rights reserved.
#
# Warning: This computer program is protected by copyright law and
# international treaties. Unauthorized reproduction or distribution of this
# program, or any portion of it, may result in severe civil and criminal
# penalties, and will be prosecuted under the maximum extent possible under
# law.

import socket
from AsyncProxy import AsyncProxy

class ForwarderFast(AsyncProxy):
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

    @property
    def port2(self):
        if self._port2 == None:
            p = self.getsockname()[1]
            if p != 0:
                self._port2 = p
        return self._port2
