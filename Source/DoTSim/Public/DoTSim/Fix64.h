// Source/DoTSim/Public/DoTSim/Fix64.h
// 64-bit Q32.32 fixed-point number and 2D vector (XZ plane).
// All simulation math uses these types so results are bit-identical on every
// platform (x86-64, ARM64, MSVC, Clang, GCC).
#pragma once

#include "DoTSim/Common.h"

namespace dot
{
DOTSIM_API int64_t FixMulRaw(int64_t A, int64_t B);
DOTSIM_API int64_t FixDivRaw(int64_t A, int64_t B);
DOTSIM_API uint64_t ISqrt64(uint64_t Value);

struct Fix64
{
	int64_t raw = 0;

	static constexpr int kFracBits = 32;
	static constexpr int64_t kOneRaw = int64_t(1) << 32;

	constexpr Fix64() = default;

	static constexpr Fix64 FromRaw(int64_t Raw)
	{
		Fix64 F;
		F.raw = Raw;
		return F;
	}
	static constexpr Fix64 FromInt(int64_t Value) { return FromRaw(Value * kOneRaw); }
	/** Exact rational constant, e.g. FromRatio(13, 10) == 1.3 (rounded toward zero). */
	static Fix64 FromRatio(int64_t Numerator, int64_t Denominator) { return FromRaw(FixDivRaw(Numerator * kOneRaw, Denominator * kOneRaw)); }
	/** Content-load time only (never inside a simulation step). IEEE doubles parsed from text are identical everywhere. */
	DOTSIM_API static Fix64 FromDouble(double Value);

	static constexpr Fix64 Zero() { return FromRaw(0); }
	static constexpr Fix64 One() { return FromRaw(kOneRaw); }
	static constexpr Fix64 Half() { return FromRaw(kOneRaw / 2); }
	static constexpr Fix64 Pi() { return FromRaw(13493037705LL); }
	static constexpr Fix64 TwoPi() { return FromRaw(26986075409LL); }
	static constexpr Fix64 HalfPi() { return FromRaw(6746518852LL); }
	static constexpr Fix64 MaxValue() { return FromRaw(INT64_MAX); }

	/** Presentation / logging only. */
	double ToDouble() const { return static_cast<double>(raw) / 4294967296.0; }
	float ToFloat() const { return static_cast<float>(ToDouble()); }

	int64_t FloorToInt() const { return raw >> 32; }
	int64_t RoundToInt() const { return (raw + (kOneRaw >> 1)) >> 32; }

	Fix64 operator-() const { return FromRaw(-raw); }
	Fix64& operator+=(Fix64 O) { raw += O.raw; return *this; }
	Fix64& operator-=(Fix64 O) { raw -= O.raw; return *this; }
	Fix64& operator*=(Fix64 O) { raw = FixMulRaw(raw, O.raw); return *this; }
	Fix64& operator/=(Fix64 O) { raw = FixDivRaw(raw, O.raw); return *this; }
};

inline Fix64 operator+(Fix64 A, Fix64 B) { return Fix64::FromRaw(A.raw + B.raw); }
inline Fix64 operator-(Fix64 A, Fix64 B) { return Fix64::FromRaw(A.raw - B.raw); }
inline Fix64 operator*(Fix64 A, Fix64 B) { return Fix64::FromRaw(FixMulRaw(A.raw, B.raw)); }
inline Fix64 operator/(Fix64 A, Fix64 B) { return Fix64::FromRaw(FixDivRaw(A.raw, B.raw)); }
inline Fix64 operator*(Fix64 A, int B) { return Fix64::FromRaw(A.raw * B); }
inline Fix64 operator*(int A, Fix64 B) { return Fix64::FromRaw(B.raw * A); }
inline Fix64 operator/(Fix64 A, int B) { return Fix64::FromRaw(A.raw / B); }
inline bool operator==(Fix64 A, Fix64 B) { return A.raw == B.raw; }
inline bool operator!=(Fix64 A, Fix64 B) { return A.raw != B.raw; }
inline bool operator<(Fix64 A, Fix64 B) { return A.raw < B.raw; }
inline bool operator<=(Fix64 A, Fix64 B) { return A.raw <= B.raw; }
inline bool operator>(Fix64 A, Fix64 B) { return A.raw > B.raw; }
inline bool operator>=(Fix64 A, Fix64 B) { return A.raw >= B.raw; }

namespace fx
{
inline Fix64 Abs(Fix64 V) { return V.raw < 0 ? -V : V; }
inline Fix64 Min(Fix64 A, Fix64 B) { return A < B ? A : B; }
inline Fix64 Max(Fix64 A, Fix64 B) { return A > B ? A : B; }
inline Fix64 Clamp(Fix64 V, Fix64 Lo, Fix64 Hi) { return V < Lo ? Lo : (V > Hi ? Hi : V); }
inline Fix64 Clamp01(Fix64 V) { return Clamp(V, Fix64::Zero(), Fix64::One()); }
inline Fix64 Lerp(Fix64 A, Fix64 B, Fix64 T) { return A + (B - A) * T; }
inline Fix64 Int(int64_t V) { return Fix64::FromInt(V); }
/** Milli-units: Milli(1500) == 1.5 */
inline Fix64 Milli(int64_t V) { return Fix64::FromRatio(V, 1000); }
inline Fix64 Pct(int64_t Percent) { return Fix64::FromRatio(Percent, 100); }
DOTSIM_API Fix64 Sqrt(Fix64 V);
DOTSIM_API Fix64 Sin(Fix64 Radians);
DOTSIM_API Fix64 Cos(Fix64 Radians);
DOTSIM_API Fix64 DegToRad(Fix64 Degrees);
} // namespace fx

struct FixVec2
{
	Fix64 x;
	Fix64 z;

