// Source/DoTSim/Public/DoTSim/BattleSimulation.h
// Deterministic battle simulation (Section 4 / 18.3 of the master prompt).
// Fixed 30 Hz tick, Q32.32 math, seeded RNG, no engine dependencies.
#pragma once

#include "DoTSim/BattleTypes.h"
#include "DoTSim/NavGrid.h"
#include "DoTSim/Random.h"

#include <string>
#include <utility>
#include <vector>

namespace dot
{
enum class SoldierState : uint8_t { Idle, Moving, Fighting, Airborne, KnockedDown, Dead, Left };
enum class MoraleState : uint8_t { Steady, Shaken, Wavering, Routing };
enum class OrderType : uint8_t { None, Move, Attack, Retreat, CycleWithdraw, Skirmish };
enum class TitanState : uint8_t { Idle, Moving, Attacking, Stomping, Grabbing, Casting, Leaping, Clashing, Staggered, Dead };
enum class ProjectileKind : uint8_t { Arrow, Bolt, ThrownSoldier, TitanBolt, Firebomb };

struct StatusEffect
{
	StatType stat = StatType::MoveSpeedPct;
	Fix64 magnitude;
	int32_t endTick = 0;
	int32_t sourceAbility = -1;
};

struct Soldier
{
	int32_t squad = -1;
	Side side = Side::Attacker;
	SoldierState state = SoldierState::Idle;
	FixVec2 pos;
	FixVec2 vel;
	FixVec2 facing;
	FixVec2 airVel;
	Fix64 height; // above ground, while airborne
	Fix64 vy;
	Fix64 hp;
	Fix64 maxHp;
	int32_t slot = 0;
	int32_t targetSoldier = -1;
	int32_t targetTitan = -1;
	int32_t attackCooldown = 0;
	int32_t stateTicks = 0;
	int32_t stunTicks = 0;
	int32_t burnTicks = 0;
	Fix64 burnDps;
	int32_t poisonTicks = 0;
	Fix64 poisonDps;
	int32_t lastAttackTick = -100000;
	int32_t deathTick = -1;
	int32_t lastTrampledTick = -100000;
	int32_t attackers = 0;
	/** Siege engines fire only from this tick on: they must stand still for the troop's setup time first. */
	int32_t readyTick = 0;
	bool chargeArmed = false;
	/** The body was raised by Raise Dead (or was itself a raised soldier) and cannot rise again. */
	bool raised = false;

	bool Alive() const { return state != SoldierState::Dead && state != SoldierState::Left; }
	bool CanAct() const { return Alive() && state != SoldierState::Airborne && state != SoldierState::KnockedDown && stunTicks <= 0; }
};

struct Squad
{
	int32_t id = 0;
	Side side = Side::Attacker;
	int32_t troop = -1;
	int32_t tier = 1;
	// Runtime stats (troop x tier)
	Fix64 hp;
	Fix64 atk;
	Fix64 armor;
	Fix64 range;
	Fix64 speed;
	Fix64 spacing;
	int32_t interval = 30;
	// Formation and control
	Formation formation = Formation::Line;
	Stance stance = Stance::Aggressive;
	bool autoControl = false;
	FixVec2 leader;
	FixVec2 facing;
	FixVec2 anchor;
	FixVec2 centroid;
	Fix64 radius;
	Fix64 lag; // average soldier distance from its slot
	std::vector<int32_t> members;
	int32_t startCount = 0;
	int32_t aliveCount = 0;
	int32_t leftCount = 0;
	// Orders
	OrderType order = OrderType::None;
	int32_t orderTarget = 0;
	int32_t autoTarget = 0;
	FixVec2 orderPoint;
	std::vector<FixVec2> path;
	size_t pathIndex = 0;
	bool attackAfterPath = false;
	int32_t repathTick = 0;
	int32_t retargetTick = 0;
	int32_t chargeUntil = -1;
	int32_t chargeReadyTick = 0;
	int32_t withdrawTarget = 0;
	// Morale
	Fix64 morale = fx::Int(100);
	MoraleState moraleState = MoraleState::Steady;
	int32_t lastMeleeTick = -100000;
	int32_t lastTitanHitMoraleTick = -100000;
	int32_t lastFlankMoraleTick = -100000;
	int32_t formationChangeUntil = -1;
	// Combat
	std::vector<int32_t> engagedSquads;
	int32_t meleeStartTick = -1;
	bool fightingTitan = false;
	bool moving = false;
	std::vector<StatusEffect> statuses;
	int64_t powerPerSoldier = 0;
	bool destroyed = false;
	bool leftField = false;
	bool hidden = false;
	/** Raised from the fallen mid-battle: fearless, not part of the army, crumbles at expireTick. */
	bool summoned = false;
	int32_t expireTick = -1;
	int32_t chainReadyTick = 0;
	// AI bookkeeping
	int32_t aiNextTick = 0;
	int32_t aiTarget = 0;

