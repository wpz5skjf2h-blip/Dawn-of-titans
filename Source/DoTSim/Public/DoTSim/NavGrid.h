// Source/DoTSim/Public/DoTSim/NavGrid.h
// Battlefield navigation grid (2 m cells): terrain flags, heights, A* paths.
#pragma once

#include "DoTSim/Content.h"

#include <vector>

namespace dot
{
namespace cellflag
{
constexpr uint8_t Forest = 1;
constexpr uint8_t Ford = 2;
constexpr uint8_t Deep = 4;
constexpr uint8_t Road = 8;
constexpr uint8_t Ruins = 16;
constexpr uint8_t Rock = 32;
} // namespace cellflag

class DOTSIM_API NavGrid
{
public:
	void Build(const MapDef& Map);

	int Width() const { return CellsX; }
	int Depth() const { return CellsZ; }
	Fix64 CellSize() const { return Cell; }
	Fix64 WorldWidth() const { return SizeX; }
	Fix64 WorldDepth() const { return SizeZ; }

	bool InBounds(int Cx, int Cz) const { return Cx >= 0 && Cz >= 0 && Cx < CellsX && Cz < CellsZ; }
	bool InWorld(FixVec2 P) const { return P.x >= Fix64::Zero() && P.z >= Fix64::Zero() && P.x < SizeX && P.z < SizeZ; }
	void WorldToCell(FixVec2 P, int& OutCx, int& OutCz) const;
	FixVec2 CellCenter(int Cx, int Cz) const;
	FixVec2 ClampToWorld(FixVec2 P, Fix64 Margin) const;

	uint8_t FlagsAt(FixVec2 P) const;
	uint8_t FlagsCell(int Cx, int Cz) const { return InBounds(Cx, Cz) ? Flags[Index(Cx, Cz)] : uint8_t(0); }
	Fix64 HeightAt(FixVec2 P) const;
	Fix64 HeightCell(int Cx, int Cz) const { return InBounds(Cx, Cz) ? Heights[Index(Cx, Cz)] : Fix64::Zero(); }

	bool Walkable(MoveClass Move, int Cx, int Cz) const;
	bool WalkableAt(MoveClass Move, FixVec2 P) const;
	/** True if every sample along the segment is walkable. */
	bool LineWalkable(MoveClass Move, FixVec2 A, FixVec2 B) const;
	/** Closest walkable cell center within MaxRadiusCells, or P itself if already walkable. */
	FixVec2 NearestWalkable(MoveClass Move, FixVec2 P, int MaxRadiusCells = 12) const;
	/**
	 * A* (8-neighbour) + string pulling. OutWaypoints receives the waypoints after From,
	 * ending at To (or the nearest walkable point to it). Returns false if no path exists.
	 */
	bool FindPath(MoveClass Move, FixVec2 From, FixVec2 To, std::vector<FixVec2>& OutWaypoints) const;

	/** Speed multiplier of the terrain at P for a movement class. */
	Fix64 SpeedMult(MoveClass Move, FixVec2 P, const RulesDef& Rules) const;

	/** Dynamic obstacles (e.g. a fallen titan) block troops only. */
	void AddDynamicBlock(int Cx, int Cz);
	void RemoveDynamicBlock(int Cx, int Cz);
	void AddFlag(int Cx, int Cz, uint8_t Flag);

	const std::vector<uint8_t>& RawFlags() const { return Flags; }
	const std::vector<Fix64>& RawHeights() const { return Heights; }

private:
	int Index(int Cx, int Cz) const { return Cz * CellsX + Cx; }
	int StepCost(MoveClass Move, int Cx, int Cz, bool bDiagonal) const;

	int CellsX = 0;
	int CellsZ = 0;
	Fix64 Cell;
	Fix64 SizeX;
	Fix64 SizeZ;
	std::vector<uint8_t> Flags;
	std::vector<Fix64> Heights;
	std::vector<uint8_t> DynamicBlocks;
	int TitanClearanceCells = 2;
};
} // namespace dot
