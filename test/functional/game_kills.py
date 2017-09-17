#!/usr/bin/env python3
# Copyright (c) 2016-2017 Daniel Kraft
# Distributed under the MIT/X11 software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

# Check wallet handling of kill transactions and their behaviour
# under reorganisations.

from test_framework.game import GameTestFramework
from test_framework.util import *

class GameKillsTest (GameTestFramework):

  def set_test_params (self):
    self.setup_name_test ([[]] * 4)

  def run_test (self):
    # Create two players and kill them.
    print ("Creating and killing hunters...")
    self.nodes[0].name_register ("a", '{"color":1}')
    self.nodes[1].name_register ("b", '{"color":1}')
    self.advance (3, 1)
    self.get (0, "a", 0).destruct ()
    self.get (1, "b", 0).destruct ()
    self.advance (3, 1)

    # Verify that both players have been killed.
    state = self.nodes[3].game_getstate ()
    assert_equal ({}, state['players'])
    self.checkName (3, "a", None, True)
    self.checkName (3, "b", None, True)

    # Get the kill transaction from the block and verify it.
    print ("Verifying kill transaction...")
    blkhash, txid, tx = self.fetchKill (2)
    txIn = tx['vin']
    assert_equal (2, len (txIn))
    txA = txIn[0]['gametx']
    txB = txIn[1]['gametx']
    assert_equal ({"player": 'a', "killers": ['a'], "op": "killed_by"}, txA)
    assert_equal ({"player": 'b', "killers": ['b'], "op": "killed_by"}, txB)

    # Verify transactions in the wallet.
    self.checkKillTx (0, txid, blkhash, 0, "a", 1)
    self.checkKillTx (1, txid, blkhash, 1, "b", 1)

    # Invalidate the block to revive the players.
    print ("Reviving them by invalidating the block...")
    for i in range(4):
      self.nodes[i].invalidateblock (blkhash)
    self.checkName (3, "a", '{"color":1}', False)
    self.checkName (3, "b", '{"color":1}', False)
    state = self.nodes[3].game_getstate ()
    assert_equal (len (state['players']), 2)

    # Remine another block, but this time only with one of the destructs.
    print ("Remining one of the kills...")
    mempool = self.nodes[3].getrawmempool ()
    assert_equal (len (mempool), 2)
    self.advance (3, 1, ["b"])

    # Check the names to make sure killing / not killing has worked as expected.
    state = self.nodes[3].game_getstate ()
    assert_equal (len (state['players']), 1)
    assert 'b' in state['players']
    self.checkName (3, "a", None, True)
    self.checkName (3, "b", '{"color":1}', False)

    # Check wallet handling of the previous kill transaction.
    self.checkKillTx (0, txid, None, 0, "a", 0)
    self.checkKillTx (1, txid, None, 1, "b", 0)

    # Get the new kill transaction from the block.
    print ("Verifying new kill transaction...")
    blkhash, txidNew, tx = self.fetchKill (2)
    txIn = tx['vin']
    assert_equal (len (txIn), 1)
    txA = txIn[0]['gametx']
    assert_equal ({"player": 'a', "killers": ['a'], "op": "killed_by"}, txA)

    # Verify wallet handling for the new tx.
    self.checkKillTx (0, txidNew, blkhash, 0, "a", 1)
    assert_raises_jsonrpc (-5, 'Invalid or non-wallet transaction',
                           self.nodes[1].gettransaction, txidNew)

    # Check wallet conflicts settings.
    txA = self.nodes[0].gettransaction (txid)
    txB = self.nodes[1].gettransaction (txid)
    txNew = self.nodes[0].gettransaction (txidNew)
    assert_equal (txA['walletconflicts'], [txidNew])
    assert_equal (txB['walletconflicts'], [])
    assert_equal (txNew['walletconflicts'], [txid])

  def fetchKill (self, node):
    """
    Fetch the kill transaction from the block and verify it roughly.
    """

    blkhash = self.nodes[node].getbestblockhash ()
    blk = self.nodes[node].getblock (blkhash)
    assert_equal (1, len (blk['gametx']))
    txid = blk['gametx'][0]
    print ("  txid: %s" % txid)
    tx = self.nodes[node].getrawtransaction (txid, 1)
    assert_equal (blkhash, tx['blockhash'])
    assert_equal ([], tx['vout'])

    return blkhash, txid, tx

  def checkKillTx (self, node, txid, blkhash, vout, name, conf):
    """
    Check the listtransactions output for a kill transaction.
    """

    tx = self.nodes[node].gettransaction (txid)
    if blkhash is not None:
      assert_equal (tx['blockhash'], blkhash)
    assert_equal (tx['amount'], Decimal ('0'))
    assert_equal (tx['confirmations'], conf)

    assert_equal (len (tx['details']), 1)
    detail = tx['details'][0]
    assert_equal (detail['name'], "killed: %s" % name)
    assert_equal (detail['vout'], vout)

    if conf == 0:
      assert_equal (detail['category'], 'orphan_killed')
    else:
      assert_equal (detail['category'], 'killed')

if __name__ == '__main__':
  GameKillsTest ().main ()