	constexpr FixVec2() = default;
	constexpr FixVec2(Fix64 InX, Fix64 InZ) : x(InX), z(InZ) {}

	static FixVec2 FromInts(int64_t X, int64_t Z) { return FixVec2(Fix64::FromInt(X), Fix64::FromInt(Z)); }

	FixVec2 operator+(FixVec2 O) const { return FixVec2(x + O.x, z + O.z); }
	FixVec2 operator-(FixVec2 O) const { return FixVec2(x - O.x, z - O.z); }
	FixVec2 operator-() const { return FixVec2(-x, -z); }
	FixVec2 operator*(Fix64 S) const { return FixVec2(x * S, z * S); }
	FixVec2 operator/(Fix64 S) const { return FixVec2(x / S, z / S); }
	FixVec2 operator*(int S) const { return FixVec2(x * S, z * S); }
	FixVec2 operator/(int S) const { return FixVec2(x / S, z / S); }
	FixVec2& operator+=(FixVec2 O) { x += O.x; z += O.z; return *this; }
	FixVec2& operator-=(FixVec2 O) { x -= O.x; z -= O.z; return *this; }
	bool operator==(const FixVec2& O) const { return x == O.x && z == O.z; }
	bool operator!=(const FixVec2& O) const { return !(*this == O); }

	Fix64 Dot(FixVec2 O) const { return x * O.x + z * O.z; }
	/** 2D cross product (positive when O is counter-clockwise from this). */
	Fix64 Cross(FixVec2 O) const { return x * O.z - z * O.x; }
	Fix64 LengthSq() const { return x * x + z * z; }
	Fix64 Length() const { return fx::Sqrt(LengthSq()); }
	bool IsZero() const { return x.raw == 0 && z.raw == 0; }
	/** Unit vector, or zero vector if length is zero. */
	DOTSIM_API FixVec2 Normalized() const;
	/** Clockwise perpendicular: for facing +Z returns +X ("right"). */
	FixVec2 Right() const { return FixVec2(z, -x); }
	FixVec2 RotatedBy(Fix64 CosA, Fix64 SinA) const { return FixVec2(x * CosA - z * SinA, x * SinA + z * CosA); }
	DOTSIM_API FixVec2 ClampedLength(Fix64 MaxLen) const;
};

inline FixVec2 operator*(Fix64 S, FixVec2 V) { return V * S; }

namespace fx
{
inline Fix64 DistSq(FixVec2 A, FixVec2 B) { return (A - B).LengthSq(); }
inline Fix64 Dist(FixVec2 A, FixVec2 B) { return (A - B).Length(); }
/** Unit vector for an angle in degrees measured clockwise from +Z (0 = facing +Z, 90 = facing +X). */
DOTSIM_API FixVec2 DirFromDegrees(Fix64 Degrees);
/**
 * Rotates unit vector Current toward unit vector Desired by at most the angle whose
 * cosine/sine are CosMax/SinMax. Returns the new (re-normalized) facing.
 */
DOTSIM_API FixVec2 RotateToward(FixVec2 Current, FixVec2 Desired, Fix64 CosMax, Fix64 SinMax);
} // namespace fx
} // namespace dot
