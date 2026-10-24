// Source/DoTSim/Public/DoTSim/Hash.h
// xxHash64 and a little-endian state writer used for determinism checks.
#pragma once

#include "DoTSim/Fix64.h"

#include <string>
#include <vector>

namespace dot
{
DOTSIM_API uint64_t XXHash64(const void* Data, size_t Length, uint64_t Seed = 0);

/** Serializes values in a fixed little-endian layout, independent of platform. */
class StateWriter
{
public:
	void U8(uint8_t V) { Bytes.push_back(V); }
	void U32(uint32_t V)
	{
		for (int I = 0; I < 4; ++I)
		{
			Bytes.push_back(static_cast<uint8_t>((V >> (8 * I)) & 0xFFu));
		}
	}
	void I32(int32_t V) { U32(static_cast<uint32_t>(V)); }
	void U64(uint64_t V)
	{
		for (int I = 0; I < 8; ++I)
		{
			Bytes.push_back(static_cast<uint8_t>((V >> (8 * I)) & 0xFFu));
		}
	}
	void I64(int64_t V) { U64(static_cast<uint64_t>(V)); }
	void F(Fix64 V) { I64(V.raw); }
	void V(FixVec2 Vec)
	{
		F(Vec.x);
		F(Vec.z);
	}
	void Bool(bool B) { U8(B ? 1 : 0); }

	uint64_t Hash() const { return XXHash64(Bytes.data(), Bytes.size(), 0); }
	void Clear() { Bytes.clear(); }
	size_t Size() const { return Bytes.size(); }

private:
	std::vector<uint8_t> Bytes;
};

DOTSIM_API std::string HashToHex(uint64_t Hash);
} // namespace dot
