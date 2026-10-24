// Source/DoTSim/Public/DoTSim/Common.h
// Shared definitions for the deterministic battle simulation (DoTSim).
// This library is pure C++17: it must never include Unreal Engine headers,
// use floating point inside the simulation step, or depend on wall-clock time.
#pragma once

#include <cstddef>
#include <cstdint>

// UnrealBuildTool defines DOTSIM_API (dllexport/dllimport) when this code is
// compiled as the DoTSim module. Standalone builds (CMake tools, server
// verifier) compile it as a plain static library.
#ifndef DOTSIM_API
#define DOTSIM_API
#endif

#if defined(_MSC_VER)
// std:: containers inside exported classes.
#pragma warning(disable : 4251)
#endif

namespace dot
{
constexpr int kTicksPerSecond = 30;

inline constexpr int SecondsToTicks(int Seconds) { return Seconds * kTicksPerSecond; }
} // namespace dot
