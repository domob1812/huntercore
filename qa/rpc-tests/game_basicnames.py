#!/usr/bin/env python
# Copyright (c) 2016 Daniel Kraft
# Distributed under the MIT/X11 software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

# Test basic handling of names (name RPC commands) in the context of gaming.

from test_framework.names import NameTestFramework
from test_framework.util import *

class GameBasicNamesTest (NameTestFramework):

  def run_test (self):
    NameTestFramework.run_test (self)

    # Perform some invalid name_new's and check for the corresponding
    # error messages.
    invalids = ["x" * 11, "", " abc", "abc ", "abc  abc", "a+b"]
    for nm in invalids:
      try:
        self.nodes[0].name_new (nm)
        raise AssertionError ("invalid name not recognised by name_new")
      except JSONRPCException as exc:
        assert_equal (exc.error['code'], -8)

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
    try:
      self.nodes[0].name_show (testname)
      raise AssertionError ("name_show succeeded for non-existing name")
    except JSONRPCException as exc:
      assert_equal (exc.error['code'], -4)
    try:
      self.nodes[0].game_getplayerstate (testname)
      raise AssertionError ("getplayerstate succeeded for non-existing name")
    except JSONRPCException as exc:
      assert_equal (exc.error['code'], -5)
    state = self.nodes[0].game_getstate ()
    assert_equal (state['players'], {})

    # Register the player and verify that it appears on the map.
    self.firstupdateName (0, testname, new, '{"color":0}')
    self.sync_all ()
    self.generate (1, 1)
    self.checkName (2, testname, '{"color":0}', False)
    dat = self.nodes[2].game_getplayerstate (testname)
    state = self.nodes[2].game_getstate ()
    assert_equal (state['players'], {testname: dat})

    # Issue a move command.  Wait long enough for the other hunters
    # to be killed in spawn.
    self.nodes[0].name_update (testname, '{"0":{"wp":[2,2]}}')
    self.sync_all ()
    self.generate (1, 35)

    # Verify that the player is at the position we expect it to be.
    dat = self.nodes[2].game_getplayerstate (testname)
    assert_equal (dat['value'], Decimal('1'))
    assert_equal (dat['0']['x'], 2)
    assert_equal (dat['0']['y'], 2)

    # Check that registering another player of this name is not possible.
    new = self.nodes[1].name_new (testname)
    self.generate (1, 2)
    try:
      self.firstupdateName (1, testname, new, '{"color":0}')
      raise AssertionError ("reregistered existing name")
    except JSONRPCException as exc:
      assert_equal (exc.error['code'], -25)

    # Kill the player on the map.
    self.nodes[0].name_update (testname, '{"0":{"destruct":true}}')
    self.generate (0, 1)
    self.checkName (2, testname, None, True)
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

    # Also perform a new-style name_register registration.
    self.nodes[1].name_register ("newstyle", '{"color":1}')
    self.generate (0, 1)
    self.checkName (2, "newstyle", '{"color":1}', False)
    dat = self.nodes[2].game_getplayerstate ("newstyle")
    state = self.nodes[2].game_getstate ()
    assert_equal (len (state['players']), 2)
    assert_equal (state['players']['newstyle'], dat)

if __name__ == '__main__':
  GameBasicNamesTest ().main ()
