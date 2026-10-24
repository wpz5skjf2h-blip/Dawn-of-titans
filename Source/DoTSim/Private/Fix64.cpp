// Source/DoTSim/Private/Fix64.cpp
#include "DoTSim/Fix64.h"

#include <cmath>

namespace dot
{
namespace
{
#if defined(__SIZEOF_INT128__) && !defined(DOT_NO_INT128)
__extension__ typedef unsigned __int128 DotU128;
#endif

inline uint64_t AbsToUnsigned(int64_t V)
{
	return V < 0 ? (uint64_t(0) - static_cast<uint64_t>(V)) : static_cast<uint64_t>(V);
}

inline int64_t ApplySign(uint64_t Magnitude, bool bNegative)
{
	return bNegative ? -static_cast<int64_t>(Magnitude) : static_cast<int64_t>(Magnitude);
}
} // namespace

int64_t FixMulRaw(int64_t A, int64_t B)
{
	const bool bNegative = (A < 0) != (B < 0);
	const uint64_t UA = AbsToUnsigned(A);
	const uint64_t UB = AbsToUnsigned(B);
	const uint64_t AHi = UA >> 32;
	const uint64_t ALo = UA & 0xFFFFFFFFull;
	const uint64_t BHi = UB >> 32;
	const uint64_t BLo = UB & 0xFFFFFFFFull;
	// (A * B) >> 32, computed from 32-bit halves so no 128-bit type is needed.
	const uint64_t Result = ((AHi * BHi) << 32) + AHi * BLo + ALo * BHi + ((ALo * BLo) >> 32);
	return ApplySign(Result, bNegative);
}

int64_t FixDivRaw(int64_t A, int64_t B)
{
	if (B == 0)
	{
		return A >= 0 ? INT64_MAX : INT64_MIN;
	}
	const bool bNegative = (A < 0) != (B < 0);
	const uint64_t UA = AbsToUnsigned(A);
	const uint64_t UB = AbsToUnsigned(B);
	const uint64_t Quotient = UA / UB;
	uint64_t Remainder = UA % UB;
	uint64_t Fraction = 0;
	if (UB <= 0xFFFFFFFFull)
	{
		Fraction = (Remainder << 32) / UB;
	}
#if defined(__SIZEOF_INT128__) && !defined(DOT_NO_INT128)
	else
	{
		// Exact integer division: bit-identical to the portable loop below.
		Fraction = static_cast<uint64_t>((static_cast<DotU128>(Remainder) << 32) / UB);
	}
#else
	else
	{
		for (int Bit = 0; Bit < 32; ++Bit)
		{
			Remainder <<= 1;
			Fraction <<= 1;
			if (Remainder >= UB)
			{
				Remainder -= UB;
				Fraction |= 1;
			}
		}
	}
#endif
	return ApplySign((Quotient << 32) | Fraction, bNegative);
}

uint64_t ISqrt64(uint64_t Value)
{
	uint64_t Result = 0;
	uint64_t Bit = uint64_t(1) << 62;
	while (Bit > Value)
	{
		Bit >>= 2;
	}
	while (Bit != 0)
	{
		if (Value >= Result + Bit)
		{
			Value -= Result + Bit;
			Result = (Result >> 1) + Bit;
		}
		else
		{
			Result >>= 1;
		}
		Bit >>= 2;
	}
	return Result;
}

Fix64 Fix64::FromDouble(double Value)
{
	return FromRaw(static_cast<int64_t>(std::llround(Value * 4294967296.0)));
}

namespace fx
{
Fix64 Sqrt(Fix64 V)
{
	if (V.raw <= 0)
	{
		return Fix64::Zero();
	}
	// sqrt(raw * 2^32) == sqrt(raw * 2^S) * 2^((32 - S) / 2), with the largest even S that does not overflow.
	const uint64_t Raw = static_cast<uint64_t>(V.raw);
	int Shift = 32;
	while (Shift > 0 && (Raw >> (64 - Shift)) != 0)
	{
		Shift -= 2;
	}
	const uint64_t Root = ISqrt64(Raw << Shift);
	return Fix64::FromRaw(static_cast<int64_t>(Root << ((32 - Shift) / 2)));
}

Fix64 Sin(Fix64 Radians)
{
	// Reduce to [-pi, pi].
	const int64_t TwoPiRaw = Fix64::TwoPi().raw;
	int64_t R = Radians.raw + Fix64::Pi().raw;
	int64_t K = R / TwoPiRaw;
	if (R < 0 && K * TwoPiRaw != R)
	{
		--K;
	}
	R -= K * TwoPiRaw;
	Fix64 X = Fix64::FromRaw(R) - Fix64::Pi();
	// Fold into [-pi/2, pi/2].
	if (X > Fix64::HalfPi())
	{
		X = Fix64::Pi() - X;
	}
	else if (X < -Fix64::HalfPi())
	{
		X = -Fix64::Pi() - X;
	}
	// Taylor series to x^9 (max error ~3e-6 on [-pi/2, pi/2]), Horner form.
	const Fix64 X2 = X * X;
	Fix64 S = Fix64::One() - X2 / 72;
	S = Fix64::One() - (X2 / 42) * S;
	S = Fix64::One() - (X2 / 20) * S;
	S = Fix64::One() - (X2 / 6) * S;
	return X * S;
}

Fix64 Cos(Fix64 Radians)
{
	return Sin(Radians + Fix64::HalfPi());
}

Fix64 DegToRad(Fix64 Degrees)
{
	return Degrees * Fix64::Pi() / 180;
}

FixVec2 DirFromDegrees(Fix64 Degrees)
{
	const Fix64 R = DegToRad(Degrees);
	return FixVec2(Sin(R), Cos(R)).Normalized();
}

FixVec2 RotateToward(FixVec2 Current, FixVec2 Desired, Fix64 CosMax, Fix64 SinMax)
{
	if (Desired.IsZero())
	{
		return Current;
	}
	if (Current.IsZero())
	{
		return Desired;
	}
	const Fix64 DotValue = Current.Dot(Desired);
	if (DotValue >= CosMax)
	{
		return Desired;
	}
	// Rotate toward the side Desired lies on (cross sign); clockwise is negative angle here.
	const Fix64 CrossValue = Current.Cross(Desired);
	const Fix64 SignedSin = CrossValue >= Fix64::Zero() ? SinMax : -SinMax;
	return Current.RotatedBy(CosMax, SignedSin).Normalized();
}
} // namespace fx

FixVec2 FixVec2::Normalized() const
{
	const Fix64 Len = Length();
	if (Len.raw == 0)
	{
		return FixVec2();
	}
	return FixVec2(x / Len, z / Len);
}

FixVec2 FixVec2::ClampedLength(Fix64 MaxLen) const
{
	const Fix64 LenSq = LengthSq();
	if (LenSq <= MaxLen * MaxLen)
	{
		return *this;
	}
	return Normalized() * MaxLen;
}
} // namespace dot
