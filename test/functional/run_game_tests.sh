#!/bin/sh

./create_cache.py || exit 1

echo "\nAuxpow..."
./auxpow_mining.py
./auxpow_mining.py --segwit

echo "\nBasic name operations..."
./game_basicnames.py

echo "\nFee policy..."
./game_feepolicy.py

echo "\nGame transaction indexing..."
./game_txindex.py

echo "\nGame bounties..."
./game_bounties.py

echo "\nGame kills..."
./game_kills.py

echo "\nGame mempool cleanup..."
./game_mempool.py

echo "\nGame miner taxes..."
./game_minertaxes.py

echo "\nDual-algo..."
./mining_dualalgo.py

echo "\ngetstatsforheight..."
./rpc_getstatsforheight.py
