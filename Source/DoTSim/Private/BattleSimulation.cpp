// Source/DoTSim/Private/BattleSimulation.cpp
// Setup, tick pipeline, commands, objectives and hashing.
#include "DoTSim/BattleSimulation.h"

#include "DoTSim/Hash.h"

#include <algorithm>
#include <cstdlib>

namespace dot
{
namespace
{
const char* const kCommandNames[] = {"Move", "Path", "Attack", "Charge", "Halt", "Retreat", "SetFormation", "SetStance",
	"SetAuto", "CastAbility", "CastSpell", "ClashInput", "DeployMove", "AutoDeploy", "StartBattle", "Surrender"};

/** sqrt(EHP * DPS * AoE) * 100, using integers (Power is a display/matchmaking value). */
int64_t ComputePower(Fix64 Hp, Fix64 Armor, Fix64 Atk, int IntervalTicks, int64_t AoeFactor)
{
	const Fix64 Ehp = Hp / fx::Max(Fix64::One() - Armor, fx::Pct(5));
	const Fix64 Dps = Atk * kTicksPerSecond / fx::Int(std::max(IntervalTicks, 1));
	const int64_t EhpI = std::max<int64_t>(1, (Ehp * 100).RoundToInt());
	const int64_t DpsI = std::max<int64_t>(1, (Dps * 100).RoundToInt());
	return static_cast<int64_t>(ISqrt64(static_cast<uint64_t>(EhpI) * static_cast<uint64_t>(DpsI) * static_cast<uint64_t>(AoeFactor)));
}
} // namespace

const char* ToString(CommandType T)
{
	const int I = static_cast<int>(T);
	return I >= 0 && I < static_cast<int>(CommandType::Count) ? kCommandNames[I] : "Unknown";
}

bool ParseCommandType(const std::string& S, CommandType& Out)
{
	for (int I = 0; I < static_cast<int>(CommandType::Count); ++I)
	{
		if (S == kCommandNames[I])
		{
			Out = static_cast<CommandType>(I);
			return true;
		}
	}
	return false;
}

BattleSimulation::BattleSimulation(const SimContent& InContent) : ContentRef(InContent) {}

bool BattleSimulation::Init(const BattleSetup& Setup, std::string& Error)
{
	SetupData = Setup;
	const int MapIndex = ContentRef.FindMap(Setup.mapId);
	if (MapIndex < 0)
	{
		Error = "unknown map '" + Setup.mapId + "'";
		return false;
	}
	MapRef = &ContentRef.maps[static_cast<size_t>(MapIndex)];
	Nav.Build(*MapRef);
	Random.Reseed(Setup.seed);
	Tick = 0;
	TimerTicks = std::max(1, Setup.timerSec) * kTicksPerSecond;
	SoldiersList.clear();
	SquadsList.clear();
	TitansList.clear();
	ProjectilesList.clear();
	ZonesList.clear();
	Impacts.clear();
	Bodies.clear();
	Summons.clear();
	Fires.clear();
	Events.clear();
	Hashes.clear();
	Scheduled.clear();
	ClashData = ClashState();
	ResultData = BattleResult();
	Objectives[0] = Objectives[1] = Objectives[2] = false;

	SquadTurnCos = fx::Cos(fx::DegToRad(fx::Int(6)));
	SquadTurnSin = fx::Sin(fx::DegToRad(fx::Int(6)));
	HashCell = fx::Int(4);
	HashX = static_cast<int>((MapRef->width / HashCell).FloorToInt()) + 1;
	HashZ = static_cast<int>((MapRef->depth / HashCell).FloorToInt()) + 1;

	for (int S = 0; S < 2; ++S)
	{
		if (!BuildSide(static_cast<Side>(S), Setup.sides[S], Error))
		{
			return false;
		}
	}
	for (Squad& Sq : SquadsList)
	{
		Sides[SideIndex(Sq.side)].startPower += Sq.powerPerSoldier * Sq.startCount;
	}
	for (const Titan& T : TitansList)
	{
		Sides[SideIndex(T.side)].startPower += T.power;
	}

	CurrentPhase = BattlePhase::Deploy;
	if (Setup.skipDeploy)
	{
		CurrentPhase = BattlePhase::Countdown;
		CountdownEndTick = ContentRef.rules.countdownTicks;
	}
	BuildSpatialHash();
	return true;
}

bool BattleSimulation::BuildSide(Side S, const SideSetup& SS, std::string& Error)
{
	const RulesDef& R = ContentRef.rules;
	SideState& State = Sides[SideIndex(S)];
	State = SideState();
	State.valor = R.valorStart;
	State.spellLevel = std::max(1, SS.spellLevel);
	State.autoClash = SS.autoClash;
	if (!SS.aiProfile.empty())
	{
		State.aiProfile = ContentRef.FindAiProfile(SS.aiProfile);
		if (State.aiProfile < 0)
		{
			Error = "unknown AI profile '" + SS.aiProfile + "'";
			return false;
		}
	}
	for (const std::string& SpellId : SS.spells)
	{
		const int A = ContentRef.FindAbility(SpellId);
		if (A < 0 || !ContentRef.abilities[static_cast<size_t>(A)].isSpell)
		{
			Error = "unknown spell '" + SpellId + "'";
			return false;
		}
		const size_t Slot = State.spells.size();
		State.spells.push_back(A);
		State.spellReadyTick.push_back(0);
		State.spellCharges.push_back(Slot < SS.spellCharges.size() ? std::max(0, SS.spellCharges[Slot]) : -1);
		State.spellLevels.push_back(Slot < SS.spellLevels.size() ? std::max(1, SS.spellLevels[Slot]) : State.spellLevel);
	}

	if (SS.titan.present)
	{
		const int Def = ContentRef.FindTitan(SS.titan.titanId);
		if (Def < 0)
		{
			Error = "unknown titan '" + SS.titan.titanId + "'";
			return false;
		}
		const TitanDef& TD = ContentRef.titans[static_cast<size_t>(Def)];
		const TitanClassDef& CD = ContentRef.titanClasses[static_cast<size_t>(TD.cls)];
		const SizeDef& SD = ContentRef.sizes[static_cast<size_t>(TD.size)];
		const ProgressionDef& P = ContentRef.progression;
		Titan T;
		T.id = kTitanIdBase + static_cast<int32_t>(TitansList.size());
		T.side = S;
		T.def = Def;
		T.stars = std::max(1, std::min(6, SS.titan.stars));
		T.level = std::max(1, std::min(10 + 10 * T.stars + (SS.titan.ascended ? 10 : 0), SS.titan.level));
		const Fix64 LevelMult = Fix64::One() + P.levelGrowth * (T.level - 1);
		const Fix64 Growth = P.rarityMult[static_cast<int>(TD.rarity)] * LevelMult * P.starMult[T.stars];
		// Relic bonuses scale the titan's own stats (armor is capped like every armor).
		T.maxHp = CD.hp * TD.varianceHp * Growth * fx::Pct(100 + std::max(0, SS.titan.hpPct));
		T.hp = T.maxHp;
		T.atk = CD.atk * TD.varianceAtk * Growth * fx::Pct(100 + std::max(0, SS.titan.atkPct));
		T.armor = fx::Min(fx::Pct(75), CD.armor * fx::Pct(100 + std::max(0, SS.titan.armorPct)));
		T.maxPoise = CD.poise * TD.variancePoise * Growth;
		T.rage = R.rageStart;
		T.moveSpeed = TD.moveSpeedOverride > Fix64::Zero() ? TD.moveSpeedOverride : CD.moveSpeed;
		T.radius = SD.capsuleRadius;
		T.height = SD.height;
		T.footRadius = SD.footRadius;
		T.stepLength = SD.stepLength;
		T.sweepRadius = CD.sweepRadius;
		T.sweepHalfArcCos = CD.sweepHalfArcCos;
		T.critChance = fx::Min(Fix64::One(), CD.critChance + fx::Pct(std::max(0, SS.titan.critPct)));
		T.critDamage = CD.critDamage;
		T.pierce = fx::Pct(std::max(0, std::min(75, SS.titan.piercePct)));
		const Fix64 TurnPerTick = fx::DegToRad(CD.turnRateDeg / kTicksPerSecond);
		T.turnCos = fx::Cos(TurnPerTick);
		T.turnSin = fx::Sin(TurnPerTick);
		T.attackRange = CD.attackRange;
		T.splashRadius = CD.splashRadius;
		T.vsTitanMult = CD.vsTitanMult;
		T.rangedArmorBonus = TD.rangedArmorBonus;
		T.attackInterval = CD.attackIntervalTicks;
		T.rangedAttack = CD.rangedAttack;
		T.abilityLevel[0] = std::max(1, SS.titan.abilityLevels[0]);
		T.abilityLevel[1] = std::max(1, SS.titan.abilityLevels[1]);
		T.pos = Nav.ClampToWorld(SS.titan.position, fx::Int(6));
		T.facing = SS.titan.facing.IsZero() ? FixVec2(Fix64::Zero(), Fix64::One()) : SS.titan.facing.Normalized();
		T.power = ComputePower(T.maxHp, T.armor, T.atk, T.attackInterval, CD.aoeFactor.RoundToInt());
		T.autoControl = !SS.aiProfile.empty();
		TitansList.push_back(T);
		State.hasTitan = true;
	}

	for (const SquadSetup& Q : SS.squads)
	{
		const int TroopIndex = ContentRef.FindTroop(Q.troopId);
		if (TroopIndex < 0)
		{
			Error = "unknown troop '" + Q.troopId + "'";
			return false;
		}
		const TroopDef& TD = ContentRef.troops[static_cast<size_t>(TroopIndex)];
		Squad Sq;
		Sq.id = static_cast<int32_t>(SquadsList.size()) + 1;
		Sq.side = S;
		InitSquadStats(Sq, TroopIndex, Q.tier);
		if (SS.troopHpPct > 0 || SS.troopAtkPct > 0)
		{
			// Relics on the titan that strengthen the whole army.
			Sq.hp = Sq.hp * fx::Pct(100 + SS.troopHpPct);
			Sq.atk = Sq.atk * fx::Pct(100 + SS.troopAtkPct);
			Sq.powerPerSoldier = ComputePower(Sq.hp, Sq.armor, Sq.atk, Sq.interval, 1);
		}
		Sq.formation = TD.AllowsFormation(Q.formation) ? Q.formation : Formation::Line;
		Sq.stance = Q.stanceSet ? Q.stance : TD.defaultStance;
		const int Count = Q.count > 0 ? Q.count : TD.squadSize;
		Sq.startCount = Count;
		Sq.aliveCount = Count;
		const int SquadIndex = static_cast<int>(SquadsList.size());
		for (int I = 0; I < Count; ++I)
		{
			Soldier Sol;
			Sol.squad = SquadIndex;
			Sol.side = S;
			Sol.hp = Sq.hp;
			Sol.maxHp = Sq.hp;
			Sol.slot = I;
			Sol.attackCooldown = Random.RangeInt(0, std::max(1, Sq.interval / 2));
			Sq.members.push_back(static_cast<int32_t>(SoldiersList.size()));
			SoldiersList.push_back(Sol);
		}
		SquadsList.push_back(Sq);
		PlaceSquadAt(SquadsList.back(), Q.position, Q.facing.IsZero() ? FixVec2(Fix64::Zero(), Fix64::One()) : Q.facing.Normalized());
	}
	return true;
}

void BattleSimulation::InitSquadStats(Squad& Sq, int TroopIndex, int Tier) const
{
	const TroopDef& TD = ContentRef.troops[static_cast<size_t>(TroopIndex)];
	Sq.troop = TroopIndex;
	Sq.tier = std::max(1, std::min(static_cast<int>(TD.tiers.size()), Tier));
	const TroopTierDef& TierDef = TD.tiers[static_cast<size_t>(Sq.tier - 1)];
	Sq.hp = TD.hp * TierDef.hpMult;
	Sq.atk = TD.atk * TierDef.atkMult;
	Sq.armor = TD.armor + TierDef.armorAdd;
	Sq.range = TD.range;
	Sq.speed = TD.moveSpeed;
	Sq.spacing = TD.spacing;
	Sq.interval = TD.attackIntervalTicks;
	Sq.morale = fx::Int(100);
	Sq.powerPerSoldier = ComputePower(Sq.hp, Sq.armor, Sq.atk, Sq.interval, 1);
}

void BattleSimulation::PlaceSquadAt(Squad& Sq, FixVec2 Position, FixVec2 Facing)
{
	const MoveClass Move = TroopOf(Sq).move;
	Sq.leader = Nav.NearestWalkable(Move, Nav.ClampToWorld(Position, fx::Int(3)));
	Sq.facing = Facing;
	Sq.anchor = Sq.leader;
	Sq.path.clear();
	Sq.pathIndex = 0;
	AssignSlots(Sq);
	for (const int32_t M : Sq.members)
	{
		Soldier& S = SoldiersList[static_cast<size_t>(M)];
		if (!S.Alive())
		{
			continue;
		}
		S.pos = Nav.NearestWalkable(Move, SlotWorld(Sq, S.slot), 4);
		S.vel = FixVec2();
		S.facing = Facing;
	}
	RefreshSquadGeometry(Sq);
}

bool BattleSimulation::CanControl(Side S) const
{
	return Sides[SideIndex(S)].aiProfile < 0;
}

int32_t BattleSimulation::BattleTicksElapsed() const
{
	if (BattleStartTick < 0)
	{
		return 0;
	}
	if (CurrentPhase == BattlePhase::Finished)
	{
		return ResultData.durationTicks;
	}
	return Tick - BattleStartTick;
}

int32_t BattleSimulation::TimerTicksRemaining() const
{
	return std::max(0, TimerTicks - BattleTicksElapsed());
}

int32_t BattleSimulation::CountdownTicksRemaining() const
{
	return CurrentPhase == BattlePhase::Countdown ? std::max(0, CountdownEndTick - Tick) : 0;
}

void BattleSimulation::Step(const std::vector<BattleCommand>& Commands)
{
	if (CurrentPhase == BattlePhase::Finished)
	{
		return;
	}
	for (const BattleCommand& Incoming : Commands)
	{
		BattleCommand Cmd = Incoming;
		Cmd.tick = Tick;
		if (!CanControl(Cmd.side))
		{
			++Rejected;
			continue;
		}
		ApplyCommand(Cmd);
	}
	if (!Scheduled.empty())
	{
		std::vector<BattleCommand> Due;
		std::vector<BattleCommand> Later;
		for (const BattleCommand& Cmd : Scheduled)
		{
			(Cmd.tick <= Tick ? Due : Later).push_back(Cmd);
		}
		Scheduled.swap(Later);
		for (const BattleCommand& Cmd : Due)
		{
			ApplyCommand(Cmd);
		}
	}
	if (CurrentPhase == BattlePhase::Finished)
	{
		return;
	}

	if (CurrentPhase == BattlePhase::Countdown && Tick >= CountdownEndTick)
	{
		BeginBattle();
	}

	if (CurrentPhase == BattlePhase::Battle)
	{
		UpdateAI();
		BuildSpatialHash();
		UpdateValor();
		UpdateSquads();
		UpdateTitans();
		UpdateSoldiers();
		BuildSpatialHash();
		UpdateEngagement();
		UpdateSoldierAttacks();
		UpdateProjectiles();
		UpdateFirePatches();
		UpdateChains();
		UpdateSanctuaries();
		UpdateZonesAndImpacts();
		SpawnSummons();
		UpdateSoldierStatus();
		UpdateMorale();
		ExpireSummons();
		CleanupDead();
		UpdateFallenBodies();
		if (Tick % 15 == 0)
		{
			UpdateVisibility();
		}
		UpdateObjectives();
	}
	RecordHashIfNeeded();
	if (CurrentPhase != BattlePhase::Finished)
	{
		++Tick;
	}
}

void BattleSimulation::BeginBattle()
{
	CurrentPhase = BattlePhase::Battle;
	BattleStartTick = Tick;
	for (SideState& S : Sides)
	{
		S.nextValorTick = Tick + ContentRef.rules.valorIntervalTicks;
		S.aiNextTick = Tick;
		S.aiSpellNextTick = Tick + kTicksPerSecond * 4;
		S.aiTitanNextTick = Tick;
	}
	for (Squad& Sq : SquadsList)
	{
		Sq.anchor = Sq.leader;
	}
	UpdateVisibility();
	Emit(SimEventType::BattleStarted, Side::Attacker, 0, 0, FixVec2());
}

void BattleSimulation::ApplyCommand(const BattleCommand& Cmd)
{
	const RulesDef& R = ContentRef.rules;
	if (CurrentPhase == BattlePhase::Finished)
	{
		return;
	}
	switch (Cmd.type)
	{
	case CommandType::StartBattle:
		if (CurrentPhase == BattlePhase::Deploy)
		{
			CurrentPhase = BattlePhase::Countdown;
			CountdownEndTick = Tick + R.countdownTicks;
		}
		return;
	case CommandType::Surrender:
		if (CurrentPhase == BattlePhase::Battle || CurrentPhase == BattlePhase::Countdown)
		{
			if (CurrentPhase == BattlePhase::Countdown)
			{
				BattleStartTick = Tick;
			}
			FinishBattle(Cmd.side == Side::Attacker);
		}
		return;
	case CommandType::AutoDeploy:
		if (CurrentPhase == BattlePhase::Deploy)
		{
			AutoDeploySide(Cmd.side);
		}
		return;
	case CommandType::DeployMove:
	{
		if (CurrentPhase != BattlePhase::Deploy)
		{
			++Rejected;
			return;
		}
		const int SideI = SideIndex(Cmd.side);
		const FixVec2 Lo = MapRef->deployMin[SideI];
		const FixVec2 Hi = MapRef->deployMax[SideI];
		const FixVec2 P(fx::Clamp(Cmd.point.x, Lo.x, Hi.x), fx::Clamp(Cmd.point.z, Lo.z, Hi.z));
		if (IsSquadId(Cmd.unitId))
		{
			const int Index = SquadIndexFromId(Cmd.unitId);
			if (Index >= 0 && SquadsList[static_cast<size_t>(Index)].side == Cmd.side)
			{
				Squad& Sq = SquadsList[static_cast<size_t>(Index)];
				PlaceSquadAt(Sq, P, Sq.facing);
			}
		}
		else if (IsTitanId(Cmd.unitId))
		{
			const int Index = TitanIndexFromId(Cmd.unitId);
			if (Index >= 0 && TitansList[static_cast<size_t>(Index)].side == Cmd.side)
			{
				TitansList[static_cast<size_t>(Index)].pos = P;
			}
		}
		return;
	}
	case CommandType::ClashInput:
		if (ClashData.active && Tick >= ClashData.windupStart && Tick <= ClashData.resolveTick)
		{
			const int Slot = Cmd.side == Side::Attacker ? 0 : 1;
			if (!ClashData.inputReceived[Slot])
			{
				// The client measures the exact offset; it must agree with the tick it was sent on (+/-50 ms).
				const int TickOffsetMs = (Tick - ClashData.markerTick) * 1000 / kTicksPerSecond;
				int Offset = Cmd.param;
				if (std::abs(Offset - TickOffsetMs) > 50)
				{
					Offset = TickOffsetMs;
				}
				ClashData.inputReceived[Slot] = true;
				ClashData.inputOffsetMs[Slot] = Offset;
			}
		}
		return;
	case CommandType::CastSpell:
		if (CurrentPhase == BattlePhase::Battle && !CastSpell(Cmd.side, Cmd.param, Cmd.point))
		{
			++Rejected;
		}
		return;
	default: break;
	}

	// Unit commands. unitId 0 targets every squad of the side (group order).
	if (IsTitanId(Cmd.unitId))
	{
		const int Index = TitanIndexFromId(Cmd.unitId);
		if (Index < 0)
		{
			++Rejected;
			return;
		}
		Titan& T = TitansList[static_cast<size_t>(Index)];
		if (T.side != Cmd.side || !T.Alive())
		{
			++Rejected;
			return;
		}
		switch (Cmd.type)
		{
		case CommandType::Move:
		case CommandType::Path:
		case CommandType::Retreat:
		{
			std::vector<FixVec2> Waypoints;
			if (Cmd.type == CommandType::Path && !Cmd.waypoints.empty())
			{
				for (size_t I = 0; I < Cmd.waypoints.size() && I < 24; ++I)
				{
					Waypoints.push_back(Nav.ClampToWorld(Cmd.waypoints[I], fx::Int(6)));
				}
			}
			else
			{
				const FixVec2 Goal = Cmd.type == CommandType::Retreat ? OwnEdgePoint(T.side, T.pos) : Cmd.point;
				Nav.FindPath(MoveClass::Titan, T.pos, Goal, Waypoints);
			}
			T.path = Waypoints;
			T.pathIndex = 0;
			T.order = OrderType::Move;
			T.orderTarget = 0;
			if (Cmd.type == CommandType::Path && Cmd.targetId != 0 && UnitAlive(Cmd.targetId) && UnitSide(Cmd.targetId) != T.side)
			{
				T.orderTarget = Cmd.targetId;
			}
			T.autoTarget = 0;
			return;
		}
		case CommandType::Attack:
		case CommandType::Charge:
		{
			const int32_t Target = Cmd.targetId;
			const bool bValidTarget = Target != 0 && UnitAlive(Target) && UnitSide(Target) != T.side;
			if (Cmd.type == CommandType::Charge && Tick >= T.chargeReadyTick)
			{
				const FixVec2 Dest = bValidTarget ? UnitPosition(Target) : Cmd.point;
				if (fx::Dist(Dest, T.pos) >= R.titanChargeMinDistance)
				{
					T.chargeStartTick = Tick;
					T.chargeUntil = Tick + R.titanChargeTicks;
					T.chargeReadyTick = Tick + R.titanChargeCooldownTicks;
					Emit(SimEventType::TitanChargeStart, T.side, T.id, 0, T.pos);
				}
			}
			if (bValidTarget)
			{
				T.order = OrderType::Attack;
				T.orderTarget = Target;
				T.path.clear();
				T.repathTick = Tick;
			}
			else if (Cmd.type == CommandType::Charge)
			{
				std::vector<FixVec2> Waypoints;
				Nav.FindPath(MoveClass::Titan, T.pos, Cmd.point, Waypoints);
				T.path = Waypoints;
				T.pathIndex = 0;
				T.order = OrderType::Move;
				T.orderTarget = 0;
			}
			else
			{
				++Rejected;
			}
			return;
		}
		case CommandType::Halt:
			T.order = OrderType::None;
			T.orderTarget = 0;
			T.path.clear();
			return;
		case CommandType::SetAuto: T.autoControl = Cmd.param != 0; return;
		case CommandType::CastAbility:
			if (CurrentPhase != BattlePhase::Battle || !StartAbility(T, Index, Cmd.param, Cmd.point))
			{
				++Rejected;
			}
			return;
		default: ++Rejected; return;
		}
	}

	std::vector<int> Targets;
	if (Cmd.unitId == 0)
	{
		for (size_t I = 0; I < SquadsList.size(); ++I)
		{
			if (SquadsList[I].side == Cmd.side && SquadsList[I].Active() && !SquadsList[I].Routing() && !SquadsList[I].summoned)
			{
				Targets.push_back(static_cast<int>(I));
			}
		}
	}
	else
	{
		const int Index = SquadIndexFromId(Cmd.unitId);
		// The raised dead obey the spell, not the commander.
		if (Index < 0 || SquadsList[static_cast<size_t>(Index)].side != Cmd.side || !SquadsList[static_cast<size_t>(Index)].Active() ||
			SquadsList[static_cast<size_t>(Index)].summoned)
		{
			++Rejected;
			return;
		}
		Targets.push_back(Index);
	}
	if (Targets.empty())
	{
		return;
	}

	// Group orders keep each squad's offset from the group's center.
	FixVec2 GroupCenter;
	for (const int I : Targets)
	{
		GroupCenter += SquadsList[static_cast<size_t>(I)].leader;
	}
	GroupCenter = GroupCenter / fx::Int(static_cast<int64_t>(Targets.size()));

	for (const int I : Targets)
	{
		Squad& Sq = SquadsList[static_cast<size_t>(I)];
		if (Sq.Routing() && Cmd.type != CommandType::SetAuto)
		{
			continue; // routing squads ignore orders
		}
		const FixVec2 Offset = Targets.size() > 1 ? Sq.leader - GroupCenter : FixVec2();
		switch (Cmd.type)
		{
		case CommandType::Move:
			Sq.order = OrderType::Move;
			Sq.orderTarget = 0;
			Sq.autoTarget = 0;
			Sq.attackAfterPath = false;
			SetSquadPathTo(Sq, Cmd.point + Offset);
			Sq.anchor = Cmd.point + Offset;
			break;
		case CommandType::Path:
		{
			std::vector<FixVec2> Waypoints;
			for (size_t W = 0; W < Cmd.waypoints.size() && W < 24; ++W)
			{
				Waypoints.push_back(Cmd.waypoints[W] + Offset);
			}
			if (Waypoints.empty())
			{
				Waypoints.push_back(Cmd.point + Offset);
			}
			Sq.order = OrderType::Move;
			Sq.autoTarget = 0;
			Sq.orderTarget = 0;
			Sq.attackAfterPath = false;
			if (Cmd.targetId != 0 && UnitAlive(Cmd.targetId) && UnitSide(Cmd.targetId) != Sq.side)
			{
				Sq.attackAfterPath = true;
				Sq.orderTarget = Cmd.targetId;
			}
			SetSquadWaypoints(Sq, Waypoints);
			Sq.anchor = Waypoints.back();
			break;
		}
		case CommandType::Attack:
		case CommandType::Charge:
		{
			const bool bValid = Cmd.targetId != 0 && UnitAlive(Cmd.targetId) && UnitSide(Cmd.targetId) != Sq.side;
			if (Cmd.type == CommandType::Charge && Tick >= Sq.chargeReadyTick)
			{
				Sq.chargeUntil = Tick + R.chargeCommandTicks;
				Sq.chargeReadyTick = Tick + R.chargeCommandCooldownTicks;
			}
			if (bValid)
			{
				Sq.order = OrderType::Attack;
				Sq.orderTarget = Cmd.targetId;
				Sq.autoTarget = 0;
				Sq.attackAfterPath = false;
				Sq.path.clear();
				Sq.repathTick = Tick;
			}
			else if (Cmd.type == CommandType::Charge)
			{
				Sq.order = OrderType::Move;
				Sq.orderTarget = 0;
				Sq.autoTarget = 0;
				SetSquadPathTo(Sq, Cmd.point + Offset);
				Sq.anchor = Cmd.point + Offset;
			}
			break;
		}
		case CommandType::Halt:
			Sq.order = OrderType::None;
			Sq.orderTarget = 0;
			Sq.autoTarget = 0;
			Sq.path.clear();
			Sq.anchor = Sq.leader;
			break;
		case CommandType::Retreat:
			Sq.order = OrderType::Retreat;
			Sq.orderTarget = 0;
			Sq.autoTarget = 0;
			SetSquadPathTo(Sq, OwnEdgePoint(Sq.side, Sq.leader));
			break;
		case CommandType::SetFormation:
		{
			const Formation F = static_cast<Formation>(std::max(0, std::min(Cmd.param, static_cast<int>(Formation::Count) - 1)));
			if (TroopOf(Sq).AllowsFormation(F) && F != Sq.formation)
			{
				Sq.formation = F;
				Sq.formationChangeUntil = Tick + R.formationChangeTicks;
				AssignSlots(Sq);
			}
			break;
		}
		case CommandType::SetStance:
			Sq.stance = static_cast<Stance>(std::max(0, std::min(Cmd.param, static_cast<int>(Stance::Count) - 1)));
			break;
		case CommandType::SetAuto: Sq.autoControl = Cmd.param != 0; break;
		default: ++Rejected; break;
		}
	}
}

void BattleSimulation::AutoDeploySide(Side S)
{
	const int SideI = SideIndex(S);
	const FixVec2 Lo = MapRef->deployMin[SideI];
	const FixVec2 Hi = MapRef->deployMax[SideI];
	const bool bAttacker = S == Side::Attacker;
	const Fix64 Front = bAttacker ? Hi.z : Lo.z;
	const Fix64 Back = bAttacker ? fx::Int(-1) : fx::Int(1); // direction away from the enemy
	const FixVec2 Facing = bAttacker ? FixVec2(Fix64::Zero(), Fix64::One()) : FixVec2(Fix64::Zero(), -Fix64::One());
	const Fix64 CenterX = (Lo.x + Hi.x) / 2;

	std::vector<int> Melee;
	std::vector<int> Ranged;
	std::vector<int> Cavalry;
	for (size_t I = 0; I < SquadsList.size(); ++I)
	{
		const Squad& Sq = SquadsList[I];
		if (Sq.side != S)
		{
			continue;
		}
		const TroopDef& TD = TroopOf(Sq);
		if (TD.IsCavalry())
		{
			Cavalry.push_back(static_cast<int>(I));
		}
		else if (TD.ranged)
		{
			Ranged.push_back(static_cast<int>(I));
		}
		else
		{
			Melee.push_back(static_cast<int>(I));
		}
	}
	auto PlaceRow = [&](const std::vector<int>& Row, Fix64 Depth)
	{
		const Fix64 Gap = fx::Int(26);
		const int N = static_cast<int>(Row.size());
		for (int K = 0; K < N; ++K)
		{
			const Fix64 X = CenterX + Gap * (2 * K - (N - 1)) / 2;
			PlaceSquadAt(SquadsList[static_cast<size_t>(Row[static_cast<size_t>(K)])], FixVec2(X, Front + Back * Depth), Facing);
		}
	};
	PlaceRow(Melee, fx::Int(6));
	PlaceRow(Ranged, fx::Int(20));
	for (size_t K = 0; K < Cavalry.size(); ++K)
	{
		const bool bLeft = (K % 2) == 0;
		const Fix64 X = bLeft ? Lo.x + fx::Int(14) + fx::Int(static_cast<int64_t>(K / 2) * 18) : Hi.x - fx::Int(14) - fx::Int(static_cast<int64_t>(K / 2) * 18);
		PlaceSquadAt(SquadsList[Cavalry[K]], FixVec2(X, Front + Back * fx::Int(8)), Facing);
	}
	for (Titan& T : TitansList)
	{
		if (T.side == S)
		{
			T.pos = FixVec2(CenterX, Front + Back * fx::Int(14));
			T.facing = Facing;
		}
	}
}

void BattleSimulation::UpdateValor()
{
	for (SideState& S : Sides)
	{
		if (Tick >= S.nextValorTick)
		{
			S.valor = fx::Min(ContentRef.rules.valorMax, S.valor + Fix64::One());
			S.nextValorTick += ContentRef.rules.valorIntervalTicks;
		}
	}
}

void BattleSimulation::UpdateObjectives()
{
	const SideState& Def = Sides[SideIndex(Side::Defender)];
	bool Now[3];
	Now[0] = Def.startPower > 0 && Def.destroyedPower * 2 >= Def.startPower;
	// Without a defending titan the titan star is earned together with the first one, so a battle
	// with no titan on the other side is not won before it starts.
	Now[1] = Def.hasTitan ? Def.titanDefeated : Now[0];
	bool bDefenderWiped = !Def.hasTitan || Def.titanDefeated;
	bool bAttackerAlive = false;
	for (const Squad& Sq : SquadsList)
	{
		// Raised skeletons fight on but do not hold the field for their side.
		if (Sq.Active() && !Sq.summoned)
		{
			if (Sq.side == Side::Defender)
			{
				bDefenderWiped = false;
			}
			else
			{
				bAttackerAlive = true;
			}
		}
	}
	for (const Titan& T : TitansList)
	{
		if (T.side == Side::Attacker && T.Alive())
		{
			bAttackerAlive = true;
		}
	}
	Now[2] = bDefenderWiped;
	for (int I = 0; I < 3; ++I)
	{
		if (Now[I] && !Objectives[I])
		{
			Objectives[I] = true;
			Emit(SimEventType::StarEarned, Side::Attacker, I, 0, FixVec2());
		}
	}
	if (Objectives[2] || !bAttackerAlive || BattleTicksElapsed() >= TimerTicks)
	{
		FinishBattle(false);
	}
}

void BattleSimulation::FinishBattle(bool bSurrender)
{
	if (CurrentPhase == BattlePhase::Finished)
	{
		return;
	}
	const int32_t Duration = BattleStartTick >= 0 ? Tick - BattleStartTick : 0;
	CurrentPhase = BattlePhase::Finished;
	BattleResult& R = ResultData;
	R.durationTicks = Duration;
	R.surrendered = bSurrender;
	int Stars = 0;
	for (int I = 0; I < 3; ++I)
	{
		R.objectives[I] = Objectives[I] && !bSurrender;
		Stars += R.objectives[I] ? 1 : 0;
	}
	R.stars = Stars;
	R.outcome = Stars >= 1 ? BattleOutcome::Victory : BattleOutcome::Defeat;
	const SideState& Def = Sides[SideIndex(Side::Defender)];
	const SideState& Att = Sides[SideIndex(Side::Attacker)];
	R.destructionPct = Def.startPower > 0 ? static_cast<int>(std::min<int64_t>(100, Def.destroyedPower * 100 / Def.startPower)) : 0;
	R.attackerSurvivors = R.attackerLost = R.defenderSurvivors = R.defenderLost = 0;
	for (const Soldier& S : SoldiersList)
	{
		if (SquadsList[static_cast<size_t>(S.squad)].summoned)
		{
			continue;
		}
		const bool bAttacker = S.side == Side::Attacker;
		if (S.state == SoldierState::Dead)
		{
			(bAttacker ? R.attackerLost : R.defenderLost)++;
		}
		else
		{
			(bAttacker ? R.attackerSurvivors : R.defenderSurvivors)++;
		}
	}
	const int64_t SurvivingPct = Att.startPower > 0 ? (Att.startPower - Att.destroyedPower) * 100 / Att.startPower : 0;
	const int64_t RemainingSec = std::max(0, TimerTicks - Duration) / kTicksPerSecond;
	R.battleScore = 1000 * Stars + 10 * R.destructionPct + 5 * RemainingSec + 5 * SurvivingPct;
	R.finalHash = ComputeHash();
	Hashes.emplace_back(Tick, R.finalHash);
	Emit(SimEventType::BattleEnded, Side::Attacker, Stars, static_cast<int32_t>(R.outcome), FixVec2());
}

void BattleSimulation::Emit(SimEventType Type, Side S, int32_t A, int32_t B, FixVec2 Pos, int32_t Value)
{
	SimEvent E;
	E.type = Type;
	E.tick = Tick;
	E.side = S;
	E.a = A;
	E.b = B;
	E.position = Pos;
	E.value = Value;
	Events.push_back(E);
}

void BattleSimulation::DrainEvents(std::vector<SimEvent>& Out)
{
	Out.insert(Out.end(), Events.begin(), Events.end());
	Events.clear();
}

void BattleSimulation::RecordHashIfNeeded()
{
	if (Tick % kTicksPerSecond == 0 && CurrentPhase != BattlePhase::Finished)
	{
		Hashes.emplace_back(Tick, ComputeHash());
	}
}

uint64_t BattleSimulation::ComputeHash() const
{
	StateWriter W;
	W.I32(Tick);
	W.U8(static_cast<uint8_t>(CurrentPhase));
	for (int I = 0; I < 4; ++I)
	{
		W.U32(Random.RawState()[I]);
	}
	for (const Soldier& S : SoldiersList)
	{
		W.U8(static_cast<uint8_t>(S.state));
		W.V(S.pos);
		W.V(S.vel);
		W.F(S.hp);
		W.F(S.height);
		W.I32(S.targetSoldier);
		W.I32(S.targetTitan);
		W.I32(S.attackCooldown);
		W.I32(S.stateTicks);
	}
	for (const Squad& Sq : SquadsList)
	{
		W.V(Sq.leader);
		W.V(Sq.facing);
		W.F(Sq.morale);
		W.U8(static_cast<uint8_t>(Sq.order));
		W.U8(static_cast<uint8_t>(Sq.formation));
		W.U8(static_cast<uint8_t>(Sq.stance));
		W.I32(Sq.orderTarget);
		W.I32(Sq.autoTarget);
		W.I32(Sq.aliveCount);
		W.U64(static_cast<uint64_t>(Sq.pathIndex));
	}
	for (const Titan& T : TitansList)
	{
		W.V(T.pos);
		W.V(T.facing);
		W.F(T.hp);
		W.F(T.rage);
		W.F(T.poiseDamage);
		W.U8(static_cast<uint8_t>(T.state));
		W.I32(T.combo);
		W.I32(T.attackReadyTick);
		W.I32(T.orderTarget);
		W.I32(T.autoTarget);
	}
	for (const Projectile& P : ProjectilesList)
	{
		W.Bool(P.active);
		W.I32(P.impactTick);
		W.Bool(P.hit);
		W.I32(P.targetSoldier);
	}
	for (const Zone& Z : ZonesList)
	{
		W.Bool(Z.active);
		W.I32(Z.nextPulseTick);
	}
	for (const SideState& S : Sides)
	{
		W.F(S.valor);
		W.I64(S.destroyedPower);
		for (const int32_t Charges : S.spellCharges)
		{
			if (Charges >= 0)
			{
				W.I32(Charges); // limited spells only, so unlimited battles hash as before
			}
		}
	}
	W.Bool(ClashData.active);
	W.I32(ClashData.exchange);
	for (const bool B : Objectives)
	{
		W.Bool(B);
	}
	return W.Hash();
}

int BattleSimulation::SquadIndexFromId(int32_t Id) const
{
	const int Index = Id - 1;
	return (IsSquadId(Id) && Index < static_cast<int>(SquadsList.size())) ? Index : -1;
}

int BattleSimulation::TitanIndexFromId(int32_t Id) const
{
	const int Index = Id - kTitanIdBase;
	return (IsTitanId(Id) && Index < static_cast<int>(TitansList.size())) ? Index : -1;
}

FixVec2 BattleSimulation::UnitPosition(int32_t UnitId) const
{
	if (IsTitanId(UnitId))
	{
		const int I = TitanIndexFromId(UnitId);
		return I >= 0 ? TitansList[static_cast<size_t>(I)].pos : FixVec2();
	}
	const int I = SquadIndexFromId(UnitId);
	return I >= 0 ? SquadsList[static_cast<size_t>(I)].centroid : FixVec2();
}

bool BattleSimulation::UnitAlive(int32_t UnitId) const
{
	if (IsTitanId(UnitId))
	{
		const int I = TitanIndexFromId(UnitId);
		return I >= 0 && TitansList[static_cast<size_t>(I)].Alive();
	}
	const int I = SquadIndexFromId(UnitId);
	return I >= 0 && SquadsList[static_cast<size_t>(I)].Active();
}

Side BattleSimulation::UnitSide(int32_t UnitId) const
{
	if (IsTitanId(UnitId))
	{
		const int I = TitanIndexFromId(UnitId);
		return I >= 0 ? TitansList[static_cast<size_t>(I)].side : Side::Attacker;
	}
	const int I = SquadIndexFromId(UnitId);
	return I >= 0 ? SquadsList[static_cast<size_t>(I)].side : Side::Attacker;
}

int64_t BattleSimulation::SidePowerRemaining(Side S) const
{
	const SideState& State = Sides[SideIndex(S)];
	return State.startPower - State.destroyedPower;
}

int BattleSimulation::FindSquadAt(Side S, FixVec2 P, Fix64 Radius) const
{
	int Best = -1;
	Fix64 BestDist;
	for (size_t I = 0; I < SquadsList.size(); ++I)
	{
		const Squad& Sq = SquadsList[I];
		if (Sq.side != S || !Sq.Active())
		{
			continue;
		}
		const Fix64 D = fx::Dist(P, Sq.centroid) - Sq.radius;
		if (D <= Radius && (Best < 0 || D < BestDist))
		{
			Best = static_cast<int>(I);
			BestDist = D;
		}
	}
	return Best;
}

int BattleSimulation::FindTitanAt(Side S, FixVec2 P, Fix64 Radius) const
{
	for (size_t I = 0; I < TitansList.size(); ++I)
	{
		const Titan& T = TitansList[I];
		if (T.side == S && T.Alive() && fx::Dist(P, T.pos) <= T.radius + Radius)
		{
			return static_cast<int>(I);
		}
	}
	return -1;
}

bool BattleSimulation::IsSquadVisibleTo(Side Viewer, int SquadIndex) const
{
	const Squad& Sq = SquadsList[static_cast<size_t>(SquadIndex)];
	return Sq.side == Viewer || !Sq.hidden;
}

void BattleSimulation::UpdateVisibility()
{
	const Fix64 Radius = ContentRef.rules.forestHideRadius;
	for (Squad& Sq : SquadsList)
	{
		if (!Sq.Active())
		{
			Sq.hidden = false;
			continue;
		}
		bool bAllInForest = true;
		for (const int32_t M : Sq.members)
		{
			const Soldier& S = SoldiersList[static_cast<size_t>(M)];
			if (S.Alive() && (Nav.FlagsAt(S.pos) & cellflag::Forest) == 0)
			{
				bAllInForest = false;
				break;
			}
		}
		bool bHidden = bAllInForest;
		if (bHidden && CountEnemySoldiersNear(Sq.side, Sq.centroid, Radius) > 0)
		{
			bHidden = false;
		}
		if (bHidden)
		{
			for (const Titan& T : TitansList)
			{
				if (T.side != Sq.side && T.Alive() && fx::Dist(T.pos, Sq.centroid) <= Radius + T.radius)
				{
					bHidden = false;
				}
			}
		}
		Sq.hidden = bHidden;
	}
}

Fix64 BattleSimulation::SquadStat(const Squad& Sq, StatType Stat) const
{
	Fix64 Sum;
	for (const StatusEffect& E : Sq.statuses)
	{
		if (E.stat == Stat && E.endTick > Tick)
		{
			Sum += E.magnitude;
		}
	}
	return Sum;
}

Fix64 BattleSimulation::TitanStat(const Titan& T, StatType Stat) const
{
	Fix64 Sum;
	for (const StatusEffect& E : T.statuses)
	{
		if (E.stat == Stat && E.endTick > Tick)
		{
			Sum += E.magnitude;
		}
	}
	return Sum;
}

FixVec2 BattleSimulation::OwnEdgePoint(Side S, FixVec2 From) const
{
	const Fix64 Z = S == Side::Attacker ? fx::Int(2) : MapRef->depth - fx::Int(2);
	return FixVec2(fx::Clamp(From.x, fx::Int(4), MapRef->width - fx::Int(4)), Z);
}
} // namespace dot
