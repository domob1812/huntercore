// Copyright (C) 2015 Crypto Realities Ltd

//  This program is free software: you can redistribute it and/or modify
//  it under the terms of the GNU General Public License as published by
//  the Free Software Foundation, either version 3 of the License, or
//  (at your option) any later version.
//
//  This program is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//  GNU General Public License for more details.
//
//  You should have received a copy of the GNU General Public License
//  along with this program.  If not, see <http://www.gnu.org/licenses/>.

#ifndef GAME_MAP_H
#define GAME_MAP_H

static const int MAP_WIDTH = 502;
static const int MAP_HEIGHT = 502;

static const int SPAWN_AREA_LENGTH = 15;
static const int NUM_HARVEST_AREAS = 18;
static const int NUM_CROWN_LOCATIONS = 416;

static const int CROWN_START_X = 250;
static const int CROWN_START_Y = 248;

// for FORK_TIMESAVE
extern unsigned char SpawnMap[MAP_HEIGHT][MAP_WIDTH];
#define SPAWNMAPFLAG_BANK 1
#define SPAWNMAPFLAG_PLAYER 2
#define CHARACTER_MODE_NORMAL 6
// difference of 2 means we can walk over (and along) the player spawn strip without logout
#define CHARACTER_MODE_LOGOUT 8
#define CHARACTER_MODE_SPECTATOR_BEGIN 9
#define CHARACTER_HAS_SPAWN_PROTECTION(S) (S<CHARACTER_MODE_NORMAL)
#define CHARACTER_IS_PROTECTED(S) ((S<CHARACTER_MODE_NORMAL)||(S>CHARACTER_MODE_LOGOUT))
#define CHARACTER_SPAWN_PROTECTION_ALMOST_FINISHED(S) (S==CHARACTER_MODE_NORMAL-1)
#define CHARACTER_IN_SPECTATOR_MODE(S) (S>CHARACTER_MODE_LOGOUT)
#define CHARACTER_NO_LOGOUT(S) ((S!=CHARACTER_MODE_LOGOUT)&&(S<CHARACTER_MODE_SPECTATOR_BEGIN+15))

extern const unsigned char ObstacleMap[MAP_HEIGHT][MAP_WIDTH];

// HarvestAreas[i] has size 2*HarvestAreaSizes[i] and contains alternating x,y coordinates
extern const int *HarvestAreas[NUM_HARVEST_AREAS];
extern const int HarvestAreaSizes[NUM_HARVEST_AREAS];

// Harvest amounts are subject to block reward halving
extern const int HarvestPortions[NUM_HARVEST_AREAS];  // Harvest amounts in cents
static const int TOTAL_HARVEST = 900;                 // Total harvest in cents (includes CROWN_BONUS)
static const int CROWN_BONUS = 25;                    // Bonus for holding Crown of the Fortune in cents

// Locations where the crown can spawn when the crown holder enters spawn area (x,y pairs)
extern const int CrownSpawn[NUM_CROWN_LOCATIONS * 2];

inline bool IsInsideMap(int x, int y)
{
    return x >= 0 && x < MAP_WIDTH && y >= 0 && y < MAP_HEIGHT;
}

inline bool IsWalkable(int x, int y)
{
    return ObstacleMap[y][x] == 0;
}

inline bool IsOriginalSpawnArea(int x, int y)
{
    return ((x == 0 || x == MAP_WIDTH - 1) && (y < SPAWN_AREA_LENGTH || y >= MAP_HEIGHT - SPAWN_AREA_LENGTH))
        || ((y == 0 || y == MAP_HEIGHT - 1) && (x < SPAWN_AREA_LENGTH || x >= MAP_WIDTH - SPAWN_AREA_LENGTH));
}

#endif