	bool Active() const { return !destroyed && !leftField && aliveCount > 0; }
	bool InMelee() const { return !engagedSquads.empty() || fightingTitan; }
	bool Routing() const { return moraleState == MoraleState::Routing; }
};

struct Titan
{
	int32_t id = 0;
	Side side = Side::Attacker;
	int32_t def = -1;
	int level = 1;
	int stars = 1;
	Fix64 maxHp;
	Fix64 hp;
	Fix64 atk;
	Fix64 armor;
	Fix64 maxPoise;
	Fix64 poiseDamage;
	Fix64 rage;
	Fix64 moveSpeed;
	Fix64 radius;
	Fix64 height;
	Fix64 footRadius;
	Fix64 stepLength;
	Fix64 sweepRadius;
	Fix64 sweepHalfArcCos;
	Fix64 critChance;
	Fix64 critDamage;
	Fix64 pierce; // share of the target's armor ignored (relics)
	Fix64 turnCos;
	Fix64 turnSin;
	Fix64 attackRange;
	Fix64 splashRadius;
	Fix64 vsTitanMult;
	Fix64 rangedArmorBonus;
	int32_t attackInterval = 60;
	bool rangedAttack = false;
	int64_t power = 0;
	FixVec2 pos;
	FixVec2 facing;
	TitanState state = TitanState::Idle;
	int32_t actionHitTick = -1;
	int32_t actionEndTick = -1;
	int combo = 0;
	int comboHits = 0;
	int32_t lastAttackTick = -100000;
	int32_t attackReadyTick = 0;
	int32_t attackTargetUnit = 0;
	// Orders
	OrderType order = OrderType::None;
	int32_t orderTarget = 0;
	FixVec2 orderPoint;
	std::vector<FixVec2> path;
	size_t pathIndex = 0;
	int32_t repathTick = 0;
	int32_t autoTarget = 0;
	int32_t retargetTick = 0;
	bool autoControl = false;
	// Built-in actions
	int32_t stompReadyTick = 0;
	int32_t grabReadyTick = 0;
	int32_t chargeReadyTick = 0;
	int32_t chargeUntil = -1;
	int32_t chargeStartTick = -1;
	bool chargedIntoClash = false;
	Fix64 stepAccum;
	int footSide = 0;
	int32_t throwTargetSquad = -1;
	// Abilities
	int32_t abilityReadyTick[2] = {0, 0};
	int abilityLevel[2] = {1, 1};
	int castingAbility = -1;
	FixVec2 castPoint;
	FixVec2 leapFrom;
	FixVec2 leapTo;
	int32_t leapStart = -1;
	int32_t leapEnd = -1;
	int leapAbility = -1;
	// Status
	int32_t staggerUntil = -1;
	Fix64 staggerTakenMult;
	int32_t stunUntil = -1;
	int32_t lastHitTick = -100000;
	FixVec2 lastHitDir;
	std::vector<StatusEffect> statuses;
	int clashIndex = -1;
	bool dead = false;
	int32_t deathTick = -1;
	FixVec2 fallDir;
	Fix64 killRageThisSecond;
	int32_t killRageSecond = -1;
	Fix64 hpLostAccumPct;
	// Chainbreakers' harpoons: Bound (rooted and staggered) at the troop's bindAt chains, then immune for a while.
	int32_t boundUntil = -1;
	int32_t chainImmuneUntil = -1;

