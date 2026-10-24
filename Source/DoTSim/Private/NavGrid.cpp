// Source/DoTSim/Private/NavGrid.cpp
#include "DoTSim/NavGrid.h"

#include <algorithm>
#include <cstdlib>

namespace dot
{
namespace
{
struct HeapNode
{
	int f = 0;
	int h = 0;
	int cell = 0;
};

// Total order so every platform pops nodes in the same sequence.
inline bool HeapLess(const HeapNode& A, const HeapNode& B)
{
	if (A.f != B.f)
	{
		return A.f < B.f;
	}
	if (A.h != B.h)
	{
		return A.h < B.h;
	}
	return A.cell < B.cell;
}

class MinHeap
{
public:
	bool Empty() const { return Nodes.empty(); }

	void Push(const HeapNode& N)
	{
		Nodes.push_back(N);
		size_t I = Nodes.size() - 1;
		while (I > 0)
		{
			const size_t Parent = (I - 1) / 2;
			if (!HeapLess(Nodes[I], Nodes[Parent]))
			{
				break;
			}
			std::swap(Nodes[I], Nodes[Parent]);
			I = Parent;
		}
	}

	HeapNode Pop()
	{
		const HeapNode Top = Nodes.front();
		Nodes.front() = Nodes.back();
		Nodes.pop_back();
		size_t I = 0;
		while (true)
		{
			const size_t L = I * 2 + 1;
			const size_t R = L + 1;
			size_t Smallest = I;
			if (L < Nodes.size() && HeapLess(Nodes[L], Nodes[Smallest]))
			{
				Smallest = L;
			}
			if (R < Nodes.size() && HeapLess(Nodes[R], Nodes[Smallest]))
			{
				Smallest = R;
			}
			if (Smallest == I)
			{
				break;
			}
			std::swap(Nodes[I], Nodes[Smallest]);
			I = Smallest;
		}
		return Top;
	}

private:
	std::vector<HeapNode> Nodes;
};

bool InsideFeature(const MapFeature& F, FixVec2 P)
{
	if (F.circle)
	{
		return fx::DistSq(P, F.center) <= F.radius * F.radius;
	}
	return P.x >= F.min.x && P.x <= F.max.x && P.z >= F.min.z && P.z <= F.max.z;
}
} // namespace

void NavGrid::Build(const MapDef& Map)
{
	Cell = Map.cellSize;
	SizeX = Map.width;
	SizeZ = Map.depth;
	CellsX = static_cast<int>((SizeX / Cell).FloorToInt());
	CellsZ = static_cast<int>((SizeZ / Cell).FloorToInt());
	const size_t Count = static_cast<size_t>(CellsX * CellsZ);
	Flags.assign(Count, 0);
	Heights.assign(Count, Fix64::Zero());
	DynamicBlocks.assign(Count, 0);

	for (int Cz = 0; Cz < CellsZ; ++Cz)
	{
		for (int Cx = 0; Cx < CellsX; ++Cx)
		{
			const FixVec2 P = CellCenter(Cx, Cz);
			const int I = Index(Cx, Cz);
			for (const MapFeature& F : Map.features)
			{
				if (F.type == FeatureType::Hill)
				{
					if (!F.circle || !InsideFeature(F, P))
					{
						continue;
					}
					// Plateau hill: full height in the inner half, linear slope outside.
					const Fix64 D = fx::Dist(P, F.center);
					const Fix64 T = fx::Clamp01((F.radius - D) / (F.radius / 2));
					Heights[static_cast<size_t>(I)] = fx::Max(Heights[static_cast<size_t>(I)], F.height * T);
					continue;
				}
				if (!InsideFeature(F, P))
				{
					continue;
				}
				switch (F.type)
				{
				case FeatureType::Forest: Flags[static_cast<size_t>(I)] |= cellflag::Forest; break;
				case FeatureType::ShallowWater: Flags[static_cast<size_t>(I)] = static_cast<uint8_t>((Flags[static_cast<size_t>(I)] & ~cellflag::Deep) | cellflag::Ford); break;
				case FeatureType::DeepWater:
					if ((Flags[static_cast<size_t>(I)] & (cellflag::Ford | cellflag::Road)) == 0)
					{
						Flags[static_cast<size_t>(I)] |= cellflag::Deep;
					}
					break;
				case FeatureType::Rock: Flags[static_cast<size_t>(I)] |= cellflag::Rock; break;
				case FeatureType::Road: Flags[static_cast<size_t>(I)] = static_cast<uint8_t>((Flags[static_cast<size_t>(I)] & ~cellflag::Deep) | cellflag::Road); break;
				case FeatureType::Ruins: Flags[static_cast<size_t>(I)] |= cellflag::Ruins; break;
				default: break;
				}
			}
		}
	}
}

void NavGrid::WorldToCell(FixVec2 P, int& OutCx, int& OutCz) const
{
	OutCx = static_cast<int>((P.x / Cell).FloorToInt());
	OutCz = static_cast<int>((P.z / Cell).FloorToInt());
}

FixVec2 NavGrid::CellCenter(int Cx, int Cz) const
{
	return FixVec2(Cell * Cx + Cell / 2, Cell * Cz + Cell / 2);
}

FixVec2 NavGrid::ClampToWorld(FixVec2 P, Fix64 Margin) const
{
	return FixVec2(fx::Clamp(P.x, Margin, SizeX - Margin), fx::Clamp(P.z, Margin, SizeZ - Margin));
}

uint8_t NavGrid::FlagsAt(FixVec2 P) const
{
	int Cx = 0;
	int Cz = 0;
	WorldToCell(P, Cx, Cz);
	return FlagsCell(Cx, Cz);
}

Fix64 NavGrid::HeightAt(FixVec2 P) const
{
	int Cx = 0;
	int Cz = 0;
	WorldToCell(P, Cx, Cz);
	return HeightCell(Cx, Cz);
}

bool NavGrid::Walkable(MoveClass Move, int Cx, int Cz) const
{
	if (Move == MoveClass::Titan)
	{
		// Titans wade through water and step over rocks; they only need clearance from the map edge.
		return Cx >= TitanClearanceCells && Cz >= TitanClearanceCells && Cx < CellsX - TitanClearanceCells && Cz < CellsZ - TitanClearanceCells;
	}
	if (!InBounds(Cx, Cz))
	{
		return false;
	}
	const size_t I = static_cast<size_t>(Index(Cx, Cz));
	return (Flags[I] & (cellflag::Rock | cellflag::Deep)) == 0 && DynamicBlocks[I] == 0;
}

bool NavGrid::WalkableAt(MoveClass Move, FixVec2 P) const
{
	if (!InWorld(P))
	{
		return false;
	}
	int Cx = 0;
	int Cz = 0;
	WorldToCell(P, Cx, Cz);
	return Walkable(Move, Cx, Cz);
}

bool NavGrid::LineWalkable(MoveClass Move, FixVec2 A, FixVec2 B) const
{
	const FixVec2 Delta = B - A;
	const Fix64 Len = Delta.Length();
	const Fix64 Step = Cell / 2;
	const int Samples = static_cast<int>((Len / Step).FloorToInt()) + 1;
	for (int I = 0; I <= Samples; ++I)
	{
		const FixVec2 P = A + Delta * (fx::Int(I) / fx::Int(Samples));
		if (!WalkableAt(Move, P))
		{
			return false;
		}
	}
	return true;
}

FixVec2 NavGrid::NearestWalkable(MoveClass Move, FixVec2 P, int MaxRadiusCells) const
{
	if (WalkableAt(Move, P))
	{
		return P;
	}
	int Cx = 0;
	int Cz = 0;
	WorldToCell(P, Cx, Cz);
	for (int R = 1; R <= MaxRadiusCells; ++R)
	{
		bool bFound = false;
		Fix64 BestDist;
		FixVec2 Best;
		for (int Dz = -R; Dz <= R; ++Dz)
		{
			for (int Dx = -R; Dx <= R; ++Dx)
			{
				if (std::max(std::abs(Dx), std::abs(Dz)) != R || !Walkable(Move, Cx + Dx, Cz + Dz))
				{
					continue;
				}
				const FixVec2 C = CellCenter(Cx + Dx, Cz + Dz);
				const Fix64 D = fx::DistSq(C, P);
				if (!bFound || D < BestDist)
				{
					bFound = true;
					BestDist = D;
					Best = C;
				}
			}
		}
		if (bFound)
		{
			return Best;
		}
	}
	return P;
}

int NavGrid::StepCost(MoveClass Move, int Cx, int Cz, bool bDiagonal) const
{
	const uint8_t F = FlagsCell(Cx, Cz);
	int Cost = bDiagonal ? 14 : 10;
	if (Move == MoveClass::Titan)
	{
		if (F & cellflag::Deep)
		{
			Cost = Cost * 17 / 10;
		}
		else if (F & cellflag::Ford)
		{
			Cost = Cost * 13 / 10;
		}
		return Cost;
	}
	if (F & cellflag::Road)
	{
		Cost = Cost * 9 / 10;
	}
	if (F & cellflag::Ford)
	{
		Cost *= 2;
	}
	if ((F & cellflag::Forest) && Move == MoveClass::Cavalry)
	{
		Cost = Cost * 16 / 10;
	}
	return Cost;
}

bool NavGrid::FindPath(MoveClass Move, FixVec2 From, FixVec2 To, std::vector<FixVec2>& OutWaypoints) const
{
	OutWaypoints.clear();
	const FixVec2 Goal = NearestWalkable(Move, ClampToWorld(To, Cell), 16);
	const FixVec2 Start = NearestWalkable(Move, ClampToWorld(From, Cell), 16);
	if (LineWalkable(Move, Start, Goal))
	{
		OutWaypoints.push_back(Goal);
		return true;
	}
	int Sx = 0;
	int Sz = 0;
	int Gx = 0;
	int Gz = 0;
	WorldToCell(Start, Sx, Sz);
	WorldToCell(Goal, Gx, Gz);
	if (!Walkable(Move, Gx, Gz) || !Walkable(Move, Sx, Sz))
	{
		return false;
	}

	const size_t Count = static_cast<size_t>(CellsX * CellsZ);
	std::vector<int> G(Count, INT32_MAX);
	std::vector<int> Parent(Count, -1);
	std::vector<uint8_t> Closed(Count, 0);
	MinHeap Open;

	auto Heuristic = [Gx, Gz](int Cx, int Cz)
	{
		const int Dx = std::abs(Cx - Gx);
		const int Dz = std::abs(Cz - Gz);
		const int Diagonal = std::min(Dx, Dz);
		const int Straight = std::max(Dx, Dz) - Diagonal;
		return Straight * 9 + Diagonal * 12; // admissible: road cost is 9 / 12
	};

	const int StartIndex = Index(Sx, Sz);
	const int GoalIndex = Index(Gx, Gz);
	G[static_cast<size_t>(StartIndex)] = 0;
	Open.Push({Heuristic(Sx, Sz), Heuristic(Sx, Sz), StartIndex});

	static const int Dirs[8][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}, {1, 1}, {1, -1}, {-1, 1}, {-1, -1}};
	bool bReached = false;
	while (!Open.Empty())
	{
		const HeapNode Node = Open.Pop();
		if (Closed[static_cast<size_t>(Node.cell)])
		{
			continue;
		}
		Closed[static_cast<size_t>(Node.cell)] = 1;
		if (Node.cell == GoalIndex)
		{
			bReached = true;
			break;
		}
		const int Cx = Node.cell % CellsX;
		const int Cz = Node.cell / CellsX;
		for (int D = 0; D < 8; ++D)
		{
			const int Nx = Cx + Dirs[D][0];
			const int Nz = Cz + Dirs[D][1];
			if (!Walkable(Move, Nx, Nz))
			{
				continue;
			}
			const bool bDiagonal = D >= 4;
			if (bDiagonal && (!Walkable(Move, Cx + Dirs[D][0], Cz) || !Walkable(Move, Cx, Cz + Dirs[D][1])))
			{
				continue; // no corner cutting
			}
			const int NIndex = Index(Nx, Nz);
			if (Closed[static_cast<size_t>(NIndex)])
			{
				continue;
			}
			const int NewG = G[static_cast<size_t>(Node.cell)] + StepCost(Move, Nx, Nz, bDiagonal);
			if (NewG < G[static_cast<size_t>(NIndex)])
			{
				G[static_cast<size_t>(NIndex)] = NewG;
				Parent[static_cast<size_t>(NIndex)] = Node.cell;
				const int H = Heuristic(Nx, Nz);
				Open.Push({NewG + H, H, NIndex});
			}
		}
	}
	if (!bReached)
	{
		return false;
	}

