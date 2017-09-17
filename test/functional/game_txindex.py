#!/usr/bin/env python3
# Copyright (c) 2016-2017 Daniel Kraft
# Distributed under the MIT/X11 software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

# Verify -txindex handling of game and non-game tx.

from test_framework.game import GameTestFramework
from test_framework.util import *

class GameTxIndexTest (GameTestFramework):

  def set_test_params (self):
    self.setup_name_test ([[]] * 3)

  def run_test (self):
    # Register a player and perform basic move command.
    self.register (0, "me", 0)
    self.advance (0, 1)
    me = self.get (0, "me", 0)
    me.move ([25, 25])
    me = self.finishMove (0, "me", 0)
    assert_equal ([25, 25], me.pos)

    # Collect nearest loot.
    pos = self.nearestLoot (0, me.pos)
    print ("Nearest loot on (%d, %d), collecting..." % (pos[0], pos[1]))
    me.move (pos)
    me = self.finishMove (0, "me", 0)
    print ("Collected %.8f HUC." % me.loot)
    assert me.loot > 0

    # Create another player that can be used for a death game tx.
    self.register (0, "red-shirt", 0)
    self.advance (0, 1)
    redshirt = self.get (0, "red-shirt", 0)
    redshirt.move ([1, 1])

    # Bank the bounty (almost).
    print ("Banking the coins...")
    me = self.get (0, "me", 0)
    me.move ([1, 1])
    me = self.finishMove (0, "me", 0)
    assert_equal ([1, 1], me.pos)

    # Finish banking and kill the redshirt at the same time,
    # so that we get a block with two game tx.
    # Also send an ordinary currency tx in the same block.
    currencyAmount = Decimal ('1')
    currencyAddr = self.nodes[2].getnewaddress ()
    currencyTxid = self.nodes[0].sendtoaddress (currencyAddr, currencyAmount)
    me.move ([0, 0])
    redshirt = self.get (0, "red-shirt", 0)
    redshirt.destruct ()
    self.advance (0, 1)

    # There should be two game tx in the best block.
    print ("Verifying transactions...")
    blkhash = self.nodes[0].getbestblockhash ()
    blkdata = self.nodes[0].getblock (blkhash)
    gametxIds = blkdata['gametx']
    assert_equal (len (gametxIds), 2)
    gameTx = []
    for txid in gametxIds:
      # Node 1 has -txindex set.
      txdata = self.nodes[1].getrawtransaction (txid, 1)
      assert_equal (txdata['blockhash'], blkhash)
      gameTx.append (txdata)

    # The first game tx should be killing of redshirt.
    assert_equal (len (gameTx[0]['vin']), 1)
    assert_equal (len (gameTx[0]['vout']), 0)
    gameIn = gameTx[0]['vin'][0]
    assert 'gametx' in gameIn
    assert_equal (gameIn['gametx']['op'], 'killed_by')
    assert_equal (gameIn['gametx']['player'], 'red-shirt')

    # The second game tx should be our bounty for me.
    assert_equal (len (gameTx[1]['vin']), 1)
    assert_equal (len (gameTx[1]['vout']), 1)
    gameIn = gameTx[1]['vin'][0]
    assert 'gametx' in gameIn
    assert_equal (gameIn['gametx']['op'], 'banking')
    assert_equal (gameIn['gametx']['player'], 'me')

    # It should be possible to look up the currency tx as well.
    txdata = self.nodes[1].getrawtransaction (currencyTxid, 1)
    assert_equal (txdata['blockhash'], blkhash)
    found = False
    for out in txdata['vout']:
      addr = None
      if len (out['scriptPubKey']['addresses']) == 1:
        curAddr = out['scriptPubKey']['addresses'][0]
      if out['value'] == currencyAmount and curAddr == currencyAddr:
        found = True
        break
    assert found

if __name__ == '__main__':
  GameTxIndexTest ().main ()