	bool Alive() const { return !dead; }
};

struct Projectile
{
	ProjectileKind kind = ProjectileKind::Arrow;
	Side side = Side::Attacker;
	FixVec2 from;
	FixVec2 to;
	Fix64 fromHeight;
	int32_t fireTick = 0;
	int32_t impactTick = 0;
	int32_t targetSoldier = -1;
	int32_t targetSquad = -1;
	int32_t targetTitan = -1;
	int32_t sourceSquad = -1;
	int32_t sourceTitan = -1;
	bool hit = false;
	bool active = true;
};

struct Zone
{
	int32_t ability = -1;
	Side side = Side::Attacker;
	int32_t casterTitan = -1;
	FixVec2 center;
	Fix64 radius;
	int32_t startTick = 0;
	int32_t endTick = 0;
	int32_t nextPulseTick = 0;
	int level = 1;
	bool active = true;
};

struct PendingImpact
{
	int32_t ability = -1;
	Side side = Side::Attacker;
	int32_t casterTitan = -1;
	FixVec2 origin;
	FixVec2 point;
	FixVec2 dir;
	int32_t castTick = 0;
	int32_t impactTick = 0;
	int level = 1;
	bool active = true;
};

/** Burning ground left by a Grenadier firebomb. */
struct FirePatch
{
	Side side = Side::Attacker;
	FixVec2 center;
	Fix64 radius;
	Fix64 dps;
	Fix64 friendlyMult;
	int32_t endTick = 0;
};

struct PendingSummon
{
	Side side = Side::Attacker;
	int32_t troop = -1;
	int tier = 1;
	FixVec2 center;
	Fix64 radius;
	int maxCount = 0;
	int32_t corpseAgeTicks = 0;
	int32_t lifetimeTicks = 0;
};

struct ClashState
{
	bool active = false;
	int titan[2] = {-1, -1}; // titan indices; titan[0] belongs to the attacker side
	int32_t startTick = 0;
	int exchange = 0;
	int32_t windupStart = 0;
	int32_t markerTick = 0;
	int32_t resolveTick = 0;
	int32_t exchangeEnd = 0;
	int bonus[2] = {0, 0};
	bool inputReceived[2] = {false, false};
	int inputOffsetMs[2] = {0, 0};
	int lastGrade[2] = {0, 0};
	int lastWinner = -1;
	int32_t pairCooldownUntil = 0;
};

struct SideState
{
	Fix64 valor;
	int32_t nextValorTick = 0;
	std::vector<int32_t> spells;
	std::vector<int32_t> spellReadyTick;
	std::vector<int32_t> spellCharges; // -1 = no limit
	std::vector<int32_t> spellLevels;
	int spellLevel = 1;
	int64_t startPower = 0;
	int64_t destroyedPower = 0;
	bool hasTitan = false;
	bool titanDefeated = false;
	int aiProfile = -1;
	bool autoClash = false;
	int32_t aiNextTick = 0;
	int32_t aiSpellNextTick = 0;
	int32_t aiTitanNextTick = 0;
};

struct FallenBody
{
	std::vector<std::pair<int, int>> cells;
	int32_t removeTick = 0;
	bool active = true;
};

/** A squad's and a titan's power as a battle counts it at the start (a side's start power is their sum). */
DOTSIM_API int64_t SquadPower(const SimContent& Content, const SquadSetup& Squad);
DOTSIM_API int64_t TitanPower(const SimContent& Content, const TitanSetup& Titan);

class DOTSIM_API BattleSimulation
{
public:
	explicit BattleSimulation(const SimContent& InContent);

	/** Builds the battle from a setup. Returns false (with Error) if content references are invalid. */
	bool Init(const BattleSetup& Setup, std::string& Error);

