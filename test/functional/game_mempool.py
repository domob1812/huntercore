#!/usr/bin/env python3
# Copyright (c) 2016-2017 Daniel Kraft
# Distributed under the MIT/X11 software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

# Check that the mempool is properly cleaned up after game changes
# invalidate pending moves.

from test_framework.game import GameTestFramework
from test_framework.util import *

class GameMempoolTest (GameTestFramework):

  def set_test_params (self):
    self.setup_name_test ([[]] * 4)

  def run_test (self):
    # Split the network and create two conflicting name_register transactions.
    print ("Creating conflicting name registrations...")
    self.split_network ()
    txidA = self.nodes[0].name_register ("foobar", '{"color":0}')
    txidB = self.nodes[2].name_register ("foobar", '{"color":1}')
    self.join_network ()
    assert_equal ([txidA], self.nodes[0].getrawmempool ())
    assert_equal ([txidB], self.nodes[2].getrawmempool ())

    # Mine one of them and verify that this works and clears the other.
    self.generate (0, 1, False)
    for i in range(4):
      assert_equal ([], self.nodes[i].getrawmempool ())
    self.checkName (1, "foobar", '{"color":0}', False)
    data = self.nodes[1].name_show ("foobar")
    addr = data['address']
    valid = self.nodes[0].validateaddress (addr)
    assert valid['ismine']
    data = self.nodes[2].gettransaction (txidB)
    assert_equal (data['confirmations'], -1)
    assert_equal (data['walletconflicts'], [])

    # Create an enemy hunter to use for killing foobar.
    self.nodes[0].name_register ("killer", '{"color":1}')
    self.advance (0, 1)

    # Move foobar out of spawn and killer close to it.
    print ("Moving hunters...")
    self.get (0, "foobar", 0).move ([1, 4])
    self.get (0, "killer", 0).move ([2, 4])
    self.finishMove (0, "killer", 0)

    # Create a tx to update foobar and kill it at the same time using killer.
    print ("Killing hunter with pending move...")
    self.get (0, "killer", 0).destruct ()
    self.get (0, "foobar", 1).move ([0, 0])
    self.issueMoves ()
    txidMove = self.pendingTxid (0, "foobar")
    txidKill = self.pendingTxid (0, "killer")
    assert txidMove is not None
    assert txidKill is not None
    self.advance (0, 1, ["foobar"])
    assert_equal (self.players (0), [])
    assert_equal (self.nodes[0].name_pending (), [])
    assert_equal (self.nodes[0].getrawmempool (), [])
    data = self.nodes[0].gettransaction (txidMove)
    assert_equal (data['confirmations'], -1)
    assert_equal (len (data['walletconflicts']), 1)

    # Add pending re-registration and roll back the chain.
    print ("Rollback pending re-registration...")
    txidReg = self.nodes[0].name_register ("foobar", '{"color":1}')
    assert_equal (self.nodes[0].getrawmempool (), [txidReg])
    assert_equal (self.players (0), [])
    blkhash = self.nodes[0].getbestblockhash ()
    for i in range(3):
      self.nodes[i].invalidateblock (blkhash)
    assert_equal (self.players (0), ["foobar", "killer"])
    assert_equal (self.nodes[0].getrawmempool (), [txidKill])
    data = self.nodes[0].gettransaction (txidReg)
    assert_equal (data['confirmations'], -1)
    assert_equal (data['walletconflicts'], [])

if __name__ == '__main__':
  GameMempoolTest ().main ()
