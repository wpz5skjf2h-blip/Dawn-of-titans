// Source/DoTSim/Private/SimAI.cpp
// Commander AI (Section 21): utility-based squad targeting, flanking, screening, focus fire,
// formation micro, titan abilities and spells. The AI only issues BattleCommands, exactly like a player.
#include "DoTSim/BattleSimulation.h"

#include <algorithm>

namespace dot
{
void BattleSimulation::Schedule(const BattleCommand& Cmd, int DelayTicks)
{
	BattleCommand C = Cmd;
	C.tick = Tick + std::max(0, DelayTicks);
	Scheduled.push_back(C);
}

int BattleSimulation::CountEnemySoldiersNear(Side S, FixVec2 P, Fix64 Radius) const
{
	QuerySoldiers(P, Radius, QueryScratch);
	int Count = 0;
	for (const int32_t N : QueryScratch)
	{
		const Soldier& Sol = SoldiersList[static_cast<size_t>(N)];
		Count += (Sol.side != S && Sol.Alive()) ? 1 : 0;
	}
	return Count;
}

int BattleSimulation::CountEnemyInAbilityShape(Side S, const AbilityDef& A, FixVec2 Origin, FixVec2 Point, FixVec2 Dir) const
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
	int Count = 0;
	for (const int32_t N : QueryScratch)
	{
		const Soldier& Sol = SoldiersList[static_cast<size_t>(N)];
		if (Sol.side != S && Sol.Alive() && InShape(A, Origin, Point, Dir, Sol.pos, Fix64::Zero()))
		{
			++Count;
		}
	}
	return Count;
}

Fix64 BattleSimulation::TargetScore(const Squad& Sq, int32_t TargetUnit, const AiProfileDef* Profile) const
{
	(void)Profile;
	const TroopDef& TD = TroopOf(Sq);
	if (IsTitanId(TargetUnit))
	{
		const Titan& T = TitansList[static_cast<size_t>(TitanIndexFromId(TargetUnit))];
		const Fix64 Counter = ContentRef.counters.Get(TD.counterRow, DefenderClass::Titan);
		Fix64 Score = Counter * 100 - fx::Dist(T.pos, Sq.centroid);
		if (Counter >= fx::Int(2))
		{
			Score += fx::Int(40);
		}
		if (Tick < T.staggerUntil)
		{
			Score += fx::Int(30);
		}
		return Score;
	}
	const Squad& E = SquadsList[static_cast<size_t>(SquadIndexFromId(TargetUnit))];
	const TroopDef& ETD = TroopOf(E);
	Fix64 Score = ContentRef.counters.Get(TD.counterRow, ETD.cls) * 100 - fx::Dist(E.centroid, Sq.centroid);
	if (E.InMelee())
	{
		Score += fx::Int(15);
	}
	if (TD.IsCavalry())
	{
		if (ETD.cls == DefenderClass::Ranged || ETD.cls == DefenderClass::Siege || ETD.cls == DefenderClass::Support)
		{
			Score += fx::Int(30);
		}
		if (ETD.brace && (E.formation == Formation::Square || E.stance == Stance::Hold || !E.moving))
		{
			Score -= fx::Int(80);
		}
	}
	if (E.Routing())
	{
		Score -= fx::Int(40);
	}
	return Score;
}

void BattleSimulation::UpdateAI()
{
	const int Veteran = ContentRef.FindAiProfile("veteran");
	for (int SI = 0; SI < 2; ++SI)
	{
		const Side S = static_cast<Side>(SI);
		const SideState& State = Sides[SI];
		if (State.aiProfile >= 0)
		{
			UpdateCommanderAI(S, ContentRef.aiProfiles[static_cast<size_t>(State.aiProfile)]);
			continue;
		}
		if (Veteran < 0)
		{
			continue;
		}
		// Player side: only squads (and the titan) switched to Auto are driven by the AI.
		const AiProfileDef& Profile = ContentRef.aiProfiles[static_cast<size_t>(Veteran)];
		for (size_t I = 0; I < SquadsList.size(); ++I)
		{
			Squad& Sq = SquadsList[I];
			if (Sq.side == S && Sq.autoControl && Sq.Active() && Tick >= Sq.aiNextTick)
			{
				Sq.aiNextTick = Tick + Profile.decisionTicks + static_cast<int32_t>(I % 5);
				AiDecideSquad(S, static_cast<int>(I), Profile);
			}
		}
		for (const Titan& T : TitansList)
		{
			if (T.side == S && T.autoControl && T.Alive() && Tick >= Sides[SI].aiTitanNextTick)
			{
				Sides[SI].aiTitanNextTick = Tick + Profile.decisionTicks;
				AiDecideTitan(S, Profile);
			}
		}
	}
}

