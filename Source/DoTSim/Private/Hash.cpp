// Source/DoTSim/Private/Hash.cpp
#include "DoTSim/Hash.h"

namespace dot
{
namespace
{
constexpr uint64_t kPrime1 = 0x9E3779B185EBCA87ull;
constexpr uint64_t kPrime2 = 0xC2B2AE3D27D4EB4Full;
constexpr uint64_t kPrime3 = 0x165667B19E3779F9ull;
constexpr uint64_t kPrime4 = 0x85EBCA77C2B2AE63ull;
constexpr uint64_t kPrime5 = 0x27D4EB2F165667C5ull;

inline uint64_t Rotl64(uint64_t X, int R) { return (X << R) | (X >> (64 - R)); }

inline uint64_t Read64(const uint8_t* P)
{
	uint64_t V = 0;
	for (int I = 7; I >= 0; --I)
	{
		V = (V << 8) | P[I];
	}
	return V;
}

inline uint32_t Read32(const uint8_t* P)
{
	return static_cast<uint32_t>(P[0]) | (static_cast<uint32_t>(P[1]) << 8) | (static_cast<uint32_t>(P[2]) << 16) |
		(static_cast<uint32_t>(P[3]) << 24);
}

inline uint64_t Round(uint64_t Acc, uint64_t Input)
{
	Acc += Input * kPrime2;
	Acc = Rotl64(Acc, 31);
	return Acc * kPrime1;
}

inline uint64_t MergeRound(uint64_t Acc, uint64_t Val)
{
	Acc ^= Round(0, Val);
	return Acc * kPrime1 + kPrime4;
}
} // namespace

uint64_t XXHash64(const void* Data, size_t Length, uint64_t Seed)
{
	const uint8_t* P = static_cast<const uint8_t*>(Data);
	const uint8_t* const End = P + Length;
	uint64_t H = 0;

	if (Length >= 32)
	{
		const uint8_t* const Limit = End - 32;
		uint64_t V1 = Seed + kPrime1 + kPrime2;
		uint64_t V2 = Seed + kPrime2;
		uint64_t V3 = Seed;
		uint64_t V4 = Seed - kPrime1;
		do
		{
			V1 = Round(V1, Read64(P));
			V2 = Round(V2, Read64(P + 8));
			V3 = Round(V3, Read64(P + 16));
			V4 = Round(V4, Read64(P + 24));
			P += 32;
		} while (P <= Limit);
		H = Rotl64(V1, 1) + Rotl64(V2, 7) + Rotl64(V3, 12) + Rotl64(V4, 18);
		H = MergeRound(H, V1);
		H = MergeRound(H, V2);
		H = MergeRound(H, V3);
		H = MergeRound(H, V4);
	}
	else
	{
		H = Seed + kPrime5;
	}

	H += static_cast<uint64_t>(Length);

	while (P + 8 <= End)
	{
		H ^= Round(0, Read64(P));
		H = Rotl64(H, 27) * kPrime1 + kPrime4;
		P += 8;
	}
	if (P + 4 <= End)
	{
		H ^= static_cast<uint64_t>(Read32(P)) * kPrime1;
		H = Rotl64(H, 23) * kPrime2 + kPrime3;
		P += 4;
	}
	while (P < End)
	{
		H ^= static_cast<uint64_t>(*P) * kPrime5;
		H = Rotl64(H, 11) * kPrime1;
		++P;
	}

	H ^= H >> 33;
	H *= kPrime2;
	H ^= H >> 29;
	H *= kPrime3;
	H ^= H >> 32;
	return H;
}

std::string HashToHex(uint64_t Hash)
{
	static const char* const Digits = "0123456789abcdef";
	std::string S(16, '0');
	for (int I = 15; I >= 0; --I)
	{
		S[static_cast<size_t>(I)] = Digits[Hash & 0xFu];
		Hash >>= 4;
	}
	return S;
}
} // namespace dot
