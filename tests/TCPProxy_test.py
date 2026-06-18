import unittest

from asyncproxy.ForwarderFast import ForwarderFast


class TCPProxyTest(unittest.TestCase):
    def test_Forwarder_fast(self):
        self.assertIs(ForwarderFast.fast, True)


def runme():
    unittest.main(module=__name__)


if __name__ == '__main__':
    runme()
