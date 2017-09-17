#!/usr/bin/env python3
# Copyright (c) 2016-2017 Daniel Kraft
# Distributed under the MIT/X11 software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

# Check tax handling of miners (in particular, that they credit taxes
# for themselves at all).

from test_framework.game import GameTestFramework
from test_framework.util import *

from decimal import Decimal

class GameMinerTaxesTest (GameTestFramework):

  def set_test_params (self):
    self.setup_name_test ([[]] * 2)

  def run_test (self):
    # Register a player.  We use node 1 here since it has -txindex and
    # we later need that.
    self.register (1, "me", 0)
    self.advance (1, 1)
    me = self.get (1, "me", 0)

    # Collect nearest loot and bank.
    pos = self.nearestLoot (1, me.pos)
    print ("Nearest loot on (%d, %d), collecting..." % (pos[0], pos[1]))
    me.move (pos)
    me = self.finishMove (1, "me", 0)
    bounty = me.loot
    print ("Collected %.8f HUC." % bounty)
    assert bounty > 0
    print ("Banking the coins...")
    me.move ([0, 0])
    self.finishMove (1, "me", 0)

    # Find bounty tx and extract its value.
    blkhash = self.nodes[1].getbestblockhash ()
    blkdata = self.nodes[1].getblock (blkhash)
    gametxIds = blkdata['gametx']
    assert_equal (len (gametxIds), 1)
    txid = gametxIds[0]
    tx = self.nodes[1].getrawtransaction (txid, 1)
    assert_equal (len (tx['vin']), 1)
    assert_equal (len (tx['vout']), 1)
    txOut = tx['vout'][0]
    value = txOut['value']
    assert_greater_than (bounty, value)
    print ("Credited bounty: %.8f HUC." % value)

    # Extract coinbase value and verify taxes.
    taxes = self.getTaxes (1)
    assert_equal (taxes + value, bounty)

    # Mine an ordinary block and verify that it has zero taxes.
    print ("Verifying zero taxes for empty block...")
    self.advance (1, 1)
    assert_equal (self.getTaxes (1), 0)

    # Move player out of spawn so that death taxes apply.  Then kill the player.
    print ("Killing player for death taxes...")
    pos = [2, 2]
    assert_equal (self.lootOnTile (1, pos), Decimal ('0'))
    self.get (1, "me", 0).move (pos)
    self.finishMove (1, "me", 0)
    me = self.get (1, "me", 0)
    value = me.value
    print ("Original hunter value: %.8f HUC" % value)
    me.destruct ()
    self.advance (1, 1)
    loot = self.lootOnTile (1, pos)
    print ("Dropped loot: %.8f HUC" % loot)
    taxes = self.getTaxes (1)
    assert_equal (taxes + loot, value)

  def getTaxes (self, node):
    """
    Extract the coinbase and compute the amount of game taxes included in it.
    For this, we also look through all transactions in the block and add their
    fees, so we can subtract them from the coinbase.  This requires that all
    transactions are within the wallet of node.
    """

    blkhash = self.nodes[node].getbestblockhash ()
    blkdata = self.nodes[1].getblock (blkhash)
    txs = blkdata['tx']

    coinbase = self.nodes[node].getrawtransaction (txs[0], 1)
    assert len (coinbase['vout']) >= 1
    coinbaseValue = coinbase['vout'][0]['value']
    print ("Coinbase value: %.8f HUC" % coinbaseValue)

    txs = txs[1:]
    fees = Decimal ('0')
    for txid in txs:
      tx = self.nodes[node].gettransaction (txid)
      fees += tx['fee']
    fees = -fees

    subsidy = self.getSubsidy (node)
    taxes = coinbaseValue - subsidy - fees
    assert (taxes >= 0)

    print ("  subsidy %.8f HUC, fees %.8f HUC" % (subsidy, fees))
    print ("  => taxes: %.8f HUC" % taxes)

    return taxes

  def getSubsidy (self, node):
    """
    Compute the current block subsidy at the best block height.
    """

    interval = 150

    height = self.nodes[node].getblockcount ()
    subsidy = Decimal ('1')

    while height >= interval:
      height -= interval
      subsidy /= 2

    return subsidy

if __name__ == '__main__':
  GameMinerTaxesTest ().main ()