void BattleSimulation::UpdateCommanderAI(Side S, const AiProfileDef& Profile)
{
	SideState& State = Sides[SideIndex(S)];
	for (size_t I = 0; I < SquadsList.size(); ++I)
	{
		Squad& Sq = SquadsList[I];
		if (Sq.side == S && Sq.Active() && Tick >= Sq.aiNextTick)
		{
			Sq.aiNextTick = Tick + Profile.decisionTicks + static_cast<int32_t>(I % 5);
			AiDecideSquad(S, static_cast<int>(I), Profile);
		}
	}
	if (Tick >= State.aiTitanNextTick)
	{
		State.aiTitanNextTick = Tick + Profile.decisionTicks;
		AiDecideTitan(S, Profile);
	}
	if (Tick >= State.aiSpellNextTick)
	{
		State.aiSpellNextTick = Tick + Profile.decisionTicks * 2;
		AiDecideSpells(S, Profile);
	}
}

void BattleSimulation::AiDecideSquad(Side S, int SquadIndex, const AiProfileDef& Profile)
{
	Squad& Sq = SquadsList[static_cast<size_t>(SquadIndex)];
	if (Sq.Routing() || Sq.order == OrderType::CycleWithdraw || Sq.order == OrderType::Skirmish)
	{
		return;
	}
	const TroopDef& TD = TroopOf(Sq);
	BattleCommand Cmd;
	Cmd.side = S;
	Cmd.unitId = Sq.id;

	// Formation micro: spread out when an enemy titan is coming.
	if (Profile.formationMicro && TD.AllowsFormation(Formation::Loose))
	{
		bool bTitanNear = false;
		for (const Titan& T : TitansList)
		{
			if (T.side != S && T.Alive() && fx::Dist(T.pos, Sq.centroid) <= fx::Int(25) + T.radius)
			{
				bTitanNear = true;
			}
		}
		if (bTitanNear != (Sq.formation == Formation::Loose))
		{
			Cmd.type = CommandType::SetFormation;
			Cmd.param = static_cast<int32_t>(bTitanNear ? Formation::Loose : Formation::Line);
			Schedule(Cmd, Profile.reactionTicks);
		}
	}
	// Regroup: wavering squads fall back toward their own lines.
	if (Profile.formationMicro && Sq.moraleState == MoraleState::Wavering && !Sq.InMelee())
	{
		Cmd.type = CommandType::Move;
		const Fix64 Back = S == Side::Attacker ? fx::Int(-20) : fx::Int(20);
		Cmd.point = Nav.ClampToWorld(Sq.centroid + FixVec2(Fix64::Zero(), Back), fx::Int(4));
		Sq.aiTarget = 0;
		Schedule(Cmd, Profile.reactionTicks);
		return;
	}
	if (Sq.InMelee() && !TD.ranged)
	{
		return; // keep fighting
	}

	// Candidate targets.
	int32_t Best = 0;
	Fix64 BestScore;
	int32_t Nearest = 0;
	Fix64 NearestDist;
	for (const Squad& E : SquadsList)
	{
		if (E.side == S || !E.Active() || !IsSquadVisibleTo(S, E.id - 1))
		{
			continue;
		}
		const Fix64 Score = TargetScore(Sq, E.id, &Profile);
		const Fix64 D = fx::Dist(E.centroid, Sq.centroid);
		if (Best == 0 || Score > BestScore)
		{
			Best = E.id;
			BestScore = Score;
		}
		if (Nearest == 0 || D < NearestDist)
		{
			Nearest = E.id;
			NearestDist = D;
		}
	}
	for (const Titan& T : TitansList)
	{
		if (T.side == S || !T.Alive())
		{
			continue;
		}
		const Fix64 Score = TargetScore(Sq, T.id, &Profile);
		if (Best == 0 || Score > BestScore)
		{
			Best = T.id;
			BestScore = Score;
		}
		const bool bFocus = Profile.focusFire >= 1 && Tick < T.staggerUntil && (TD.ranged || ContentRef.counters.Get(TD.counterRow, DefenderClass::Titan) >= fx::Int(2));
		if (bFocus)
		{
			Best = T.id;
			BestScore = fx::Int(10000);
		}
	}
	// Screen friendly ranged units from enemy cavalry.
	if (!TD.ranged && !TD.IsCavalry())
	{
		for (const Squad& Friend : SquadsList)
		{
			if (Friend.side != S || !Friend.Active() || !TroopOf(Friend).ranged)
			{
				continue;
			}
			for (const Squad& E : SquadsList)
			{
				if (E.side != S && E.Active() && TroopOf(E).IsCavalry() && IsSquadVisibleTo(S, E.id - 1) &&
					fx::Dist(E.centroid, Friend.centroid) <= fx::Int(40) && fx::Dist(E.centroid, Sq.centroid) <= fx::Int(60))
				{
					const Fix64 Screen = TargetScore(Sq, E.id, &Profile) + fx::Int(60);
					if (Screen > BestScore)
					{
						Best = E.id;
						BestScore = Screen;
					}
				}
			}
		}
	}
	if (Best != 0 && Nearest != 0 && !Random.Chance(Profile.bestCounterChance))
	{
		Best = Nearest; // lower skill: just go for the closest enemy
	}
	if (Best == 0)
	{
		return;
	}
	if (Best == Sq.aiTarget && (Sq.orderTarget == Best || (Sq.order == OrderType::Move && Sq.attackAfterPath)))
	{
		return;
	}
	Sq.aiTarget = Best;
	const FixVec2 TargetPos = UnitPosition(Best);
	const Fix64 Dist = fx::Dist(TargetPos, Sq.centroid);

	// Cavalry flanks around the side with fewer enemies once the front is engaged.
	bool bFrontEngaged = false;
	for (const Squad& Friend : SquadsList)
	{
		if (Friend.side == S && Friend.Active() && Friend.InMelee())
		{
			bFrontEngaged = true;
		}
	}
	if (TD.IsCavalry() && Profile.flanking >= 1 && Dist > fx::Int(40) && (bFrontEngaged || Profile.flanking >= 2))
	{
		const FixVec2 Mid = (TargetPos + Sq.centroid) / 2;
		const FixVec2 Perp = (TargetPos - Sq.centroid).Normalized().Right();
		const FixVec2 Left = Nav.ClampToWorld(Mid - Perp * fx::Int(30), fx::Int(6));
		const FixVec2 RightP = Nav.ClampToWorld(Mid + Perp * fx::Int(30), fx::Int(6));
		const int ThreatLeft = CountEnemySoldiersNear(S, Left, fx::Int(25));
		const int ThreatRight = CountEnemySoldiersNear(S, RightP, fx::Int(25));
		Cmd.type = CommandType::Path;
		Cmd.waypoints.clear();
		Cmd.waypoints.push_back(ThreatLeft <= ThreatRight ? Left : RightP);
		Cmd.waypoints.push_back(TargetPos);
		Cmd.targetId = Best;
		Schedule(Cmd, Profile.reactionTicks);
		return;
	}
	const bool bCharge = TD.IsCavalry() && Dist > fx::Int(20) && Tick >= Sq.chargeReadyTick;
	Cmd.type = bCharge ? CommandType::Charge : CommandType::Attack;
	Cmd.targetId = Best;
	Cmd.point = TargetPos;
	Schedule(Cmd, Profile.reactionTicks);
}