	/** Advances one tick, applying the given player commands (their tick is set to the current tick). */
	void Step(const std::vector<BattleCommand>& Commands);
	void Step() { Step(std::vector<BattleCommand>()); }

	// --- State access (read-only, for presentation, tests and tools) ---
	int32_t CurrentTick() const { return Tick; }
	BattlePhase Phase() const { return CurrentPhase; }
	bool IsFinished() const { return CurrentPhase == BattlePhase::Finished; }
	int32_t BattleTicksElapsed() const;
	int32_t TimerTicksRemaining() const;
	int32_t CountdownTicksRemaining() const;
	const BattleSetup& Setup() const { return SetupData; }
	const SimContent& Content() const { return ContentRef; }
	const MapDef& Map() const { return *MapRef; }
	const NavGrid& Grid() const { return Nav; }
	const std::vector<Soldier>& Soldiers() const { return SoldiersList; }
	const std::vector<Squad>& Squads() const { return SquadsList; }
	const std::vector<Titan>& Titans() const { return TitansList; }
	const std::vector<Projectile>& Projectiles() const { return ProjectilesList; }
	const std::vector<Zone>& Zones() const { return ZonesList; }
	const std::vector<PendingImpact>& PendingImpacts() const { return Impacts; }
	const std::vector<FirePatch>& FirePatches() const { return Fires; }
	/** Chains on a titan right now (Chainbreakers). */
	int TitanChains(const Titan& T) const;
	/** Ballista pins on a titan right now. */
	int TitanPins(const Titan& T) const;
	/** Crewmen still serving a siege engine (one dies per 1/crew of the engine's HP lost); 0 for other troops. */
	int CrewAlive(const Soldier& S) const;
	const ClashState& Clash() const { return ClashData; }
	const SideState& GetSide(Side S) const { return Sides[SideIndex(S)]; }
	const BattleResult& Result() const { return ResultData; }
	bool ObjectiveMet(int Index) const { return Objectives[Index]; }
	const std::vector<std::pair<int32_t, uint64_t>>& HashLog() const { return Hashes; }
	int RejectedCommands() const { return Rejected; }

	/** Moves pending events into Out (appending) and clears the internal queue. */
	void DrainEvents(std::vector<SimEvent>& Out);
	uint64_t ComputeHash() const;

	// --- Helpers ---
	int SquadIndexFromId(int32_t Id) const;
	int TitanIndexFromId(int32_t Id) const;
	int FindSquadAt(Side S, FixVec2 P, Fix64 Radius) const;
	int FindTitanAt(Side S, FixVec2 P, Fix64 Radius) const;
	bool IsSquadVisibleTo(Side Viewer, int SquadIndex) const;
	FixVec2 UnitPosition(int32_t UnitId) const;
	bool UnitAlive(int32_t UnitId) const;
	Side UnitSide(int32_t UnitId) const;
	int64_t SidePowerRemaining(Side S) const;
	Fix64 SquadStat(const Squad& Sq, StatType Stat) const;
	Fix64 TitanStat(const Titan& T, StatType Stat) const;
	const TroopDef& TroopOf(const Squad& Sq) const { return ContentRef.troops[static_cast<size_t>(Sq.troop)]; }
	const TitanDef& TitanDefOf(const Titan& T) const { return ContentRef.titans[static_cast<size_t>(T.def)]; }

private:
	// BattleSimulation.cpp
	bool BuildSide(Side S, const SideSetup& SetupSide, std::string& Error);
	void ApplyCommand(const BattleCommand& Cmd);
	void BeginBattle();
	void UpdateValor();
	void UpdateObjectives();
	void FinishBattle(bool bSurrender);
	void Emit(SimEventType Type, Side S, int32_t A, int32_t B, FixVec2 Pos, int32_t Value = 0);
	void RecordHashIfNeeded();
	void UpdateVisibility();
	void PlaceSquadAt(Squad& Sq, FixVec2 Position, FixVec2 Facing);
	void InitSquadStats(Squad& Sq, int TroopIndex, int Tier) const;
	void AutoDeploySide(Side S);
	bool CanControl(Side S) const;

