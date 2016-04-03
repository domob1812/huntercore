#!/usr/bin/env python
# Copyright (c) 2016 Daniel Kraft
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

# Test the "getstatsforheight" RPC command.

from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import *

class GetStatsForHeightTest (BitcoinTestFramework):

  def run_test (self):
    BitcoinTestFramework.run_test (self)

    # Generate a block so that we are not "downloading blocks".
    self.nodes[0].generate (1)

    # Register a player.
    self.nodes[0].name_register ("foo", '{"color": 0}')
    self.nodes[0].generate (1)
    self.sync_all ('blocks')

    # Perform some transactions.
    addr = self.nodes[3].getnewaddress ()
    self.nodes[2].sendtoaddress (addr, Decimal ('1'))
    self.nodes[0].name_update ("foo", '{"0": {"destruct": true}}')
    self.nodes[1].name_register ("bar", '{"color": 1}')

    # Mine a block and get its stats.
    self.sync_all ('mempool')
    blkhash = self.nodes[3].generate (1)
    self.sync_all ('blocks')
    assert_equal (1, len (blkhash))
    blkhash = blkhash[0]
    cnt = self.nodes[0].getblockcount ()
    stats = self.nodes[0].getstatsforheight (cnt)

    # Verify stats against expectations.
    assert_equal (blkhash, stats['blockhash'])
    assert_equal (cnt, stats['height'])
    assert 'time' in stats
    assert 'size' in stats
    assert_equal (2, stats['transactions']['currency'])
    assert_equal (2, stats['transactions']['name'])
    assert_equal (1, stats['transactions']['game'])
    assert_equal (1, stats['game']['players'])
    assert_equal (3, stats['game']['hunters'])

if __name__ == '__main__':
  GetStatsForHeightTest ().main ()
