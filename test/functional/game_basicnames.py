#!/usr/bin/env python3
# Copyright (c) 2016-2017 Daniel Kraft
# Distributed under the MIT/X11 software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

# Test basic handling of names (name RPC commands) in the context of gaming.

from test_framework.names import NameTestFramework
from test_framework.util import *

class GameBasicNamesTest (NameTestFramework):

  def run_test (self):
    # Perform some invalid name_new's and check for the corresponding
    # error messages.
    invalids = ["x" * 11, "", " abc", "abc ", "abc  abc", "a+b"]
    for nm in invalids:
      assert_raises_jsonrpc (-8, None, self.nodes[0].name_new, nm)

    # On the other hand, these names should all be allowed.  We do not
    # finalise the registration, just make sure that the same test that
    # failed before now succeeds.
    valids = ["x" * 10, "x", "abc def_01", "AB CD-EF"]
    for nm in valids:
      self.nodes[0].name_new (nm)

    # Verify that our name is not yet there.
    testname = "foobar"
    new = self.nodes[0].name_new (testname)
    self.generate (0, 2)
    assert_raises_jsonrpc (-4, 'name not found',
                           self.nodes[0].name_show, testname)
    assert_raises_jsonrpc (-5, 'No such player',
                           self.nodes[0].game_getplayerstate, testname)
    state = self.nodes[0].game_getstate ()
    assert_equal (state['players'], {})
    assert_equal ([], self.nodes[0].name_list ())

    # Register the player and verify that it appears on the map.
    self.firstupdateName (0, testname, new, '{"color":0}')
    self.sync_with_mode ('both')
    assert_equal ([], self.nodes[0].name_list ())
    self.generate (1, 1)
    self.checkName (2, testname, '{"color":0}', False)
    arr = self.nodes[0].name_list ()
    assert_equal (1, len (arr))
    self.checkNameData (arr[0], testname, '{"color":0}', False)
    assert_equal ([], self.nodes[1].name_list ())
    dat = self.nodes[2].game_getplayerstate (testname)
    state = self.nodes[2].game_getstate ()
    assert_equal (state['players'], {testname: dat})

    # Issue a move command.  Wait long enough for the other hunters
    # to be killed in spawn.
    self.nodes[0].name_update (testname, '{"0":{"wp":[2,2]}}')
    self.sync_with_mode ('both')
    self.generate (1, 35)

    # Verify that the player is at the position we expect it to be.
    dat = self.nodes[2].game_getplayerstate (testname)
    assert_equal (dat['value'], Decimal('1'))
    assert_equal (dat['characters']['0']['x'], 2)
    assert_equal (dat['characters']['0']['y'], 2)

    # Check that registering another player of this name is not possible.
    new = self.nodes[1].name_new (testname)
    self.generate (1, 2)
    assert_raises_jsonrpc (-25, 'this name is already active',
                           self.firstupdateName,
                           1, testname, new, '{"color":0}')

    # Kill the player on the map.
    self.nodes[0].name_update (testname, '{"0":{"destruct":true}}')
    self.generate (0, 1)
    self.checkName (2, testname, None, True)
    arr = self.nodes[0].name_list ()
    assert_equal (1, len (arr))
    self.checkNameData (arr[0], testname, None, True)
    state = self.nodes[2].game_getstate ()
    assert_equal (state['players'], {})
    found = False
    for sq in state['loot']:
      if (sq['x'], sq['y']) == (2, 2):
        found = True
        assert_equal (sq['amount'], Decimal('0.96'))
    assert found

    # Now we can register another one with the same name.
    self.firstupdateName (1, testname, new, '{"color":0}')
    self.generate (1, 1)
    self.checkName (2, testname, '{"color":0}', False)
    dat = self.nodes[2].game_getplayerstate (testname)
    state = self.nodes[2].game_getstate ()
    assert_equal (state['players'], {testname: dat})

    # The old wallet should still list the dead entry.
    arr = self.nodes[0].name_list ()
    assert_equal (1, len (arr))
    self.checkNameData (arr[0], testname, None, True)

    # Also perform a new-style name_register registration.
    self.nodes[2].name_register ("newstyle", '{"color":1}')
    assert_equal ([], self.nodes[2].name_list ())
    self.generate (0, 1)
    arr = self.nodes[2].name_list ()
    assert_equal (1, len (arr))
    self.checkNameData (arr[0], "newstyle", '{"color":1}', False)
    self.checkName (3, "newstyle", '{"color":1}', False)
    dat = self.nodes[3].game_getplayerstate ("newstyle")
    state = self.nodes[3].game_getstate ()
    assert_equal (len (state['players']), 2)
    assert_equal (state['players']['newstyle'], dat)

    # Transfer the name and check name_list afterwards.
    addr = self.nodes[1].getnewaddress ()
    self.nodes[2].name_update ("newstyle", '{}', addr)
    self.generate (0, 1)
    self.checkName (3, "newstyle", '{}', False)
    arr = self.nodes[1].name_list ()
    assert_equal (2, len (arr))
    self.checkNameData (arr[0], testname, '{"color":0}', False)
    self.checkNameData (arr[1], "newstyle", '{}', False)
    assert not arr[1]['transferred']
    arr = self.nodes[2].name_list ()
    assert_equal (1, len (arr))
    self.checkNameData (arr[0], "newstyle", '{}', False)
    assert arr[0]['transferred']

    # Check listtransactions for the transferred name.
    tx = self.nodes[1].listtransactions ("*", 1)
    assert_equal ("receive", tx[0]['category'])
    assert_equal ("update: newstyle", tx[0]['name'])
    tx = self.nodes[2].listtransactions ("*", 1)
    assert_equal ("send", tx[0]['category'])
    assert_equal ("update: newstyle", tx[0]['name'])

    # Kill both names and verify that name_list handles that.
    self.nodes[1].name_update (testname, '{"0":{"destruct":true}}')
    self.nodes[1].name_update ("newstyle", '{"0":{"destruct":true}}')
    self.generate (0, 1)
    self.checkName (3, testname, None, True)
    self.checkName (3, "newstyle", None, True)
    arr = self.nodes[1].name_list ()
    assert_equal (2, len (arr))
    self.checkNameData (arr[0], testname, None, True)
    self.checkNameData (arr[1], "newstyle", None, True)

if __name__ == '__main__':
  GameBasicNamesTest ().main ()