	// SimMovement.cpp
	FixVec2 SlotOffset(const Squad& Sq, int SlotIndex, int Count) const;
	FixVec2 SlotWorld(const Squad& Sq, int SlotIndex) const;
	void RefreshSquadGeometry(Squad& Sq);
	void AssignSlots(Squad& Sq);
	void SetSquadPathTo(Squad& Sq, FixVec2 Target);
	void SetSquadWaypoints(Squad& Sq, const std::vector<FixVec2>& Waypoints);
	void UpdateSquads();
	void UpdateSquadMovement(Squad& Sq);
	bool MoveLeaderAlongPath(Squad& Sq, Fix64 Speed);
	Fix64 SquadMoveSpeed(const Squad& Sq) const;
	void UpdateSoldiers();
	void UpdateSoldierMotion(Soldier& S, int Index);
	void UpdateAirborne(Soldier& S, int Index);
	void BuildSpatialHash();
	void QuerySoldiers(FixVec2 Center, Fix64 Radius, std::vector<int32_t>& Out) const;
	void Knockback(int SoldierIndex, FixVec2 Direction, Fix64 Horizontal, Fix64 Vertical);
	void KnockDown(int SoldierIndex, int Ticks);
	void LeaveField(Squad& Sq);
	int32_t ResolveSquadTarget(const Squad& Sq) const;
	FixVec2 OwnEdgePoint(Side S, FixVec2 From) const;

	// SimCombat.cpp
	void UpdateEngagement();
	void PairMelee(Squad& Sq);
	void UpdateSoldierAttacks();
	void SoldierAttack(int Index);
	/** Element effects of a landed melee hit: frost, poison and the goliath's sweep. */
	void ApplyMeleeOnHit(int Index, int TargetIndex);
	void FireRanged(int Index, Squad& Sq, int32_t TargetUnit);
	void UpdateProjectiles();
	Fix64 ComputeTroopDamage(const Squad& Attacker, const Soldier& AttackerSoldier, const Soldier& Defender, const Squad& DefenderSquad, bool bRanged, Fix64 ExtraMult);
	Fix64 ComputeTroopVsTitanDamage(const Squad& Attacker, const Titan& Target, bool bRanged);
	void DamageSoldier(int Index, Fix64 Amount, Side AttackerSide, int AttackerTitan);
	void KillSoldier(int Index, int AttackerTitan);
	void UpdateMorale();
	void ChangeMorale(Squad& Sq, Fix64 Delta);
	void UpdateSoldierStatus();
	void CleanupDead();
	void ExpireStatuses();
	void ExplodeFirebomb(const Projectile& P);
	/** A ballista bolt flies on along its line and hits up to the troop's pierce count of enemy soldiers. */
	void ImpactBolt(const Projectile& P);
	void PinTitan(Titan& T, const TroopDef& TD);
	void UpdateFirePatches();
	void UpdateChains();
	void ClearChains(Titan& T);
	void UpdateSanctuaries();
	void UpdateSupportMovement(Squad& Sq);