	std::vector<FixVec2> Raw;
	for (int C = GoalIndex; C != -1 && C != StartIndex; C = Parent[static_cast<size_t>(C)])
	{
		Raw.push_back(CellCenter(C % CellsX, C / CellsX));
	}
	std::reverse(Raw.begin(), Raw.end());
	if (Raw.empty())
	{
		OutWaypoints.push_back(Goal);
		return true;
	}
	Raw.back() = Goal;

	// String pulling: keep only the waypoints needed to stay on walkable ground.
	FixVec2 Current = Start;
	size_t I = 0;
	while (I < Raw.size())
	{
		size_t Farthest = I;
		for (size_t K = Raw.size(); K-- > I + 1;)
		{
			if (LineWalkable(Move, Current, Raw[K]))
			{
				Farthest = K;
				break;
			}
		}
		OutWaypoints.push_back(Raw[Farthest]);
		Current = Raw[Farthest];
		I = Farthest + 1;
	}
	return true;
}

Fix64 NavGrid::SpeedMult(MoveClass Move, FixVec2 P, const RulesDef& Rules) const
{
	const uint8_t F = FlagsAt(P);
	if (Move == MoveClass::Titan)
	{
		if (F & cellflag::Deep)
		{
			return Rules.titanDeepSpeedMult;
		}
		if (F & cellflag::Ford)
		{
			return Rules.titanFordSpeedMult;
		}
		return Fix64::One();
	}
	Fix64 M = Fix64::One();
	if (F & cellflag::Road)
	{
		M = M * Rules.roadSpeedMult;
	}
	if (F & cellflag::Ford)
	{
		M = M * Rules.fordSpeedMult;
	}
	if ((F & cellflag::Forest) && Move == MoveClass::Cavalry)
	{
		M = M * Rules.forestCavalrySpeedMult;
	}
	return M;
}

void NavGrid::AddDynamicBlock(int Cx, int Cz)
{
	if (InBounds(Cx, Cz))
	{
		++DynamicBlocks[static_cast<size_t>(Index(Cx, Cz))];
	}
}

void NavGrid::RemoveDynamicBlock(int Cx, int Cz)
{
	if (InBounds(Cx, Cz) && DynamicBlocks[static_cast<size_t>(Index(Cx, Cz))] > 0)
	{
		--DynamicBlocks[static_cast<size_t>(Index(Cx, Cz))];
	}
}

void NavGrid::AddFlag(int Cx, int Cz, uint8_t Flag)
{
	if (InBounds(Cx, Cz))
	{
		Flags[static_cast<size_t>(Index(Cx, Cz))] |= Flag;
	}
}
} // namespace dot
