#!/usr/bin/python
# Copyright (C) 2016 by Daniel Kraft <d@domob.eu>

# Find information about nearby loot and banks for a player.

import jsonrpc
import sys

name = "domob"
rpc = jsonrpc.ServiceProxy ("http://huntercoind:password@localhost:8399/")

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

  if len (path) == 0:
    return 0
  res = distLInf (a, [path[0], path[1]])
  for i in range (2, len (path), 2):
    res += distLInf ([path[i - 2], path[i - 1]], [path[i], path[i + 1]])

  return res

def distTo (rpc, a, b):
  """
  Compute the travel distance from a to b.
  """
  path = rpc.game_getpath (a, b)
  return pathlen (a, path)

def findNearby (rpc, pos, arr):
  """
  Find the nearest 'object' from arr, which can be loot or banks.
  """

  bestObj = None
  for obj in arr:
    curPos = [obj['x'], obj['y']]
    curDist = distLInf (pos, curPos)
    if bestObj is None or curDist < bestDist:
      curDist = distTo (rpc, pos, curPos)
    if bestObj is None or curDist < bestDist:
      bestObj = obj
      bestPos = curPos
      bestDist = curDist

  return bestObj, bestPos, bestDist

state = rpc.game_getstate ()
if not name in state['players']:
  print "Player not found!"
  sys.exit (-1)

player = state['players'][name]["0"]
pos = [player['x'], player['y']]

#loot, pos, dist = findNearby (rpc, pos, state['loot'])
#print "Nearest loot: (%d, %d), distance = %d, %.8f HUC" % (pos[0], pos[1], dist, loot['amount'])

bank, pos, dist = findNearby (rpc, pos, state['banks'])
print "Nearest bank: (%d, %d), distance = %d, life %d" % (pos[0], pos[1], dist, bank['life'])
