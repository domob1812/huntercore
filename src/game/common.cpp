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

#include <game/common.h>

#include <hash.h>
#include <names/common.h>
#include <tinyformat.h>

std::string CharacterID::ToString() const
{
    if (!index)
        return player;
    return player + strprintf(".%d", int(index));
}

RandomGenerator::RandomGenerator (const uint256& hashBlock)
  : state0(SerializeHash (hashBlock, SER_GETHASH, 0))
{
    state = UintToArith256 (state0);
}

int
RandomGenerator::GetIntRnd (int modulo)
{
  // Advance generator state, if most bits of the current state were used
  if (state < MIN_STATE)
    {
      /* The original "legacy" implementation based on CBigNum serialised
         the value based on valtype and with leading zeros removed.  For
         compatibility with the old consensus behaviour, we replicate this.  */

      valtype data(state0.begin (), state0.end ());
      while (data.back () == 0)
        data.pop_back ();

      /* The legacy representation uses the highest bit as sign bit.  Thus
         we have to add a zero at the end if the highest bit is set.  */
      if (data.back () & 128)
        data.push_back (0);

      state0 = SerializeHash (data, SER_GETHASH, 0);
      state = UintToArith256 (state0);
    }

  arith_uint256 res = state;
  state /= modulo;
  res -= state * modulo;

  assert (res.bits () < 64);
  return res.GetLow64 ();
}

const arith_uint256 RandomGenerator::MIN_STATE
  = arith_uint256().SetCompact (0x097FFFFFu);
