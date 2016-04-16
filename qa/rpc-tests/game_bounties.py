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
    print "  txid: %s" % txid

    # Try to get information of the tx in the wallet of node 0.
    print "Verifying bounty transaction in the wallet..."
    valid = self.nodes[0].validateaddress (addr)
    assert valid['ismine']
    blkhash = self.nodes[0].getbestblockhash ()
    self.verifyTx (0, txid, blkhash, value, 1)

    # Import the private key into node 3 and verify that rescanning
    # makes the game tx appear.
    print "Importing private key into wallet and rescanning..."
    try:
      self.verifyTx (3, txid, blkhash, value, 1)
      raise AssertionError ("transaction in wallet before importing")
    except JSONRPCException as exc:
      assert_equal (-5, exc.error['code'])
    privkey = self.nodes[0].dumpprivkey (addr)
    self.nodes[3].importprivkey (privkey)
    self.verifyTx (3, txid, blkhash, value, 1)

    # Construct a tx spending the bounty.
    print "Trying to spend immature bounty..."
    spendValue = value - Decimal ('0.01')
    toAddr = self.nodes[1].getnewaddress ()
    inputs = [{"txid": txid, "vout": 0}]
    outputs = {toAddr: spendValue}
    rawtx = self.nodes[0].createrawtransaction (inputs, outputs)
    data = self.nodes[0].signrawtransaction (rawtx)
    assert data['complete']
    rawtx = data['hex']
    try:
      self.nodes[2].sendrawtransaction (rawtx)
      raise AssertionError ("immature spend accepted")
    except JSONRPCException as exc:
      assert_equal (-26, exc.error['code'])

    # The amount should become available as the tx matures.
    print "Letting bounty tx mature..."
    self.advance(0, 99)
    self.verifyTx (0, txid, blkhash, value, 100)
    self.verifyTx (3, txid, blkhash, value, 100)
    self.advance(0, 1)
    self.verifyTx (0, txid, blkhash, value, 101)
    self.verifyTx (3, txid, blkhash, value, 101)

    # Now the spend should succeed.
    spendTxid = self.nodes[2].sendrawtransaction (rawtx)
    self.advance (0, 1)
    txdata = self.nodes[1].gettransaction (spendTxid)
    assert_equal (1, txdata['confirmations'])

    # TODO: Check reorg that invalidates the bounty.

  def verifyTx (self, node, txid, blkhash, value, conf):
    """
    Verify the bounty transaction in the wallet and its UTXO entry.
    """

    txdata = self.nodes[node].gettransaction (txid)
    assert txdata['bounty']
    assert_equal (blkhash, txdata['blockhash'])
    assert_equal (txid, txdata['txid'])
    assert_equal (conf, txdata['confirmations'])

    if conf > 100:
      cat = 'bounty'
      amount = value
    else:
      cat = 'immature_bounty'
      amount = Decimal ('0')

    assert_equal (1, len (txdata['details']))
    assert_equal (cat, txdata['details'][0]['category'])
    assert_equal (amount, txdata['amount'])

    utxo = self.nodes[node].gettxout (txid, 0)
    assert_equal (value, utxo['value'])
    assert_equal (conf, utxo['confirmations'])
    assert not utxo['coinbase']
    assert utxo['bounty']

if __name__ == '__main__':
  GameTxIndexTest ().main ()
