#!/usr/bin/env python3
# Copyright (c) 2016-2017 Daniel Kraft
# Distributed under the MIT/X11 software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

# Test for the HUC-specific fee policies.

from test_framework.names import NameTestFramework
from test_framework.util import *

class GameFeePolicyTest (NameTestFramework):

  def set_test_params (self):
    self.setup_name_test ([[]] * 3)

  def run_test (self):
    # Send ordinary tx and verify that it has low fee.
    addr = self.nodes[1].getnewaddress ()
    txid = self.nodes[0].sendtoaddress (addr, Decimal('1'))
    self.mineAndVerify ("currency tx", txid, Decimal('0'), Decimal('0.005'))

    # Register a hunter.
    txid = self.nodes[0].name_register ("hunter", '{"color":0}')
    self.mineAndVerify ("name_register", txid, Decimal('1'), Decimal('1.005'))

    # Send messages of various lengths.
    self.verifyUpdateWithLength (99, Decimal ('0.01'))
    self.verifyUpdateWithLength (100, Decimal ('0.012'))
    self.verifyUpdateWithLength (199, Decimal ('0.012'))
    self.verifyUpdateWithLength (200, Decimal ('0.014'))

  def verifyUpdateWithLength (self, length, expectedFee):
    """
    Send a name_update of the given length and expect the given base fee.
    """
    prefix = '{"msg":"'
    postfix = '"}'
    mid = 'x' * (length - len (prefix) - len (postfix))
    full = prefix + mid + postfix
    assert_equal (len (full), length)
    txid = self.nodes[0].name_update ("hunter", full)
    self.mineAndVerify ("name_update length %d" % length, txid,
                        expectedFee, expectedFee + Decimal ('0.005'))

  def mineAndVerify (self, name, txid, minFee, maxFee):
    """
    Mine a block and verify that the given tx is included in it
    and has the expected fee.
    """
    self.generate(2, 1)
    data = self.nodes[0].gettransaction (txid)
    fee = -Decimal (data['fee'])
    print ("%s: %.8f" % (name, fee))
    assert_equal (data['confirmations'], 1)
    assert fee >= minFee and fee <= maxFee

if __name__ == '__main__':
  GameFeePolicyTest ().main ()
