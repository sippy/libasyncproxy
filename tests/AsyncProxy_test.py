# Copyright (c) 2017-2025 Sippy Software, Inc. All rights reserved.
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

import unittest, sys
from time import sleep
from socket import socketpair, AF_INET

from asyncproxy.AsyncProxy import AsyncProxy, AsyncProxy2FD, setdebug

@unittest.skipIf(sys.platform == 'darwin', "asyncproxy tests hang on macOS")
class AsyncProxyTest(unittest.TestCase):
    debug = False
    def test_AsyncProxy(self):
        getnull = lambda: (open('/dev/null', 'r+'), open('/dev/null', 'r+'))
        getrandom = lambda: (open('/dev/urandom', 'r'), open('/dev/urandom', 'r'))

        if self.debug: setdebug(2)

        dn = socketpair()
        a = AsyncProxy(dn[0].fileno(), 'gmail-smtp-in.l.google.com', 25, AF_INET, None)
        a.start()
        while a.getsockname()[1] == 0:
            print(a.isAlive(), a.getsockname())
        lclsrc = a.getsockname()[0]
        a.join()
        for s in dn: s.close()

        for source in (getnull(), getrandom(), socketpair()):
            for sport in 80, 12345:
                #source = socketpair()
                a = AsyncProxy(source[0].fileno(), 'gmail-smtp-in.l.google.com', 25, AF_INET, lclsrc)
                print(a.isAlive(), a.getsockname())
                a.start()
                print(a.isAlive())
                b = AsyncProxy(source[1].fileno(), 'www.google.com', sport, AF_INET, lclsrc)
                print(b.isAlive())
                b.start()
                print(b.isAlive())
                print('a=%s b=%s' % (a.describe(), b.describe()))
                i = 0
                while (a.isAlive() or b.isAlive()) and i < 10:
                    sleep(1)
                    i += 1
                print('a=%s b=%s' % (a.describe(), b.describe()))
                a.join()
                b.join()
            for s in source: s.close()
        args = getnull()
        a = AsyncProxy2FD(*(x.fileno() for x in args))
        a.start()
        a.join(shutdown=False)
        for s in args: s.close()

def runme():
    unittest.main(module = __name__)

if __name__ == '__main__':
    AsyncProxyTest.debug = True
    runme()
