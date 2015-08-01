#include "game/common.h"

#include "hash.h"
#include "tinyformat.h"
#include "uint256.h"

std::string CharacterID::ToString() const
{
    if (!index)
        return player;
    return player + strprintf(".%d", int(index));
}

RandomGenerator::RandomGenerator (const uint256& hashBlock)
  : state0(UintToArith256 (SerializeHash (hashBlock, SER_GETHASH, 0)))
{
    state = state0;
}

int
RandomGenerator::GetIntRnd (int modulo)
{
  // Advance generator state, if most bits of the current state were used
  if (state < MIN_STATE)
    {
      const uint256 fixedState0 = ArithToUint256 (state0);
      state0 = UintToArith256 (SerializeHash (fixedState0, SER_GETHASH, 0));
      state = state0;
    }

  arith_uint256 res = state;
  state /= modulo;
  res -= state * modulo;

  assert (res.bits () < 64);
  return res.GetLow64 ();
}

const arith_uint256 RandomGenerator::MIN_STATE
  = arith_uint256().SetCompact (0x097FFFFFu);
