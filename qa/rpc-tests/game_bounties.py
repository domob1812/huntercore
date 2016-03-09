#!/usr/bin/env python
# Copyright (c) 2016 Daniel Kraft
# Distributed under the MIT/X11 software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

# Test collecting bounties in the game world and how
# the wallet handles it.

from test_framework.game import GameTestFramework
from test_framework.util import *

class GameBountiesTest (GameTestFramework):

  def run_test (self):
    GameTestFramework.run_test (self)

    # Register a player and perform basic move command.
    self.register (0, "me", 0)
    self.advance (0, 1)
    me = self.get (0, "me", 0)
    me.move ([25, 25])
    me = self.finishMove (0, "me", 0)
    assert_equal (me.pos, [25, 25])

    # Collect nearest loot.
    pos = self.nearestLoot (0, me.pos)
    print "Nearest loot on (%d, %d), collecting..." % (pos[0], pos[1])
    me.move (pos)
    me = self.finishMove (0, "me", 0)
    print "Collected %.8f HUC." % me.loot
    assert me.loot > 0

    # Bank the bounty.
    print "Banking the coins..."
    me.move ([0, 0])
    me = self.finishMove (0, "me", 0)

    # There should be a game bounty tx in the last block.
    blkhash = self.nodes[0].getbestblockhash ()
    blkdata = self.nodes[0].getblock (blkhash)
    gametx = blkdata['gametx']
    assert_equal (len (gametx), 1)
    txid = gametx[0]
    # TODO: Finish the test once the main code handles it.
    #print self.nodes[0].listtransactions ()
    #txdata = self.nodes[0].getrawtransaction (txid, 1)
    #print txdata

if __name__ == '__main__':
  GameBountiesTest ().main ()
