#!/usr/bin/env python3

# Copyright (c) 2016-2017 Crypto Realities Ltd
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.

# Basic botting framework for testing game elements.

from .names import NameTestFramework

from decimal import Decimal
import json

class GameTestFramework (NameTestFramework):

  def __init__ (self):
    NameTestFramework.__init__ (self)
    self.batched = {}

  def register (self, node, name, col):
    """
    Register a new hunter on the given node with the given
    name and colour.  Returns the registration txid.
    """
    data = {'color': col}
    return self.nodes[node].name_register (name, json.dumps (data))

  def get (self, node, name, ind):
    """
    Constructs an object corresponding to the given hunter
    on the map, assumed to be owned by the given node.
    Returns None if the hunter does not exist.
    """

    state = self.nodes[node].game_getstate ()
    if not name in state['players']:
      return None
    data = state['players'][name]
    if not str (ind) in data['characters']:
      return None

    return Hunter (self, self.nodes[node], node, name, ind, data)

  def issueMoves (self):
    """
    Issue all batched moves as pending transactions onto the network.
    """

    for nind, ops in self.batched.items ():
      for name, op in ops.items ():
        self.nodes[nind].name_update (name, json.dumps (op))
    self.batched = {}

    self.sync_with_mode ('mempool')

  def advance (self, node, numBlocks, doNotMine = []):
    """
    Mine the given number of blocks, advancing the game state.
    This should be used instead of generate, since it takes care
    of issuing the batched moves before that.

    doNotMine may contain a set of names for which we should not mine
    their pending updates.  We de-prioritise those before advancing, and reset
    the priority afterwards.

    If the network is split, moves should only be batched on nodes
    connected to node!
    """

    self.issueMoves ()

    txids = []
    for name in doNotMine:
      txid = self.pendingTxid (node, name)
      assert txid is not None
      txids.append (txid)
      self.nodes[node].prioritisetransaction (txid=txid, fee_delta=-100000000)

    self.generate (node, numBlocks)

    for txid in txids:
      self.nodes[node].prioritisetransaction (txid=txid, fee_delta=100000000)

  def finishMove (self, node, name, ind, advanceFirst = True):
    """
    Advance blocks until the given hunter no longer moves.
    If advanceFirst is true, issue an advance command first
    so that scheduled moves are sent.
    """

    if advanceFirst:
      self.advance (node, 1)

    me = self.get (node, name, ind)
    if me.moving:
      self.advance (node, me.eta)

    me = self.get (node, name, ind)
    assert not me.moving
    return me

  def nearestLoot (self, node, pos):
    """
    Return the position of the loot closest to pos.
    """

    bestPos = None
    bestDist = 1000

    state = self.nodes[node].game_getstate ()
    for l in state['loot']:
      curPos = [int (l['x']), int (l['y'])]
      curDist = distLInf (curPos, pos)
      if bestPos is None or curDist < bestDist:
        bestPos = curPos
        bestDist = curDist
      
    return bestPos

  def lootOnTile (self, node, pos):
    """
    Return the total loot on the given position (on the ground).
    """

    state = self.nodes[node].game_getstate ()
    for l in state['loot']:
      if l['x'] == pos[0] and l['y'] == pos[1]:
        return l['amount']

    return Decimal('0')

  def players (self, node):
    """
    Return list of living player names on the map.
    """

    state = self.nodes[node].game_getstate ()
    res = list (state['players'].keys ())
    res.sort ()

    return res

class Hunter:
  """
  Represents a single hunter on the map.  This class allows abstracted
  access to sending commands and retrieving information from it.
  """

  def __init__ (self, tester, rpcNode, node, name, ind, data):
    self.tester = tester
    self.rpcNode = rpcNode
    self.node = node
    self.name = name
    self.ind = ind

    self.colour = data['color']
    hunterData = data['characters'][str (ind)]
    self.pos = [int (hunterData['x']), int (hunterData['y'])]
    self.loot = hunterData['loot']
    self.value = data['value']
    self.moving = 'wp' in hunterData
    if self.moving:
      self.eta = pathlen (self.pos, hunterData['wp'])

  def _setInBatch (self, key, val, onTeam = False):
    if not self.node in self.tester.batched:
      self.tester.batched[self.node] = {}
    if not self.name in self.tester.batched[self.node]:
      self.tester.batched[self.node][self.name] = {}

    if onTeam:
      self.tester.batched[self.node][self.name][key] = val
    else:
      if not str (self.ind) in self.tester.batched[self.node][self.name]:
        self.tester.batched[self.node][self.name][str (self.ind)] = {}
      self.tester.batched[self.node][self.name][str (self.ind)][key] = val

  def move (self, target):
    path = self.rpcNode.game_getpath (self.pos, target)
    self._setInBatch ('wp', path)

  def destruct (self):
    self._setInBatch ('destruct', True)

  def setAddress (self, addr):
    self._setInBatch ('address', addr, True)

def distLInf (a, b):
  """
  Utility method to compute the L-infinity distance between
  the given two coordinates.
  """
  return max (abs (a[0] - b[0]), abs (a[1] - b[1]))

def pathlen (a, path):
  """
  Compute the L-infinity path length from a via the given
  list of way points.
  """
  res = distLInf (a, [path[0], path[1]])
  for i in range (2, len (path), 2):
    res += distLInf ([path[i - 2], path[i - 1]], [path[i], path[i + 1]])

  return res
