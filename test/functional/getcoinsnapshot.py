#!/usr/bin/env python3
# Copyright (c) 2017 Daniel Kraft
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

# Test the "getcoinsnapshot" RPC command.

from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import *

class GetStatsForHeightTest (BitcoinTestFramework):

  def set_test_params (self):
    self.num_nodes = 2

  def run_test (self):
    # Generate a block so that we are not "downloading blocks".
    self.nodes[0].generate (1)

    # Create a name_new output and an actually registered name.
    # This locks 1.2 HUC.
    self.nodes[0].name_new ("foo")
    self.nodes[0].name_register ("foo", '{"color": 0}')
    self.nodes[0].generate (1)
    inNames = Decimal ('1.2')

    # Send 1 and 2 HUC to two addresses.  This can then be used to verify that
    # the 2 HUC address shows up while the 1 HUC does not if we set the
    # minAmount accordingly.  Create the addresses on a different node so that
    # we won't accidentally spend these coins again with future transactions.
    # We send the 2 HUC in parts below the threshold, to verify that all balance
    # of an address counts, not individual outputs.
    addrOne = self.nodes[1].getnewaddress ()
    addrTwo = self.nodes[1].getnewaddress ()
    self.nodes[0].sendtoaddress (addrOne, 1)
    self.nodes[0].sendtoaddress (addrTwo, 0.75)
    self.nodes[0].sendtoaddress (addrTwo, 1.25)
    self.nodes[0].generate (1)

    # Send coins to a multisig ("strange") address.
    pk1 = self.nodes[1].validateaddress (addrOne)['pubkey']
    pk2 = self.nodes[1].validateaddress (addrTwo)['pubkey']
    addr = self.nodes[1].addmultisigaddress (1, [pk1, pk2])
    strange = Decimal ('5')
    self.nodes[0].sendtoaddress (addr, strange)
    self.nodes[0].generate (1)

    # Send coins to an unspendable OP_RETURN output.
    inp = self.nodes[0].listunspent ()[0]
    rawtx = self.nodes[0].createrawtransaction ([inp], {'data': '00112233'})
    unspendable = Decimal ('0.5')
    # The amount is set to zero by createrawtransaction, so we have to manually
    # override it in the hex data.  The suffix of the tx (after the amount)
    # includes the nLockTime and the script, which is 066a0400112233.
    newtx = rawtx[:-38]
    newtx += self.hex_amount (int (Decimal (1e8) * unspendable))
    newtx += rawtx[-22:]
    signed = self.nodes[0].signrawtransaction (newtx)
    self.nodes[0].sendrawtransaction (signed['hex'], True)
    self.nodes[0].generate (1)

    # Finally, call getcoinsnapshot and verify expected results.
    snapshot = self.nodes[0].getcoinsnapshot (1.5)
    assert_equal (self.nodes[0].getbestblockhash (), snapshot['hashblock'])
    assert_equal (inNames, snapshot['innames'])
    assert_equal (strange, snapshot['strange'])
    assert addrOne not in snapshot['addresses']
    assert addrTwo in snapshot['addresses']
    assert_equal (2, snapshot['addresses'][addrTwo])
    total = snapshot['innames']
    total += snapshot['strange']
    total += snapshot['toosmall']
    total += snapshot['amount']
    total += unspendable
    assert_equal (self.expected_coins (self.nodes[0].getblockcount ()), total)

  def hex_amount (self, val):
    """
    Utility method to encode a Satoshi amount in hex.
    """
    res = ''
    for n in range(8):
      rem = val % (1 << 8)
      val >>= 8
      res += '%02x' % rem
    return res

  def expected_coins (self, height):
    """
    Compute the expected total number of coins (according to mining rewards)
    at the given block height.  Assumes no game transactions have occured so
    far, i. e., the mining reward starts at 1 HUC before halving.
    """
    span = 150
    reward = Decimal (1)
    res = Decimal (50)  # Genesis block premine on regtest net.
    for h in range(1, height + 1):
      if h % span == 0:
        reward /= 2
      res += reward
    return res

if __name__ == '__main__':
  GetStatsForHeightTest ().main ()
