// Source/DawnOfTitans/DoTCoords.h
// Conversions between simulation space and Unreal world space.
// Simulation: meters on the XZ plane, X = across the battlefield, Z = toward the defender.
// Unreal: centimeters, X forward (toward the defender), Y right, Z up.
// The default camera looks along +X, so simulation X grows to the right of the screen.
#pragma once

#include "CoreMinimal.h"

#include "DoTSim/Fix64.h"

namespace DoTCoords
{
inline FVector SimToWorld(const dot::FixVec2& P, double HeightMeters = 0.0)
{
	return FVector(P.z.ToDouble() * 100.0, P.x.ToDouble() * 100.0, HeightMeters * 100.0);
}

inline FVector SimToWorldD(double X, double Z, double HeightMeters = 0.0)
{
	return FVector(Z * 100.0, X * 100.0, HeightMeters * 100.0);
}

inline dot::FixVec2 WorldToSim(const FVector& W)
{
	return dot::FixVec2(dot::Fix64::FromDouble(W.Y / 100.0), dot::Fix64::FromDouble(W.X / 100.0));
}

/** Unreal yaw (degrees) of a simulation direction. */
inline float SimDirToYaw(const dot::FixVec2& D)
{
	return FMath::RadiansToDegrees(FMath::Atan2(D.x.ToFloat(), D.z.ToFloat()));
}

inline FVector SimDirToWorld(const dot::FixVec2& D)
{
	return FVector(D.z.ToDouble(), D.x.ToDouble(), 0.0);
}
} // namespace DoTCoords
