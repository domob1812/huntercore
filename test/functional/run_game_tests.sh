#!/bin/sh

echo "\nAuxpow..."
./auxpow_mining.py

echo "\nDual-algo..."
./dualalgo.py

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

echo "\ngetstatsforheight..."
./getstatsforheight.py
