// Source/DoTSim/Private/SimCombat.cpp
// Melee engagement and pairing, soldier attacks, ranged fire, projectiles, damage formula, morale.
#include "DoTSim/BattleSimulation.h"

#include <algorithm>

namespace dot
{
namespace
{
const Fix64 kCos45 = Fix64::FromRaw(3037000500LL); // 0.70710678
const Fix64 kCos60 = Fix64::Half();
} // namespace

// --- Engagement -----------------------------------------------------------------

void BattleSimulation::UpdateEngagement()
{
	const RulesDef& R = ContentRef.rules;
	std::vector<bool> WasEngaged(SquadsList.size());
	for (size_t I = 0; I < SquadsList.size(); ++I)
	{
		WasEngaged[I] = !SquadsList[I].engagedSquads.empty();
		SquadsList[I].engagedSquads.clear();
		SquadsList[I].fightingTitan = false;
	}
	for (size_t A = 0; A < SquadsList.size(); ++A)
	{
		Squad& SA = SquadsList[A];
		if (!SA.Active() || SA.Routing() || SA.order == OrderType::CycleWithdraw)
		{
			continue;
		}
		for (size_t B = A + 1; B < SquadsList.size(); ++B)
		{
			Squad& SB = SquadsList[B];
			if (SB.side == SA.side || !SB.Active() || SB.Routing() || SB.order == OrderType::CycleWithdraw)
			{
				continue;
			}
			if (fx::Dist(SA.centroid, SB.centroid) - SA.radius - SB.radius <= R.engageDistance)
			{
				SA.engagedSquads.push_back(static_cast<int32_t>(B));
				SB.engagedSquads.push_back(static_cast<int32_t>(A));
			}
		}
	}
	for (size_t I = 0; I < SquadsList.size(); ++I)
	{
		Squad& Sq = SquadsList[I];
		if (!Sq.Active())
		{
			continue;
		}
		if (!Sq.engagedSquads.empty())
		{
			Sq.lastMeleeTick = Tick;
			if (!WasEngaged[I])
			{
				Sq.meleeStartTick = Tick;
				const Squad& Primary = SquadsList[static_cast<size_t>(Sq.engagedSquads.front())];
				const FixVec2 Dir = (Primary.centroid - Sq.centroid).Normalized();
				if (!Dir.IsZero())
				{
					Sq.facing = Dir;
				}
			}
			continue;
		}
		Sq.meleeStartTick = -1;
		// Melee squads targeting a nearby titan fight it.
		const int32_t Target = ResolveSquadTarget(Sq);
		if (!TroopOf(Sq).ranged && IsTitanId(Target) && !Sq.Routing())
		{
			const Titan& T = TitansList[static_cast<size_t>(TitanIndexFromId(Target))];
			if (T.Alive() && fx::Dist(T.pos, Sq.centroid) <= Sq.radius + T.radius + fx::Int(10))
			{
				Sq.fightingTitan = true;
				Sq.lastMeleeTick = Tick;
			}
		}
	}

	// Pairing at 5 Hz (staggered: half of the squads on alternating passes would be uneven, so all at once).
	if (Tick % R.pairingIntervalTicks != 0)
	{
		return;
	}
	for (Soldier& S : SoldiersList)
	{
		S.attackers = 0;
	}
	for (Soldier& S : SoldiersList)
	{
		if (S.Alive() && S.targetSoldier >= 0)
		{
			Soldier& T = SoldiersList[static_cast<size_t>(S.targetSoldier)];
			if (T.Alive())
			{
				++T.attackers;
			}
		}
	}
	for (Squad& Sq : SquadsList)
	{
		if (!Sq.Active())
		{
			continue;
		}
		if (!Sq.engagedSquads.empty())
		{
			PairMelee(Sq);
			continue;
		}
		const int32_t Target = ResolveSquadTarget(Sq);
		const int TitanIndex = Sq.fightingTitan ? TitanIndexFromId(Target) : -1;
		for (const int32_t M : Sq.members)
		{
			Soldier& S = SoldiersList[static_cast<size_t>(M)];
			S.targetSoldier = -1;
			S.targetTitan = TitanIndex;
		}
	}
}

void BattleSimulation::PairMelee(Squad& Sq)
{
	const RulesDef& R = ContentRef.rules;
	for (const int32_t M : Sq.members)
	{
		Soldier& S = SoldiersList[static_cast<size_t>(M)];
		S.targetTitan = -1;
		if (!S.CanAct())
		{
			continue;
		}
		bool bValid = false;
		if (S.targetSoldier >= 0)
		{
			const Soldier& T = SoldiersList[static_cast<size_t>(S.targetSoldier)];
			bValid = T.Alive() && std::find(Sq.engagedSquads.begin(), Sq.engagedSquads.end(), T.squad) != Sq.engagedSquads.end();
		}
		if (bValid)
		{
			continue;
		}
		S.targetSoldier = -1;
		int Best = -1;
		Fix64 BestDist;
		for (const int32_t E : Sq.engagedSquads)
		{
			for (const int32_t Candidate : SquadsList[static_cast<size_t>(E)].members)
			{
				const Soldier& C = SoldiersList[static_cast<size_t>(Candidate)];
				if (!C.Alive() || C.attackers >= R.maxAttackersPerSoldier)
				{
					continue;
				}
				const Fix64 D = fx::DistSq(C.pos, S.pos);
				if (Best < 0 || D < BestDist)
				{
					Best = Candidate;
					BestDist = D;
				}
			}
		}
		if (Best >= 0)
		{
			S.targetSoldier = Best;
			++SoldiersList[static_cast<size_t>(Best)].attackers;
		}
	}
}

// --- Attacks ----------------------------------------------------------------------

void BattleSimulation::UpdateSoldierAttacks()
{
	for (size_t I = 0; I < SoldiersList.size(); ++I)
	{
		Soldier& S = SoldiersList[I];
		if (!S.CanAct())
		{
			continue;
		}
		if (S.attackCooldown > 0)
		{
			--S.attackCooldown;
			continue;
		}
		SoldierAttack(static_cast<int>(I));
	}
}

void BattleSimulation::SoldierAttack(int Index)
{
	const RulesDef& R = ContentRef.rules;
	Soldier& S = SoldiersList[static_cast<size_t>(Index)];
	Squad& Sq = SquadsList[static_cast<size_t>(S.squad)];
	const TroopDef& TD = TroopOf(Sq);
	if (Sq.Routing())
	{
		return;
	}
	const Fix64 SpeedMult = Fix64::One() + SquadStat(Sq, StatType::AttackSpeedPct);
	const int32_t Cooldown = std::max(1, static_cast<int>((fx::Int(Sq.interval) / fx::Max(SpeedMult, fx::Pct(20))).RoundToInt()));

	if (S.targetSoldier >= 0)
	{
		// Keep the index: brace damage below may kill the attacker, which clears its target.
		const int TargetIndex = S.targetSoldier;
		Soldier& T = SoldiersList[static_cast<size_t>(TargetIndex)];
		if (!T.Alive())
		{
			S.targetSoldier = -1;
			return;
		}
		if (T.state == SoldierState::Airborne)
		{
			return;
		}
		Squad& TSq = SquadsList[static_cast<size_t>(T.squad)];
		const TroopDef& TTD = TroopOf(TSq);
		const Fix64 MeleeReach = (TD.ranged ? fx::Milli(1500) : Sq.range) + TD.collisionRadius + TTD.collisionRadius + fx::Milli(200);
		if (fx::DistSq(S.pos, T.pos) > MeleeReach * MeleeReach)
		{
			return;
		}
		Fix64 Extra = TD.ranged ? fx::Pct(50) : Fix64::One();
		bool bChargeHit = false;
		if (S.chargeArmed && TD.chargeMult > Fix64::Zero())
		{
			bChargeHit = true;
			S.chargeArmed = false;
			Extra = Extra * TD.chargeMult * (Sq.formation == Formation::Wedge ? R.wedgeChargeMult : Fix64::One());
		}
		const Fix64 Damage = ComputeTroopDamage(Sq, S, T, TSq, false, Extra);
		if (bChargeHit)
		{
			const bool bBraced = TTD.brace && (TSq.formation == Formation::Square || TSq.stance == Stance::Hold || !TSq.moving);
			if (bBraced)
			{
				// Bracing pikes punish the charger.
				const Fix64 Brace = TSq.atk * TTD.braceDamageMult * ContentRef.counters.Get(TTD.counterRow, TD.cls);
				DamageSoldier(Index, Brace, TSq.side, -1);
			}
			else if (TTD.cls != DefenderClass::Cavalry && TSq.formation != Formation::Square)
			{
				KnockDown(TargetIndex, TD.chargeKnockdownTicks);
			}
			const FixVec2 Dir = (S.pos - T.pos).Normalized();
			if (Dir.Dot(TSq.facing) < kCos45 && Tick - TSq.lastFlankMoraleTick > kTicksPerSecond * 2)
			{
				TSq.lastFlankMoraleTick = Tick;
				ChangeMorale(TSq, -R.moraleChargedFlank);
				Emit(SimEventType::ChargeImpact, Sq.side, Sq.id, TSq.id, T.pos, 1);
			}
		}
		S.lastAttackTick = Tick;
		S.attackCooldown = Cooldown + Random.RangeInt(0, 3);
		DamageSoldier(TargetIndex, Damage, Sq.side, -1);
		return;
	}

	if (S.targetTitan >= 0 && !TD.ranged)
	{
		Titan& T = TitansList[static_cast<size_t>(S.targetTitan)];
		if (!T.Alive())
		{
			S.targetTitan = -1;
			return;
		}
		const Fix64 Reach = T.radius + Sq.range + TD.collisionRadius + fx::Milli(300);
		if (fx::DistSq(S.pos, T.pos) > Reach * Reach)
		{
			return;
		}
		const Fix64 Damage = ComputeTroopVsTitanDamage(Sq, T, false);
		S.lastAttackTick = Tick;
		S.attackCooldown = Cooldown + Random.RangeInt(0, 3);
		DamageTitan(S.targetTitan, Damage, Damage * R.poiseFactorTroops, S.pos, -1);
		return;
	}

	if (TD.ranged)
	{
		const int32_t Target = ResolveSquadTarget(Sq);
		if (Target != 0 && UnitAlive(Target))
		{
			FireRanged(Index, Sq, Target);
		}
	}
}

void BattleSimulation::FireRanged(int Index, Squad& Sq, int32_t TargetUnit)
{
	const RulesDef& R = ContentRef.rules;
	Soldier& S = SoldiersList[static_cast<size_t>(Index)];
	const TroopDef& TD = TroopOf(Sq);
	const bool bTitan = IsTitanId(TargetUnit);
	const int TargetIndex = bTitan ? TitanIndexFromId(TargetUnit) : SquadIndexFromId(TargetUnit);
	if (TargetIndex < 0)
	{
		return;
	}
	if (!bTitan && !IsSquadVisibleTo(Sq.side, TargetIndex))
	{
		return;
	}
	const FixVec2 TargetPos = bTitan ? TitansList[static_cast<size_t>(TargetIndex)].pos : SquadsList[static_cast<size_t>(TargetIndex)].centroid;
	const Fix64 TargetRadius = bTitan ? TitansList[static_cast<size_t>(TargetIndex)].radius : SquadsList[static_cast<size_t>(TargetIndex)].radius;
	Fix64 Range = Sq.range;
	if (Nav.HeightAt(S.pos) >= Nav.HeightAt(TargetPos) + R.elevationThreshold)
	{
		Range = Range * (Fix64::One() + R.elevationRangeBonus);
	}
	const Fix64 Dist = fx::Dist(S.pos, TargetPos);
	if (Dist > Range + TargetRadius || Dist < TD.minRange)
	{
		return;
	}

	Fix64 Chance;
	if (bTitan)
	{
		Chance = TD.accuracyVsTitan;
	}
	else
	{
		const Fix64 EdgeDist = fx::Max(Fix64::Zero(), Dist - TargetRadius / 2);
		if (EdgeDist <= TD.accuracyNearRange || Range <= TD.accuracyNearRange)
		{
			Chance = TD.accuracyNear;
		}
		else
		{
			const Fix64 T = fx::Clamp01((EdgeDist - TD.accuracyNearRange) / (Range - TD.accuracyNearRange));
			Chance = fx::Lerp(TD.accuracyNear, TD.accuracyFar, T);
		}
		if (SquadsList[static_cast<size_t>(TargetIndex)].formation == Formation::Loose)
		{
			Chance = Chance * R.looseAccuracyMult;
		}
	}
	if (Nav.FlagsAt(S.pos) & cellflag::Forest)
	{
		Chance = Chance * R.forestAccuracyMult;
	}
	Chance = fx::Clamp(Chance, fx::Pct(5), fx::Pct(98));

	Projectile P;
	P.kind = ProjectileKind::Arrow;
	P.side = Sq.side;
	P.from = S.pos;
	P.fromHeight = fx::Milli(1600) + Nav.HeightAt(S.pos);
	P.fireTick = Tick;
	P.sourceSquad = Sq.id - 1;
	P.hit = Random.Chance(Chance);
	if (bTitan)
	{
		P.targetTitan = TargetIndex;
		P.to = TargetPos;
	}
	else
	{
		const Squad& TSq = SquadsList[static_cast<size_t>(TargetIndex)];
		P.targetSquad = TargetIndex;
		int AliveCount = 0;
		for (const int32_t M : TSq.members)
		{
			AliveCount += SoldiersList[static_cast<size_t>(M)].Alive() ? 1 : 0;
		}
		if (AliveCount == 0)
		{
			return;
		}
		int Pick = Random.RangeInt(0, AliveCount - 1);
		for (const int32_t M : TSq.members)
		{
			if (SoldiersList[static_cast<size_t>(M)].Alive() && Pick-- == 0)
			{
				P.targetSoldier = M;
				break;
			}
		}
		P.to = SoldiersList[static_cast<size_t>(P.targetSoldier)].pos;
		if (!P.hit)
		{
			P.to = P.to + FixVec2(Random.Range(-fx::Int(3), fx::Int(3)), Random.Range(-fx::Int(3), fx::Int(3)));
		}
	}
	const Fix64 Speed = fx::Max(TD.projectileSpeed, Fix64::One());
	P.impactTick = Tick + std::max(1, static_cast<int>((Dist * kTicksPerSecond / Speed).RoundToInt()));
	ProjectilesList.push_back(P);

	const Fix64 SpeedMult = Fix64::One() + SquadStat(Sq, StatType::AttackSpeedPct);
	S.attackCooldown = std::max(1, static_cast<int>((fx::Int(Sq.interval) / fx::Max(SpeedMult, fx::Pct(20))).RoundToInt())) + Random.RangeInt(0, 4);
	S.lastAttackTick = Tick;
	S.facing = (TargetPos - S.pos).Normalized();
}

void BattleSimulation::UpdateProjectiles()
{
	const RulesDef& R = ContentRef.rules;
	for (size_t I = 0; I < ProjectilesList.size(); ++I)
	{
		Projectile P = ProjectilesList[I];
		if (!P.active || Tick < P.impactTick)
		{
			continue;
		}
		ProjectilesList[I].active = false;
		if (P.kind == ProjectileKind::ThrownSoldier)
		{
			if (P.sourceTitan < 0)
			{
				continue;
			}
			const Titan& T = TitansList[static_cast<size_t>(P.sourceTitan)];
			QuerySoldiers(P.to, R.throwSplash, QueryScratch);
			const std::vector<int32_t> Hit = QueryScratch;
			for (const int32_t N : Hit)
			{
				const Soldier& S = SoldiersList[static_cast<size_t>(N)];
				if (!S.Alive() || S.side == P.side)
				{
					continue;
				}
				const Fix64 Damage = TitanDamageToSoldier(T, S, R.throwCoef, true, false);
				Knockback(N, S.pos - P.to, R.smashKnockH, R.smashKnockV);
				DamageSoldier(N, Damage, P.side, P.sourceTitan);
			}
			Emit(SimEventType::TitanThrowImpact, P.side, T.id, 0, P.to);
			continue;
		}
		if (!P.hit || P.sourceSquad < 0)
		{
			continue;
		}
		const Squad& Source = SquadsList[static_cast<size_t>(P.sourceSquad)];
		if (P.targetTitan >= 0)
		{
			Titan& T = TitansList[static_cast<size_t>(P.targetTitan)];
			if (T.Alive())
			{
				const Fix64 Damage = ComputeTroopVsTitanDamage(Source, T, true);
				DamageTitan(P.targetTitan, Damage, Damage * R.poiseFactorTroops, P.from, -1);
			}
			continue;
		}
		int Victim = P.targetSoldier;
		if (Victim < 0 || !SoldiersList[static_cast<size_t>(Victim)].Alive())
		{
			Victim = -1;
			if (P.targetSquad >= 0)
			{
				for (const int32_t M : SquadsList[static_cast<size_t>(P.targetSquad)].members)
				{
					if (SoldiersList[static_cast<size_t>(M)].Alive())
					{
						Victim = M;
						break;
					}
				}
			}
		}
		if (Victim < 0)
		{
			continue;
		}
		Soldier Shooter;
		Shooter.pos = P.from;
		Shooter.side = P.side;
		const Soldier& V = SoldiersList[static_cast<size_t>(Victim)];
		const Fix64 Damage = ComputeTroopDamage(Source, Shooter, V, SquadsList[static_cast<size_t>(V.squad)], true, Fix64::One());
		DamageSoldier(Victim, Damage, P.side, -1);
	}
	ProjectilesList.erase(std::remove_if(ProjectilesList.begin(), ProjectilesList.end(), [](const Projectile& P) { return !P.active; }),
		ProjectilesList.end());
}

// --- Damage ----------------------------------------------------------------------

Fix64 BattleSimulation::ComputeTroopDamage(const Squad& Attacker, const Soldier& AttackerSoldier, const Soldier& Defender, const Squad& DefenderSquad,
	bool bRanged, Fix64 ExtraMult)
{
	const RulesDef& R = ContentRef.rules;
	const TroopDef& ATD = TroopOf(Attacker);
	const TroopDef& DTD = TroopOf(DefenderSquad);
	Fix64 Mult = ExtraMult;
	if (Attacker.moraleState == MoraleState::Shaken)
	{
		Mult = Mult * R.shakenAtkMult;
	}
	else if (Attacker.moraleState == MoraleState::Wavering)
	{
		Mult = Mult * R.waveringAtkMult;
	}
	if (Tick < Attacker.formationChangeUntil)
	{
		Mult = Mult * R.formationChangeDamageMult;
	}
	if (!bRanged && Attacker.formation == Formation::Loose)
	{
		Mult = Mult * R.looseMeleeDealtMult;
	}
	if (!bRanged && ATD.strengthInNumbers && Attacker.aliveCount > DefenderSquad.aliveCount)
	{
		Mult = Mult * (Fix64::One() + ATD.strengthInNumbersBonus);
	}
	Mult = Mult * ContentRef.counters.Get(ATD.counterRow, DTD.cls);

	const FixVec2 ToAttacker = (AttackerSoldier.pos - Defender.pos).Normalized();
	const Fix64 FacingDot = ToAttacker.Dot(DefenderSquad.facing);
	if (!bRanged && DefenderSquad.formation != Formation::Square)
	{
		if (FacingDot <= -kCos45)
		{
			Mult = Mult * R.rearMult;
		}
		else if (FacingDot < kCos45)
		{
			Mult = Mult * R.flankMult;
			if (DefenderSquad.formation == Formation::Column)
			{
				Mult = Mult * R.columnFlankTakenMult;
			}
		}
	}
	const Fix64 HeightA = Nav.HeightAt(AttackerSoldier.pos);
	const Fix64 HeightD = Nav.HeightAt(Defender.pos);
	if (bRanged && HeightA >= HeightD + R.elevationThreshold)
	{
		Mult = Mult * R.elevationRangedMult;
	}
	else if (!bRanged && HeightD >= HeightA + R.elevationThreshold)
	{
		Mult = Mult * R.uphillMeleeMult;
	}
	if (Random.Chance(ATD.critChance))
	{
		Mult = Mult * ATD.critDamage;
	}
	Mult = Mult * Random.Range(R.varianceMin, R.varianceMax);

	Fix64 Armor = DefenderSquad.armor + SquadStat(DefenderSquad, StatType::ArmorAdd);
	if (DefenderSquad.formation == Formation::Wedge)
	{
		Armor += R.wedgeArmorAdd;
	}
	Armor = fx::Clamp(Armor, Fix64::Zero(), fx::Pct(75));
	Fix64 Damage = Attacker.atk * Mult * (Fix64::One() - Armor);
	if (bRanged && DTD.shieldBlock && FacingDot >= kCos60)
	{
		Damage = Damage * (Fix64::One() - DTD.shieldBlockReduction);
	}
	if (bRanged && (Nav.FlagsAt(Defender.pos) & cellflag::Ruins))
	{
		Damage = Damage * R.ruinsRangedTakenMult;
	}
	Damage = Damage * (Fix64::One() + SquadStat(DefenderSquad, StatType::DamageTakenPct));
	return fx::Max(Damage, Fix64::One());
}

Fix64 BattleSimulation::ComputeTroopVsTitanDamage(const Squad& Attacker, const Titan& Target, bool bRanged)
{
	const RulesDef& R = ContentRef.rules;
	const TroopDef& ATD = TroopOf(Attacker);
	Fix64 Mult = ContentRef.counters.Get(ATD.counterRow, DefenderClass::Titan);
	if (Attacker.moraleState == MoraleState::Shaken)
	{
		Mult = Mult * R.shakenAtkMult;
	}
	else if (Attacker.moraleState == MoraleState::Wavering)
	{
		Mult = Mult * R.waveringAtkMult;
	}
	if (Random.Chance(ATD.critChance))
	{
		Mult = Mult * ATD.critDamage;
	}
	Mult = Mult * Random.Range(R.varianceMin, R.varianceMax);
	const Fix64 Damage = Attacker.atk * Mult * (Fix64::One() - TitanEffectiveArmor(Target, bRanged)) * TitanDamageTakenMult(Target);
	return fx::Max(Damage, Fix64::One());
}

void BattleSimulation::DamageSoldier(int Index, Fix64 Amount, Side AttackerSide, int AttackerTitan)
{
	(void)AttackerSide;
	Soldier& S = SoldiersList[static_cast<size_t>(Index)];
	if (!S.Alive() || Amount <= Fix64::Zero())
	{
		return;
	}
	S.hp -= Amount;
	if (S.hp <= Fix64::Zero())
	{
		KillSoldier(Index, AttackerTitan);
	}
}

void BattleSimulation::KillSoldier(int Index, int AttackerTitan)
{
	const RulesDef& R = ContentRef.rules;
	Soldier& S = SoldiersList[static_cast<size_t>(Index)];
	if (!S.Alive())
	{
		return;
	}
	S.hp = Fix64::Zero();
	S.state = SoldierState::Dead;
	S.deathTick = Tick;
	S.vel = FixVec2();
	S.targetSoldier = -1;
	S.targetTitan = -1;
	Squad& Sq = SquadsList[static_cast<size_t>(S.squad)];
	Sq.aliveCount = std::max(0, Sq.aliveCount - 1);
	ChangeMorale(Sq, -(R.moraleLossPerPctLost * 100 / fx::Int(std::max(1, Sq.startCount))));
	if (Sq.summoned)
	{
		S.raised = true; // a fallen skeleton stays down
	}
	else
	{
		Sides[SideIndex(S.side)].destroyedPower += Sq.powerPerSoldier;
	}
	Emit(SimEventType::SoldierDied, S.side, Index, AttackerTitan, S.pos);
	if (AttackerTitan >= 0)
	{
		Titan& T = TitansList[static_cast<size_t>(AttackerTitan)];
		const int32_t Second = Tick / kTicksPerSecond;
		if (T.killRageSecond != Second)
		{
			T.killRageSecond = Second;
			T.killRageThisSecond = Fix64::Zero();
		}
		if (T.killRageThisSecond < R.rageMaxKillPerSec)
		{
			T.killRageThisSecond += R.ragePerKill;
			T.rage = fx::Min(fx::Int(100), T.rage + R.ragePerKill);
		}
	}
}

// --- Morale & status ----------------------------------------------------------------

void BattleSimulation::ChangeMorale(Squad& Sq, Fix64 Delta)
{
	if (Sq.summoned)
	{
		return; // the raised dead know no fear
	}
	Sq.morale = fx::Clamp(Sq.morale + Delta, Fix64::Zero(), fx::Int(100));
}

void BattleSimulation::UpdateMorale()
{
	const RulesDef& R = ContentRef.rules;
	for (size_t I = 0; I < SquadsList.size(); ++I)
	{
		Squad& Sq = SquadsList[I];
		if (!Sq.Active())
		{
			continue;
		}
		if (Sq.Routing())
		{
			ChangeMorale(Sq, R.moraleRoutRegenPerSec / kTicksPerSecond);
			if (Sq.morale >= R.moraleRally)
			{
				Sq.moraleState = MoraleState::Shaken;
				Sq.order = OrderType::None;
				Sq.orderTarget = 0;
				Sq.autoTarget = 0;
				Sq.path.clear();
				Sq.anchor = Sq.leader;
				Emit(SimEventType::SquadRallied, Sq.side, Sq.id, 0, Sq.centroid);
			}
			continue;
		}
		if (Tick - Sq.lastMeleeTick >= R.moraleRegenDelayTicks)
		{
			ChangeMorale(Sq, R.moraleRegenPerSec / kTicksPerSecond);
		}
		MoraleState NewState = MoraleState::Steady;
		if (Sq.morale <= Fix64::Zero())
		{
			NewState = MoraleState::Routing;
		}
		else if (Sq.morale < R.moraleWavering)
		{
			NewState = MoraleState::Wavering;
		}
		else if (Sq.morale < R.moraleShaken)
		{
			NewState = MoraleState::Shaken;
		}
		if (NewState == MoraleState::Routing)
		{
			Sq.moraleState = MoraleState::Routing;
			Sq.order = OrderType::None;
			Sq.path.clear();
			Sq.engagedSquads.clear();
			Sq.fightingTitan = false;
			for (const int32_t M : Sq.members)
			{
				SoldiersList[static_cast<size_t>(M)].targetSoldier = -1;
				SoldiersList[static_cast<size_t>(M)].targetTitan = -1;
			}
			Emit(SimEventType::SquadRouted, Sq.side, Sq.id, 0, Sq.centroid);
			for (Squad& Other : SquadsList)
			{
				if (&Other != &Sq && Other.side == Sq.side && Other.Active() && !Other.Routing() &&
					fx::Dist(Other.centroid, Sq.centroid) <= R.moraleFriendlyRoutRadius)
				{
					ChangeMorale(Other, -R.moraleFriendlyRout);
				}
			}
		}
		else
		{
			Sq.moraleState = NewState;
		}
	}
}

void BattleSimulation::UpdateSoldierStatus()
{
	for (size_t I = 0; I < SoldiersList.size(); ++I)
	{
		Soldier& S = SoldiersList[I];
		if (!S.Alive())
		{
			continue;
		}
		if (S.stunTicks > 0)
		{
			--S.stunTicks;
		}
		if (S.burnTicks > 0)
		{
			--S.burnTicks;
			DamageSoldier(static_cast<int>(I), S.burnDps / kTicksPerSecond, Opponent(S.side), -1);
		}
	}
}

void BattleSimulation::ExpireStatuses()
{
	auto Expired = [this](const StatusEffect& E) { return E.endTick <= Tick; };
	for (Squad& Sq : SquadsList)
	{
		Sq.statuses.erase(std::remove_if(Sq.statuses.begin(), Sq.statuses.end(), Expired), Sq.statuses.end());
	}
	for (Titan& T : TitansList)
	{
		T.statuses.erase(std::remove_if(T.statuses.begin(), T.statuses.end(), Expired), T.statuses.end());
	}
}

void BattleSimulation::CleanupDead()
{
	for (Squad& Sq : SquadsList)
	{
		if (Sq.destroyed || Sq.leftField)
		{
			continue;
		}
		AssignSlots(Sq);
		if (Sq.aliveCount == 0)
		{
			Sq.destroyed = true;
			Emit(SimEventType::SquadDestroyed, Sq.side, Sq.id, 0, Sq.centroid);
		}
	}
	ExpireStatuses();
}
} // namespace dot
