// Source/DoTSim/Public/DoTSim/Random.h
// xoshiro128** pseudo-random generator, seeded with splitmix64.
// Fully deterministic and platform independent.
#pragma once

#include "DoTSim/Fix64.h"

namespace dot
{
class Rng
{
public:
	explicit Rng(uint64_t Seed = 1) { Reseed(Seed); }

	void Reseed(uint64_t Seed)
	{
		uint64_t Z = Seed;
		for (int Pair = 0; Pair < 2; ++Pair)
		{
			const uint64_t V = SplitMix64(Z);
			State[Pair * 2] = static_cast<uint32_t>(V & 0xFFFFFFFFull);
			State[Pair * 2 + 1] = static_cast<uint32_t>(V >> 32);
		}
		if ((State[0] | State[1] | State[2] | State[3]) == 0)
		{
			State[0] = 0x9E3779B9u;
		}
	}

	uint32_t NextU32()
	{
		const uint32_t Result = Rotl(State[1] * 5u, 7) * 9u;
		const uint32_t T = State[1] << 9;
		State[2] ^= State[0];
		State[3] ^= State[1];
		State[1] ^= State[2];
		State[0] ^= State[3];
		State[2] ^= T;
		State[3] = Rotl(State[3], 11);
		return Result;
	}

	/** Uniform integer in [Lo, HiInclusive]. */
	int RangeInt(int Lo, int HiInclusive)
	{
		if (HiInclusive <= Lo)
		{
			return Lo;
		}
		const uint32_t Span = static_cast<uint32_t>(HiInclusive - Lo) + 1u;
		return Lo + static_cast<int>(NextU32() % Span);
	}

	/** Uniform fixed-point value in [0, 1). */
	Fix64 Next01() { return Fix64::FromRaw(static_cast<int64_t>(NextU32())); }

	Fix64 Range(Fix64 Lo, Fix64 Hi) { return Lo + (Hi - Lo) * Next01(); }

	bool Chance(Fix64 Probability) { return Next01() < Probability; }

	const uint32_t* RawState() const { return State; }

private:
	static uint32_t Rotl(uint32_t X, int K) { return (X << K) | (X >> (32 - K)); }

	static uint64_t SplitMix64(uint64_t& X)
	{
		X += 0x9E3779B97F4A7C15ull;
		uint64_t Z = X;
		Z = (Z ^ (Z >> 30)) * 0xBF58476D1CE4E5B9ull;
		Z = (Z ^ (Z >> 27)) * 0x94D049BB133111EBull;
		return Z ^ (Z >> 31);
	}

	uint32_t State[4] = {0, 0, 0, 0};
};
} // namespace dot
