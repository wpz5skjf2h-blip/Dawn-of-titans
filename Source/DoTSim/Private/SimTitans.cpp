// Source/DoTSim/Private/SimTitans.cpp
// Titans: movement, targeting, combo attacks, built-in actions, knockback, poise/stagger,
// death and fall line, the Titan Clash, abilities, spells, zones (Section 4.11 / 4.12).
#include "DoTSim/BattleSimulation.h"

#include <algorithm>
#include <cstdlib>

namespace dot
{
namespace
{
const Fix64 kDt = Fix64::FromRaw(Fix64::kOneRaw / kTicksPerSecond);

Fix64 PointSegmentDistance(FixVec2 P, FixVec2 A, FixVec2 B)
{
	const FixVec2 AB = B - A;
	const Fix64 LenSq = AB.LengthSq();
	if (LenSq.raw <= 0)
	{
		return fx::Dist(P, A);
	}
	const Fix64 T = fx::Clamp01((P - A).Dot(AB) / LenSq);
	return fx::Dist(P, A + AB * T);
}
} // namespace

// --- Update loop ------------------------------------------------------------------

void BattleSimulation::UpdateTitans()
{
	TryStartClash();
	UpdateClash();
	for (size_t I = 0; I < TitansList.size(); ++I)
	{
		UpdateTitan(TitansList[I], static_cast<int>(I));
	}
	SeparateTitans();
}

void BattleSimulation::UpdateTitan(Titan& T, int Index)
{
	const RulesDef& R = ContentRef.rules;
	if (T.dead)
	{
		return;
	}
	T.rage = fx::Min(fx::Int(100), T.rage + R.rageRegenPerSec / kTicksPerSecond);
	if (Tick - T.lastHitTick >= R.poiseRegenDelayTicks && T.poiseDamage > Fix64::Zero())
	{
		T.poiseDamage = fx::Max(Fix64::Zero(), T.poiseDamage - T.maxPoise * R.poiseRegenPctPerSec / kTicksPerSecond);
	}
	if (T.clashIndex >= 0)
	{
		T.state = TitanState::Clashing;
		return;
	}
	if (Tick < T.staggerUntil)
	{
		T.state = TitanState::Staggered;
		return;
	}
	if (T.state == TitanState::Staggered)
	{
		T.state = TitanState::Idle;
		T.staggerTakenMult = Fix64::Zero();
	}
	if (Tick < T.stunUntil)
	{
		return;
	}

	switch (T.state)
	{
	case TitanState::Leaping:
	{
		const int32_t Total = std::max(1, T.leapEnd - T.leapStart);
		const Fix64 Alpha = fx::Clamp01(fx::Int(Tick - T.leapStart) / fx::Int(Total));
		T.pos = FixVec2(fx::Lerp(T.leapFrom.x, T.leapTo.x, Alpha), fx::Lerp(T.leapFrom.z, T.leapTo.z, Alpha));
		if (Tick >= T.leapEnd)
		{
			T.pos = T.leapTo;
			T.state = TitanState::Idle;
			const int Level = TitanDefOf(T).abilities[1] == T.leapAbility ? T.abilityLevel[1] : T.abilityLevel[0];
			if (ContentRef.abilities[static_cast<size_t>(T.leapAbility)].shape == TargetShape::Line)
			{
				// A rush: everything along the path from the start of the leap is hit.
				FixVec2 Dir = (T.leapTo - T.leapFrom).Normalized();
				ApplyEffects(T.leapAbility, T.side, Index, T.leapFrom, T.pos, Dir.IsZero() ? T.facing : Dir, Level, false);
			}
			else
			{
				ApplyEffects(T.leapAbility, T.side, Index, T.pos, T.pos, T.facing, Level, false);
			}
			Emit(SimEventType::AbilityImpact, T.side, T.id, T.leapAbility, T.pos);
		}
		return;
	}
	case TitanState::Casting:
		if (Tick >= T.actionHitTick)
		{
			T.state = TitanState::Idle;
			ExecuteAbility(Index, T.castingAbility, T.castPoint);
			T.castingAbility = -1;
		}
		return;
	case TitanState::Attacking:
	case TitanState::Stomping:
	case TitanState::Grabbing:
		if (Tick == T.actionHitTick)
		{
			TitanApplyAttackHit(T, Index);
		}
		if (Tick >= T.actionEndTick)
		{
			T.state = TitanState::Idle;
		}
		return;
	default: break;
	}

	if (TitanTryAutoActions(T, Index))
	{
		return;
	}

	// Orders.
	if (T.order == OrderType::Attack && !UnitAlive(T.orderTarget))
	{
		T.order = OrderType::None;
		T.orderTarget = 0;
	}
	if (T.order == OrderType::Move)
	{
		if (T.pathIndex >= T.path.size())
		{
			T.path.clear();
			T.pathIndex = 0;
			if (T.orderTarget != 0 && UnitAlive(T.orderTarget))
			{
				T.order = OrderType::Attack;
			}
			else
			{
				T.order = OrderType::None;
				T.orderTarget = 0;
			}
			T.state = TitanState::Idle;
			return;
		}
		const FixVec2 Wp = T.path[T.pathIndex];
		if (fx::Dist(Wp, T.pos) <= fx::Milli(1500))
		{
			++T.pathIndex;
			return;
		}
		TitanMove(T, Index, Wp, false, Fix64::Zero());
		return;
	}

	int32_t Target = 0;
	if (T.order == OrderType::Attack)
	{
		Target = T.orderTarget;
	}
	else
	{
		if (Tick >= T.retargetTick || !UnitAlive(T.autoTarget))
		{
			T.retargetTick = Tick + 15;
			T.autoTarget = ChooseTitanTarget(T, Index);
		}
		Target = T.autoTarget;
	}
	if (Target == 0)
	{
		T.state = TitanState::Idle;
		return;
	}

	const FixVec2 TargetPos = UnitPosition(Target);
	if (IsTitanId(Target))
	{
		const Titan& Other = TitansList[static_cast<size_t>(TitanIndexFromId(Target))];
		const Fix64 Reach = T.rangedAttack ? T.attackRange + Other.radius : T.radius + Other.radius + T.sweepRadius / 2;
		const Fix64 Dist = fx::Dist(T.pos, Other.pos);
		if (Dist <= Reach)
		{
			const FixVec2 Dir = (Other.pos - T.pos).Normalized();
			T.facing = fx::RotateToward(T.facing, Dir, T.turnCos, T.turnSin);
			if (Tick >= T.attackReadyTick && T.facing.Dot(Dir) >= T.sweepHalfArcCos)
			{
				TitanBasicAttack(T, Index, Target);
			}
			else
			{
				T.state = TitanState::Idle;
			}
			return;
		}
		TitanMove(T, Index, Other.pos, true, Reach);
		return;
	}

	// Squad target: attack whatever enemy soldiers are in the sweep, otherwise close in.
	const Fix64 Reach = T.rangedAttack ? T.attackRange : T.radius + T.sweepRadius;
	if (T.rangedAttack)
	{
		const Fix64 Dist = fx::Dist(T.pos, TargetPos);
		if (Dist <= Reach)
		{
			const FixVec2 Dir = (TargetPos - T.pos).Normalized();
			T.facing = fx::RotateToward(T.facing, Dir, T.turnCos, T.turnSin);
			if (Tick >= T.attackReadyTick)
			{
				TitanBasicAttack(T, Index, Target);
			}
			return;
		}
		TitanMove(T, Index, TargetPos, true, Reach);
		return;
	}
	QuerySoldiers(T.pos, Reach, QueryScratch);
	bool bEnemyInArc = false;
	bool bEnemyInReach = false;
	for (const int32_t N : QueryScratch)
	{
		const Soldier& S = SoldiersList[static_cast<size_t>(N)];
		if (S.side == T.side || !S.Alive())
		{
			continue;
		}
		bEnemyInReach = true;
		const FixVec2 Dir = (S.pos - T.pos).Normalized();
		if (T.facing.Dot(Dir) >= T.sweepHalfArcCos)
		{
			bEnemyInArc = true;
			break;
		}
	}
	if (bEnemyInArc)
	{
		if (Tick >= T.attackReadyTick)
		{
			TitanBasicAttack(T, Index, Target);
		}
		return;
	}
	if (bEnemyInReach)
	{
		const FixVec2 Dir = (TargetPos - T.pos).Normalized();
		T.facing = fx::RotateToward(T.facing, Dir, T.turnCos, T.turnSin);
		T.state = TitanState::Idle;
		return;
	}
	TitanMove(T, Index, TargetPos, true, T.radius + T.sweepRadius / 2);
}

void BattleSimulation::TitanMove(Titan& T, int Index, FixVec2 Goal, bool bStopWithinReach, Fix64 Reach)
{
	const RulesDef& R = ContentRef.rules;
	const FixVec2 Delta = Goal - T.pos;
	const Fix64 Dist = Delta.Length();
	if (Dist <= fx::Milli(300) || (bStopWithinReach && Dist <= Reach))
	{
		T.state = TitanState::Idle;
		return;
	}
	const FixVec2 Desired = Delta / Dist;
	T.facing = fx::RotateToward(T.facing, Desired, T.turnCos, T.turnSin);
	const Fix64 Alignment = T.facing.Dot(Desired);
	Fix64 Factor = Fix64::One();
	if (Alignment < Fix64::Zero())
	{
		Factor = fx::Pct(25);
	}
	else if (Alignment < fx::Pct(70))
	{
		Factor = fx::Pct(50);
	}
	Fix64 Speed = T.moveSpeed * Nav.SpeedMult(MoveClass::Titan, T.pos, R) * (Fix64::One() + TitanStat(T, StatType::MoveSpeedPct)) * Factor;
	if (Tick < T.chargeUntil)
	{
		Speed = Speed * (Fix64::One() + R.titanChargeSpeedBonus);
	}
	Fix64 Step = Speed * kDt;
	if (bStopWithinReach)
	{
		Step = fx::Min(Step, fx::Max(Fix64::Zero(), Dist - Reach + fx::Milli(100)));
	}
	else
	{
		Step = fx::Min(Step, Dist);
	}
	const FixVec2 Next = Nav.ClampToWorld(T.pos + Desired * Step, fx::Int(5));
	const Fix64 Moved = fx::Dist(Next, T.pos);
	T.pos = Next;
	T.state = TitanState::Moving;
	T.stepAccum += Moved;
	while (T.stepAccum >= T.stepLength)
	{
		T.stepAccum -= T.stepLength;
		TitanFootstep(T, Index);
	}
	if (Tick < T.chargeUntil)
	{
		TitanTrample(T, Index);
	}
}

int32_t BattleSimulation::ChooseTitanTarget(const Titan& T, int Index) const
{
	(void)Index;
	const bool bSeekAnywhere = T.autoControl || Sides[SideIndex(T.side)].aiProfile >= 0;
	const Fix64 MaxRange = bSeekAnywhere ? fx::Int(1000) : fx::Int(30);
	int32_t Best = 0;
	Fix64 BestScore;
	Fix64 CurrentScore = fx::Int(-1000);
	for (const Titan& O : TitansList)
	{
		if (O.side == T.side || !O.Alive())
		{
			continue;
		}
		const Fix64 D = fx::Dist(O.pos, T.pos) - O.radius - T.radius;
		if (D > MaxRange)
		{
			continue;
		}
		Fix64 Score = D <= fx::Int(15) ? fx::Int(100) : fx::Int(50) - D / 5;
		if (O.id == T.autoTarget)
		{
			CurrentScore = Score;
		}
		if (Best == 0 || Score > BestScore)
		{
			Best = O.id;
			BestScore = Score;
		}
	}
	std::vector<int> Attackers(SquadsList.size(), 0);
	const int MyIndex = T.id - kTitanIdBase;
	for (const Soldier& S : SoldiersList)
	{
		if (S.Alive() && S.targetTitan == MyIndex)
		{
			++Attackers[static_cast<size_t>(S.squad)];
		}
	}
	for (size_t I = 0; I < SquadsList.size(); ++I)
	{
		const Squad& Sq = SquadsList[I];
		if (Sq.side == T.side || !Sq.Active() || !IsSquadVisibleTo(T.side, static_cast<int>(I)))
		{
			continue;
		}
		const Fix64 D = fx::Dist(Sq.centroid, T.pos) - Sq.radius - T.radius;
		if (D > MaxRange)
		{
			continue;
		}
		Fix64 Score;
		if (Attackers[I] > 0)
		{
			Score = fx::Int(60) + fx::Int(Attackers[I]) * 2;
		}
		else if (D <= fx::Int(20))
		{
			Score = fx::Int(2 * Sq.aliveCount);
		}
		else
		{
			Score = fx::Int(10) + fx::Int(Sq.aliveCount) / 4 - D / 10;
		}
		if (Sq.id == T.autoTarget)
		{
			CurrentScore = Score;
		}
		if (Best == 0 || Score > BestScore)
		{
			Best = Sq.id;
			BestScore = Score;
		}
	}
	// Hysteresis: keep the current target unless the new one is clearly better.
	if (T.autoTarget != 0 && UnitAlive(T.autoTarget) && Best != T.autoTarget && BestScore < CurrentScore * fx::Milli(1200) + fx::Int(1))
	{
		return T.autoTarget;
	}
	return Best;
}

bool BattleSimulation::TitanTryAutoActions(Titan& T, int Index)
{
	const RulesDef& R = ContentRef.rules;
	(void)Index;
	if (Tick >= T.stompReadyTick)
	{
		QuerySoldiers(T.pos, R.stompTriggerRadius + T.radius, QueryScratch);
		int Count = 0;
		for (const int32_t N : QueryScratch)
		{
			const Soldier& S = SoldiersList[static_cast<size_t>(N)];
			Count += (S.side != T.side && S.Alive()) ? 1 : 0;
		}
		if (Count >= R.stompTriggerCount)
		{
			T.state = TitanState::Stomping;
			T.actionHitTick = Tick + R.stompWindupTicks;
			T.actionEndTick = Tick + R.stompWindupTicks + 12;
			T.stompReadyTick = Tick + R.stompCooldownTicks;
			return true;
		}
	}
	if (!T.rangedAttack && Tick >= T.grabReadyTick)
	{
		QuerySoldiers(T.pos, R.grabRadius + T.radius, QueryScratch);
		int Grabbed = -1;
		Fix64 BestDist;
		for (const int32_t N : QueryScratch)
		{
			const Soldier& S = SoldiersList[static_cast<size_t>(N)];
			if (S.side == T.side || !S.Alive() || !TroopOf(SquadsList[static_cast<size_t>(S.squad)]).IsCavalry())
			{
				continue;
			}
			const Fix64 D = fx::DistSq(S.pos, T.pos);
			if (Grabbed < 0 || D < BestDist)
			{
				Grabbed = N;
				BestDist = D;
			}
		}
		if (Grabbed >= 0)
		{
			const FixVec2 GrabPos = SoldiersList[static_cast<size_t>(Grabbed)].pos;
			KillSoldier(Grabbed, Index);
			int ThrowTarget = -1;
			int BestCount = 0;
			for (size_t I = 0; I < SquadsList.size(); ++I)
			{
				const Squad& Sq = SquadsList[I];
				if (Sq.side != T.side && Sq.Active() && fx::Dist(Sq.centroid, T.pos) <= R.throwRange && Sq.aliveCount > BestCount)
				{
					ThrowTarget = static_cast<int>(I);
					BestCount = Sq.aliveCount;
				}
			}
			T.throwTargetSquad = ThrowTarget;
			T.state = TitanState::Grabbing;
			T.actionHitTick = Tick + 18;
			T.actionEndTick = Tick + 30;
			T.grabReadyTick = Tick + R.grabCooldownTicks;
			Emit(SimEventType::TitanGrab, T.side, T.id, Grabbed, GrabPos);
			return true;
		}
	}
	return false;
}

void BattleSimulation::TitanBasicAttack(Titan& T, int Index, int32_t TargetUnit)
{
	(void)Index;
	const RulesDef& R = ContentRef.rules;
	const Fix64 SpeedMult = fx::Max(Fix64::One() + TitanStat(T, StatType::AttackSpeedPct), fx::Pct(20));
	T.state = TitanState::Attacking;
	T.attackTargetUnit = TargetUnit;
	T.actionHitTick = Tick + R.titanWindupTicks;
	const int32_t Interval = std::max(R.titanWindupTicks + 1, static_cast<int>((fx::Int(T.attackInterval) / SpeedMult).RoundToInt()));
	T.actionEndTick = Tick + std::min(Interval, R.titanWindupTicks + 15);
	T.attackReadyTick = Tick + Interval;
	if (Tick - T.lastAttackTick > R.comboResetTicks)
	{
		T.combo = 0;
	}
	T.lastAttackTick = Tick;
	Emit(SimEventType::TitanAttack, T.side, T.id, TargetUnit, T.pos, T.combo);
}

Fix64 BattleSimulation::TitanDamageToSoldier(const Titan& T, const Soldier& S, Fix64 Coef, bool bArea, bool bCanCrit)
{
	const RulesDef& R = ContentRef.rules;
	const Squad& Sq = SquadsList[static_cast<size_t>(S.squad)];
	const TroopDef& TD = TroopOf(Sq);
	Fix64 Mult = Coef * ContentRef.counters.Get(ContentRef.titanCounterRow, TD.cls);
	if (bCanCrit && Random.Chance(T.critChance))
	{
		Mult = Mult * T.critDamage;
	}
	Mult = Mult * Random.Range(R.varianceMin, R.varianceMax);
	Fix64 Armor = Sq.armor + SquadStat(Sq, StatType::ArmorAdd);
	if (Sq.formation == Formation::Wedge)
	{
		Armor += R.wedgeArmorAdd;
	}
	Armor = fx::Clamp(Armor, Fix64::Zero(), fx::Pct(75));
	if (bArea && Sq.formation == Formation::Loose)
	{
		Mult = Mult * R.looseAreaDamageMult;
	}
	const Fix64 Damage = T.atk * Mult * (Fix64::One() - Armor) * (Fix64::One() + SquadStat(Sq, StatType::DamageTakenPct));
	return fx::Max(Damage, Fix64::One());
}

void BattleSimulation::TitanApplyAttackHit(Titan& T, int Index)
{
	const RulesDef& R = ContentRef.rules;
	std::vector<bool> SquadHit(SquadsList.size(), false);
	auto HitSoldier = [&](int32_t N, Fix64 Coef, FixVec2 KnockDir, Fix64 KH, Fix64 KV)
	{
		const Soldier& S = SoldiersList[static_cast<size_t>(N)];
		const Fix64 Damage = TitanDamageToSoldier(T, S, Coef, true, true);
		SquadHit[static_cast<size_t>(S.squad)] = true;
		Knockback(N, KnockDir, KH, KV);
		DamageSoldier(N, Damage, T.side, Index);
	};

	if (T.state == TitanState::Stomping)
	{
		QuerySoldiers(T.pos, R.stompRadius + T.radius, QueryScratch);
		const std::vector<int32_t> Hits = QueryScratch;
		for (const int32_t N : Hits)
		{
			const Soldier& S = SoldiersList[static_cast<size_t>(N)];
			if (S.side != T.side && S.Alive())
			{
				HitSoldier(N, R.stompCoef, S.pos - T.pos, R.stompKnockH, R.stompKnockV);
			}
		}
		Emit(SimEventType::TitanStomp, T.side, T.id, 0, T.pos);
	}
	else if (T.state == TitanState::Grabbing)
	{
		if (T.throwTargetSquad >= 0 && SquadsList[static_cast<size_t>(T.throwTargetSquad)].Active())
		{
			Projectile P;
			P.kind = ProjectileKind::ThrownSoldier;
			P.side = T.side;
			P.from = T.pos;
			P.fromHeight = T.height;
			P.to = SquadsList[static_cast<size_t>(T.throwTargetSquad)].centroid;
			P.fireTick = Tick;
			P.impactTick = Tick + R.throwFlightTicks;
			P.sourceTitan = Index;
			P.hit = true;
			ProjectilesList.push_back(P);
		}
	}
	else if (T.state == TitanState::Attacking)
	{
		const int32_t Target = T.attackTargetUnit;
		if (IsTitanId(Target) && UnitAlive(Target))
		{
			const int OtherIndex = TitanIndexFromId(Target);
			Titan& Other = TitansList[static_cast<size_t>(OtherIndex)];
			const Fix64 Reach = T.rangedAttack ? T.attackRange + Other.radius : T.radius + Other.radius + T.sweepRadius;
			if (fx::Dist(T.pos, Other.pos) <= Reach + fx::Int(2))
			{
				Fix64 Mult = T.vsTitanMult * R.titanVsTitanMult * Random.Range(R.varianceMin, R.varianceMax);
				if (Random.Chance(T.critChance))
				{
					Mult = Mult * T.critDamage;
				}
				const Fix64 Damage = T.atk * Mult * (Fix64::One() - TitanEffectiveArmor(Other, T.rangedAttack)) * TitanDamageTakenMult(Other);
				DamageTitan(OtherIndex, Damage, Damage * R.poiseFactorTitans, T.pos, Index);
			}
		}
		else if (T.rangedAttack)
		{
			const FixVec2 Center = UnitPosition(Target);
			QuerySoldiers(Center, fx::Max(T.splashRadius, fx::Int(2)), QueryScratch);
			const std::vector<int32_t> Hits = QueryScratch;
			for (const int32_t N : Hits)
			{
				const Soldier& S = SoldiersList[static_cast<size_t>(N)];
				if (S.side != T.side && S.Alive())
				{
					HitSoldier(N, Fix64::One(), S.pos - Center, R.smashKnockH, R.smashKnockV / 2);
				}
			}
		}
		else
		{
			const bool bSmash = T.combo == 2;
			if (bSmash)
			{
				const FixVec2 Center = T.pos + T.facing * (T.radius + R.smashRadius / 2);
				QuerySoldiers(Center, R.smashRadius, QueryScratch);
				const std::vector<int32_t> Hits = QueryScratch;
				for (const int32_t N : Hits)
				{
					const Soldier& S = SoldiersList[static_cast<size_t>(N)];
					if (S.side != T.side && S.Alive())
					{
						HitSoldier(N, R.smashCoef, S.pos - Center, R.smashKnockH, R.smashKnockV);
					}
				}
				Emit(SimEventType::TitanSmash, T.side, T.id, 0, Center);
			}
			else
			{
				QuerySoldiers(T.pos, T.radius + T.sweepRadius, QueryScratch);
				const std::vector<int32_t> Hits = QueryScratch;
				for (const int32_t N : Hits)
				{
					const Soldier& S = SoldiersList[static_cast<size_t>(N)];
					if (S.side == T.side || !S.Alive())
					{
						continue;
					}
					const FixVec2 Dir = (S.pos - T.pos).Normalized();
					if (T.facing.Dot(Dir) >= T.sweepHalfArcCos)
					{
						HitSoldier(N, Fix64::One(), Dir, R.sweepKnockH, R.sweepKnockV);
					}
				}
			}
			T.combo = (T.combo + 1) % 3;
			++T.comboHits;
			const TitanDef& TD = TitanDefOf(T);
			if (TD.chainLightningEvery > 0 && T.comboHits % TD.chainLightningEvery == 0)
			{
				// Passive: chain lightning to the nearest enemy soldiers.
				QuerySoldiers(T.pos, fx::Int(14), QueryScratch);
				std::vector<std::pair<Fix64, int32_t>> Candidates;
				for (const int32_t N : QueryScratch)
				{
					const Soldier& S = SoldiersList[static_cast<size_t>(N)];
					if (S.side != T.side && S.Alive())
					{
						Candidates.emplace_back(fx::DistSq(S.pos, T.pos), N);
					}
				}
				std::sort(Candidates.begin(), Candidates.end(), [](const std::pair<Fix64, int32_t>& A, const std::pair<Fix64, int32_t>& B)
					{ return A.first != B.first ? A.first < B.first : A.second < B.second; });
				for (size_t K = 0; K < Candidates.size() && static_cast<int>(K) < TD.chainLightningTargets; ++K)
				{
					const Soldier& S = SoldiersList[static_cast<size_t>(Candidates[K].second)];
					DamageSoldier(Candidates[K].second, TitanDamageToSoldier(T, S, TD.chainLightningCoef, false, false), T.side, Index);
				}
			}
		}
	}

	for (size_t I = 0; I < SquadsList.size(); ++I)
	{
		Squad& Sq = SquadsList[I];
		if (SquadHit[I] && Tick - Sq.lastTitanHitMoraleTick >= R.moraleTitanHitCooldownTicks)
		{
			Sq.lastTitanHitMoraleTick = Tick;
			ChangeMorale(Sq, -R.moraleTitanHit);
		}
	}
}

void BattleSimulation::TitanFootstep(Titan& T, int Index)
{
	const RulesDef& R = ContentRef.rules;
	T.footSide ^= 1;
	const Fix64 SideOffset = T.radius * fx::Pct(45) * (T.footSide ? 1 : -1);
	const FixVec2 Foot = T.pos + T.facing * (T.radius * fx::Pct(30)) + T.facing.Right() * SideOffset;
	QuerySoldiers(Foot, T.footRadius, QueryScratch);
	const std::vector<int32_t> Hits = QueryScratch;
	for (const int32_t N : Hits)
	{
		const Soldier& S = SoldiersList[static_cast<size_t>(N)];
		if (S.side != T.side && S.Alive() && S.state != SoldierState::Airborne)
		{
			const Fix64 Damage = TitanDamageToSoldier(T, S, R.footstepCoef, true, false);
			KnockDown(N, R.footstepKnockdownTicks);
			DamageSoldier(N, Damage, T.side, Index);
		}
	}
	Emit(SimEventType::TitanFootstep, T.side, T.id, T.footSide, Foot);
}

void BattleSimulation::TitanTrample(Titan& T, int Index)
{
	const RulesDef& R = ContentRef.rules;
	QuerySoldiers(T.pos, T.radius + Fix64::One(), QueryScratch);
	const std::vector<int32_t> Hits = QueryScratch;
	for (const int32_t N : Hits)
	{
		Soldier& S = SoldiersList[static_cast<size_t>(N)];
		if (S.side == T.side || !S.Alive() || S.lastTrampledTick >= T.chargeStartTick || (S.pos - T.pos).Dot(T.facing) < Fix64::Zero())
		{
			continue;
		}
		S.lastTrampledTick = Tick;
		Fix64 Coef = R.titanTrampleCoef;
		const Squad& Sq = SquadsList[static_cast<size_t>(S.squad)];
		const TroopDef& TD = TroopOf(Sq);
		if (TD.brace && (Sq.formation == Formation::Square || Sq.stance == Stance::Hold || !Sq.moving))
		{
			Coef = Coef * TD.braceTitanChargeMult;
		}
		const Fix64 Damage = TitanDamageToSoldier(T, S, Coef, true, false);
		const FixVec2 Lateral = T.facing.Right() * ((S.pos - T.pos).Dot(T.facing.Right()) >= Fix64::Zero() ? 1 : -1);
		Knockback(N, Lateral + T.facing, R.sweepKnockH, R.sweepKnockV);
		DamageSoldier(N, Damage, T.side, Index);
	}
}

// --- Damage, stagger, death -----------------------------------------------------

Fix64 BattleSimulation::TitanEffectiveArmor(const Titan& T, bool bRanged) const
{
	return fx::Clamp(T.armor + (bRanged ? T.rangedArmorBonus : Fix64::Zero()) + TitanStat(T, StatType::ArmorAdd), Fix64::Zero(), fx::Pct(75));
}

Fix64 BattleSimulation::TitanDamageTakenMult(const Titan& T) const
{
	Fix64 Mult = Fix64::One() + TitanStat(T, StatType::DamageTakenPct);
	if (Tick < T.staggerUntil)
	{
		Mult += T.staggerTakenMult;
	}
	return fx::Max(Mult, fx::Pct(10));
}

void BattleSimulation::DamageTitan(int Index, Fix64 Amount, Fix64 PoiseDamage, FixVec2 FromPos, int AttackerTitan)
{
	const RulesDef& R = ContentRef.rules;
	(void)AttackerTitan;
	Titan& T = TitansList[static_cast<size_t>(Index)];
	if (T.dead || Amount <= Fix64::Zero())
	{
		return;
	}
	T.hp -= Amount;
	T.hpLostAccumPct += Amount * 100 / T.maxHp;
	while (T.hpLostAccumPct >= Fix64::One())
	{
		T.hpLostAccumPct -= Fix64::One();
		T.rage = fx::Min(fx::Int(100), T.rage + R.ragePerPctHpLost);
	}
	T.lastHitTick = Tick;
	const FixVec2 Dir = (T.pos - FromPos).Normalized();
	if (!Dir.IsZero())
	{
		T.lastHitDir = Dir;
	}
	if (T.clashIndex < 0)
	{
		T.poiseDamage += PoiseDamage;
		if (T.poiseDamage >= T.maxPoise && Tick >= T.staggerUntil)
		{
			T.poiseDamage = Fix64::Zero();
			T.staggerUntil = Tick + R.staggerTicks;
			T.staggerTakenMult = R.staggerDamageTakenPct;
			T.state = TitanState::Staggered;
			T.castingAbility = -1;
			Emit(SimEventType::TitanStaggered, T.side, T.id, 0, T.pos);
		}
	}
	if (T.hp <= Fix64::Zero())
	{
		KillTitan(Index);
	}
}

void BattleSimulation::KillTitan(int Index)
{
	const RulesDef& R = ContentRef.rules;
	Titan& T = TitansList[static_cast<size_t>(Index)];
	if (T.dead)
	{
		return;
	}
	if (T.clashIndex >= 0)
	{
		EndClash();
	}
	T.dead = true;
	T.hp = Fix64::Zero();
	T.state = TitanState::Dead;
	T.deathTick = Tick;
	T.fallDir = T.lastHitDir.IsZero() ? T.facing : T.lastHitDir;
	SideState& Own = Sides[SideIndex(T.side)];
	Own.titanDefeated = true;
	Own.destroyedPower += T.power;

	// Fall line: everyone under the falling body is crushed.
	const FixVec2 A = T.pos;
	const FixVec2 B = T.pos + T.fallDir * T.height;
	const Fix64 HalfWidth = R.fallWidth / 2;
	QuerySoldiers((A + B) / 2, T.height / 2 + R.fallWidth, QueryScratch);
	const std::vector<int32_t> Hits = QueryScratch;
	for (const int32_t N : Hits)
	{
		const Soldier& S = SoldiersList[static_cast<size_t>(N)];
		if (S.Alive() && PointSegmentDistance(S.pos, A, B) <= HalfWidth)
		{
			const Fix64 Damage = R.fallFlatDamage + S.maxHp * R.fallPctMaxHp;
			KnockDown(N, R.knockdownTicks);
			DamageSoldier(N, Damage, Opponent(S.side), -1);
		}
	}
	// The body blocks troops for a while, then becomes rubble (cover).
	FallenBody Body;
	Body.removeTick = Tick + R.fallObstacleTicks;
	const Fix64 Step = Nav.CellSize() / 2;
	const int Samples = static_cast<int>((T.height / Step).FloorToInt());
	for (int K = 0; K <= Samples; ++K)
	{
		const FixVec2 Center = A + T.fallDir * (Step * K);
		for (int Lateral = -1; Lateral <= 1; ++Lateral)
		{
			const FixVec2 P = Center + T.fallDir.Right() * (HalfWidth * Lateral / 2);
			int Cx = 0;
			int Cz = 0;
			Nav.WorldToCell(P, Cx, Cz);
			const std::pair<int, int> Cell(Cx, Cz);
			if (Nav.InBounds(Cx, Cz) && std::find(Body.cells.begin(), Body.cells.end(), Cell) == Body.cells.end())
			{
				Body.cells.push_back(Cell);
				Nav.AddDynamicBlock(Cx, Cz);
			}
		}
	}
	Bodies.push_back(Body);

	for (Squad& Sq : SquadsList)
	{
		if (Sq.Active())
		{
			ChangeMorale(Sq, Sq.side == T.side ? -R.moraleOwnTitanDown : R.moraleEnemyTitanDown);
		}
	}
	for (Soldier& S : SoldiersList)
	{
		if (S.targetTitan == Index)
		{
			S.targetTitan = -1;
		}
	}
	Emit(SimEventType::TitanDied, T.side, T.id, 0, T.pos);
}

void BattleSimulation::UpdateFallenBodies()
{
	for (FallenBody& Body : Bodies)
	{
		if (Body.active && Tick >= Body.removeTick)
		{
			Body.active = false;
			for (const std::pair<int, int>& Cell : Body.cells)
			{
				Nav.RemoveDynamicBlock(Cell.first, Cell.second);
				Nav.AddFlag(Cell.first, Cell.second, cellflag::Ruins);
			}
		}
	}
}

void BattleSimulation::PushSoldiersFromTitans()
{
	for (const Titan& T : TitansList)
	{
		if (T.dead)
		{
			continue;
		}
		QuerySoldiers(T.pos, T.radius + fx::Int(2), QueryScratch);
		const std::vector<int32_t> Near = QueryScratch;
		for (const int32_t N : Near)
		{
			Soldier& S = SoldiersList[static_cast<size_t>(N)];
			if (!S.Alive() || S.state == SoldierState::Airborne)
			{
				continue;
			}
			FixVec2 Dir = S.pos - T.pos;
			const Fix64 D = Dir.Length();
			if (D >= T.radius)
			{
				continue;
			}
			Dir = D.raw > 0 ? Dir / D : T.facing.Right();
			const FixVec2 Out = T.pos + Dir * (T.radius + fx::Milli(200));
			if (Nav.InWorld(Out))
			{
				S.pos = Out;
			}
			if (S.side != T.side && S.state != SoldierState::KnockedDown)
			{
				KnockDown(N, 12);
			}
		}
	}
}

void BattleSimulation::SeparateTitans()
{
	for (size_t I = 0; I < TitansList.size(); ++I)
	{
		for (size_t J = I + 1; J < TitansList.size(); ++J)
		{
			Titan& A = TitansList[I];
			Titan& B = TitansList[J];
			if (A.dead || B.dead)
			{
				continue;
			}
			const FixVec2 Delta = B.pos - A.pos;
			const Fix64 D = Delta.Length();
			const Fix64 MinD = A.radius + B.radius;
			if (D >= MinD)
			{
				continue;
			}
			const FixVec2 Dir = D.raw > 0 ? Delta / D : FixVec2(Fix64::One(), Fix64::Zero());
			const Fix64 Push = (MinD - D) / 2;
			A.pos = Nav.ClampToWorld(A.pos - Dir * Push, fx::Int(5));
			B.pos = Nav.ClampToWorld(B.pos + Dir * Push, fx::Int(5));
		}
	}
}

// --- Titan Clash (4.11.9) ------------------------------------------------------------

void BattleSimulation::TryStartClash()
{
	const RulesDef& R = ContentRef.rules;
	if (ClashData.active || Tick < ClashData.pairCooldownUntil)
	{
		return;
	}
	auto Ready = [this](const Titan& T)
	{
		return !T.dead && T.state != TitanState::Casting && T.state != TitanState::Leaping && Tick >= T.staggerUntil && Tick >= T.stunUntil;
	};
	for (size_t I = 0; I < TitansList.size(); ++I)
	{
		for (size_t J = I + 1; J < TitansList.size(); ++J)
		{
			Titan& A = TitansList[I];
			Titan& B = TitansList[J];
			if (A.side == B.side || !Ready(A) || !Ready(B))
			{
				continue;
			}
			if (fx::Dist(A.pos, B.pos) > A.radius + B.radius + R.clashTriggerExtra)
			{
				continue;
			}
			auto Targets = [](const Titan& X, const Titan& Y)
			{ return X.orderTarget == Y.id || X.autoTarget == Y.id || X.attackTargetUnit == Y.id; };
			if (!Targets(A, B) && !Targets(B, A))
			{
				continue;
			}
			const int AttackerSlotTitan = A.side == Side::Attacker ? static_cast<int>(I) : static_cast<int>(J);
			const int DefenderSlotTitan = A.side == Side::Attacker ? static_cast<int>(J) : static_cast<int>(I);
			ClashState& C = ClashData;
			const int32_t Cooldown = C.pairCooldownUntil;
			C = ClashState();
			C.pairCooldownUntil = Cooldown;
			C.active = true;
			C.titan[0] = AttackerSlotTitan;
			C.titan[1] = DefenderSlotTitan;
			C.startTick = Tick;
			C.exchange = 0;
			C.windupStart = Tick + R.clashLockTicks;
			C.markerTick = C.windupStart + R.clashWindupTicks;
			C.resolveTick = C.markerTick + (R.clashGoodMs * kTicksPerSecond + 999) / 1000;
			C.exchangeEnd = C.windupStart + R.clashWindupTicks + R.clashStrikeTicks;
			for (int Slot = 0; Slot < 2; ++Slot)
			{
				Titan& T = TitansList[static_cast<size_t>(C.titan[Slot])];
				Titan& O = TitansList[static_cast<size_t>(C.titan[1 - Slot])];
				C.bonus[Slot] = Tick < T.chargeUntil ? 1 : 0;
				T.chargeUntil = -1;
				T.clashIndex = 0;
				T.state = TitanState::Clashing;
				T.castingAbility = -1;
				const FixVec2 Dir = (O.pos - T.pos).Normalized();
				if (!Dir.IsZero())
				{
					T.facing = Dir;
				}
			}
			Emit(SimEventType::ClashStarted, Side::Attacker, TitansList[static_cast<size_t>(C.titan[0])].id, TitansList[static_cast<size_t>(C.titan[1])].id,
				(A.pos + B.pos) / 2);
			return;
		}
	}
}

int BattleSimulation::ClashGradeFor(int Slot)
{
	const RulesDef& R = ContentRef.rules;
	const Titan& T = TitansList[static_cast<size_t>(ClashData.titan[Slot])];
	const SideState& S = Sides[SideIndex(T.side)];
	int ProfileIndex = S.aiProfile;
	if (ProfileIndex < 0 && (S.autoClash || T.autoControl))
	{
		ProfileIndex = ContentRef.FindAiProfile("veteran");
	}
	if (ProfileIndex >= 0 || S.autoClash || T.autoControl)
	{
		Fix64 Perfect = fx::Pct(15);
		Fix64 Good = fx::Pct(50);
		if (ProfileIndex >= 0)
		{
			Perfect = ContentRef.aiProfiles[static_cast<size_t>(ProfileIndex)].clashPerfect;
			Good = ContentRef.aiProfiles[static_cast<size_t>(ProfileIndex)].clashGood;
		}
		const Fix64 Roll = Random.Next01();
		return Roll < Perfect ? 3 : (Roll < Perfect + Good ? 2 : 1);
	}
	if (!ClashData.inputReceived[Slot])
	{
		return 1;
	}
	const int Offset = std::abs(ClashData.inputOffsetMs[Slot]);
	return Offset <= R.clashPerfectMs ? 3 : (Offset <= R.clashGoodMs ? 2 : 1);
}

void BattleSimulation::UpdateClash()
{
	const RulesDef& R = ContentRef.rules;
	ClashState& C = ClashData;
	if (!C.active)
	{
		return;
	}
	if (TitansList[static_cast<size_t>(C.titan[0])].dead || TitansList[static_cast<size_t>(C.titan[1])].dead)
	{
		EndClash();
		return;
	}
	if (Tick == C.resolveTick)
	{
		ResolveClashExchange();
		if (!C.active)
		{
			return;
		}
	}
	if (Tick >= C.exchangeEnd)
	{
		++C.exchange;
		if (C.exchange >= 3)
		{
			EndClash();
			return;
		}
		C.windupStart = Tick;
		C.markerTick = C.windupStart + R.clashWindupTicks;
		C.resolveTick = C.markerTick + (R.clashGoodMs * kTicksPerSecond + 999) / 1000;
		C.exchangeEnd = C.windupStart + R.clashWindupTicks + R.clashStrikeTicks;
		C.inputReceived[0] = C.inputReceived[1] = false;
		C.bonus[0] = C.bonus[1] = 0;
	}
}

void BattleSimulation::ResolveClashExchange()
{
	const RulesDef& R = ContentRef.rules;
	ClashState& C = ClashData;
	const int Grade[2] = {ClashGradeFor(0), ClashGradeFor(1)};
	C.lastGrade[0] = Grade[0];
	C.lastGrade[1] = Grade[1];
	Titan& A = TitansList[static_cast<size_t>(C.titan[0])];
	Titan& B = TitansList[static_cast<size_t>(C.titan[1])];
	const FixVec2 Mid = (A.pos + B.pos) / 2;

	// Shockwave knocks back nearby soldiers of both sides.
	QuerySoldiers(Mid, R.clashShockwaveRadius, QueryScratch);
	const std::vector<int32_t> Near = QueryScratch;
	for (const int32_t N : Near)
	{
		Soldier& S = SoldiersList[static_cast<size_t>(N)];
		if (S.Alive())
		{
			const Fix64 Damage = S.maxHp * R.clashShockwavePct;
			Knockback(N, S.pos - Mid, R.stompKnockH, R.stompKnockV / 2);
			DamageSoldier(N, Damage, Opponent(S.side), -1);
		}
	}

	if (Grade[0] == 1 && Grade[1] == 1)
	{
		const Fix64 DamageToB = A.atk * R.titanVsTitanMult * R.clashBothMissCoef * (Fix64::One() - TitanEffectiveArmor(B, false));
		const Fix64 DamageToA = B.atk * R.titanVsTitanMult * R.clashBothMissCoef * (Fix64::One() - TitanEffectiveArmor(A, false));
		const FixVec2 Dir = (B.pos - A.pos).Normalized();
		A.pos = Nav.ClampToWorld(A.pos - Dir * (R.clashPushback / 2), fx::Int(5));
		B.pos = Nav.ClampToWorld(B.pos + Dir * (R.clashPushback / 2), fx::Int(5));
		C.lastWinner = -1;
		Emit(SimEventType::ClashExchange, Side::Attacker, Grade[0], Grade[1], Mid, -1);
		const int IA = C.titan[0];
		const int IB = C.titan[1];
		EndClash();
		DamageTitan(IB, DamageToB, Fix64::Zero(), A.pos, IA);
		DamageTitan(IA, DamageToA, Fix64::Zero(), B.pos, IB);
		return;
	}

	const int ScoreA = Grade[0] + C.bonus[0];
	const int ScoreB = Grade[1] + C.bonus[1];
	int Winner = 0;
	if (ScoreB > ScoreA)
	{
		Winner = 1;
	}
	else if (ScoreA == ScoreB)
	{
		const Fix64 PowerA = A.atk * (Fix64::One() + A.maxPoise / 1000);
		const Fix64 PowerB = B.atk * (Fix64::One() + B.maxPoise / 1000);
		Winner = PowerB > PowerA ? 1 : 0;
	}
	C.lastWinner = Winner;
	Titan& W = Winner == 0 ? A : B;
	Titan& L = Winner == 0 ? B : A;
	const int WinnerIndex = C.titan[Winner];
	const int LoserIndex = C.titan[1 - Winner];
	const Fix64 WinCoef = Grade[Winner] == 3 ? R.clashWinPerfectCoef : R.clashWinCoef;
	const Fix64 ToLoser = W.atk * R.titanVsTitanMult * WinCoef * (Fix64::One() - TitanEffectiveArmor(L, false)) * TitanDamageTakenMult(L);
	const Fix64 ToWinner = L.atk * R.titanVsTitanMult * R.clashLoseCoef * (Fix64::One() - TitanEffectiveArmor(W, false)) * TitanDamageTakenMult(W);
	L.poiseDamage += L.maxPoise * R.clashPoisePct;
	Emit(SimEventType::ClashExchange, Side::Attacker, Grade[0], Grade[1], Mid, Winner);
	const bool bBreak = L.poiseDamage >= L.maxPoise;
	const Side WinnerSide = W.side;
	if (bBreak)
	{
		EndClash();
		L.poiseDamage = Fix64::Zero();
		L.staggerUntil = Tick + R.clashStaggerTicks;
		L.staggerTakenMult = R.clashStaggerDamageTakenPct;
		L.state = TitanState::Staggered;
		SideState& WS = Sides[SideIndex(WinnerSide)];
		WS.valor = fx::Min(R.valorMax, WS.valor + R.valorOnClashStagger);
		Emit(SimEventType::TitanStaggered, L.side, L.id, 1, L.pos);
	}
	DamageTitan(LoserIndex, ToLoser, Fix64::Zero(), W.pos, WinnerIndex);
	DamageTitan(WinnerIndex, ToWinner, Fix64::Zero(), L.pos, LoserIndex);
}

void BattleSimulation::EndClash()
{
	ClashState& C = ClashData;
	if (!C.active)
	{
		return;
	}
	C.active = false;
	C.pairCooldownUntil = Tick + ContentRef.rules.clashPairCooldownTicks;
	for (int Slot = 0; Slot < 2; ++Slot)
	{
		Titan& T = TitansList[static_cast<size_t>(C.titan[Slot])];
		T.clashIndex = -1;
		if (!T.dead && T.state == TitanState::Clashing)
		{
			T.state = TitanState::Idle;
		}
		T.attackReadyTick = Tick + 15;
	}
	Emit(SimEventType::ClashEnded, Side::Attacker, TitansList[static_cast<size_t>(C.titan[0])].id, TitansList[static_cast<size_t>(C.titan[1])].id, FixVec2(),
		C.lastWinner);
}

// --- Abilities & spells --------------------------------------------------------------

bool BattleSimulation::StartAbility(Titan& T, int Index, int AbilitySlot, FixVec2 Point)
{
	(void)Index;
	if (AbilitySlot < 0 || AbilitySlot > 1)
	{
		return false;
	}
	const int AbilityIndex = TitanDefOf(T).abilities[AbilitySlot];
	if (AbilityIndex < 0 || T.dead || T.clashIndex >= 0 || Tick < T.staggerUntil || Tick < T.stunUntil || T.state == TitanState::Casting ||
		T.state == TitanState::Leaping)
	{
		return false;
	}
	const AbilityDef& A = ContentRef.abilities[static_cast<size_t>(AbilityIndex)];
	if (Tick < T.abilityReadyTick[AbilitySlot] || T.rage < fx::Int(A.rageCost))
	{
		return false;
	}
	T.rage -= fx::Int(A.rageCost);
	T.abilityReadyTick[AbilitySlot] = Tick + A.cooldownTicks;
	FixVec2 Dir = Point - T.pos;
	if (Dir.IsZero())
	{
		Dir = T.facing;
	}
	const Fix64 Len = Dir.Length();
	if (A.shape != TargetShape::Self && A.range > Fix64::Zero() && Len > A.range && (A.shape == TargetShape::Circle || A.delivery == Delivery::Leap))
	{
		Point = T.pos + Dir / Len * A.range;
	}
	if (A.shape != TargetShape::Self)
	{
		T.facing = Dir.Normalized();
	}
	T.castPoint = Nav.ClampToWorld(Point, fx::Int(3));
	T.castingAbility = AbilityIndex;
	T.state = TitanState::Casting;
	T.actionHitTick = Tick + std::max(1, A.castTicks);
	Emit(SimEventType::AbilityCast, T.side, T.id, AbilitySlot, T.castPoint, AbilityIndex);
	return true;
}

void BattleSimulation::ExecuteAbility(int TitanIdx, int AbilityIdx, FixVec2 Point)
{
	if (AbilityIdx < 0)
	{
		return;
	}
	Titan& T = TitansList[static_cast<size_t>(TitanIdx)];
	const AbilityDef& A = ContentRef.abilities[static_cast<size_t>(AbilityIdx)];
	const int Level = TitanDefOf(T).abilities[1] == AbilityIdx ? T.abilityLevel[1] : T.abilityLevel[0];
	FixVec2 Dir = (Point - T.pos).Normalized();
	if (Dir.IsZero())
	{
		Dir = T.facing;
	}
	switch (A.delivery)
	{
	case Delivery::Instant:
		ApplyEffects(AbilityIdx, T.side, TitanIdx, T.pos, Point, Dir, Level, false);
		Emit(SimEventType::AbilityImpact, T.side, T.id, AbilityIdx, Point);
		break;
	case Delivery::Projectile:
	{
		const int32_t Travel = std::max(1, static_cast<int>((A.range * kTicksPerSecond / fx::Max(A.projectileSpeed, Fix64::One())).RoundToInt()));
		PendingImpact P;
		P.ability = AbilityIdx;
		P.side = T.side;
		P.casterTitan = TitanIdx;
		P.origin = T.pos;
		P.point = T.pos + Dir * A.range;
		P.dir = Dir;
		P.castTick = Tick;
		P.impactTick = Tick + Travel;
		P.level = Level;
		Impacts.push_back(P);
		if (A.returns)
		{
			P.impactTick = Tick + Travel * 2;
			Impacts.push_back(P);
		}
		break;
	}
	case Delivery::Leap:
		T.leapFrom = T.pos;
		T.leapTo = Nav.ClampToWorld(Point, fx::Int(6));
		T.leapStart = Tick;
		T.leapEnd = Tick + std::max(1, A.leapTicks);
		T.leapAbility = AbilityIdx;
		T.state = TitanState::Leaping;
		break;
	case Delivery::Zone:
	{
		Zone Z;
		Z.ability = AbilityIdx;
		Z.side = T.side;
		Z.casterTitan = TitanIdx;
		Z.center = Point;
		Z.radius = A.radius;
		Z.startTick = Tick;
		Z.endTick = Tick + A.zoneDurationTicks;
		Z.nextPulseTick = Tick;
		Z.level = Level;
		ZonesList.push_back(Z);
		break;
	}
	case Delivery::Delayed:
	{
		PendingImpact P;
		P.ability = AbilityIdx;
		P.side = T.side;
		P.casterTitan = TitanIdx;
		P.origin = T.pos;
		P.point = Point;
		P.dir = Dir;
		P.castTick = Tick;
		P.impactTick = Tick + std::max(1, A.delayTicks);
		P.level = Level;
		Impacts.push_back(P);
		break;
	}
	}
}

bool BattleSimulation::CastSpell(Side S, int Slot, FixVec2 Point)
{
	SideState& State = Sides[SideIndex(S)];
	if (Slot < 0 || Slot >= static_cast<int>(State.spells.size()))
	{
		return false;
	}
	const int AbilityIdx = State.spells[static_cast<size_t>(Slot)];
	const AbilityDef& A = ContentRef.abilities[static_cast<size_t>(AbilityIdx)];
	int32_t& Charges = State.spellCharges[static_cast<size_t>(Slot)];
	if (Tick < State.spellReadyTick[static_cast<size_t>(Slot)] || State.valor < fx::Int(A.valorCost) || Charges == 0)
	{
		return false;
	}
	State.valor -= fx::Int(A.valorCost);
	State.spellReadyTick[static_cast<size_t>(Slot)] = Tick + A.cooldownTicks;
	if (Charges > 0)
	{
		--Charges;
	}
	const int Level = State.spellLevels[static_cast<size_t>(Slot)];
	Point = Nav.ClampToWorld(Point, Fix64::One());
	const FixVec2 Dir(Fix64::Zero(), S == Side::Attacker ? Fix64::One() : -Fix64::One());
	switch (A.delivery)
	{
	case Delivery::Zone:
	{
		Zone Z;
		Z.ability = AbilityIdx;
		Z.side = S;
		Z.center = Point;
		Z.radius = A.radius;
		Z.startTick = Tick;
		Z.endTick = Tick + A.zoneDurationTicks;
		Z.nextPulseTick = Tick;
		Z.level = Level;
		ZonesList.push_back(Z);
		break;
	}
	case Delivery::Delayed:
	case Delivery::Projectile:
	{
		PendingImpact P;
		P.ability = AbilityIdx;
		P.side = S;
		P.origin = Point;
		P.point = Point;
		P.dir = Dir;
		P.castTick = Tick;
		P.impactTick = Tick + std::max(1, A.delayTicks);
		P.level = Level;
		Impacts.push_back(P);
		break;
	}
	default:
		ApplyEffects(AbilityIdx, S, -1, Point, Point, Dir, Level, false);
		Emit(SimEventType::SpellImpact, S, AbilityIdx, Slot, Point);
		break;
	}
	Emit(SimEventType::SpellCast, S, AbilityIdx, Slot, Point);
	return true;
}

bool BattleSimulation::InShape(const AbilityDef& A, FixVec2 Origin, FixVec2 Point, FixVec2 Dir, FixVec2 P, Fix64 Extra) const
{
	switch (A.shape)
	{
	case TargetShape::Circle: return fx::Dist(P, Point) <= A.radius + Extra;
	case TargetShape::Cone:
	{
		const FixVec2 V = P - Origin;
		const Fix64 D = V.Length();
		if (D > A.range + Extra)
		{
			return false;
		}
		return D < Fix64::One() || V.Dot(Dir) / D >= A.coneHalfAngleCos;
	}
	case TargetShape::Line:
	{
		const Fix64 Along = (P - Origin).Dot(Dir);
		if (Along < -Extra || Along > A.range + Extra)
		{
			return false;
		}
		return fx::Abs((P - Origin).Cross(Dir)) <= A.width / 2 + Extra;
	}
	default: return false;
	}
}

void BattleSimulation::AddSquadStatus(Squad& Sq, const EffectDef& E, int AbilityIdx, Fix64 LevelMult)
{
	for (StatusEffect& S : Sq.statuses)
	{
		if (S.sourceAbility == AbilityIdx && S.stat == E.stat)
		{
			S.endTick = Tick + E.durationTicks;
			S.magnitude = E.magnitude * LevelMult;
			return;
		}
	}
	Sq.statuses.push_back({E.stat, E.magnitude * LevelMult, Tick + E.durationTicks, AbilityIdx});
}

void BattleSimulation::AddTitanStatus(Titan& T, const EffectDef& E, int AbilityIdx, Fix64 LevelMult)
{
	for (StatusEffect& S : T.statuses)
	{
		if (S.sourceAbility == AbilityIdx && S.stat == E.stat)
		{
			S.endTick = Tick + E.durationTicks;
			S.magnitude = E.magnitude * LevelMult;
			return;
		}
	}
	T.statuses.push_back({E.stat, E.magnitude * LevelMult, Tick + E.durationTicks, AbilityIdx});
}

void BattleSimulation::ApplyEffects(int AbilityIdx, Side S, int CasterTitan, FixVec2 Origin, FixVec2 Point, FixVec2 Dir, int Level, bool bPulse)
{
	(void)bPulse;
	const RulesDef& R = ContentRef.rules;
	const AbilityDef& A = ContentRef.abilities[static_cast<size_t>(AbilityIdx)];
	const Fix64 LevelMult = Fix64::One() + A.perLevelPct * (std::max(1, Level) - 1);
	const Titan* Caster = CasterTitan >= 0 ? &TitansList[static_cast<size_t>(CasterTitan)] : nullptr;

	// Gather soldiers in the shape once.
	std::vector<int32_t> InArea;
	if (A.shape != TargetShape::Self)
	{
		FixVec2 Center = Point;
		Fix64 Radius = A.radius;
		if (A.shape == TargetShape::Cone)
		{
			Center = Origin;
			Radius = A.range;
		}
		else if (A.shape == TargetShape::Line)
		{
			Center = Origin + Dir * (A.range / 2);
			Radius = A.range / 2 + A.width;
		}
		QuerySoldiers(Center, Radius + Fix64::One(), QueryScratch);
		for (const int32_t N : QueryScratch)
		{
			const Soldier& Sol = SoldiersList[static_cast<size_t>(N)];
			if (Sol.Alive() && InShape(A, Origin, Point, Dir, Sol.pos, Fix64::Zero()))
			{
				InArea.push_back(N);
			}
		}
	}

	std::vector<bool> SquadTouched(SquadsList.size(), false);
	for (const int32_t N : InArea)
	{
		SquadTouched[static_cast<size_t>(SoldiersList[static_cast<size_t>(N)].squad)] = true;
	}
	for (const EffectDef& E : A.effects)
	{
		const bool bEnemyTroops = (E.affectsMask & affects::EnemyTroops) != 0;
		const bool bAllyTroops = (E.affectsMask & affects::AllyTroops) != 0;
		const bool bEnemyTitans = (E.affectsMask & affects::EnemyTitans) != 0;
		const bool bAllyTitans = (E.affectsMask & affects::AllyTitans) != 0;
		// A returning projectile applies one hit on each pass (out and back).
		const int Repeat = A.returns ? 1 : std::max(1, E.hits);

		if (E.type == EffectType::RaiseDead)
		{
			// Bodies rise after the effects resolve (SpawnSummons), so no squad is added mid-iteration.
			PendingSummon Req;
			Req.side = S;
			Req.troop = E.summonTroop;
			Req.tier = 1 + (std::max(1, Level) - 1) / 4;
			Req.center = A.shape == TargetShape::Self ? Origin : Point;
			Req.radius = A.radius;
			Req.maxCount = E.summonMax + E.summonMaxPerLevel * (std::max(1, Level) - 1);
			Req.corpseAgeTicks = E.corpseAgeTicks;
			Req.lifetimeTicks = E.durationTicks;
			Summons.push_back(Req);
			continue;
		}

		if (E.affectsMask & affects::Self)
		{
			if (Caster && E.type == EffectType::StatMod)
			{
				AddTitanStatus(TitansList[static_cast<size_t>(CasterTitan)], E, AbilityIdx, LevelMult);
			}
			else if (Caster && E.type == EffectType::Heal)
			{
				Titan& Self = TitansList[static_cast<size_t>(CasterTitan)];
				Self.hp = fx::Min(Self.maxHp, Self.hp + (E.flat + E.pctMaxHp * Self.maxHp) * LevelMult);
			}
		}

		if (bEnemyTroops || bAllyTroops)
		{
			if (E.type == EffectType::StatMod || E.type == EffectType::Morale)
			{
				for (size_t I = 0; I < SquadsList.size(); ++I)
				{
					Squad& Sq = SquadsList[I];
					if (!SquadTouched[I] || !Sq.Active())
					{
						continue;
					}
					if ((Sq.side == S && bAllyTroops) || (Sq.side != S && bEnemyTroops))
					{
						if (E.type == EffectType::Morale)
						{
							// Terror: enemies lose morale, allies gain it.
							ChangeMorale(Sq, Sq.side == S ? E.flat * LevelMult : -E.flat * LevelMult);
						}
						else
						{
							AddSquadStatus(Sq, E, AbilityIdx, LevelMult);
						}
					}
				}
			}
			else
			{
				for (const int32_t N : InArea)
				{
					Soldier& Sol = SoldiersList[static_cast<size_t>(N)];
					if (!Sol.Alive())
					{
						continue;
					}
					const bool bEnemy = Sol.side != S;
					if ((bEnemy && !bEnemyTroops) || (!bEnemy && !bAllyTroops))
					{
						continue;
					}
					const Squad& Sq = SquadsList[static_cast<size_t>(Sol.squad)];
					switch (E.type)
					{
					case EffectType::Damage:
					{
						for (int K = 0; K < Repeat && Sol.Alive(); ++K)
						{
							Fix64 Damage;
							if (Caster && E.coef > Fix64::Zero())
							{
								Damage += TitanDamageToSoldier(*Caster, Sol, E.coef * LevelMult, true, true);
							}
							if (E.flat > Fix64::Zero() || E.pctMaxHp > Fix64::Zero())
							{
								Fix64 Armor = fx::Clamp(Sq.armor + SquadStat(Sq, StatType::ArmorAdd), Fix64::Zero(), fx::Pct(75));
								Fix64 Flat = (E.flat + E.pctMaxHp * Sol.maxHp) * LevelMult * (Fix64::One() - Armor);
								if (Sq.formation == Formation::Loose)
								{
									Flat = Flat * R.looseAreaDamageMult;
								}
								Damage += Flat;
							}
							DamageSoldier(N, Damage, S, CasterTitan);
						}
						break;
					}
					case EffectType::Heal:
						Sol.hp = fx::Min(Sol.maxHp, Sol.hp + (E.flat + E.pctMaxHp * Sol.maxHp) * LevelMult);
						break;
					case EffectType::Knockback:
					{
						const FixVec2 KDir = A.shape == TargetShape::Circle ? Sol.pos - Point : Dir + (Sol.pos - Origin).Normalized() * fx::Pct(50);
						Knockback(N, KDir, E.horizontal, E.vertical);
						break;
					}
					case EffectType::Stun: Sol.stunTicks = std::max(Sol.stunTicks, E.durationTicks); break;
					case EffectType::Burn:
						Sol.burnTicks = E.durationTicks;
						Sol.burnDps = E.flat > Fix64::Zero() ? E.flat * LevelMult : R.burnDps;
						break;
					default: break;
					}
				}
			}
			if (Caster && bEnemyTroops && E.type == EffectType::Damage)
			{
				for (size_t I = 0; I < SquadsList.size(); ++I)
				{
					Squad& Sq = SquadsList[I];
					if (SquadTouched[I] && Sq.side != S && Tick - Sq.lastTitanHitMoraleTick >= R.moraleTitanHitCooldownTicks)
					{
						Sq.lastTitanHitMoraleTick = Tick;
						ChangeMorale(Sq, -R.moraleTitanHit);
					}
				}
			}
		}

		if (bEnemyTitans || bAllyTitans)
		{
			for (size_t I = 0; I < TitansList.size(); ++I)
			{
				Titan& T = TitansList[I];
				if (T.dead || static_cast<int>(I) == CasterTitan)
				{
					continue;
				}
				const bool bEnemy = T.side != S;
				if ((bEnemy && !bEnemyTitans) || (!bEnemy && !bAllyTitans))
				{
					continue;
				}
				if (A.shape == TargetShape::Self || !InShape(A, Origin, Point, Dir, T.pos, T.radius))
				{
					continue;
				}
				switch (E.type)
				{
				case EffectType::Damage:
				{
					for (int K = 0; K < Repeat && !T.dead; ++K)
					{
						Fix64 Raw = E.flat + E.pctMaxHp * T.maxHp;
						if (Caster)
						{
							Raw += Caster->atk * E.coef * R.titanVsTitanMult;
						}
						const Fix64 Damage = Raw * LevelMult * (Fix64::One() - TitanEffectiveArmor(T, false)) * TitanDamageTakenMult(T);
						const Fix64 Poise = E.poiseDamage * LevelMult + (Caster ? Damage * R.poiseFactorTitans : Fix64::Zero());
						DamageTitan(static_cast<int>(I), Damage, Poise, Caster ? Caster->pos : Point, CasterTitan);
					}
					break;
				}
				case EffectType::Heal: T.hp = fx::Min(T.maxHp, T.hp + (E.flat + E.pctMaxHp * T.maxHp) * LevelMult); break;
				case EffectType::Stun: T.stunUntil = std::max(T.stunUntil, Tick + E.durationTicks); break;
				case EffectType::StatMod: AddTitanStatus(T, E, AbilityIdx, LevelMult); break;
				default: break;
				}
			}
		}
	}
}

void BattleSimulation::UpdateZonesAndImpacts()
{
	for (size_t I = 0; I < Impacts.size(); ++I)
	{
		if (!Impacts[I].active || Tick < Impacts[I].impactTick)
		{
			continue;
		}
		Impacts[I].active = false;
		const PendingImpact P = Impacts[I];
		const bool bCasterAlive = P.casterTitan < 0 || !TitansList[static_cast<size_t>(P.casterTitan)].dead;
		if (!bCasterAlive && ContentRef.abilities[static_cast<size_t>(P.ability)].delivery == Delivery::Projectile)
		{
			continue;
		}
		ApplyEffects(P.ability, P.side, P.casterTitan, P.origin, P.point, P.dir, P.level, false);
		Emit(P.casterTitan >= 0 ? SimEventType::AbilityImpact : SimEventType::SpellImpact, P.side, P.casterTitan, P.ability, P.point);
	}
	Impacts.erase(std::remove_if(Impacts.begin(), Impacts.end(), [](const PendingImpact& P) { return !P.active; }), Impacts.end());

	for (size_t I = 0; I < ZonesList.size(); ++I)
	{
		Zone& Z = ZonesList[I];
		if (!Z.active)
		{
			continue;
		}
		if (Tick >= Z.endTick)
		{
			Z.active = false;
			continue;
		}
		if (Tick >= Z.nextPulseTick)
		{
			const Zone Copy = Z;
			Z.nextPulseTick += std::max(1, ContentRef.abilities[static_cast<size_t>(Z.ability)].zonePulseTicks);
			ApplyEffects(Copy.ability, Copy.side, Copy.casterTitan, Copy.center, Copy.center, FixVec2(Fix64::Zero(), Fix64::One()), Copy.level, true);
		}
	}
	ZonesList.erase(std::remove_if(ZonesList.begin(), ZonesList.end(), [](const Zone& Z) { return !Z.active; }), ZonesList.end());
}

int BattleSimulation::CountRaisableCorpses(FixVec2 Center, Fix64 Radius, int32_t MaxAgeTicks) const
{
	int Count = 0;
	const Fix64 RadiusSq = Radius * Radius;
	for (const Soldier& Sol : SoldiersList)
	{
		if (Sol.state == SoldierState::Dead && !Sol.raised && Tick - Sol.deathTick <= MaxAgeTicks && fx::DistSq(Sol.pos, Center) <= RadiusSq)
		{
			++Count;
		}
	}
	return Count;
}

void BattleSimulation::SpawnSummons()
{
	if (Summons.empty())
	{
		return;
	}
	std::vector<PendingSummon> Requests;
	Requests.swap(Summons);
	for (const PendingSummon& Req : Requests)
	{
		if (Req.troop < 0 || Req.maxCount <= 0 || SquadsList.size() + 1 >= static_cast<size_t>(kTitanIdBase))
		{
			continue;
		}
		// The nearest fresh bodies of either side rise (ties by soldier index keep it deterministic).
		std::vector<std::pair<Fix64, int32_t>> Corpses;
		const Fix64 RadiusSq = Req.radius * Req.radius;
		for (size_t I = 0; I < SoldiersList.size(); ++I)
		{
			const Soldier& Sol = SoldiersList[I];
			if (Sol.state != SoldierState::Dead || Sol.raised || Tick - Sol.deathTick > Req.corpseAgeTicks)
			{
				continue;
			}
			const Fix64 D = fx::DistSq(Sol.pos, Req.center);
			if (D <= RadiusSq)
			{
				Corpses.emplace_back(D, static_cast<int32_t>(I));
			}
		}
		if (Corpses.empty())
		{
			continue;
		}
		std::sort(Corpses.begin(), Corpses.end(), [](const std::pair<Fix64, int32_t>& A, const std::pair<Fix64, int32_t>& B) {
			return A.first < B.first || (A.first == B.first && A.second < B.second);
		});
		if (static_cast<int>(Corpses.size()) > Req.maxCount)
		{
			Corpses.resize(static_cast<size_t>(Req.maxCount));
		}

		const int SquadIndex = static_cast<int>(SquadsList.size());
		Squad Sq;
		Sq.id = SquadIndex + 1;
		Sq.side = Req.side;
		InitSquadStats(Sq, Req.troop, Req.tier);
		const TroopDef& TD = ContentRef.troops[static_cast<size_t>(Req.troop)];
		Sq.formation = TD.AllowsFormation(Formation::Loose) ? Formation::Loose : Formation::Line;
		Sq.stance = Stance::Aggressive;
		Sq.summoned = true;
		Sq.autoControl = true;
		Sq.expireTick = Tick + std::max(1, Req.lifetimeTicks);
		Sq.startCount = static_cast<int32_t>(Corpses.size());
		Sq.aliveCount = Sq.startCount;
		Sq.facing = FixVec2(Fix64::Zero(), Req.side == Side::Attacker ? Fix64::One() : -Fix64::One());
		FixVec2 Sum;
		for (size_t K = 0; K < Corpses.size(); ++K)
		{
			Soldier& Body = SoldiersList[static_cast<size_t>(Corpses[K].second)];
			Body.raised = true;
			Soldier Sol;
			Sol.squad = SquadIndex;
			Sol.side = Req.side;
			Sol.hp = Sq.hp;
			Sol.maxHp = Sq.hp;
			Sol.slot = static_cast<int32_t>(K);
			Sol.pos = Nav.NearestWalkable(TD.move, Body.pos, 4);
			Sol.facing = Sq.facing;
			Sol.raised = true;
			Sol.attackCooldown = Random.RangeInt(0, std::max(1, Sq.interval / 2));
			Sum += Sol.pos;
			Sq.members.push_back(static_cast<int32_t>(SoldiersList.size()));
			SoldiersList.push_back(Sol);
		}
		Sq.leader = Nav.NearestWalkable(TD.move, Sum / fx::Int(static_cast<int64_t>(Corpses.size())));
		Sq.anchor = Sq.leader;
		Sq.aiNextTick = Tick;
		SquadsList.push_back(Sq);
		RefreshSquadGeometry(SquadsList.back());
		Emit(SimEventType::DeadRaised, Req.side, Sq.id, Sq.startCount, SquadsList.back().centroid);
	}
}

void BattleSimulation::ExpireSummons()
{
	for (Squad& Sq : SquadsList)
	{
		if (!Sq.summoned || Sq.destroyed || Tick < Sq.expireTick)
		{
			continue;
		}
		// The spell fades: the raised crumble where they stand (not a loss for anyone).
		for (const int32_t M : Sq.members)
		{
			Soldier& Sol = SoldiersList[static_cast<size_t>(M)];
			if (Sol.Alive())
			{
				Sol.state = SoldierState::Dead;
				Sol.hp = Fix64::Zero();
				Sol.deathTick = Tick;
				Sol.vel = FixVec2();
				Sol.targetSoldier = -1;
				Sol.targetTitan = -1;
				Sol.raised = true;
			}
		}
		Sq.aliveCount = 0;
		Sq.engagedSquads.clear();
		Sq.fightingTitan = false;
	}
}
} // namespace dot
