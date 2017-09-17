#!/usr/bin/env python3
# Copyright (c) 2016-2017 Daniel Kraft
# Distributed under the MIT/X11 software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

# Check wallet handling of bounty transactions.

from test_framework.game import GameTestFramework
from test_framework.util import *

class GameBountiesTest (GameTestFramework):

  def set_test_params (self):
    self.setup_name_test ([[]] * 4)

  def run_test (self):
    # Register a player.
    self.register (0, "me", 0)
    self.advance (0, 1)
    me = self.get (0, "me", 0)

    # Collect nearest loot and bank.
    pos = self.nearestLoot (0, me.pos)
    print ("Nearest loot on (%d, %d), collecting..." % (pos[0], pos[1]))
    me.move (pos)
    me = self.finishMove (0, "me", 0)
    print ("Collected %.8f HUC." % me.loot)
    assert me.loot > 0
    print ("Banking the coins...")
    me.move ([0, 0])
    self.finishMove (0, "me", 0)
    blkhash, txid, addr, value = self.extractBounty (1)

    # Try to get information of the tx in the wallet of node 0.
    print ("Verifying bounty transaction in the wallet...")
    valid = self.nodes[0].validateaddress (addr)
    assert valid['ismine']
    blkhash = self.nodes[0].getbestblockhash ()
    self.verifyTx (0, txid, blkhash, value, 1)

    # Reorg the block away and recreate the bounty tx in another block.
    # Since no details of the bounty are changed, the original txid
    # is preserved.  This should be handled gracefully.
    print ("Invalidating block to reorg the game transaction...")
    for i in range(4):
      self.nodes[i].invalidateblock (blkhash)
    self.verifyTx (0, txid, None, value, 0)
    self.advance (0, 1)
    blkhashTmp, txidTmp, addr, value = self.extractBounty (1)
    assert_equal (txid, txidTmp)
    assert blkhash != blkhashTmp
    self.verifyTx (0, txid, blkhashTmp, value, 1)

    # Invalidate the block again.  This time, set the player's bounty tx
    # so that we get a different txid in the recreated game tx.
    print ("Invalidating again, changing bounty address...")
    for i in range(4):
      self.nodes[i].invalidateblock (blkhashTmp)
    me = self.get (0, "me", 0)
    addr = self.nodes[0].getnewaddress ()
    me.setAddress (addr)
    self.advance (0, 1)
    blkhash, txid, addrTx, value = self.extractBounty (1)
    assert_equal (addr, addrTx)
    assert txidTmp != txid
    self.verifyTx (0, txidTmp, None, value, 0)
    self.verifyTx (0, txid, blkhash, value, 1)

    # Import the private key into node 3 and verify that rescanning
    # makes the game tx appear.
    print ("Importing private key into wallet and rescanning...")
    assert_raises_jsonrpc (-5, 'Invalid or non-wallet transaction',
                           self.verifyTx, 3, txid, blkhash, value, 1)
    privkey = self.nodes[0].dumpprivkey (addr)
    self.nodes[3].importprivkey (privkey)
    self.verifyTx (3, txid, blkhash, value, 1)

    # Construct a tx spending the bounty.
    print ("Trying to spend immature bounty...")
    spendValue = value - Decimal ('0.01')
    toAddr = self.nodes[1].getnewaddress ()
    inputs = [{"txid": txid, "vout": 0}]
    outputs = {toAddr: spendValue}
    rawtx = self.nodes[0].createrawtransaction (inputs, outputs)
    data = self.nodes[0].signrawtransaction (rawtx)
    assert data['complete']
    rawtx = data['hex']
    assert_raises_jsonrpc (-26, 'premature-spend-of-gametx',
                           self.nodes[2].sendrawtransaction, rawtx)

    # The amount should become available as the tx matures.
    print ("Letting bounty tx mature...")
    self.advance(0, 99)
    self.verifyTx (0, txid, blkhash, value, 100)
    self.verifyTx (3, txid, blkhash, value, 100)
    self.advance(0, 1)
    self.verifyTx (0, txid, blkhash, value, 101)
    self.verifyTx (3, txid, blkhash, value, 101)
    self.verifyTx (0, txidTmp, None, value, 0)

    # Now the spend should succeed.
    spendTxid = self.nodes[2].sendrawtransaction (rawtx)
    self.advance (0, 1)
    txdata = self.nodes[1].gettransaction (spendTxid)
    assert_equal (1, txdata['confirmations'])

  def extractBounty (self, node):
    """
    Extract the bounty transaction from the current best block on the
    node with the given index.  Checks some stuff and returns information.
    """

    blkhash = self.nodes[node].getbestblockhash ()
    blkdata = self.nodes[node].getblock (blkhash)
    gametxIds = blkdata['gametx']
    assert_equal (1, len (gametxIds))
    txid = gametxIds[0]

    tx = self.nodes[node].getrawtransaction (txid, 1)
    assert_equal (len (tx['vin']), 1)
    assert_equal (len (tx['vout']), 1)
    gametxIn = tx['vin'][0]['gametx']
    assert_equal (gametxIn['player'], 'me')
    assert_equal (gametxIn['op'], 'banking')
    gametxOut = tx['vout'][0]
    value = gametxOut['value']
    assert_equal (len (gametxOut['scriptPubKey']['addresses']), 1)
    addr = gametxOut['scriptPubKey']['addresses'][0]
    print ("Bounty: %.8f to %s" % (value, addr))
    print ("  txid: %s" % txid)

    return blkhash, txid, addr, value

  def verifyTx (self, node, txid, blkhash, value, conf):
    """
    Verify the bounty transaction in the wallet and its UTXO entry.
    """

    txdata = self.nodes[node].gettransaction (txid)
    assert txdata['bounty']
    if blkhash is None:
      assert 'blockhash' not in txdata
    else:
      assert_equal (blkhash, txdata['blockhash'])
    assert_equal (txid, txdata['txid'])
    assert_equal (conf, txdata['confirmations'])

    if conf == 0:
      cat = 'orphan_bounty'
      amount = Decimal ('0')
    elif conf > 100:
      cat = 'bounty'
      amount = value
    else:
      cat = 'immature_bounty'
      amount = Decimal ('0')

    assert_equal (1, len (txdata['details']))
    assert_equal (cat, txdata['details'][0]['category'])
    assert_equal (amount, txdata['amount'])

    if conf > 0:
      utxo = self.nodes[node].gettxout (txid, 0)
      assert_equal (value, utxo['value'])
      assert_equal (conf, utxo['confirmations'])
      assert not utxo['coinbase']
      assert utxo['bounty']

if __name__ == '__main__':
  GameBountiesTest ().main ()