void BattleSimulation::AiDecideTitan(Side S, const AiProfileDef& Profile)
{
	int TitanIndex = -1;
	for (size_t I = 0; I < TitansList.size(); ++I)
	{
		if (TitansList[I].side == S && TitansList[I].Alive())
		{
			TitanIndex = static_cast<int>(I);
		}
	}
	if (TitanIndex < 0)
	{
		return;
	}
	const Titan& T = TitansList[static_cast<size_t>(TitanIndex)];
	if (T.clashIndex >= 0 || Tick < T.staggerUntil || T.state == TitanState::Casting || T.state == TitanState::Leaping)
	{
		return;
	}
	const TitanDef& TD = TitanDefOf(T);
	BattleCommand Cmd;
	Cmd.side = S;
	Cmd.unitId = T.id;

	for (int Slot = 0; Slot < 2; ++Slot)
	{
		const int AbilityIdx = TD.abilities[Slot];
		if (AbilityIdx < 0 || Tick < T.abilityReadyTick[Slot])
		{
			continue;
		}
		const AbilityDef& A = ContentRef.abilities[static_cast<size_t>(AbilityIdx)];
		if (T.rage < fx::Int(A.rageCost))
		{
			continue;
		}
		int BestValue = 0;
		FixVec2 BestPoint;
		if (A.ai == AiHeuristic::Corpses)
		{
			BestValue = BestCorpsePoint(A, T.pos, A.range + A.radius, BestPoint);
		}
		else if (A.ai == AiHeuristic::SelfDefense)
		{
			bool bThreat = false;
			for (const Titan& O : TitansList)
			{
				if (O.side != S && O.Alive() && fx::Dist(O.pos, T.pos) <= fx::Int(15) + O.radius)
				{
					bThreat = true;
				}
			}
			bThreat = bThreat || CountEnemySoldiersNear(S, T.pos, T.radius + fx::Int(3)) >= 10;
			if (bThreat && T.hp < T.maxHp * A.aiHpBelow)
			{
				BestValue = A.aiMinTargets;
				BestPoint = T.pos;
			}
		}
		else
		{
			std::vector<FixVec2> Candidates;
			for (const Squad& E : SquadsList)
			{
				if (E.side != S && E.Active() && IsSquadVisibleTo(S, E.id - 1))
				{
					Candidates.push_back(E.centroid);
				}
			}
			for (const Titan& O : TitansList)
			{
				if (O.side != S && O.Alive())
				{
					Candidates.push_back(O.pos);
				}
			}
			const Fix64 MaxReach = A.shape == TargetShape::Circle ? A.range + A.radius : A.range;
			for (const FixVec2& P : Candidates)
			{
				const Fix64 D = fx::Dist(P, T.pos);
				if (D > MaxReach + fx::Int(2) || D.raw == 0)
				{
					continue;
				}
				const FixVec2 Dir = (P - T.pos) / D;
				// Circles are cast at the target but no farther than the ability's range (as StartAbility clamps).
				const FixVec2 Point = A.shape != TargetShape::Circle ? T.pos + Dir * A.range : (A.range > Fix64::Zero() && D > A.range ? T.pos + Dir * A.range : P);
				int Value = CountEnemyInAbilityShape(S, A, T.pos, Point, Dir);
				for (const Titan& O : TitansList)
				{
					if (O.side != S && O.Alive() && InShape(A, T.pos, Point, Dir, O.pos, O.radius))
					{
						Value += A.aiPreferTitan ? 12 : 4;
					}
				}
				if (Value > BestValue)
				{
					BestValue = Value;
					BestPoint = Point;
				}
			}
		}
		if (BestValue >= A.aiMinTargets)
		{
			Cmd.type = CommandType::CastAbility;
			Cmd.param = Slot;
			Cmd.point = BestPoint;
			Schedule(Cmd, Profile.reactionTicks);
			return;
		}
	}

	// Charge the enemy titan from mid range.
	if (Profile.flanking >= 1 && Tick >= T.chargeReadyTick && T.order == OrderType::None)
	{
		for (const Titan& O : TitansList)
		{
			if (O.side == S || !O.Alive())
			{
				continue;
			}
			const Fix64 D = fx::Dist(O.pos, T.pos);
			if (D >= ContentRef.rules.titanChargeMinDistance && D <= fx::Int(70))
			{
				Cmd.type = CommandType::Charge;
				Cmd.targetId = O.id;
				Cmd.point = O.pos;
				Schedule(Cmd, Profile.reactionTicks);
				return;
			}
		}
	}
}

