#!/usr/bin/env python3
# Copyright (c) 2015-2018 Daniel Kraft
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

# Test mining (generate and aux mining) with dual-algo.

from test_framework import auxpow
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import *

class DualAlgoTest (BitcoinTestFramework):

  def set_test_params (self):
    self.num_nodes = 1

  def run_test (self):
    # Check for difficulty reports in various RPC calls.  Where the "current"
    # state is involved, we get two (for each algo).  Where a particular block
    # is involved, we just get that block's difficulty (whatever the algo).
    dual = []
    dual.append (self.nodes[0].getblockchaininfo ())
    dual.append (self.nodes[0].getmininginfo ())
    for data in dual:
      assert 'difficulty_sha256d' in data
      assert 'difficulty_scrypt' in data
      assert 'difficulty' not in data
    bestHash = self.nodes[0].getbestblockhash ()
    data = self.nodes[0].getblock (bestHash)
    assert 'difficulty' in data
    assert 'difficulty_sha256d' not in data
    assert 'difficulty_scrypt' not in data

    # Check getdifficulty RPC call.
    diffSHA = self.nodes[0].getdifficulty (0)
    assert_equal (diffSHA, dual[0]['difficulty_sha256d'])
    diffScrypt = self.nodes[0].getdifficulty (1)
    assert_equal (diffScrypt, dual[0]['difficulty_scrypt'])
    assert_raises_rpc_error (-1, None, self.nodes[0].getdifficulty)

    # Generate a few blocks with SHA256D.  Ensure that they are, indeed,
    # with the correct algo.
    arr1 = self.nodes[0].generate (5)
    arr2 = self.nodes[0].generate (5, 0)
    for blk in arr1 + arr2:
      data = self.nodes[0].getblock (blk)
      assert_equal (data['algo'], 0)

    # Generate a few blocks with scrypt.  Ensure the algo parameter.
    # Furthermore, at least one of them "should" have an obviously high
    # hash value.  It may, of course, happen that this is not the case,
    # but the probability for that is negligible (about 2^(-20)).
    arr = self.nodes[0].generate (20, 1)
    foundHigh = False
    for blk in arr:
      data = self.nodes[0].getblock (blk)
      assert_equal (data['algo'], 1)
      if blk[0] in '89abcdef':
        foundHigh = True
    assert foundHigh

    # Verify check for algo parameter.
    for p in [-1, 2]:
      assert_raises_rpc_error (-8, 'invalid algo', self.nodes[0].generate, 1, p)

    # Briefly test generatetoaddress as well.
    for algo in [0, 1]:
      addr = self.nodes[0].getnewaddress ()
      blkhash = self.nodes[0].generatetoaddress (1, addr, algo)
      assert_equal (1, len (blkhash))
      data = self.nodes[0].getblock(blkhash[0])
      assert_equal (algo, data['algo'])
      coinbaseTx = data['tx'][0]
      utxo = self.nodes[0].gettxout (coinbaseTx, 0)
      assert_equal ([addr], utxo['scriptPubKey']['addresses'])

    # Check updates of the returned block (or not) with aux mining.
    # Note that this behaviour needs not necessarily be exactly as tested,
    # but the test ensures that no change is introduced accidentally.
    coinbaseAddr = self.nodes[0].getnewaddress ()
    auxblock1 = self.nodes[0].createauxblock (coinbaseAddr)
    auxblock2 = self.nodes[0].createauxblock (coinbaseAddr, 0)
    assert_equal (auxblock1['hash'], auxblock2['hash'])
    auxblock2 = self.nodes[0].createauxblock (coinbaseAddr, 1)
    auxblock3 = self.nodes[0].createauxblock (coinbaseAddr, 1)
    assert_equal (auxblock2['hash'], auxblock3['hash'])
    assert auxblock2['hash'] != auxblock1['hash']
    auxblock3 = self.nodes[0].createauxblock (coinbaseAddr)
    assert auxblock1['hash'] != auxblock3['hash']

    # Use createauxblock with explicit algo to mine a SHA256D block.
    # Assert that this works (the block is saved) even if we request
    # another scrypt block before.
    auxblock = self.nodes[0].createauxblock (coinbaseAddr)
    assert_equal (auxblock['chainid'], 6)
    assert_equal (auxblock['algo'], 0)
    dummy = self.nodes[0].createauxblock (coinbaseAddr, 1)
    assert_equal (dummy['chainid'], 2)
    assert_equal (dummy['algo'], 1)

    # Solve the auxpow requested before.
    curcnt = self.nodes[0].getblockcount ()
    target = auxpow.reverseHex (auxblock['_target'])
    apow = auxpow.computeAuxpow (auxblock['hash'], target, True)
    res = self.nodes[0].submitauxblock (auxblock['hash'], apow)
    assert res

    # Check submitted data.
    assert_equal (self.nodes[0].getblockcount (), curcnt + 1)
    data = self.nodes[0].getblock (auxblock['hash'])
    assert_equal (data['algo'], 0)

    # Mine an scrypt auxpow.  Since there is no built-in Python
    # function to scrypt, we do it differently:  Simply try submitting
    # until the block is accepted.  Since we need on average 2 trials,
    # this is no big hit.
    trials = 0
    curcnt = self.nodes[0].getblockcount ()
    ok = False
    while not ok:
      if trials > 100:
        raise AssertionError ("failed to merge-mine scrypt auxpow")
      trials += 1

      # Force an update of the block so we get a new trial.
      self.nodes[0].createauxblock (coinbaseAddr, 0)
      auxblock = self.nodes[0].createauxblock (coinbaseAddr, 1)

      target = auxpow.reverseHex (auxblock['_target'])
      apow = auxpow.computeAuxpow (auxblock['hash'], target, False)
      ok = self.nodes[0].submitauxblock (auxblock['hash'], apow)
    print ("Found scrypt block after %d trials." % trials)

    # Check submitted auxblock.
    assert_equal (self.nodes[0].getblockcount (), curcnt + 1)
    data = self.nodes[0].getblock (auxblock['hash'])
    assert_equal (data['algo'], 1)

if __name__ == '__main__':
  DualAlgoTest ().main ()