	// SimTitans.cpp
	void UpdateTitans();
	void UpdateTitan(Titan& T, int Index);
	void TitanMove(Titan& T, int Index, FixVec2 Goal, bool bStopWithinReach, Fix64 Reach);
	int32_t ChooseTitanTarget(const Titan& T, int Index) const;
	bool TitanTryAutoActions(Titan& T, int Index);
	void TitanBasicAttack(Titan& T, int Index, int32_t TargetUnit);
	void TitanApplyAttackHit(Titan& T, int Index);
	void TitanFootstep(Titan& T, int Index);
	void TitanTrample(Titan& T, int Index);
	Fix64 TitanDamageToSoldier(const Titan& T, const Soldier& S, Fix64 Coef, bool bArea, bool bCanCrit);
	void DamageTitan(int Index, Fix64 Amount, Fix64 PoiseDamage, FixVec2 FromPos, int AttackerTitan);
	void KillTitan(int Index);
	void UpdateFallenBodies();
	void TryStartClash();
	void UpdateClash();
	void ResolveClashExchange();
	void EndClash();
	int ClashGradeFor(int Slot);
	void PushSoldiersFromTitans();
	void SeparateTitans();
	bool StartAbility(Titan& T, int Index, int AbilitySlot, FixVec2 Point);
	void ExecuteAbility(int TitanIdx, int AbilityIdx, FixVec2 Point);
	bool CastSpell(Side S, int Slot, FixVec2 Point);
	void ApplyEffects(int AbilityIdx, Side S, int CasterTitan, FixVec2 Origin, FixVec2 Point, FixVec2 Dir, int Level, bool bPulse);
	bool InShape(const AbilityDef& A, FixVec2 Origin, FixVec2 Point, FixVec2 Dir, FixVec2 P, Fix64 Extra) const;
	void UpdateZonesAndImpacts();
	void SpawnSummons();
	void ExpireSummons();
	int CountRaisableCorpses(FixVec2 Center, Fix64 Radius, int32_t MaxAgeTicks) const;
	void AddSquadStatus(Squad& Sq, const EffectDef& E, int AbilityIdx, Fix64 LevelMult);
	void AddTitanStatus(Titan& T, const EffectDef& E, int AbilityIdx, Fix64 LevelMult);
	Fix64 TitanEffectiveArmor(const Titan& T, bool bRanged) const;
	Fix64 TitanDamageTakenMult(const Titan& T) const;

	// SimAI.cpp
	void UpdateAI();
	void UpdateCommanderAI(Side S, const AiProfileDef& Profile);
	void AiDecideSquad(Side S, int SquadIndex, const AiProfileDef& Profile);
	void AiDecideTitan(Side S, const AiProfileDef& Profile);
	void AiDecideSpells(Side S, const AiProfileDef& Profile);
	void Schedule(const BattleCommand& Cmd, int DelayTicks);
	int CountEnemySoldiersNear(Side S, FixVec2 P, Fix64 Radius) const;
	int BestCorpsePoint(const AbilityDef& A, FixVec2 From, Fix64 MaxReach, FixVec2& OutPoint) const;
	int CountEnemyInAbilityShape(Side S, const AbilityDef& A, FixVec2 Origin, FixVec2 Point, FixVec2 Dir) const;
	Fix64 TargetScore(const Squad& Sq, int32_t TargetUnit, const AiProfileDef* Profile) const;

	const SimContent& ContentRef;
	const MapDef* MapRef = nullptr;
	BattleSetup SetupData;
	NavGrid Nav;
	Rng Random;
	int32_t Tick = 0;
	BattlePhase CurrentPhase = BattlePhase::Deploy;
	int32_t BattleStartTick = -1;
	int32_t CountdownEndTick = -1;
	int32_t TimerTicks = 0;
	std::vector<Soldier> SoldiersList;
	std::vector<Squad> SquadsList;
	std::vector<Titan> TitansList;
	std::vector<Projectile> ProjectilesList;
	std::vector<Zone> ZonesList;
	std::vector<PendingImpact> Impacts;
	std::vector<FallenBody> Bodies;
	std::vector<PendingSummon> Summons;
	std::vector<FirePatch> Fires;
	ClashState ClashData;
	SideState Sides[2];
	bool Objectives[3] = {false, false, false};
	BattleResult ResultData;
	std::vector<SimEvent> Events;
	std::vector<std::pair<int32_t, uint64_t>> Hashes;
	std::vector<BattleCommand> Scheduled;
	int Rejected = 0;

	Fix64 SquadTurnCos;
	Fix64 SquadTurnSin;

	// Spatial hash (rebuilt every tick)
	Fix64 HashCell;
	int HashX = 0;
	int HashZ = 0;
	std::vector<int32_t> HashStart;
	std::vector<int32_t> HashItems;
	mutable std::vector<int32_t> QueryScratch;
};
} // namespace dot