int BattleSimulation::BestCorpsePoint(const AbilityDef& A, FixVec2 From, Fix64 MaxReach, FixVec2& OutPoint) const
{
	int32_t MaxAge = 0;
	for (const EffectDef& E : A.effects)
	{
		if (E.type == EffectType::RaiseDead)
		{
			MaxAge = std::max(MaxAge, E.corpseAgeTicks);
		}
	}
	// Try every fourth fresh body as a centre: cheap and good enough to find the thickest pile.
	int Best = 0;
	int Seen = 0;
	for (const Soldier& Sol : SoldiersList)
	{
		if (Sol.state != SoldierState::Dead || Sol.raised || Tick - Sol.deathTick > MaxAge)
		{
			continue;
		}
		if ((Seen++ & 3) != 0 || (MaxReach > Fix64::Zero() && fx::Dist(Sol.pos, From) > MaxReach))
		{
			continue;
		}
		const int Count = CountRaisableCorpses(Sol.pos, A.radius, MaxAge);
		if (Count > Best)
		{
			Best = Count;
			OutPoint = Sol.pos;
		}
	}
	return Best;
}

void BattleSimulation::AiDecideSpells(Side S, const AiProfileDef& Profile)
{
	const SideState& State = Sides[SideIndex(S)];
	if (Profile.spellSkill <= 0 && !Random.Chance(fx::Pct(30)))
	{
		return;
	}
	static const int kThresholds[4] = {6, 9, 12, 14};
	const int Threshold = kThresholds[std::max(0, std::min(3, Profile.spellSkill))];
	for (size_t Slot = 0; Slot < State.spells.size(); ++Slot)
	{
		const AbilityDef& A = ContentRef.abilities[static_cast<size_t>(State.spells[Slot])];
		if (Tick < State.spellReadyTick[Slot] || State.valor < fx::Int(A.valorCost) || State.spellCharges[Slot] == 0)
		{
			continue;
		}
		int BestValue = 0;
		FixVec2 BestPoint;
		const FixVec2 Dir(Fix64::Zero(), S == Side::Attacker ? Fix64::One() : -Fix64::One());
		if (A.ai == AiHeuristic::MaxUnitsInArea)
		{
			for (const Squad& E : SquadsList)
			{
				if (E.side == S || !E.Active() || !IsSquadVisibleTo(S, E.id - 1))
				{
					continue;
				}
				const int Value = CountEnemyInAbilityShape(S, A, E.centroid, E.centroid, Dir);
				if (Value > BestValue)
				{
					BestValue = Value;
					BestPoint = E.centroid;
				}
			}
			for (const Titan& O : TitansList)
			{
				if (O.side != S && O.Alive() && Tick < O.staggerUntil)
				{
					const int Value = CountEnemyInAbilityShape(S, A, O.pos, O.pos, Dir) + 14;
					if (Value > BestValue)
					{
						BestValue = Value;
						BestPoint = O.pos;
					}
				}
			}
			if (BestValue < Threshold)
			{
				continue;
			}
		}
		else if (A.ai == AiHeuristic::AlliesInCombat)
		{
			for (const Squad& Own : SquadsList)
			{
				if (Own.side != S || !Own.Active() || !Own.InMelee())
				{
					continue;
				}
				int Value = 0;
				for (const Squad& Other : SquadsList)
				{
					if (Other.side == S && Other.Active() && Other.InMelee() && fx::Dist(Other.centroid, Own.centroid) <= A.radius)
					{
						++Value;
					}
				}
				if (Value > BestValue)
				{
					BestValue = Value;
					BestPoint = Own.centroid;
				}
			}
			if (BestValue < (Profile.spellSkill >= 2 ? 2 : 3))
			{
				continue;
			}
		}
		else if (A.ai == AiHeuristic::Corpses)
		{
			BestValue = BestCorpsePoint(A, FixVec2(), Fix64::Zero(), BestPoint);
			if (BestValue < A.aiMinTargets)
			{
				continue;
			}
		}
		else if (A.ai == AiHeuristic::AlliesDamaged)
		{
			for (const Squad& Own : SquadsList)
			{
				if (Own.side != S || !Own.Active())
				{
					continue;
				}
				Fix64 Missing;
				for (const int32_t M : Own.members)
				{
					const Soldier& Sol = SoldiersList[static_cast<size_t>(M)];
					if (Sol.Alive())
					{
						Missing += (Sol.maxHp - Sol.hp) / Sol.maxHp;
					}
				}
				const int Value = static_cast<int>(Missing.FloorToInt());
				if (Value > BestValue)
				{
					BestValue = Value;
					BestPoint = Own.centroid;
				}
			}
			if (BestValue < 6)
			{
				continue;
			}
		}
		else
		{
			continue;
		}
		BattleCommand Cmd;
		Cmd.side = S;
		Cmd.type = CommandType::CastSpell;
		Cmd.param = static_cast<int32_t>(Slot);
		Cmd.point = BestPoint;
		Schedule(Cmd, Profile.reactionTicks);
		return; // one spell per decision
	}
}
} // namespace dot
