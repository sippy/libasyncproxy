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
from threading import Thread, Lock
import socket, select, fcntl, os
import traceback
from time import strftime
from errno import EINTR

class Forwarder(Thread):
    daemon = True
    port1 = None
    port2 = None
    dead = False
    bindhost_out = None
    sink = None
    source = None
    state = '__init__'
    state_lock = None

    def __init__(self, source, sink_addr, bindhost_out = None, logger = None):
        self.state_lock = Lock()
        self.port1 = source.getpeername()[1]
        self.port2 = None
        Thread.__init__(self)
        self.source = source
        self.sink_addr = sink_addr
        self.bindhost_out = bindhost_out
        self.logger = logger
        #print('Creating new pipe thread  %s ( %s -> %s )' % \
        #    ( self, source.getpeername(), sink_addr[0] ))

    def setstate(self, s):
        self.state_lock.acquire()
        self.state = s
        self.state_lock.release()

    def getstate(self):
        self.state_lock.acquire()
        s = self.state
        self.state_lock.release()
        return s

    def describe(self):
        self.state_lock.acquire()
        s = 'Forwarder(%s) ( %s -> %s ), state = %s' % (self, self.port1, self.port2, self.state)
        self.state_lock.release()
        return s

    def run(self):
        try:
            self.setstate('self.sink = socket.socket()')
            self.sink = socket.socket(self.sink_addr[1], socket.SOCK_STREAM)
            if self.bindhost_out != None and self.bindhost_out != '127.0.0.1':
                self.setstate('self.sink.bind(%s)' % str(self.bindhost_out))
                self.sink.bind((self.bindhost_out, 0))
            self.sink.settimeout(10)
            self.setstate('%s -> self.sink.connect(%s)' % (self.getstate(), str(self.sink_addr)))
            self.sink.connect(self.sink_addr[0])
            self.setstate('self.sink.getsockname()')
            sn = self.sink.getsockname()
            self.port2 = sn[1] if (self.sink_addr[1] != socket.AF_UNIX) else 'AF_UNIX'
            self.setstate('flags = fcntl.fcntl(self.sink)')
            flags = fcntl.fcntl(self.sink, fcntl.F_GETFL, 0)
            flags = flags | os.O_NONBLOCK
            self.setstate('fcntl.fcntl(self.sink, fcntl.F_SETFL, flags)')
            fcntl.fcntl(self.sink, fcntl.F_SETFL, flags)
            flags = fcntl.fcntl(self.source, fcntl.F_GETFL, 0)
            flags = flags | os.O_NONBLOCK
            self.setstate('fcntl.fcntl(self.source, fcntl.F_SETFL, flags)')
            fcntl.fcntl(self.source, fcntl.F_SETFL, flags)
            buf_up = b''
            buf_down = b''
            pollobj = select.poll()
            pollobj.register(self.sink.fileno(), select.POLLIN)
            pollobj.register(self.source.fileno(), select.POLLIN)
            while True:
                self.setstate('select.poll()')
                if len(buf_up) > 0:
                    pollobj.modify(self.sink.fileno(), select.POLLIN | select.POLLOUT)
                else:
                    pollobj.modify(self.sink.fileno(), select.POLLIN)
                if len(buf_down) > 0:
                    pollobj.modify(self.source.fileno(), select.POLLIN | select.POLLOUT)
                else:
                    pollobj.modify(self.source.fileno(), select.POLLIN)

                for fd, event in pollobj.poll():
                    if event & select.POLLIN != 0:
                        if fd == self.sink.fileno():
                            self.setstate('self.sink.recv()')
                            try:
                                data = self.sink.recv(1024 * 8)
                            except:
                                data = b''
                        else:
                            self.setstate('self.source.recv()')
                            try:
                                data = self.source.recv(1024 * 8)
                            except:
                                data = b''
                        #print(self, 'received %d bytes' % len(data))
                        if not data:
                            if fd == self.source.fileno():
                                buf_down = b''
                            else:
                                buf_up = b''
                            if len(buf_up) == 0 and len(buf_down) == 0:
                                self.setstate('self.shutdown(autoshutdown = True) line 80')
                                self.shutdown()
                                return
                            else:
                                continue
                        if fd == self.source.fileno():
                            buf_up += data
                        else:
                            buf_down += data
                    if event & select.POLLOUT != 0:
                        if fd == self.sink.fileno():
                            self.setstate('self.sink.send(buf_up)')
                            try:
                                size = self.sink.send(buf_up)
                                buf_up = buf_up[size:]
                            except:
                                buf_up = b''
                        else:
                            self.setstate('size = self.source.send(buf_down)')
                            try:
                                size = self.source.send(buf_down)
                                buf_down = buf_down[size:]
                            except:
                                buf_down = b''
                        #print(self, 'sent %d bytes' % size)
        except socket.timeout as e:
            if self.dead:
                return
            self.log('timed out when processing data in state %s: %s' % (self.getstate(), str(e)))
        except select.error as e:
            if self.dead:
                return
            if e.errno == EINTR:
                # Ignoring 'Interrupted system call'
                pass
            else:
                self.log("got select.error exception: %s" % str(e))
        except Exception as e:
            if self.dead:
                return
            self.log('unhandled exception when processing data in state %s' % self.getstate())
            self.log('-' * 70)
            self.log(traceback.format_exc())
            self.log('-' * 70, True)
            self.setstate('self.shutdown(autoshutdown = True) #1')
            self.shutdown()
            self.log('shutting down channel')
            raise e
        self.setstate('self.shutdown(autoshutdown = True) #2')
        self.shutdown()

    def shutdown(self):
        self.state_lock.acquire()
        if self.dead:
            self.state_lock.release()
            return
        self.dead = True
        if self.sink != None:
            self.sink.close()
            self.sink = None
        if self.source != None:
            self.source.close()
            self.source = None
        self.state_lock.release()

    def log(self, msg, flush = False):
        msg = 'Forwarder[%d]: %s' % (hash(self), msg)
        if self.logger != None:
            self.logger.log(msg, flush)
        else:
            print("%s: %s" % (strftime("%Y-%m-%d %H:%M:%S"), msg))
            if flush:
                sys.stdout.flush()

    def isAlive(self):
        return self.is_alive()
