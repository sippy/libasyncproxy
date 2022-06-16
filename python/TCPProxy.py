# Copyright (c) 2010-2017 Sippy Software, Inc. All rights reserved.
#
# Warning: This computer program is protected by copyright law and
# international treaties. Unauthorized reproduction or distribution of this
# program, or any portion of it, may result in severe civil and criminal
# penalties, and will be prosecuted under the maximum extent possible under
# law.

import sys
from threading import Thread
import socket, os, select
import traceback
from time import sleep, strftime
from errno import EADDRINUSE, ENOTCONN

try:
    from .ForwarderFast import ForwarderFast as Forwarder
except:
    from .Forwarder import Forwarder

class TCPProxy(Thread):
    dead = False
    forwarders = None
    allowed_ips = None
    bindhost_out = None

    def __init__(self, port, newhost, newport, bindhost = '127.0.0.1', logger = None):
        self.my_pid = os.getpid()
        Thread.__init__(self)
        #print('Redirecting: %s:%s -> %s:%s' % ( bindhost, port, newhost, newport ))
        self.newhost = newhost
        self.newport = newport
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
        sock.listen(500)
        self.sock = sock
        self.forwarders = []
        self.setDaemon(True)

    def run(self):
        poller = select.poll()
        READ_ONLY = select.POLLIN | select.POLLPRI | select.POLLHUP | select.POLLERR
        poller.register(self.sock.fileno(), READ_ONLY)
        while 1:
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
            e = None
            try:
                #print('self.sock.accept()')
                newsock, address = self.sock.accept()
                #print(newsock, address)
                if self.dead:
                    self.log('ignore connection attempt from IP %s during shutdown' % address[0])
                    newsock.shutdown(socket.SHUT_RDWR)
                    newsock.close()
                    continue
                if self.allowed_ips != None and address[0] not in self.allowed_ips:
                    newsock.shutdown(socket.SHUT_RDWR)
                    newsock.close()
                    self.log('connection attempt from the unknown IP %s has been rejected' % address[0])
                    continue
            except socket.error as e:
                if self.dead:
                    return
                errno, string = e.errno, e.strerror
                if errno == 54:
                    # Ignore 'Connection reset by peer'
                    self.log("Ignoring 'Connection reset by peer'")
                    continue
                elif errno == 4:
                    # Ignore 'Interrupted system call'
                    self.log("Ignoring 'Interrupted system call'")
                    continue
                self.log("got socket.error exception: %s" % str(e))
                continue

            if e != None:
                self.log('-' * 70)
                self.log(traceback.format_exc())
                self.log('-' * 70, True)
                sleep(0.01)
                continue

            try:
                fwd = Forwarder(newsock, (self.newhost, self.newport), self.bindhost_out, logger = self.logger)
                self.forwarders.append(fwd)
                fwd.start()
            except Exception as e:
                if self.dead:
                    return
            if e != None:
                self.log('setting up redirection to %s:%s failed' % (self.newhost, self.newport))
                self.log('-' * 70)
                self.log(traceback.format_exc())
                self.log('-' * 70, True)
                sleep(0.01)
                continue
            forwarders = []
            for fwd in self.forwarders:
                if fwd.isAlive():
                    forwarders.append(fwd)
                else:
                    #print('joinning forwarder:', fwd.describe())
                    fwd.join()
                    #print('joinning forwarder done:', fwd.describe())
            self.forwarders = forwarders

    def shutdown(self):
        self.dead = True
        while len(self.forwarders) > 0:
            forwarder = self.forwarders.pop()
            #print('shutting down forwarder:', forwarder.describe())
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
            print("%s: %s" % (strftime("%Y-%m-%d %H:%M:%S"), msg))
            if flush:
                sys.stdout.flush()
