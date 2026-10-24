// Source/DoTSim/Private/SimMovement.cpp
// Formations, squad orders and leader movement, soldier steering, knockback physics, spatial hash.
#include "DoTSim/BattleSimulation.h"

#include <algorithm>

namespace dot
{
namespace
{
const Fix64 kDt = Fix64::FromRaw(Fix64::kOneRaw / kTicksPerSecond);

int CeilSqrt(int Value)
{
	int R = static_cast<int>(ISqrt64(static_cast<uint64_t>(std::max(Value, 0))));
	while (R * R < Value)
	{
		++R;
	}
	return std::max(R, 1);
}
} // namespace

// --- Formations ----------------------------------------------------------------

FixVec2 BattleSimulation::SlotOffset(const Squad& Sq, int SlotIndex, int Count) const
{
	// Returns (right, back) offsets relative to the leader point, centered on the formation.
	Fix64 Spacing = Sq.spacing;
	const bool bRanged = TroopOf(Sq).ranged;
	Count = std::max(Count, 1);
	int Cols = 1;
	switch (Sq.formation)
	{
	case Formation::Line: Cols = CeilSqrt(Count * (bRanged ? 5 : 4)); break;
	case Formation::Loose:
		Cols = CeilSqrt(Count * (bRanged ? 5 : 4));
		Spacing = Spacing * 2;
		break;
	case Formation::Column: Cols = std::max(1, CeilSqrt(Count / 4)); break;
	case Formation::Square: Cols = CeilSqrt(Count); break;
	case Formation::Wedge:
	{
		// Row r holds 2r+1 soldiers.
		int Row = 0;
		int Start = 0;
		while (Start + 2 * Row + 1 <= SlotIndex)
		{
			Start += 2 * Row + 1;
			++Row;
		}
		const int InRow = SlotIndex - Start;
		const int RowCount = 2 * Row + 1;
		const Fix64 X = Spacing * (2 * InRow - (RowCount - 1)) / 2;
		int Rows = 0;
		int Filled = 0;
		while (Filled < Count)
		{
			Filled += 2 * Rows + 1;
			++Rows;
		}
		const Fix64 Back = Spacing * Row - Spacing * (Rows - 1) / 2;
		return FixVec2(X, Back);
	}
	default: break;
	}
	Cols = std::min(Cols, Count);
	const int Rows = (Count + Cols - 1) / Cols;
	const int Row = SlotIndex / Cols;
	const int Col = SlotIndex % Cols;
	const int InThisRow = (Row == Rows - 1) ? (Count - Row * Cols) : Cols;
	const Fix64 X = Spacing * (2 * Col - (InThisRow - 1)) / 2;
	const Fix64 Back = Spacing * Row - Spacing * (Rows - 1) / 2;
	return FixVec2(X, Back);
}

FixVec2 BattleSimulation::SlotWorld(const Squad& Sq, int SlotIndex) const
{
	const FixVec2 Off = SlotOffset(Sq, SlotIndex, Sq.aliveCount);
	return Sq.leader + Sq.facing.Right() * Off.x - Sq.facing * Off.z;
}

void BattleSimulation::AssignSlots(Squad& Sq)
{
	int Slot = 0;
	for (const int32_t M : Sq.members)
	{
		Soldier& S = SoldiersList[static_cast<size_t>(M)];
		if (S.Alive())
		{
			S.slot = Slot++;
		}
	}
	Sq.aliveCount = Slot;
}

void BattleSimulation::RefreshSquadGeometry(Squad& Sq)
{
	FixVec2 Sum;
	int Count = 0;
	for (const int32_t M : Sq.members)
	{
		const Soldier& S = SoldiersList[static_cast<size_t>(M)];
		if (S.Alive())
		{
			Sum += S.pos;
			++Count;
		}
	}
	Sq.aliveCount = Count;
	if (Count == 0)
	{
		Sq.radius = Fix64::Zero();
		return;
	}
	Sq.centroid = Sum / fx::Int(Count);
	Fix64 MaxSq;
	for (const int32_t M : Sq.members)
	{
		const Soldier& S = SoldiersList[static_cast<size_t>(M)];
		if (S.Alive())
		{
			MaxSq = fx::Max(MaxSq, fx::DistSq(S.pos, Sq.centroid));
		}
	}
	Sq.radius = fx::Sqrt(MaxSq) + TroopOf(Sq).collisionRadius;
}

// --- Paths ---------------------------------------------------------------------

void BattleSimulation::SetSquadPathTo(Squad& Sq, FixVec2 Target)
{
	const MoveClass Move = TroopOf(Sq).move;
	Sq.path.clear();
	Sq.pathIndex = 0;
	if (!Nav.FindPath(Move, Sq.leader, Target, Sq.path))
	{
		Sq.path.clear();
		Sq.path.push_back(Nav.NearestWalkable(Move, Nav.ClampToWorld(Target, fx::Int(2)), 16));
	}
}

void BattleSimulation::SetSquadWaypoints(Squad& Sq, const std::vector<FixVec2>& Waypoints)
{
	const MoveClass Move = TroopOf(Sq).move;
	Sq.path.clear();
	Sq.pathIndex = 0;
	FixVec2 From = Sq.leader;
	std::vector<FixVec2> Segment;
	for (const FixVec2& Raw : Waypoints)
	{
		const FixVec2 W = Nav.NearestWalkable(Move, Nav.ClampToWorld(Raw, fx::Int(2)), 8);
		if (Nav.LineWalkable(Move, From, W))
		{
			Sq.path.push_back(W);
		}
		else if (Nav.FindPath(Move, From, W, Segment))
		{
			Sq.path.insert(Sq.path.end(), Segment.begin(), Segment.end());
		}
		else
		{
			continue;
		}
		From = Sq.path.back();
	}
	if (Sq.path.empty())
	{
		Sq.path.push_back(Sq.leader);
	}
}

bool BattleSimulation::MoveLeaderAlongPath(Squad& Sq, Fix64 Speed)
{
	if (Sq.pathIndex >= Sq.path.size())
	{
		return true;
	}
	Fix64 Budget = Speed * kDt;
	while (Sq.pathIndex < Sq.path.size() && Budget > Fix64::Zero())
	{
		const FixVec2 Wp = Sq.path[Sq.pathIndex];
		const FixVec2 Delta = Wp - Sq.leader;
		const Fix64 Dist = Delta.Length();
		if (Dist.raw > 0)
		{
			Sq.facing = fx::RotateToward(Sq.facing, Delta / Dist, SquadTurnCos, SquadTurnSin);
		}
		if (Dist <= Budget)
		{
			Sq.leader = Wp;
			Budget -= Dist;
			++Sq.pathIndex;
		}
		else
		{
			Sq.leader += Delta * (Budget / Dist);
			Budget = Fix64::Zero();
		}
	}
	Sq.moving = true;
	return Sq.pathIndex >= Sq.path.size();
}

Fix64 BattleSimulation::SquadMoveSpeed(const Squad& Sq) const
{
	const RulesDef& R = ContentRef.rules;
	const TroopDef& TD = TroopOf(Sq);
	Fix64 Speed = Sq.speed * Nav.SpeedMult(TD.move, Sq.leader, R);
	Speed = Speed * (Fix64::One() + SquadStat(Sq, StatType::MoveSpeedPct));
	if (Sq.formation == Formation::Column)
	{
		Speed = Speed * R.columnSpeedMult;
	}
	else if (Sq.formation == Formation::Square)
	{
		Speed = Speed * R.squareSpeedMult;
	}
	if (Sq.moraleState == MoraleState::Wavering)
	{
		Speed = Speed * R.waveringSpeedMult;
	}
	if (Tick < Sq.chargeUntil || Sq.order == OrderType::Retreat)
	{
		Speed = Speed * (Fix64::One() + R.chargeCommandSpeedBonus);
	}
	// The leader walks slightly slower than its soldiers so the formation can keep up.
	Speed = Speed * fx::Pct(90);
	if (Sq.lag > fx::Int(3))
	{
		Speed = Speed / 2;
	}
	return Speed;
}

int32_t BattleSimulation::ResolveSquadTarget(const Squad& Sq) const
{
	if (Sq.order == OrderType::Attack)
	{
		return Sq.orderTarget;
	}
	if (Sq.order == OrderType::None || Sq.order == OrderType::Skirmish)
	{
		return Sq.autoTarget;
	}
	return 0;
}

// --- Squad orders ----------------------------------------------------------------

void BattleSimulation::UpdateSquads()
{
	for (Squad& Sq : SquadsList)
	{
		UpdateSquadMovement(Sq);
	}
}

void BattleSimulation::UpdateSquadMovement(Squad& Sq)
{
	if (!Sq.Active())
	{
		return;
	}
	const RulesDef& R = ContentRef.rules;
	const TroopDef& TD = TroopOf(Sq);
	Sq.moving = false;

	// Routing: flee to the own edge, ignoring orders.
	if (Sq.Routing())
	{
		const FixVec2 Goal = OwnEdgePoint(Sq.side, Sq.leader);
		if (Sq.path.empty() || Tick >= Sq.repathTick)
		{
			SetSquadPathTo(Sq, Goal);
			Sq.repathTick = Tick + kTicksPerSecond;
		}
		MoveLeaderAlongPath(Sq, Sq.speed * Nav.SpeedMult(TD.move, Sq.leader, R));
		if (fx::Abs(Sq.leader.z - Goal.z) <= fx::Int(3))
		{
			LeaveField(Sq);
		}
		return;
	}

	// Melee lock: the leader follows the fight.
	if (Sq.InMelee() && !TD.ranged && Sq.order != OrderType::CycleWithdraw)
	{
		Sq.leader = Sq.leader + (Sq.centroid - Sq.leader) * fx::Pct(10);
		const bool bCycles = TD.IsCavalry() && (Sq.stance == Stance::Skirmish || Sq.autoControl || Sides[SideIndex(Sq.side)].aiProfile >= 0);
		if (bCycles && Sq.meleeStartTick >= 0 && Tick - Sq.meleeStartTick >= R.cycleChargeMeleeTicks)
		{
			FixVec2 EnemyPos = Sq.centroid + Sq.facing;
			int32_t EnemyId = 0;
			if (!Sq.engagedSquads.empty())
			{
				const Squad& E = SquadsList[static_cast<size_t>(Sq.engagedSquads.front())];
				EnemyPos = E.centroid;
				EnemyId = E.id;
			}
			FixVec2 Away = (Sq.centroid - EnemyPos).Normalized();
			if (Away.IsZero())
			{
				Away = -Sq.facing;
			}
			const int32_t Current = ResolveSquadTarget(Sq);
			Sq.withdrawTarget = Current != 0 ? Current : EnemyId;
			Sq.order = OrderType::CycleWithdraw;
			SetSquadPathTo(Sq, Nav.ClampToWorld(Sq.centroid + Away * R.cycleChargeDistance, fx::Int(4)));
			Sq.meleeStartTick = -1;
		}
		return;
	}

	// Ranged squads in Skirmish stance kite away from approaching melee.
	if (TD.ranged && Sq.stance == Stance::Skirmish && Sq.order != OrderType::Skirmish && Sq.order != OrderType::Retreat && Tick % 10 == 0)
	{
		for (const Squad& E : SquadsList)
		{
			if (E.side == Sq.side || !E.Active() || TroopOf(E).ranged || !IsSquadVisibleTo(Sq.side, E.id - 1))
			{
				continue;
			}
			if (fx::Dist(E.centroid, Sq.centroid) - E.radius - Sq.radius <= R.skirmishTrigger)
			{
				FixVec2 Away = (Sq.centroid - E.centroid).Normalized();
				if (Away.IsZero())
				{
					Away = -Sq.facing;
				}
				Sq.order = OrderType::Skirmish;
				SetSquadPathTo(Sq, Nav.ClampToWorld(Sq.leader + Away * R.skirmishRetreat, fx::Int(4)));
				break;
			}
		}
	}

	switch (Sq.order)
	{
	case OrderType::Move:
	case OrderType::Retreat:
	case OrderType::Skirmish:
	case OrderType::CycleWithdraw:
	{
		const OrderType Was = Sq.order;
		if (MoveLeaderAlongPath(Sq, SquadMoveSpeed(Sq)))
		{
			Sq.path.clear();
			Sq.pathIndex = 0;
			Sq.anchor = Sq.leader;
			if (Was == OrderType::Move && Sq.attackAfterPath && UnitAlive(Sq.orderTarget))
			{
				Sq.order = OrderType::Attack;
				Sq.attackAfterPath = false;
				Sq.repathTick = Tick;
			}
			else if (Was == OrderType::CycleWithdraw && UnitAlive(Sq.withdrawTarget))
			{
				Sq.order = OrderType::Attack;
				Sq.orderTarget = Sq.withdrawTarget;
				Sq.repathTick = Tick;
			}
			else
			{
				Sq.order = OrderType::None;
				Sq.orderTarget = 0;
				Sq.attackAfterPath = false;
			}
		}
		return;
	}
	case OrderType::Attack:
	{
		if (!UnitAlive(Sq.orderTarget))
		{
			Sq.order = OrderType::None;
			Sq.orderTarget = 0;
			Sq.path.clear();
			Sq.anchor = Sq.leader;
			return;
		}
		break;
	}
	case OrderType::None:
	default:
	{
		if (Sq.stance == Stance::Hold)
		{
			// Hold: no movement; ranged units shoot anything in range.
			if (TD.ranged && Tick >= Sq.retargetTick)
			{
				Sq.retargetTick = Tick + 15;
				Sq.autoTarget = 0;
				Fix64 Best;
				for (const Squad& E : SquadsList)
				{
					if (E.side != Sq.side && E.Active() && IsSquadVisibleTo(Sq.side, E.id - 1))
					{
						const Fix64 D = fx::Dist(E.centroid, Sq.leader);
						if (D <= Sq.range + E.radius && (Sq.autoTarget == 0 || D < Best))
						{
							Sq.autoTarget = E.id;
							Best = D;
						}
					}
				}
				for (const Titan& T : TitansList)
				{
					if (T.side != Sq.side && T.Alive() && Sq.autoTarget == 0 && fx::Dist(T.pos, Sq.leader) <= Sq.range + T.radius)
					{
						Sq.autoTarget = T.id;
					}
				}
			}
			return;
		}
		if (Tick >= Sq.retargetTick)
		{
			Sq.retargetTick = Tick + 15;
			const Fix64 SearchRadius = TD.ranged ? Sq.range + fx::Int(10) : R.aggressiveRadius;
			int32_t Best = 0;
			Fix64 BestScore;
			for (const Squad& E : SquadsList)
			{
				if (E.side == Sq.side || !E.Active() || !IsSquadVisibleTo(Sq.side, E.id - 1))
				{
					continue;
				}
				if (fx::Dist(E.centroid, Sq.leader) - E.radius > SearchRadius || fx::Dist(E.centroid, Sq.anchor) > R.leashRadius + SearchRadius)
				{
					continue;
				}
				const Fix64 Score = TargetScore(Sq, E.id, nullptr);
				if (Best == 0 || Score > BestScore)
				{
					Best = E.id;
					BestScore = Score;
				}
			}
			for (const Titan& T : TitansList)
			{
				if (T.side == Sq.side || !T.Alive() || fx::Dist(T.pos, Sq.leader) - T.radius > SearchRadius)
				{
					continue;
				}
				const Fix64 Score = TargetScore(Sq, T.id, nullptr);
				if (Best == 0 || Score > BestScore)
				{
					Best = T.id;
					BestScore = Score;
				}
			}
			if (Best != Sq.autoTarget)
			{
				Sq.autoTarget = Best;
				Sq.repathTick = Tick;
			}
		}
		if (Sq.autoTarget != 0 && !UnitAlive(Sq.autoTarget))
		{
			Sq.autoTarget = 0;
		}
		if (Sq.autoTarget == 0)
		{
			if (fx::Dist(Sq.leader, Sq.anchor) > fx::Int(2))
			{
				if (Sq.path.empty())
				{
					SetSquadPathTo(Sq, Sq.anchor);
				}
				if (MoveLeaderAlongPath(Sq, SquadMoveSpeed(Sq)))
				{
					Sq.path.clear();
				}
			}
			return;
		}
		if (fx::Dist(Sq.leader, Sq.anchor) > R.leashRadius)
		{
			Sq.autoTarget = 0;
			Sq.path.clear();
			return;
		}
		break;
	}
	}

	// Pursue the current target (Attack order or auto target).
	const int32_t Target = ResolveSquadTarget(Sq);
	if (Target == 0)
	{
		return;
	}
	const FixVec2 TargetPos = UnitPosition(Target);
	const Fix64 TargetRadius = IsTitanId(Target) ? TitansList[static_cast<size_t>(TitanIndexFromId(Target))].radius : SquadsList[static_cast<size_t>(SquadIndexFromId(Target))].radius;
	const Fix64 Dist = fx::Dist(Sq.leader, TargetPos) - TargetRadius;
	if (TD.ranged)
	{
		Fix64 Range = Sq.range;
		if (Nav.HeightAt(Sq.leader) >= Nav.HeightAt(TargetPos) + R.elevationThreshold)
		{
			Range = Range * (Fix64::One() + R.elevationRangeBonus);
		}
		if (Dist <= Range - fx::Int(2))
		{
			Sq.path.clear();
			const FixVec2 ToTarget = (TargetPos - Sq.leader).Normalized();
			Sq.facing = fx::RotateToward(Sq.facing, ToTarget, SquadTurnCos, SquadTurnSin);
			return;
		}
	}
	if (Sq.path.empty() || Tick >= Sq.repathTick)
	{
		SetSquadPathTo(Sq, TargetPos);
		Sq.repathTick = Tick + 15;
	}
	if (MoveLeaderAlongPath(Sq, SquadMoveSpeed(Sq)))
	{
		Sq.path.clear();
	}
}

// --- Soldiers --------------------------------------------------------------------

void BattleSimulation::UpdateSoldiers()
{
	std::vector<Fix64> LagSum(SquadsList.size());
	for (size_t I = 0; I < SoldiersList.size(); ++I)
	{
		UpdateSoldierMotion(SoldiersList[I], static_cast<int>(I));
	}
	PushSoldiersFromTitans();
	for (size_t I = 0; I < SoldiersList.size(); ++I)
	{
		const Soldier& S = SoldiersList[I];
		if (S.Alive())
		{
			const Squad& Sq = SquadsList[static_cast<size_t>(S.squad)];
			LagSum[static_cast<size_t>(S.squad)] += fx::Dist(S.pos, SlotWorld(Sq, S.slot));
		}
	}
	for (size_t I = 0; I < SquadsList.size(); ++I)
	{
		Squad& Sq = SquadsList[I];
		RefreshSquadGeometry(Sq);
		Sq.lag = Sq.aliveCount > 0 ? LagSum[I] / fx::Int(Sq.aliveCount) : Fix64::Zero();
	}
}

void BattleSimulation::UpdateSoldierMotion(Soldier& S, int Index)
{
	if (!S.Alive())
	{
		return;
	}
	if (S.state == SoldierState::Airborne)
	{
		UpdateAirborne(S, Index);
		return;
	}
	if (S.state == SoldierState::KnockedDown)
	{
		S.vel = FixVec2();
		if (--S.stateTicks <= 0)
		{
			S.state = SoldierState::Idle;
		}
		return;
	}
	if (S.stunTicks > 0)
	{
		S.vel = FixVec2();
		return;
	}
	const RulesDef& R = ContentRef.rules;
	const Squad& Sq = SquadsList[static_cast<size_t>(S.squad)];
	const TroopDef& TD = TroopOf(Sq);
	const FixVec2 Slot = SlotWorld(Sq, S.slot);
	FixVec2 Desired = Slot;
	FixVec2 LookAt;
	bool bLook = false;

	if (!Sq.Routing() && S.targetSoldier >= 0)
	{
		const Soldier& T = SoldiersList[static_cast<size_t>(S.targetSoldier)];
		const Fix64 Reach = Sq.range + TD.collisionRadius * 2;
		const FixVec2 To = T.pos - S.pos;
		const Fix64 D = To.Length();
		Desired = D > Reach * fx::Pct(90) && D.raw > 0 ? T.pos - To / D * (Reach * fx::Pct(80)) : S.pos;
		LookAt = T.pos;
		bLook = true;
	}
	else if (!Sq.Routing() && S.targetTitan >= 0)
	{
		const Titan& T = TitansList[static_cast<size_t>(S.targetTitan)];
		FixVec2 Dir = (S.pos - T.pos).Normalized();
		if (Dir.IsZero())
		{
			Dir = -Sq.facing;
		}
		Desired = TD.ranged ? S.pos : T.pos + Dir * (T.radius + fx::Milli(1200));
		LookAt = T.pos;
		bLook = true;
	}
	if (Sq.stance == Stance::Hold && !Sq.Routing() && fx::Dist(Desired, Slot) > R.holdRadius)
	{
		Desired = Slot;
	}

	Fix64 MaxSpeed = Sq.speed * Nav.SpeedMult(TD.move, S.pos, R) * (Fix64::One() + SquadStat(Sq, StatType::MoveSpeedPct));
	if (Tick < Sq.chargeUntil || Sq.Routing() || Sq.order == OrderType::Retreat)
	{
		MaxSpeed = MaxSpeed * (Fix64::One() + R.chargeCommandSpeedBonus);
	}
	if (Sq.formation == Formation::Square)
	{
		MaxSpeed = MaxSpeed * R.squareSpeedMult;
	}

	const FixVec2 To = Desired - S.pos;
	const Fix64 D = To.Length();
	FixVec2 DesiredVel;
	if (D > fx::Milli(50))
	{
		const Fix64 Speed = fx::Min(MaxSpeed * fx::Milli(1150), D * 3);
		DesiredVel = To / D * Speed;
	}

	// Separation from nearby friendly soldiers.
	const Fix64 SepRadius = TD.collisionRadius * 2;
	QuerySoldiers(S.pos, SepRadius, QueryScratch);
	FixVec2 Push;
	for (const int32_t N : QueryScratch)
	{
		if (N == Index)
		{
			continue;
		}
		const Soldier& O = SoldiersList[static_cast<size_t>(N)];
		if (!O.Alive() || O.state == SoldierState::Airborne)
		{
			continue;
		}
		const FixVec2 Away = S.pos - O.pos;
		const Fix64 Dist = Away.Length();
		if (Dist.raw <= 0)
		{
			// Deterministic tie-break for overlapping soldiers.
			Push += FixVec2((Index > N) ? Fix64::One() : -Fix64::One(), Fix64::Zero());
			continue;
		}
		if (Dist < SepRadius)
		{
			Push += Away / Dist * ((SepRadius - Dist) * 4);
		}
	}
	DesiredVel += Push;
	DesiredVel = DesiredVel.ClampedLength(MaxSpeed * fx::Milli(1300));

	// Acceleration limit (6 m/s^2, faster deceleration).
	const FixVec2 Dv = DesiredVel - S.vel;
	const Fix64 MaxDv = fx::Milli(200);
	S.vel += Dv.ClampedLength(DesiredVel.LengthSq() < S.vel.LengthSq() ? MaxDv * 2 : MaxDv);

	const FixVec2 Next = S.pos + S.vel * kDt;
	// A soldier standing on a newly blocked cell (e.g. under a fallen titan) may always walk out.
	if (Nav.WalkableAt(TD.move, Next) || (!Nav.WalkableAt(TD.move, S.pos) && Nav.InWorld(Next)))
	{
		S.pos = Next;
	}
	else if (Nav.WalkableAt(TD.move, FixVec2(Next.x, S.pos.z)))
	{
		S.pos.x = Next.x;
		S.vel.z = Fix64::Zero();
	}
	else if (Nav.WalkableAt(TD.move, FixVec2(S.pos.x, Next.z)))
	{
		S.pos.z = Next.z;
		S.vel.x = Fix64::Zero();
	}
	else
	{
		S.vel = FixVec2();
	}

	const Fix64 SpeedSq = S.vel.LengthSq();
	if (bLook)
	{
		const FixVec2 Dir = (LookAt - S.pos).Normalized();
		if (!Dir.IsZero())
		{
			S.facing = Dir;
		}
	}
	else if (SpeedSq > fx::Milli(90))
	{
		S.facing = S.vel.Normalized();
	}
	else
	{
		S.facing = Sq.facing;
	}
	S.state = SpeedSq > fx::Milli(90) ? SoldierState::Moving : (S.targetSoldier >= 0 || S.targetTitan >= 0 ? SoldierState::Fighting : SoldierState::Idle);
	if (TD.chargeMult > Fix64::Zero())
	{
		const Fix64 Threshold = R.chargeSpeedThreshold;
		if (SpeedSq >= Threshold * Threshold)
		{
			S.chargeArmed = true;
		}
	}
}

void BattleSimulation::UpdateAirborne(Soldier& S, int Index)
{
	const RulesDef& R = ContentRef.rules;
	const TroopDef& TD = TroopOf(SquadsList[static_cast<size_t>(S.squad)]);
	const FixVec2 Next = S.pos + S.airVel * kDt;
	if (Nav.WalkableAt(TD.move, Next))
	{
		S.pos = Next;
	}
	else
	{
		S.airVel = FixVec2();
	}
	S.height += S.vy * kDt;
	S.vy -= R.gravity * kDt;
	++S.stateTicks;
	if (S.height <= Fix64::Zero() && S.vy < Fix64::Zero())
	{
		S.height = Fix64::Zero();
		S.vy = Fix64::Zero();
		S.airVel = FixVec2();
		const int AirTicks = S.stateTicks;
		S.state = SoldierState::KnockedDown;
		S.stateTicks = R.knockdownTicks;
		if (AirTicks > R.airborneDamageTicks)
		{
			DamageSoldier(Index, S.maxHp * R.landingDamagePct, Opponent(S.side), -1);
		}
		// Landing on others knocks them down too (no damage).
		QuerySoldiers(S.pos, Fix64::One(), QueryScratch);
		const std::vector<int32_t> Hit = QueryScratch;
		for (const int32_t N : Hit)
		{
			if (N != Index && SoldiersList[static_cast<size_t>(N)].CanAct())
			{
				KnockDown(N, R.knockdownTicks / 2);
			}
		}
	}
}

void BattleSimulation::Knockback(int SoldierIndex, FixVec2 Direction, Fix64 Horizontal, Fix64 Vertical)
{
	Soldier& S = SoldiersList[static_cast<size_t>(SoldierIndex)];
	if (!S.Alive())
	{
		return;
	}
	const Fix64 Mult = TroopOf(SquadsList[static_cast<size_t>(S.squad)]).knockbackMult;
	FixVec2 Dir = Direction.Normalized();
	if (Dir.IsZero())
	{
		Dir = -S.facing;
	}
	const Fix64 Vy = Vertical * Mult;
	if (Vy < fx::Milli(800))
	{
		S.pos = S.pos + Dir * (Horizontal * Mult * fx::Pct(15));
		KnockDown(SoldierIndex, ContentRef.rules.knockdownTicks);
		return;
	}
	S.state = SoldierState::Airborne;
	S.stateTicks = 0;
	S.height = fx::Milli(50);
	S.vy = Vy;
	S.airVel = Dir * (Horizontal * Mult);
	S.vel = FixVec2();
	S.targetSoldier = -1;
	S.targetTitan = -1;
}

void BattleSimulation::KnockDown(int SoldierIndex, int Ticks)
{
	Soldier& S = SoldiersList[static_cast<size_t>(SoldierIndex)];
	if (!S.Alive() || S.state == SoldierState::Airborne)
	{
		return;
	}
	S.state = SoldierState::KnockedDown;
	S.stateTicks = std::max(S.stateTicks, Ticks);
	S.vel = FixVec2();
}

void BattleSimulation::LeaveField(Squad& Sq)
{
	SideState& State = Sides[SideIndex(Sq.side)];
	for (const int32_t M : Sq.members)
	{
		Soldier& S = SoldiersList[static_cast<size_t>(M)];
		if (S.Alive())
		{
			S.state = SoldierState::Left;
			++Sq.leftCount;
			State.destroyedPower += Sq.powerPerSoldier;
		}
	}
	Sq.aliveCount = 0;
	Sq.leftField = true;
	Emit(SimEventType::SquadLeftField, Sq.side, Sq.id, 0, Sq.leader);
}

// --- Spatial hash ------------------------------------------------------------------

void BattleSimulation::BuildSpatialHash()
{
	const size_t Cells = static_cast<size_t>(HashX * HashZ);
	HashStart.assign(Cells + 1, 0);
	std::vector<int32_t> CellOf(SoldiersList.size(), -1);
	for (size_t I = 0; I < SoldiersList.size(); ++I)
	{
		const Soldier& S = SoldiersList[I];
		if (!S.Alive())
		{
			continue;
		}
		const int Cx = std::max(0, std::min(HashX - 1, static_cast<int>((S.pos.x / HashCell).FloorToInt())));
		const int Cz = std::max(0, std::min(HashZ - 1, static_cast<int>((S.pos.z / HashCell).FloorToInt())));
		const int C = Cz * HashX + Cx;
		CellOf[I] = C;
		++HashStart[static_cast<size_t>(C) + 1];
	}
	for (size_t C = 0; C < Cells; ++C)
	{
		HashStart[C + 1] += HashStart[C];
	}
	HashItems.assign(static_cast<size_t>(HashStart[Cells]), -1);
	std::vector<int32_t> Fill(HashStart.begin(), HashStart.end() - 1);
	for (size_t I = 0; I < SoldiersList.size(); ++I)
	{
		if (CellOf[I] >= 0)
		{
			HashItems[static_cast<size_t>(Fill[static_cast<size_t>(CellOf[I])]++)] = static_cast<int32_t>(I);
		}
	}
}

void BattleSimulation::QuerySoldiers(FixVec2 Center, Fix64 Radius, std::vector<int32_t>& Out) const
{
	Out.clear();
	const int X0 = std::max(0, static_cast<int>(((Center.x - Radius) / HashCell).FloorToInt()));
	const int X1 = std::min(HashX - 1, static_cast<int>(((Center.x + Radius) / HashCell).FloorToInt()));
	const int Z0 = std::max(0, static_cast<int>(((Center.z - Radius) / HashCell).FloorToInt()));
	const int Z1 = std::min(HashZ - 1, static_cast<int>(((Center.z + Radius) / HashCell).FloorToInt()));
	const Fix64 R2 = Radius * Radius;
	for (int Cz = Z0; Cz <= Z1; ++Cz)
	{
		for (int Cx = X0; Cx <= X1; ++Cx)
		{
			const size_t C = static_cast<size_t>(Cz * HashX + Cx);
			for (int32_t K = HashStart[C]; K < HashStart[C + 1]; ++K)
			{
				const int32_t I = HashItems[static_cast<size_t>(K)];
				if (fx::DistSq(SoldiersList[static_cast<size_t>(I)].pos, Center) <= R2)
				{
					Out.push_back(I);
				}
			}
		}
	}
}
} // namespace dot
