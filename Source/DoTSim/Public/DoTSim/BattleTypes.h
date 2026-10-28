// Source/DoTSim/Public/DoTSim/BattleTypes.h
// Battle input (setup + commands) and output (result + events) records (19.6).
#pragma once

#include "DoTSim/Content.h"

#include <string>
#include <vector>

namespace dot
{
enum class Side : uint8_t { Attacker = 0, Defender = 1 };
inline int SideIndex(Side S) { return static_cast<int>(S); }
inline Side Opponent(Side S) { return S == Side::Attacker ? Side::Defender : Side::Attacker; }

enum class BattlePhase : uint8_t { Deploy, Countdown, Battle, Finished };

struct SquadSetup
{
	std::string troopId;
	int tier = 1;
	int count = 0; // 0 = full squad size
	FixVec2 position;
	FixVec2 facing = FixVec2(Fix64::Zero(), Fix64::One());
	Formation formation = Formation::Line;
	Stance stance = Stance::Aggressive;
	bool stanceSet = false;
};

struct TitanSetup
{
	bool present = false;
	std::string titanId;
	int level = 1;
	int stars = 1;
	/** Ascended titans (kingdom) may be ten levels above the usual cap of 10 + 10 per star. */
	bool ascended = false;
	int abilityLevels[2] = {1, 1};
	/** Bonuses from equipped relics, in percent of the titan's own stats. */
	int hpPct = 0;
	int atkPct = 0;
	int armorPct = 0;
	/** Relics: extra critical hit chance (percentage points) and the share of the target's armor ignored. */
	int critPct = 0;
	int piercePct = 0;
	FixVec2 position;
	FixVec2 facing = FixVec2(Fix64::Zero(), Fix64::One());
};

struct SideSetup
{
	std::string name;
	TitanSetup titan;
	std::vector<SquadSetup> squads;
	std::vector<std::string> spells;
	int spellLevel = 1;
	/** Casts available per spell (the kingdom's spell stock); empty = no limit. */
	std::vector<int> spellCharges;
	/** Level per spell (Arcane Tower); empty = spellLevel for every spell. */
	std::vector<int> spellLevels;
	/** Empty = controlled by a player (commands); otherwise an AI profile id. */
	std::string aiProfile;
	/** Player option: resolve this side's Clash exchanges with the Veteran profile. */
	bool autoClash = false;
	/** Relics on the side's titan that strengthen its whole army, percent. */
	int troopHpPct = 0;
	int troopAtkPct = 0;
};

struct BattleSetup
{
	int formatVersion = 1;
	std::string contentHash;
	std::string name;
	uint64_t seed = 1;
	std::string mapId;
	int timerSec = 180;
	bool skipDeploy = false;
	SideSetup sides[2];
};

enum class CommandType : uint8_t
{
	Move,
	Path,
	Attack,
	Charge,
	Halt,
	Retreat,
	SetFormation,
	SetStance,
	SetAuto,
	CastAbility,
	CastSpell,
	ClashInput,
	DeployMove,
	AutoDeploy,
	StartBattle,
	Surrender,
	Count
};

DOTSIM_API const char* ToString(CommandType T);
DOTSIM_API bool ParseCommandType(const std::string& S, CommandType& Out);

/** Unit ids: squads are 1..999 (index + 1); titans are 1000 + titan index; 0 = none. */
constexpr int32_t kTitanIdBase = 1000;
inline bool IsTitanId(int32_t Id) { return Id >= kTitanIdBase; }
inline bool IsSquadId(int32_t Id) { return Id > 0 && Id < kTitanIdBase; }

struct BattleCommand
{
	int32_t tick = 0;
	Side side = Side::Attacker;
	CommandType type = CommandType::Halt;
	int32_t unitId = 0; // squad or titan id; 0 = all squads of the side (where meaningful)
	FixVec2 point;
	int32_t targetId = 0;
	int32_t param = 0; // formation/stance/auto/ability index/spell slot/clash offset (ms)
	std::vector<FixVec2> waypoints;
};

enum class BattleOutcome : uint8_t { InProgress, Victory, Defeat };

struct BattleResult
{
	BattleOutcome outcome = BattleOutcome::InProgress;
	int stars = 0;
	bool objectives[3] = {false, false, false};
	int32_t durationTicks = 0;
	int destructionPct = 0;
	int attackerSurvivors = 0;
	int attackerLost = 0;
	int defenderSurvivors = 0;
	int defenderLost = 0;
	int64_t battleScore = 0;
	uint64_t finalHash = 0;
	bool surrendered = false;
};

enum class SimEventType : uint8_t
{
	SoldierDied,
	TitanAttack,
	TitanSmash,
	TitanStomp,
	TitanGrab,
	TitanThrowImpact,
	TitanFootstep,
	TitanChargeStart,
	TitanStaggered,
	TitanDied,
	AbilityCast,
	AbilityImpact,
	SpellCast,
	SpellImpact,
	ClashStarted,
	ClashExchange,
	ClashEnded,
	SquadRouted,
	SquadRallied,
	SquadLeftField,
	SquadDestroyed,
	ChargeImpact,
	StarEarned,
	BattleStarted,
	BattleEnded,
	/** a = new squad id, b = soldiers raised. */
	DeadRaised,
	/** Grenadier firebomb burst: a = source squad id. */
	FirebombImpact,
	/** Chainbreakers harpoon a titan: a = squad id, b = titan id, value = chains now on it. */
	ChainThrown,
	/** A titan is Bound by chains: a = titan id. */
	TitanBound,
	Count
};

struct SimEvent
{
	SimEventType type = SimEventType::SoldierDied;
	int32_t tick = 0;
	Side side = Side::Attacker;
	int32_t a = 0; // primary unit / soldier index
	int32_t b = 0; // secondary unit / value
	FixVec2 position;
	int32_t value = 0;
};
} // namespace dot
