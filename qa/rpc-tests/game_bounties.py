#!/usr/bin/env python
# Copyright (c) 2016 Daniel Kraft
# Distributed under the MIT/X11 software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

# Check wallet handling of bounty transactions.

from test_framework.game import GameTestFramework
from test_framework.util import *

class GameTxIndexTest (GameTestFramework):

  def run_test (self):
    GameTestFramework.run_test (self)

    # Register a player.
    self.register (0, "me", 0)
    self.advance (0, 1)
    me = self.get (0, "me", 0)

    # Collect nearest loot and bank.
    pos = self.nearestLoot (0, me.pos)
    print "Nearest loot on (%d, %d), collecting..." % (pos[0], pos[1])
    me.move (pos)
    me = self.finishMove (0, "me", 0)
    print "Collected %.8f HUC." % me.loot
    assert me.loot > 0
    print "Banking the coins..."
    me.move ([0, 0])
    me = self.finishMove (0, "me", 0)

    # Pick up bounty txid.
    blkhash = self.nodes[0].getbestblockhash ()
    blkdata = self.nodes[0].getblock (blkhash)
    gametxIds = blkdata['gametx']
    assert_equal (len (gametxIds), 1)
    txid = gametxIds[0]

    # Get tx details (node 1 has -txindex) and verify them.
    tx = self.nodes[1].getrawtransaction (txid, 1)
    assert_equal (len (tx['vin']), 1)
    assert_equal (len (tx['vout']), 1)
    gametxIn = tx['vin'][0]['gametx']
    assert_equal (gametxIn['player'], 'me')
    assert_equal (gametxIn['op'], 'banking')
    gametxOut = tx['vout'][0]
    value = gametxOut['value']
    assert_equal (len (gametxOut['scriptPubKey']['addresses']), 1)
    addr = gametxOut['scriptPubKey']['addresses'][0]
    print "Bounty: %.8f to %s" % (value, addr)

    # Try to get information of the tx in the wallet of node 0.
    print "Verifying bounty transaction in the wallet..."
    print "Txid: %s" % txid
    valid = self.nodes[0].validateaddress (addr)
    assert valid['ismine']
    blkhash = self.nodes[0].getbestblockhash ()
    txdata = self.nodes[0].gettransaction (txid)
    assert txdata['bounty']
    assert_equal (blkhash, txdata['blockhash'])
    assert_equal (txid, txdata['txid'])
    assert_equal (1, txdata['confirmations'])
    assert_equal (Decimal ('0'), txdata['amount'])

    # The amount should become available as the tx matures.
    print "Letting bounty tx mature..."
    self.advance(0, 99)
    txdata = self.nodes[0].gettransaction (txid)
    assert_equal (blkhash, txdata['blockhash'])
    assert_equal (txid, txdata['txid'])
    assert_equal (100, txdata['confirmations'])
    assert_equal (Decimal ('0'), txdata['amount'])
    assert_equal (1, len (txdata['details']))
    assert_equal ('immature_bounty', txdata['details'][0]['category'])
    self.advance(0, 1)
    txdata = self.nodes[0].gettransaction (txid)
    assert_equal (blkhash, txdata['blockhash'])
    assert_equal (txid, txdata['txid'])
    assert_equal (101, txdata['confirmations'])
    assert_equal (value, txdata['amount'])
    assert_equal (1, len (txdata['details']))
    assert_equal ('bounty', txdata['details'][0]['category'])

    # TODO: Check importprivkey (rescanning) with this tx.
    # TODO: Check reorg that invalidates the bounty.
    # TODO: gettxout, gametx flag

if __name__ == '__main__':
  GameTxIndexTest ().main ()
