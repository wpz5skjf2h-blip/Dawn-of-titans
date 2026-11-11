// Source/DoTKingdom/Public/DoTKingdom/Kingdom.h
// The player's kingdom: city buildings, builders, timers, resources, expansions, lands (gold, farm, ruins and
// victory-point lands), portal stones, seasons of victory points, the army (training, troop levels, casualties),
// spells (Spell Forge stock, Arcane Tower levels), the titan roster (XP, fusion, ascension, titans guarding lands),
// relic items (combat relics on titans, economy relics on buildings; effects that open and grow with repairs) and
// raids on the kingdom (the Garrison's defense, shields, reports) and
// alliances (computer alliances in weekly leagues with promotion and relegation, an own alliance, reinforcements).
// Deterministic and engine-free like the battle simulation: integer math only, time is passed in
// explicitly (seconds), so a server can replay the same actions and reach the same state.
#pragma once

#include "DoTKingdom/KingdomContent.h"
#include "DoTSim/BattleTypes.h"

#include <algorithm>
#include <string>
#include <vector>

namespace dot
{
namespace kingdom
{
enum class Error : uint8_t
{
	None,
	BadId,
	MaxLevel,
	CastleTooLow,
	NeedRequirement,
	NeedExpansion,
	Locked,
	LimitReached,
	NoBuilder,
	NotEnough,
	BadPlacement,
	Busy,
	NotBusy,
	Nothing,
	StorageFull,
	NeedStones,
	NoLandSlot,
	NoAttack,
	QueueFull,
	Researching,
	TooManySquads,
	NoTroops,
	TitanRecovering,
	NotReady,
	RosterFull,
	NotMaxLevel,
	WrongRelic, // an economy relic on a titan, a combat relic on a building, or a building that does not take it
	SameRelic, // the original: a titan cannot wear two identical relics
	InventoryFull,
	NeedVp, // fewer VP than the alliance asks
	AllianceFull,
	InAlliance, // leave the current alliance first
	NoAlliance, // join an alliance first
	NotAllowed, // the event's rule does not let this titan or troop in
	Count
};
DOTKINGDOM_API const char* ToString(Error E);

struct Building
{
	int32_t id = 0;
	int def = -1;
	int level = 0; // 0 while the first construction is running
	int x = 0;
	int y = 0;
	bool upgrading = false;
	int64_t startAt = 0;
	int64_t finishAt = 0;
	// Producers: whole units waiting to be collected, plus the fractional remainder in unit-seconds.
	int64_t stored = 0;
	int64_t carry = 0;
	int64_t lastAt = 0;
};

struct Land
{
	int32_t id = 0;
	int type = 0;
	int level = 1;
	int32_t titanId = 0; // a titan on the land adds production per titan level
	int64_t stored = 0;
	int64_t carry = 0; // remainder in units x 100 x seconds (the titan bonus is a percentage)
	int64_t lastAt = 0;
	// Upgrading to the next level (a builder works on it; the land keeps producing at its level).
	bool upgrading = false;
	int64_t startAt = 0;
	int64_t finishAt = 0;
	int64_t shieldUntil = 0; // raiders leave it alone until then
};

/** A land on the world map that can be raided or captured: a neutral one, or one a neighbouring lord holds. */
struct WorldLand
{
	int32_t id = 0;
	int type = 0;
	int level = 1;
	int owner = -1; // the rival holding it (index into Rivals()), -1 = neutral
	int64_t shieldUntil = 0; // a lord's raided land is left alone until then
};

/** A neighbouring lord: a computer kingdom on the world map in place of the original's players. */
struct Rival
{
	int lord = 0; // index into the content's lords (name and realm)
	std::vector<int64_t> regrow; // when the lands taken from it come back, earliest first
};

/** A titan the player owns. */
/** A titan's skill in its slot: the kind (index into the content's skills) and its tier, 1 to 3. */
struct TitanSkill
{
	int kind = 0;
	int tier = 1;
};

struct TitanState
{
	int32_t id = 0;
	int def = 0;
	int level = 1;
	int64_t xp = 0; // towards the next level
	int64_t recoverUntil = 0; // resting after a battle until this time
	bool ascended = false; // ten more levels, one more relic slot and one more skill
	std::vector<int32_t> relics; // equipped relic ids
	std::vector<TitanSkill> skills; // by slot: the start one, those fusion opened, then the ascension one (always last)
};

/** A relic item (the original since 1.8): one to three effects, the later ones locked until a repair opens them.
 *  A combat relic is worn by a titan and wears out by one every battle; an economy relic is attached to a building
 *  and lasts a time. A worn-out relic does nothing until it is repaired. */
struct RelicItem
{
	int32_t id = 0;
	int kind = 0;
	int rarity = 1; // R1..R5
	/** Combat: battles left. Economy: seconds left, counted from `since` while attached to a building. */
	int64_t durability = 0;
	/** Level of each effect slot the relic has (kind slots capped by rarity): 0 = locked. */
	std::vector<int> levels;
	int32_t building = 0; // economy relics: the building it is attached to
	int64_t since = 0;
};

/** What a repair did. */
struct RepairResult
{
	bool repaired = false;
	int slot = -1; // the effect that improved, -1 for none
	bool unlocked = false; // the improvement opened a locked effect
};

/** One Arcane Tower research slot. */
struct SpellResearchSlot
{
	int spell = -1;
	int64_t from = 0;
	int64_t until = 0;
};

/** How a season ended: its VP, the league reached (-1 for none) and what that paid. In an alliance the
 *  alliance's league pays instead: its tier, the alliance's place there, the move up or down and the crystal. */
struct SeasonResult
{
	int64_t season = -1;
	int64_t vp = 0;
	int league = -1;
	int64_t gems = 0;
	int64_t stones = 0;
	int64_t divine = 0; // divine gems (the top alliance leagues)
	int alliance = -1; // the alliance that paid (-1: the personal league did)
	int allianceTier = -1; // its league during the season
	int place = 0; // 1 = first
	int groupSize = 0;
	int move = 0; // +1 promoted, -1 relegated
	int64_t allianceVp = 0;
	int crystal = 0; // rarity of the crystal (0: none)
	int32_t crystalTitan = 0;
	int32_t crystalRelic = 0;
	int64_t crystalGems = 0; // when the relic did not fit in the inventory
};

/** An alliance: a computer one or the player's own. */
struct AllianceInfo
{
	int index = -1;
	std::string name;
	std::string tag;
	int emblem = 0;
	int tier = 0; // its alliance league
	int members = 0; // the player included when they are a member
	int64_t joinVp = 0; // VP a player needs to join
	int64_t vp = 0; // all members' VP this season so far
	bool own = false;
};

/** A member of an alliance and their VP this season so far. */
struct AllianceMember
{
	std::string name; // empty for the player
	int role = 2; // 0 leader, 1 officer, 2 member
	int64_t vp = 0;
	int64_t joinedAt = 0; // recruits of an own alliance
};

/** A squad allies sent to the Alliance Camp: it fights in one battle, then it is gone. */
struct Reinforcement
{
	int troop = 0;
	int level = 1;
	int64_t at = 0; // arrives at
	int from = 0; // the member who sent it (index into Members())
};

struct Reward
{
	ResArray res{};
	int32_t landId = 0;
	int squadsLost = 0;
	int squadsBack = 0;
	int32_t titanId = 0;
	int64_t titanXp = 0;
	int titanLevels = 0;
	int relicsBroken = 0;
	std::vector<int32_t> relics; // relic items the victory dropped
	int relicsLost = 0; // drops that did not fit in the inventory
	int reinforcements = 0; // allies' squads that fought (they are gone now)
	bool firstClear = false; // a campaign mission won for the first time (res holds its reward)
	int32_t newTitan = 0; // a titan a star chest gave
	int64_t studySec = 0; // research time a Daily Trial took off (each research running)
	int64_t scrolls = 0; // War Scrolls a quest chest gave
	int64_t divine = 0; // divine gems an event paid
	int64_t honor = 0; // Honor
	int64_t silk = 0; // Banner Silk
	int tech = -1; // the Alliance Tech a donation went to
	int64_t techPts = 0; // and its points
	int techLevel = 0; // that tech's level after it
};

/** A saved army for battles (master prompt 4.6, S13): a titan, squads (a troop index each) and spells (indices). */
struct Loadout
{
	std::string name; // empty: the client numbers it
	int32_t titan = 0; // 0: none (a titan fused away later stays here and is left out when used)
	std::vector<int> squads;
	std::vector<int> spells;
};

/** A squad of an Assault run: its troop (index), its level when the run began and its share of men still standing. */
struct AssaultSquad
{
	int troop = 0;
	int level = 1;
	int pct = 100;
};

/** The chosen Assault boons summed up for a battle (%). */
struct AssaultBonus
{
	int troopAtk = 0;
	int troopHp = 0;
	int titan = 0; // attack and health
	int titanCrit = 0;
};

/** A computer member's part in this week's Raid Boss: damage (tenths of a percent of the boss's health) and battles. */
struct BossMemberHits
{
	int64_t dmg = 0;
	int battles = 0;
};

/** A member of an Alliance War roster: their base and their attacks on the other side. */
struct WarMember
{
	std::string name; // empty for the player
	int tier = 1; // the base's tier (its army) and the member's strength
	int stars = 0; // the best stars the other side has taken from the base so far
	int by = -1; // who took them (an index into the other side's roster; -1: nobody)
	int attacks = 0; // attacks made so far
	int won = 0; // stars these attacks added to the side
};

/** An Alliance War as it stands at Time (master prompt 12.9). */
struct WarInfo
{
	int64_t no = 0; // the war: the last prep day that came by Time
	int phase = 0; // 0 prep day, 1 battle days, 2 over (until the next prep day)
	int64_t prepAt = 0;
	int64_t battleAt = 0;
	int64_t endAt = 0;
	int64_t nextAt = 0; // the next war's prep day
	bool on = false; // the player's alliance is at war (the fields below are filled)
	int members = 0; // the alliance's members, the player included (0 outside one)
	int opponent = -1; // the computer alliance fought
	int league = 0;
	int roster = 0;
	int me = -1; // the player's place in the roster
	std::vector<WarMember> ours;
	std::vector<WarMember> theirs;
	int ourStars = 0; // taken from their bases
	int theirStars = 0; // taken from ours
	int result = 0; // once over: 1 won, 0 a draw, -1 lost
	int attacksLeft = 0;
	int myStars = 0; // the stars of the player's attacks, each counted in full
};

/** A tile of the Conquest map as it stands: who holds it (an alliance slot, -1 nobody), its Control left and its truce. */
struct ConquestTile
{
	int owner = -1;
	int control = 0;
	int64_t truceUntil = 0;
};

/** A Conquest as it stands at Time (master prompt 12.11). */
struct ConquestInfo
{
	int64_t no = 0; // the Conquest: the last one that began by Time (or the one before the first)
	int phase = 0; // 0 not begun yet, 1 on, 2 over (until the next one)
	int64_t startAt = 0;
	int64_t endAt = 0;
	int64_t nextAt = 0; // the next one's start
	bool on = false; // the player's alliance has a map (the fields below are filled)
	int league = 0;
	std::vector<int> alliances; // by slot: slot 0 is the player's alliance
	std::vector<int64_t> points; // by slot
	std::vector<int> place; // by slot, from 1
	std::vector<ConquestTile> tiles; // as the content's map
	int atkPct = 0; // the shrines the player's alliance holds: % more attack in Conquest battles
	int tokens = 0; // March Tokens now
	int64_t tokenIn = 0; // seconds to the next one (0 at the most)
	int attacks = 0; // the player's attacks in this Conquest (in this alliance)
	int stars = 0;
	int taken = 0; // tiles the player's attacks took
};

/** What a timer the alliance helps with is (master prompt 12.4). */
enum class HelpKind : uint8_t
{
	Building, // a construction or an upgrade (id: the building)
	Land, // a land upgrade (id: the land)
	Troop, // Armoury research (id: the troop)
	Spell, // Arcane Tower research (id: the spell)
	Titan, // a titan's recovery (id: the titan)
	Count
};

/** A timer the player asked the alliance to help with, and how far the help has gone. */
struct HelpAsk
{
	HelpKind kind = HelpKind::Building;
	int32_t id = 0;
	int64_t at = 0; // asked at
	int64_t start = 0; // the timer's start (a titan's recovery: the ask)
	int64_t end = 0; // its end now
	int64_t total = 0; // the whole time each help takes its share of
	int got = 0; // helps so far
	int max = 0; // helps it can get
	int64_t next = 0; // when the next help comes (once got < max)
	int helper = 0; // the first helper (index into Members()); the next ones follow in turn
	int64_t cut = 0; // time taken off so far
};

/** A help the player got: when, on what, from whom (index into Members()) and the time it took off. */
struct HelpGot
{
	int64_t at = 0;
	HelpKind kind = HelpKind::Building;
	int32_t id = 0;
	int member = 0;
	int64_t cut = 0;
};

/** A member's ask the player can help with. */
struct AllyAsk
{
	int64_t no = 0; // the hour it was posted × 8 + its place in that hour
	int member = 0; // index into Members()
	HelpKind kind = HelpKind::Building; // what the member is waiting for
	int64_t at = 0; // posted
	int64_t until = 0; // open until
	bool helped = false;
};

/** Kingdom Power (master prompt 7.7) and where it comes from. */
struct KingdomPowerInfo
{
	int64_t buildings = 0; // building × L^1.5 for each building, the Castle's castleMul times
	int64_t titans = 0; // the strongest titans' battle Power with their relics; 0 without the battle content
	int64_t research = 0; // per troop level from the Armoury, spell level from the Arcane Tower and War Banner level
	int64_t codex = 0; // per Codex entry open
	int64_t total = 0;
	std::vector<std::pair<int32_t, int64_t>> top; // the titans counted (id, Power), strongest first
};

/** One of today's daily quests. */
struct DailyQuest
{
	int def = 0; // index into the content's quest pool
	int64_t target = 0;
	int64_t progress = 0; // may pass the target; cancelled squads and spells take it back
	bool claimed = false;
};

/** A raid on the kingdom: when and where, how it went and what it cost. */
struct RaidReport
{
	int64_t at = 0;
	int32_t landId = 0; // 0 = the city
	int landType = -1; // the land's type (-1 for the city)
	int level = 1; // the raiders' level (a land's raiders match its level)
	int stars = 0; // the raiders' stars; 0 = the defense held
	int64_t raidPower = 0;
	int64_t defensePower = 0;
	ResArray lost{}; // what the raiders took
	int64_t vp = 0; // VP for a held defense
	int32_t titanId = 0; // the titan that defended
	int squads = 0; // the player's squads in the city's defense
	int64_t shieldUntil = 0; // shield after a lost defense
};

/** What the Marshal says before a battle (master prompt 4.6 and 9.3), from the counter matrix (counters.json). */
enum class MarshalTipKind : int32_t
{
	Bring = 1, // the camp has squads of a troop that hits this enemy class hard: take them
	Train = 2, // the troop that would hit it hard is not in the camp: train it
	Exposed = 3, // a squad taken is weak against this enemy class
};

struct MarshalTip
{
	MarshalTipKind kind = MarshalTipKind::Bring;
	int troop = -1; // kingdom troop index
	int enemyClass = -1; // the battle's DefenderClass (Titan for the enemy titan)
	int pct = 100; // the counter multiplier in percent: the troop's against that class, or for Exposed that class's against it
};

/** The Marshal's advice and plan for one battle. */
struct MarshalAdvice
{
	std::vector<MarshalTip> tips; // at most three, the strongest threat first
	/** The Marshal's Plan: squads per troop from the camp, at most the battle slots in all; the troops that hit the
	 *  enemy hardest for the hits they take, Power for Power, one squad at a time (a repeat counts a little less). */
	std::vector<int> plan;
	int64_t planScore = 0; // the plan's counter-weighted Power, and the same for the squads taken
	int64_t takenScore = 0;
};

/** What a defense can expect now: its power, the raiders' and the chance to hold (for the UI). */
struct RaidForecast
{
	bool active = false; // raids come (battle content set, Castle high enough)
	int64_t defense = 0;
	int64_t raiders = 0; // the average of the levels that can come
	int levelLow = 0;
	int levelHigh = 0;
	int holdPct = 0;
	int32_t titanId = 0; // the titan that would defend (0 = none or resting)
	int squads = 0; // the player's squads that would defend (the city)
	int guards = 0; // the Garrison's own guards in the other slots
};

/** Result of checking an action before doing it (for the UI). */
struct Check
{
	Error error = Error::None;
	Requirement missing; // for NeedRequirement
	int castle = 0; // for CastleTooLow / Locked
	int expansion = 0; // for NeedExpansion
	ResArray cost{};
	int64_t timeSec = 0;
	/** Gems that would cover the missing resources (when error is NotEnough). */
	int64_t gemsForMissing = 0;
};

class DOTKINGDOM_API Kingdom
{
public:
	explicit Kingdom(const KingdomContent& InContent);

	void NewGame(int64_t Now, uint64_t Seed);
	bool Load(const std::string& Text, std::string& OutError);
	std::string Save() const;

	/** Moves the clock forward: finishes constructions in time order and regenerates portal stones. */
	void Advance(int64_t Now);

	// --- Queries ---
	const KingdomContent& Content() const { return C; }
	int64_t Now() const { return Time; }
	int64_t Amount(Res R) const { return Res_[static_cast<size_t>(ResIndex(R))]; }
	int64_t Capacity(Res R) const;
	int64_t Protected(Res R) const;
	int64_t ProductionPerHour(Res R) const;
	int CastleLevel() const;
	int Builders() const;
	int BusyBuilders() const;
	int CountOf(int Def) const;
	int MaxCountOf(int Def) const;
	int LandCapacity() const;
	int Expansions() const { return ExpansionCount; }
	int ExtraLandSlots() const { return ExtraSlots; }
	int64_t StonesNextAt() const;
	const std::vector<Building>& Buildings() const { return BuildingList; }
	const std::vector<Land>& Lands() const { return LandList; }
	const std::vector<WorldLand>& World() const { return WorldList; }
	/** Neighbouring lords and the lands they hold (ids from kRivalLandBase, so they never meet the neutral ones). */
	const std::vector<Rival>& Rivals() const { return RivalList; }
	const std::vector<WorldLand>& RivalLands() const { return RivalLandList; }
	static constexpr int32_t kRivalLandBase = 1000000;
	/** A world land by id, neutral or a lord's. */
	const WorldLand* FindWorldLand(int32_t Id) const;
	/** A lord's Castle: the player's moved by the lord's offset. */
	int RivalCastle(int Rival) const;
	/** How many lands a lord holds when none are lost. */
	int RivalLandTarget(int Rival) const;
	/** The lord's titan (index into the content's titans) and its level on one of its lands. */
	int RivalTitan(int Rival) const;
	int RivalTitanLevel(const WorldLand& W) const;
	int32_t PendingAttack() const { return Pending; }
	bool PendingCapture() const { return PendingIsCapture; }
	const Building* Find(int32_t Id) const;
	const Land* FindLand(int32_t Id) const;
	/** What a producer holds right now. */
	int64_t Uncollected(const Building& B) const;
	int64_t UncollectedLand(const Land& L) const;
	int64_t LandStoreCap(const Land& L) const;
	/** Production per period of the land's type (an hour; a day for ruins). */
	int64_t LandRate(const Land& L) const;
	int64_t LandFillHours(const Land& L) const;
	/** Raid loot of the land's own resource at three stars; VP for winning there (both before the stars). */
	int64_t RaidLoot(const WorldLand& W) const;
	int64_t VictoryVp(int LandLevel) const;
	/** VP for winning on this world land (a lord's pays more). */
	int64_t WorldVp(const WorldLand& W) const;
	/** The highest land level the Castle allows: the top of the level band it opens on the world map. */
	int MaxLandLevel() const;
	Check CheckLandUpgrade(int32_t Id) const;
	int64_t LandSkipCost(int32_t Id) const;
	Check CheckBuild(int Def) const;
	Check CheckUpgrade(int32_t Id) const;
	Check CheckExpansion() const;
	int64_t SkipCost(int32_t Id) const;
	int64_t GemsForResources(const ResArray& Missing) const;
	bool CanPlace(int Def, int X, int Y, int32_t IgnoreId) const;
	uint64_t Hash() const;

	// --- Seasons of victory points ---
	int64_t Season() const { return SeasonNo; }
	int64_t SeasonOf(int64_t T) const;
	int64_t SeasonStart(int64_t Season) const;
	/** The league these VP reach (-1 for none). */
	int League(int64_t Vp) const;
	const SeasonResult& LastSeason() const { return Last; }

	// --- Alliances and alliance leagues ---
	/** Computer alliances plus the own one while it exists. */
	int AllianceCount() const;
	AllianceInfo Alliance(int Index) const;
	/** The alliance the player is in (-1 for none). */
	int MyAlliance() const { return AllianceIdx; }
	int64_t AllianceSince() const { return AllianceJoinedAt; }
	/** What alliances check to let the player in: the more of this season's and last season's VP. */
	int64_t AllianceRating() const;
	/** Alliances of a league, best first by this season's VP (ties by index). */
	std::vector<int> LeagueTable(int Tier) const;
	std::vector<AllianceMember> Members(int Index) const;
	/** A league's reward for a place (1-based) in a group of this size: gems, stones, crystal rarity, titan chance. */
	SeasonResult LeagueReward(int Tier, int Place, int GroupSize) const;
	int TitanChancePct(int Tier, int Place, int GroupSize) const;
	Check CheckJoin(int Index) const;
	Check CheckCreate() const;
	/** Squads the Alliance Camp holds; allies' squads sent, arriving or waiting for a battle. */
	int ReinforceCapacity() const;
	const std::vector<Reinforcement>& Reinforcements() const { return ReinforceList; }
	int ReinforcementsReady() const;
	/** When the next request may be sent (0 if never sent). */
	int64_t ReinforceReadyAt() const;
	Check CheckReinforce() const;
	const std::vector<Reinforcement>& PendingReinforcements() const { return PendingReinforce; }

	// --- Army (Barracks, Army Camp, Armoury, Medical School) ---
	int TroopLevel(int Troop) const;
	int64_t ArmyCount(int Troop) const;
	int64_t ArmyTotal() const;
	int64_t ArmyCapacity() const;
	int BattleSlots() const;
	int QueueCapacity() const;
	int MaxTroopLevel() const;
	/** The highest level this troop can be raised to now: the Armoury's limit, and for a council troop also its council's. */
	int TroopLevelCap(int Troop) const;
	/** Level of the troop's envoy council (0 if not built; -1 for a Barracks troop). */
	int CouncilLevel(int Troop) const;
	/** Share of a squad (%) that must survive a battle for it to come back; 100 without a Medical School. */
	int SurvivalPct() const;
	const std::vector<int>& TrainQueue() const { return Queue; }
	bool QueueBlocked() const { return bQueueBlocked; }
	/** When the unit at the head of the queue is (or was) ready; 0 for an empty queue. */
	int64_t QueueHeadFinish() const;
	int64_t QueueHeadStart() const { return QueueStart; }
	int64_t TrainSkipCost() const;
	int ResearchTroop() const { return Research; }
	int64_t ResearchStart() const { return ResearchFrom; }
	int64_t ResearchFinish() const { return ResearchUntil; }
	int64_t ResearchSkipCost() const;
	Check CheckTrain(int Troop) const;
	Check CheckTroopUpgrade(int Troop) const;
	const std::vector<int>& PendingSquads() const { return PendingArmy; }
	int32_t PendingTitan() const { return PendingTitanId; }

	// --- Spells (Spell Forge, Arcane Tower) ---
	int64_t SpellStock(int Spell) const;
	int SpellLevel(int Spell) const;
	int64_t SpellTotal() const;
	int64_t SpellCapacity() const;
	int CraftQueueCapacity() const;
	const std::vector<int>& CraftQueue() const { return Craft; }
	bool CraftBlocked() const { return bCraftBlocked; }
	int64_t CraftHeadFinish() const;
	int64_t CraftHeadStart() const { return CraftStart; }
	int64_t CraftSkipCost() const;
	Check CheckCraft(int Spell) const;
	Check CheckSpellUpgrade(int Spell) const;
	const std::vector<SpellResearchSlot>& SpellResearch() const { return SpellLabs; }
	int SpellResearchSlots() const;
	int MaxSpellLevel() const;
	int64_t SpellResearchSkipCost(int Index) const;
	const std::vector<int>& PendingSpells() const { return PendingSpellList; }

	// --- Relics and land titans ---
	const std::vector<RelicItem>& Relics() const { return RelicList; }
	const RelicItem* FindRelic(int32_t Id) const;
	int RelicSlots(const TitanState& T) const;
	/** Full durability: battles for a combat relic, seconds for an economy relic. */
	int64_t RelicMax(const RelicItem& R) const;
	/** Durability left now (an attached economy relic runs down). */
	int64_t RelicLeft(const RelicItem& R) const;
	/** An effect's value in tenths of a percent (0 while locked). */
	int64_t RelicEffectValue(const RelicItem& R, int Slot) const;
	/** The titan wearing a relic (0 for none). */
	int32_t RelicTitan(int32_t RelicId) const;
	/** What a titan's working relics add to a stat, tenths of a percent (capped per effect). */
	int64_t TitanRelicBonus(int32_t TitanId, RelicStat Stat) const;
	/** What a titan's skills add to a stat, tenths of a percent (not capped). */
	int64_t TitanSkillBonus(int32_t TitanId, RelicStat Stat) const;
	/** What the titan's Lineage Resonance adds to a stat, tenths of a percent (master prompt 5.9). */
	int64_t TitanResonanceBonus(int32_t TitanId, RelicStat Stat) const;
	/** Relics, skills and Resonance together, capped per effect: what the titan takes into battle. */
	int64_t TitanBonus(int32_t TitanId, RelicStat Stat) const;
	/** Skill slots of a titan: the content's slots, one more once ascended (0 without skills in the content). */
	int SkillSlots(const TitanState& T) const;
	/** A skill's bonus in tenths of a percent. */
	int64_t SkillValue(const TitanSkill& S) const;
	/** What the working relic on a building adds to a stat, tenths of a percent. */
	int64_t BuildingRelicBonus(const Building& B, RelicStat Stat) const;
	const RelicItem* BuildingRelic(int32_t BuildingId) const;
	/** Repair chance in percent: each sacrifice adds by its rarity and the target's (the original's table). */
	int RepairChancePct(int32_t Target, const std::vector<int32_t>& Sacrifices) const;
	/** Chance in percent that a successful repair also improves an effect (the target's rarity decides). */
	int EnhanceChancePct(int32_t Target) const;
	/** Whether the next locked effect can open with these sacrifices: one must be at least the target's rarity. */
	bool CanUnlockWith(int32_t Target, const std::vector<int32_t>& Sacrifices) const;

	// --- Titans (Hall of Titans, Relic Temple, fusion) ---
	const std::vector<TitanState>& Titans() const { return TitanList; }
	const TitanState* FindTitan(int32_t Id) const;
	int TitanCapacity() const;
	/** The titan's level cap: 10 + 10 per star, ten more once ascended. */
	int TitanMaxLevel(const TitanState& T) const;
	/** Ascension of a max-level titan for shards and food. */
	Check CheckAscend(int32_t TitanId) const;
	int64_t XpToNext(int Level) const;
	int64_t TitanSkipCost(int32_t Id) const;
	int64_t RelicFreeAt() const { return RelicAt; }
	Check CheckAltar(int Altar) const;
	Check CheckFusion(int32_t Target, const std::vector<int32_t>& Sacrifices) const;

	// --- Actions (they apply at Now(); call Advance first) ---
	Error Build(int Def, int X, int Y, bool UseGems, int32_t* OutId = nullptr);
	Error Upgrade(int32_t Id, bool UseGems);
	Error Speedup(int32_t Id);
	Error Collect(int32_t Id, int64_t* OutAmount = nullptr);
	Error CollectAll(ResArray* OutAmounts = nullptr);
	Error Move(int32_t Id, int X, int Y);
	Error BuyExpansion(bool UseGems);
	Error BuyLandSlot();
	Error RefillStones();
	Error CollectLand(int32_t Id, int64_t* OutAmount = nullptr);
	Error AbandonLand(int32_t Id);
	/** Raises a captured land by one level for gold; a builder works on it for a while. Missing gold can be bought with gems. */
	Error UpgradeLand(int32_t Id, bool UseGems);
	Error SpeedupLand(int32_t Id);
	/** Spends portal stones for a raid (3) or capture (6) of a world land; the battle runs elsewhere. */
	Error StartAttack(int32_t WorldId, bool Capture);
	/** The same with the army that goes: squads (troop indices, one per squad) and a titan (0 for none). */
	Error StartAttack(int32_t WorldId, bool Capture, const std::vector<int>& Squads, int32_t TitanId);
	/** The same with spells from the stock (distinct spell indices; every charge in stock goes along). */
	Error StartAttack(int32_t WorldId, bool Capture, const std::vector<int>& Squads, int32_t TitanId, const std::vector<int>& Spells);
	/** The same, taking every allies' squad that has arrived in the Alliance Camp (they fight once and are gone). */
	Error StartAttack(int32_t WorldId, bool Capture, const std::vector<int>& Squads, int32_t TitanId, const std::vector<int>& Spells, bool Reinforce);
	/** Applies the battle result: loot for a raid, a new land for a capture (needs at least one star). */
	Error FinishAttack(int32_t WorldId, bool Capture, int Stars, Reward* Out = nullptr);
	/** The same with each squad's surviving share in percent (casualties, Medical School) and titan XP. */
	Error FinishAttack(int32_t WorldId, bool Capture, int Stars, const std::vector<int>& SurvivorPct, Reward* Out);
	/** The same with the casts used of each spell taken (in the order of PendingSpells). */
	Error FinishAttack(int32_t WorldId, bool Capture, int Stars, const std::vector<int>& SurvivorPct, const std::vector<int>& SpellsUsed, Reward* Out);

	Error CraftSpell(int Spell);
	Error CancelCraft(int Index);
	Error SkipCraft();
	/** Arcane Tower research: one level of a spell. */
	Error UpgradeSpell(int Spell, bool UseGems);
	Error SkipSpellResearch(int Index);
	Error EquipRelic(int32_t TitanId, int32_t RelicId);
	/** Attaches an economy relic to a building that takes it (one per building; the old one comes off). */
	Error AttachRelic(int32_t BuildingId, int32_t RelicId);
	/** Takes a relic off its titan or building. */
	Error UnequipRelic(int32_t RelicId);
	Error DiscardRelic(int32_t RelicId);
	/** Sacrifices relics for a chance to restore the target's durability (from Castle 8); a success may also
	 *  improve one effect or open a locked one. A relic at full durability can be repaired for the improvement. */
	Error RepairRelic(int32_t Target, const std::vector<int32_t>& Sacrifices, RepairResult* Out = nullptr);
	Error AssignTitan(int32_t LandId, int32_t TitanId);
	Error RecallTitan(int32_t LandId);

	Error Train(int Troop);
	/** Cancels a queued unit and refunds its Food. */
	Error CancelTrain(int Index);
	/** Finishes queued units for gems, as many as the Army Camp has room for. */
	Error SkipTraining();
	Error DismissTroops(int Troop, int Count);
	/** Armoury research: raises a troop type by one level. */
	Error UpgradeTroop(int Troop, bool UseGems);
	Error SkipResearch();
	/** Opens a Relic Temple altar: summons a titan, or for the daily relic sometimes gives a relic item. */
	Error OpenAltar(int Altar, int32_t* OutTitanId = nullptr, int32_t* OutRelicId = nullptr);
	/** Fuses titans into the target (their XP moves to it). Each fused titan may open the target's next skill slot (it
	 *  needs as many stars as the slots open) or else raise one of its skills (up to its own stars, at most tier III). */
	Error FuseTitans(int32_t Target, const std::vector<int32_t>& Sacrifices);
	Error SkipRecovery(int32_t TitanId);
	/** Ascends a max-level titan: ten more levels, one more relic slot and a new skill. Missing Food can be bought with
	 *  gems. */
	Error Ascend(int32_t TitanId, bool UseGems);
	/** Joins a computer alliance (enough VP, a free place, not in an alliance). */
	Error JoinAlliance(int Index);
	/** Founds an own alliance in the lowest league for gems; recruits join it over time. Name and tag are UTF-8. */
	Error CreateAlliance(const std::string& Name, const std::string& Tag);
	/** Leaves the alliance (an own one is disbanded); allies take their squads back. */
	Error LeaveAlliance();
	/** Asks the alliance to fill the Alliance Camp; squads arrive one by one. */
	Error RequestReinforcements();

	// --- Titan Monuments and XP Arenas (the original since 1.8) ---
	// Battles of fixed difficulty beside the world lands. Fighting one (whatever the result) closes it for a day;
	// gems open it again. A won monument may drop shards, a won arena gives the titan extra XP and nothing else.
	int SiteCount() const { return static_cast<int>(C.sites.list.size()); }
	/** When the site can be fought again (0 or a past time: now). */
	int64_t SiteReadyAt(int Site) const;
	/** Locked (Castle too low), NotReady (cooling down), NeedStones; the cost is the portal stones. */
	Check CheckSite(int Site) const;
	/** Gems to fight the site again before its cooldown ends (0 when it is ready). */
	int64_t SiteResetGems(int Site) const;
	Error ResetSite(int Site);
	/** Spends portal stones for a site battle with this army (as StartAttack); the battle runs elsewhere. */
	Error StartSiteAttack(int Site, const std::vector<int>& Squads, int32_t TitanId, const std::vector<int>& Spells, bool Reinforce);
	/** Applies a site battle's result: casualties, spells and titan XP as on a land, then the site's reward. */
	Error FinishSiteAttack(int Site, int Stars, const std::vector<int>& SurvivorPct, const std::vector<int>& SpellsUsed, Reward* Out);
	/** Pending battle ids from here down are campaign missions: -(MissionBase + Act * 100 + Mission). */
	static constexpr int32_t MissionBase = 1000;
	/** The pending battle id of a Titan Trial. */
	static constexpr int32_t TrialPending = -(MissionBase - 1);
	/** The pending battle id of an Assault stage. */
	static constexpr int32_t AssaultPending = -(MissionBase - 2);
	/** The pending battle id of a Raid Boss battle. */
	static constexpr int32_t BossPending = -(MissionBase - 3);
	/** The pending battle id of a Daily Trial battle. */
	static constexpr int32_t DailyPending = -(MissionBase - 4);
	/** The pending battle id of an Alliance War attack. */
	static constexpr int32_t WarPending = -(MissionBase - 5);
	/** The pending battle id of a Titan Spire floor. */
	static constexpr int32_t SpirePending = -(MissionBase - 6);
	/** The pending battle id of a Conquest attack. */
	static constexpr int32_t ConquestPending = -(MissionBase - 7);
	/** The pending battle's site (-1 when the pending battle is on a world land, a mission, a trial, an Assault stage, a
	 *  Raid Boss battle, a Daily Trial, an Alliance War attack, a Titan Spire floor, a Conquest attack or there is none). */
	int PendingSite() const
	{
		return Pending < 0 && Pending != TrialPending && Pending != AssaultPending && Pending != BossPending && Pending != DailyPending
			&& Pending != WarPending && Pending != SpirePending && Pending != ConquestPending && -Pending < MissionBase ? -Pending - 1 : -1;
	}
	bool PendingTrial() const { return Pending == TrialPending; }
	bool PendingAssault() const { return Pending == AssaultPending; }
	bool PendingBoss() const { return Pending == BossPending; }
	bool PendingDaily() const { return Pending == DailyPending; }
	bool PendingWar() const { return Pending == WarPending; }
	bool PendingSpire() const { return Pending == SpirePending; }
	bool PendingConquest() const { return Pending == ConquestPending; }

	// --- Campaigns of the Adventurers' Guild ---
	// Acts of missions fought in order (the master prompt's campaign; the original opens campaigns in the Adventurers'
	// Guild). A mission costs portal stones and gives titan XP like a land battle of its level; the first win also
	// pays its first-clear reward, a repeat win rolls the loot table. Stars won in an act open its star chests.
	int ActCount() const { return static_cast<int>(C.campaigns.acts.size()); }
	int MissionCount(int Act) const;
	/** Best stars won in the mission (0 = not won yet). */
	int MissionStars(int Act, int Mission) const;
	/** Stars won in the act's missions (not on Heroic). */
	int ActStars(int Act) const;
	bool ChestClaimed(int Act, int Chest) const;
	/** BadId, NeedRequirement (no Adventurers' Guild), Locked (Castle too low, or the mission or act before is not won
	 *  yet, or on Heroic the act's last mission: castle 0), NeedStones; the cost is the portal stones. */
	Check CheckMission(int Act, int Mission) const;
	/** Spends portal stones for a mission battle with this army (as StartAttack); the battle runs elsewhere. */
	Error StartMission(int Act, int Mission, const std::vector<int>& Squads, int32_t TitanId, const std::vector<int>& Spells, bool Reinforce);
	/** Applies a mission's result as on a land (casualties, spells, titan XP), keeps the best stars and pays the
	 *  first-clear reward on the first win; a repeat win rolls the loot table. */
	Error FinishMission(int Act, int Mission, int Stars, const std::vector<int>& SurvivorPct, const std::vector<int>& SpellsUsed, Reward* Out);
	/** Opens a star chest: NotEnough (too few stars), Nothing (already open). */
	Error ClaimChest(int Act, int Chest, Reward* Out);
	/** The pending battle's mission as Act * 100 + Mission (-1 when it is not a mission). */
	int PendingMission() const { return -Pending >= MissionBase ? -Pending - MissionBase : -1; }
	// Heroic difficulty (master prompt 10.2): once an act is won to its last mission and the Castle is high enough, its
	// missions open again on Heroic, one after another. The enemy is stronger (the client's scenario) and the mission's
	// level higher; the first win pays more, the loot table rolls more and better relics, and the act's star chests open
	// again for its Heroic stars. Every campaign call above takes HeroicBase + M for mission (or chest) M on Heroic.
	static constexpr int HeroicBase = 50;
	bool HeroicOn() const { return C.campaigns.heroic.castle > 0; }
	/** Stars won in the act's Heroic missions. */
	int HeroicStars(int Act) const;
	/** A mission's level for titan XP, loot and sweep food (Heroic adds to it; 0: no such mission). */
	int MissionLevel(int Act, int Mission) const;
	// --- Army loadouts (master prompt 4.6, S13) ---
	// Saved armies the prepare screen fills in at a tap; what the camp lacks then (squads, a resting titan, spells out
	// of stock) stays out. Kept in the save; nothing else uses them.
	const std::vector<Loadout>& Loadouts() const { return LoadoutList; }
	int MaxLoadouts() const { return C.loadouts.max; }
	/** Saves a loadout in a slot, or as a new one at the end (Slot = their count): BadId (no such slot, an unknown
	 *  troop, spell or titan, a spell twice, a name with control characters or over 48 bytes), TooManySquads (more squads
	 *  than a battle takes or spells than kinds), LimitReached (every slot taken). */
	Error SaveLoadout(int Slot, const Loadout& L);
	/** BadId when there is no such loadout; the ones after it move up. */
	Error DeleteLoadout(int Slot);

	// --- Divine gems (the original's Eternal Shrine) ---
	// The shrine makes divine gems every day and keeps them up to a cap until they are collected; the top alliance
	// leagues pay some too. The shrine's altars sell a titan or a relic of the player's choice for them.
	int64_t DivineGems() const { return DivineCount; }
	/** Divine gems waiting in the shrine (0 without one). */
	int64_t ShrineStored() const;
	/** When the shrine holds one more (0: none built, or it is full). */
	int64_t ShrineNextAt() const;
	/** Collects the shrine: NeedRequirement (not built), Nothing (empty). */
	Error CollectShrine(int64_t* OutGems = nullptr);
	/** Whether an altar sells this pick (a titan type of the altar's stars, or a relic kind): BadId, NeedRequirement (no
	 *  shrine), RosterFull (the Hall of Titans), InventoryFull (relics), NotEnough (divine gems; cost is not used). */
	Check CheckDivine(int Altar, int Pick) const;
	/** Buys from an altar: the new titan's or relic's id in OutId. */
	Error BuyDivine(int Altar, int Pick, int32_t* OutId = nullptr);

	// --- Titan Spire (master prompt 10.7) ---
	// A tower of floors fought one after another, harder every floor; nobody is lost in it and no spell is spent. A
	// floor pays the first time it falls. The top floors close again every Monday 00:00 UTC and pay again; the floor
	// reached in the week places the kingdom in a table of computer lords.
	int SpireFloors() const { return C.spire.Floors(); }
	/** Floors that stay won (below them nothing closes again). */
	int SpireFixed() const { return C.spire.Floors() - C.spire.weekly; }
	/** The highest floor won, this week's top floors counted only when won this week. */
	int SpireReached() const;
	/** The next floor to fight (0: none, every floor is won this week). */
	int SpireFloor() const;
	/** When the top floors close again (the next Monday 00:00 UTC). */
	int64_t SpireResetAt() const { return ((WeekOf() + 1) * 7 - 3) * 86400; }
	/** BadId (no spire), Locked (Castle too low), Busy (a battle is on), Nothing (every floor won this week; timeSec
	 *  until the top floors close again). */
	Check CheckSpire() const;
	/** What a floor pays the first time it falls (gold and food before the Castle multiplies them). */
	EventReward SpireReward(int Floor) const;
	/** A computer lord's floor reached this week (as this week's table shows it). */
	int SpireLordFloor(int Lord) const;
	/** The kingdom's place in this week's table (ahead of the lords it ties). */
	int SpireRank() const;
	/** The pending battle's floor (0 without one). */
	int SpireBattleFloor() const { return Pending == SpirePending ? SpireAt : 0; }
	/** Fights the next floor with this army (as StartAttack, without stones or allies); the battle runs elsewhere. */
	Error StartSpire(const std::vector<int>& Squads, int32_t TitanId, const std::vector<int>& Spells);
	/** Applies a floor's battle: everyone comes back, no spell is spent; a win opens the next floor and pays the floor
	 *  (a relic without room is lost). */
	Error FinishSpire(int Stars, Reward* Out);

	// --- Comeback gift (master prompt 9.5) ---
	// A kingdom not visited for a week or more (Visit) finds a gift on return: gold and food by the Castle, gems, portal stones,
	// War Scrolls and a relic. A longer absence replaces a gift not claimed yet; the client tells what is new since.
	/** Seconds away that earned the gift waiting (0: none). */
	int64_t ComebackAway() const { return ComebackSec; }
	/** When that absence began (0: no gift). */
	int64_t ComebackSince() const { return ComebackSec > 0 ? ComebackAt - ComebackSec : 0; }
	/** Claims the gift: Nothing when none waits; a relic without room in the inventory is lost. */
	Error ClaimComeback(Reward* Out);

	// --- Honor, donations and the Alliance Shop (master prompt 8.1, 12.5, 12.8) ---
	// Honor is the player's own alliance currency: helps given, donations, Alliance War and Raid Boss chests pay it (as
	// the content says), and it stays when the player leaves. In an alliance, from the content's Castle, the player gives
	// gold or food a few times a day for Honor and spends Honor in the Alliance Shop, each item a few times a week.
	int64_t Honor() const { return HonorCount; }
	/** Donations made today (UTC). */
	int DonationsToday() const;
	/** BadId (no Honor in the content), NoAlliance, Locked (Castle too low: castle), LimitReached (the day's donations:
	 *  timeSec to the next day), NotEnough (the gold or food: cost); for a Tech picked (-1: none), BadId (no such tech)
	 *  and MaxLevel (it is at the top). */
	Check CheckDonate(bool Food, int Tech = -1) const;
	/** Gives the alliance gold (or food) for Honor and points in a tech (the one picked, or TechAuto). */
	Error Donate(bool Food, Reward* Out, int Tech = -1);
	/** How many of a shop item (index into the content's shop) the player bought this week (from Monday 00:00 UTC). */
	int ShopBought(int Item) const;
	/** BadId (no such item), NoAlliance, Locked (Castle too low: castle), LimitReached (the week's: timeSec to the next
	 *  week), NotEnough (Honor), InventoryFull (a relic without room), StorageFull (War Scrolls at their maximum). */
	Check CheckBuyShop(int Item) const;
	/** Buys a shop item for Honor. */
	Error BuyShop(int Item, Reward* Out);

	// --- Alliance Tech (master prompt 12.5) ---
	// A donation also researches the alliance's tech: its points go to the tech the player picks (or the lowest one not
	// at the top). An alliance has the levels its league's members researched, and the player's points stay with it.
	// While a member: more helps on each timer, stronger allies' squads, less lost to raids, stronger squads in Alliance
	// War battles, more Raid Boss damage.
	int TechCount() const { return static_cast<int>(C.allianceTech.list.size()); }
	/** The current alliance's level of a tech (0 outside one). */
	int TechLevel(int Tech) const;
	/** The current alliance's points in a tech: its members' and the player's. */
	int64_t TechPoints(int Tech) const;
	/** The player's own points in the current alliance's tech. */
	int64_t TechMine(int Tech) const;
	/** The points the tech's next level needs in all (0 at the top or outside an alliance). */
	int64_t TechNeed(int Tech) const;
	/** The tech a donation goes to without a pick: the lowest level not at the top, the first of those; -1 none. */
	int TechAuto() const;
	/** The effect of the current alliance's tech of this kind: its level times the effect a level (0 outside one). */
	int TechValue(TechKind Kind) const;

	// --- War Banners (master prompt 6.5) ---
	// Every troop has a banner, raised a level at a time for Banner Silk and gold from the content's Castle; each level
	// gives that troop's squads health and damage in every battle (some levels more), and counts as research in Kingdom
	// Power. Silk comes from the Banner Grounds, the Alliance War and Raid Boss chests and the Alliance Shop.
	int64_t Silk() const { return SilkCount; }
	/** The banner's level of a troop (index into the army's troops); 0 none. */
	int BannerLevel(int Troop) const;
	/** Health and damage percent the troop's squads get from its banner. */
	int BannerPct(int Troop) const;
	/** Banner Silk for the troop's next level (0 at the top). */
	int64_t BannerSilkCost(int Troop) const;
	/** BadId (no banners or no such troop), Locked (Castle too low: castle), MaxLevel, NotEnough (the gold in cost, or
	 *  the silk: BannerSilkCost). */
	Check CheckBanner(int Troop) const;
	/** Raises the troop's banner a level. */
	Error UpgradeBanner(int Troop);

	// --- Codex and Lineage Resonance (master prompt 5.9) ---
	// Every titan kind ever bound opens its Codex entry (paid once by its stars, Kingdom Power for each); a lineage whose
	// entries are all open pays its page once. Resonance: with enough kinds of a lineage at the Codex level in the Hall,
	// every titan of that lineage gets its tier's bonuses (added to the relics' and skills').
	/** Whether this titan kind (index into the content's titans) was ever bound. */
	bool CodexSeen(int Def) const;
	/** Whether its entry was paid. */
	bool CodexClaimed(int Def) const;
	/** Whether a lineage's page was paid. */
	bool CodexPageClaimed(int Line) const;
	/** Entries open. */
	int CodexEntries() const;
	/** Pays an open entry: BadId (no such titan or no Codex), Locked (never bound), Nothing (paid already). */
	Error ClaimCodex(int Def, Reward* Out);
	/** Pays a full page: BadId, NotReady (an entry of the lineage is not open), Nothing (paid already), InventoryFull
	 *  (its relic has no room; nothing is paid). */
	Error ClaimCodexPage(int Line, Reward* Out);
	/** Kinds of the lineage's titans in the Hall at the Codex level or higher. */
	int ResonanceCount(int Line) const;
	/** The Resonance tier the lineage has reached (0: none; else an index + 1 into the content's tiers). */
	int ResonanceTier(int Line) const;

	// Sweeps (master prompt 4.19): a mission won on 3 stars is resolved at once for a War Scroll and its march food. It
	// counts as a 3-star repeat win with no losses: the titan sent gets its XP and rests, the loot table is rolled, and
	// quests and the Warpath count it. Quest chests give the scrolls.
	int64_t Scrolls() const { return ScrollCount; }
	/** CheckMission's errors (but stones), then NotReady (not won on 3 stars), LimitReached (no War Scroll), NotEnough
	 *  (food); the cost is the food. */
	Check CheckSweep(int Act, int Mission) const;
	/** Sweeps a mission with this titan (0: none): BadId, TitanRecovering, Busy (a battle is on). */
	Error SweepMission(int Act, int Mission, int32_t TitanId, Reward* Out);

	// --- Daily quests ---
	// Every day (00:00 UTC) brings new quests from the pool for the Castle's level; progress counts what the player does
	// (battles won, resources collected, squads trained, upgrades started...). A done quest is claimed for its reward,
	// and claiming all of them opens the daily chest. Unclaimed quests are gone when the day ends.
	const std::vector<DailyQuest>& Quests() const { return QuestList; }
	int QuestRerollsLeft() const { return std::max(0, C.quests.rerolls - QuestRerolls); }
	bool QuestChestClaimed() const { return bQuestChest; }
	/** When today's quests are replaced. */
	int64_t QuestDayEnd() const { return (QuestDay + 1) * 86400; }
	/** NotReady (not done yet), Nothing (claimed already). */
	Error ClaimQuest(int Slot, Reward* Out);
	/** Swaps an unfinished quest for another one: LimitReached (no rerolls left), Nothing (done or claimed), NotEnough
	 *  (no other quest is open to this kingdom). */
	Error RerollQuest(int Slot);
	/** The daily chest: NotReady (a quest is not claimed yet), Nothing (opened already), InventoryFull (no room for its relic). */
	Error ClaimQuestChest(Reward* Out);
	/** Today's target of a pool quest for this Castle level. */
	int64_t QuestTarget(int Def) const;

	// --- Weekly quests (master prompt 13.5) ---
	// The same as daily quests, from their own pool with bigger targets, for a week (from Monday 00:00 UTC) and with a
	// weekly chest. Rewards of quests, chests, goals and calendars go on top of full vaults.
	const std::vector<DailyQuest>& WeeklyQuests() const { return WeekList; }
	int WeeklyRerollsLeft() const { return std::max(0, C.weekly.rerolls - WeekRerolls); }
	bool WeeklyChestClaimed() const { return bWeekChest; }
	/** When this week's quests are replaced. */
	int64_t WeekEnd() const { return (WeekNo * 7 + 4) * 86400; }
	Error ClaimWeekly(int Slot, Reward* Out);
	Error RerollWeekly(int Slot);
	Error ClaimWeeklyChest(Reward* Out);

	// --- Titan Trials (master prompt 10.4) ---
	// A weekly solo event, open from Monday 00:00 UTC for a few days from a Castle level. Each week brings the next trial
	// of the list: a rule for the player's side (which titans and troops may go) and a fixed enemy army. A few battles a
	// day, free of stones; nothing is lost in them and the titan gets no XP. The best battle score of the week counts:
	// it opens milestone rewards, and once the trial closes its place among a bracket of computer lords pays a rank
	// reward until the week ends.
	int TrialCount() const { return static_cast<int>(C.trials.list.size()); }
	/** This week's trial (index into the content's trials; -1 without trials). */
	int TrialIndex() const;
	int64_t TrialOpensAt() const;
	int64_t TrialClosesAt() const;
	/** Locked (Castle too low: castle), NotReady (closed until timeSec), LimitReached (no battles left today). */
	Check CheckTrial() const;
	int TrialAttemptsLeft() const;
	/** The week's best score (0 before the first battle). */
	int64_t TrialBest() const;
	/** The level of the trial's battle: the Castle's moved by the trial's offset, within the battle table. */
	int TrialLevel() const;
	/** Whether a titan (index) or troop (index) may go to this week's trial. */
	bool TrialAllowsTitan(int Def) const;
	bool TrialAllowsTroop(int Troop) const;
	/** A computer lord's best score this week (0 up to the bracket's size - 2); it grows while the trial is open. */
	int64_t TrialLordScore(int Lord) const;
	/** The kingdom's place in the bracket by the best score (ties go to the kingdom). */
	int TrialRank() const;
	/** The rank reward for a place (index into the content's ranks; -1 for none). */
	int TrialRankReward(int Place) const;
	bool TrialMilestoneClaimed(int I) const { return I >= 0 && I < 32 && ((TrialMask >> I) & 1u) != 0; }
	bool TrialRankClaimed() const { return bTrialRank; }
	/** Starts a trial battle with this army (as StartAttack, without stones or allies); the battle runs elsewhere.
	 *  NotAllowed: a titan or troop the rule keeps out. */
	Error StartTrial(const std::vector<int>& Squads, int32_t TitanId, const std::vector<int>& Spells);
	/** Applies a trial battle: everyone comes back, no spell is spent, the score (within what the stars allow) may be the
	 *  week's best. */
	Error FinishTrial(int Stars, int64_t Score, Reward* Out);
	/** Pays a milestone: BadId, Nothing (claimed), NotReady (best score too low), InventoryFull (no room for its relic). */
	Error ClaimTrialMilestone(int I, Reward* Out);
	/** Pays the rank reward after the trial closes: NotReady (still open or a trial battle is on), Nothing (claimed or no
	 *  reward for the place), InventoryFull. */
	Error ClaimTrialRank(Reward* Out);

	// --- Daily Trials (master prompt 10.3) ---
	// Six trials from a Castle level, each open on its weekday (Monday to Saturday, UTC) and all of them on Sunday. Each
	// has tiers that open one after another: at the tier's Castle level once the tier before it is won in that trial. A
	// few battles or sweeps a day in each trial, free of stones; nothing is lost in a battle and the titan gets no battle
	// XP. A win pays the tier's amount of the trial's reward whatever the stars; a tier once won on 3 stars can be swept:
	// the reward at once, for an attempt. Essence Springs pays titan XP to the titan that fought (so it needs one), the
	// Lineage Trial more shards with a titan of the week's lineage, Hall of Tomes minutes off every research running.
	int DailyCount() const { return static_cast<int>(C.dailyTrials.list.size()); }
	int DailyTiers() const { return static_cast<int>(C.dailyTrials.tierCastle.size()); }
	/** Today's weekday (UTC): 0 Monday ... 6 Sunday. */
	int DailyWeekday() const { return static_cast<int>((std::max<int64_t>(0, Time) / 86400 + 3) % 7); }
	/** Whether a trial is open today (its day, or Sunday). */
	bool DailyOpen(int Trial) const;
	/** This week's lineage (index into the content's lineages; -1 without them). */
	int DailyLineage() const;
	/** Best stars won on a tier (1-based) of a trial: 0 (not won yet) to 3. */
	int DailyStars(int Trial, int Tier) const;
	/** The highest tier open to the player in a trial (0 when none is). */
	int DailyTopTier(int Trial) const;
	int DailyAttemptsLeft(int Trial) const;
	/** BadId, Locked (Castle too low for the trials or the tier: castle), NeedRequirement (the tier before is not won),
	 *  NotReady (not the trial's day: it opens in timeSec), LimitReached (no attempts left today: timeSec). */
	Check CheckDaily(int Trial, int Tier) const;
	/** The amount a win of a tier pays (titan XP, resources, relic rarity, research minutes; shards without the lineage
	 *  bonus). */
	int64_t DailyAmount(int Trial, int Tier) const;
	/** The pending Daily Trial battle's trial and tier (-1 and 0 without one). */
	int DailyBattleTrial() const { return Pending == DailyPending ? DailyAt : -1; }
	int DailyBattleTier() const { return Pending == DailyPending ? DailyAtTier : 0; }
	/** Starts a Daily Trial battle with this army (as StartAttack, without stones or allies); the battle runs elsewhere.
	 *  NotAllowed: Essence Springs without a titan. InventoryFull: Forge Depths with no room for its relic. */
	Error StartDaily(int Trial, int Tier, const std::vector<int>& Squads, int32_t TitanId, const std::vector<int>& Spells);
	/** Applies a Daily Trial battle: everyone comes back, no spell is spent; a win pays the tier (a relic without room is
	 *  lost) and keeps its best stars. */
	Error FinishDaily(int Stars, Reward* Out);
	/** Sweeps a tier won on 3 stars: its reward now, for an attempt. TitanId: the titan that gets Essence Springs' XP (it
	 *  must be one; any titan, resting too) and, in the Lineage Trial, may bring the lineage bonus. NotReady (not won on 3
	 *  stars), Busy (a Daily Trial battle is on), NotAllowed, InventoryFull. */
	Error SweepDaily(int Trial, int Tier, int32_t TitanId, Reward* Out);

	// --- Warpath (master prompt 13.4) ---
	// The free track of a pass that lasts a few weeks from a Monday. Claimed daily and weekly quests, battles (up to a
	// cap a day) and Titan Trial milestones give XP; every tier reached pays its reward once claimed; gems buy the next
	// tier. Past the last tier, XP fills bonus chests. What is reached and not claimed is paid when the pass ends.
	int64_t WarpathSeason() const { return WarpathNo; }
	int64_t WarpathEnd() const;
	int64_t WarpathXp() const { return WpXp; }
	/** Tiers reached (0 up to the content's tiers). */
	int WarpathTier() const;
	bool WarpathClaimed(int Tier) const { return Tier >= 0 && Tier < 64 && ((WpMask >> Tier) & 1u) != 0; }
	/** Bonus chests filled and not opened yet. */
	int WarpathBonusReady() const;
	/** Battle XP counted today (towards the daily cap). */
	int64_t WarpathBattleXpToday() const;
	/** Pays a tier (0-based): BadId, NotReady (not reached), Nothing (claimed), InventoryFull. */
	Error ClaimWarpath(int Tier, Reward* Out);
	/** Buys XP up to the next tier: LimitReached (all tiers reached), NotEnough (gems). */
	Error BuyWarpathTier();
	/** Opens a bonus chest: NotReady (none filled), InventoryFull. */
	Error ClaimWarpathBonus(Reward* Out);
	/** What the end of the last pass paid for tiers reached and not claimed (its number, -1 for none), for a notice. */
	int64_t WarpathPaidSeason() const { return WpPaidNo; }
	const ResArray& WarpathPaid() const { return WpPaid; }
	int WarpathPaidRelics() const { return WpPaidRelics; }

	// --- Assault (master prompt 10.5) ---
	// A monthly event of a few days from a Castle level: one run of sieges in order with an army taken from the Army
	// Camp (a copy: the camp keeps its squads) and a titan. Losses carry over from stage to stage; after each won stage
	// the player takes one of a few boons (stronger squads or titan, or some of the fallen back). A lost stage spends a
	// retry and is fought again with the army it had; the run ends when the stages, the retries or the army run out.
	// Each won stage pays its reward at once.
	/** The event the state belongs to (the last one that opened; -1 before the first roll). */
	int64_t AssaultEvent() const { return AssaultNo; }
	bool AssaultOpen() const;
	/** When this event opened, or when the next one opens while none is open. */
	int64_t AssaultOpensAt() const;
	int64_t AssaultClosesAt() const;
	/** Locked (Castle too low: castle), NotReady (closed until timeSec), Nothing (this event's run is over: the next
	 *  event opens in timeSec). */
	Check CheckAssault() const;
	bool AssaultStarted() const { return bAsRun; }
	bool AssaultRunning() const { return bAsRun && !bAsOver; }
	bool AssaultOver() const { return bAsOver; }
	/** Stages won in this run. */
	int AssaultStagesWon() const { return AsStage; }
	int AssaultRetriesLeft() const;
	/** The Castle level the run began at. */
	int AssaultBase() const { return AsBase; }
	/** A stage's battle level: the Castle's at the run's start plus the stage's offset, within the battle table. */
	int AssaultLevel(int Stage) const;
	int32_t AssaultTitan() const { return AsTitan; }
	const std::vector<AssaultSquad>& AssaultArmy() const { return AsSquads; }
	/** Boons taken (indices into the content's boons, in order) and the ones offered now (empty: none to choose). */
	const std::vector<int>& AssaultBoons() const { return AsBoons; }
	const std::vector<int>& AssaultOffer() const { return AsOffer; }
	AssaultBonus AssaultBonuses() const;
	/** Starts this event's run with squads of the camp (troop indices) and a titan (0: none). Locked, NotReady, Nothing
	 *  (a run already), TooManySquads, NoTroops (none chosen, or more than the camp has), BadId, TitanRecovering. */
	Error StartAssault(const std::vector<int>& Squads, int32_t TitanId);
	/** Begins the next stage's battle with spells of the stock (they are not spent). NoAttack (no run going), NotReady
	 *  (the event is closed, or a boon is to be chosen first), Busy (another battle is on). */
	Error StartAssaultStage(const std::vector<int>& Spells);
	/** Applies a stage's battle. SurvivorPct: the share (%) still standing of each squad that went (the run's squads
	 *  with men left, in order). A win carries the losses over, pays the stage and offers boons; a loss (or a retreat)
	 *  spends a retry, or ends the run when none is left. */
	Error FinishAssaultStage(int Stars, const std::vector<int>& SurvivorPct, Reward* Out);
	/** Takes an offered boon: Nothing (none offered), BadId. */
	Error ChooseAssaultBoon(int I);
	/** Ends the run now: NoAttack (none going), Busy (a stage battle is on). */
	Error EndAssault();

	// --- Raid Boss (master prompt 12.10) ---
	// A weekly alliance event from Sunday 00:00 UTC for a few days, from a Castle level. Each week brings the next boss
	// of the list: a titan with an escort. Its tier sets its level, stars and escort; a computer alliance fights the
	// tier of the league it started in, the leader of an own alliance picks the tier of the coming events. Members share
	// one pool of health (the boss's health per member); each battle takes the boss's lost health (in tenths of a
	// percent) out of it. Computer members fight a few battles at moments of the open days, weaker against a tier over
	// theirs. A few battles a day for the player, free of stones; nothing is lost in them and the titan gets no XP. A
	// battle pays the damage milestones it reached; when the pool is empty the boss falls and everyone who fought it
	// opens the tier's chest, until the next event.
	/** The last event that opened by Time (it may be over). */
	int64_t BossEvent() const;
	bool BossOpen() const;
	/** When this event opened while it is open, else when the next one opens. */
	int64_t BossOpensAt() const;
	int64_t BossClosesAt() const;
	/** The boss of the last event (index into the content's bosses; -1 without bosses). */
	int BossIndex() const;
	/** The tier (1-based) of the last event for the player's alliance (0 outside an alliance). */
	int BossTier() const;
	/** The tier the computer members fight as planned (their league's). */
	int BossHomeTier() const;
	/** The tier of the next event that has not opened yet. */
	int BossNextTier() const;
	/** The own alliance's leader picks the tier of the events that have not opened yet (0: the league's own).
	 *  NoAlliance, NotAllowed (a computer alliance), BadId. */
	Error ChooseBossTier(int Tier);
	/** BadId (no bosses), NoAlliance, Locked (Castle too low: castle), NotReady (closed until timeSec), Nothing (the
	 *  boss has fallen: the next event opens in timeSec), LimitReached (no battles left today). */
	Check CheckBoss() const;
	int BossAttemptsLeft() const;
	/** The alliance's pool of the boss's health and the damage done so far (tenths of a percent, at most the pool). */
	int64_t BossPool() const;
	int64_t BossDamage() const;
	bool BossDefeated() const;
	/** When the pool ran out (0 while the boss stands). */
	int64_t BossDefeatedAt() const;
	/** The player's damage, battles and best battle this event in this alliance. */
	int64_t BossMyDamage() const;
	int BossMyBattles() const;
	int64_t BossMyBest() const;
	/** The computer members' parts (in the order of the alliance's members without the player). */
	std::vector<BossMemberHits> BossMembers() const;
	bool BossChestClaimed() const { return bBossChest && BossNo == BossEvent(); }
	/** Starts a boss battle with this army (as StartAttack, without stones or allies); the battle runs elsewhere. */
	Error StartBoss(const std::vector<int>& Squads, int32_t TitanId, const std::vector<int>& Spells);
	/** Applies a boss battle: everyone comes back, no spell is spent; the boss's lost health (tenths of a percent of it,
	 *  at most 1000) comes out of the pool and pays the milestones it reached (a relic one without room is lost). */
	Error FinishBoss(int Stars, int64_t DamagePermille, Reward* Out);
	/** Opens the alliance chest: NotReady (the boss stands, or a boss battle is on), NotAllowed (the player did not fight
	 *  this boss in this alliance), Nothing (opened), InventoryFull. */
	Error ClaimBossChest(Reward* Out);

	// --- Alliance War (master prompt 12.9) ---
	// A weekly war of the player's alliance (enough members) against a computer alliance of its league with a roster of
	// the same size: a prep day, then battle days. Each member of a roster has a base whose tier follows their place
	// (the strongest first) and a few attacks on the other side's bases; a base gives up to 3 stars and an attack only
	// adds the stars over its best so far. Computer members attack at moments of the battle days: the strongest base
	// they can match that still has stars to give, with chances by the tiers. The side with more stars wins; after the
	// battle days every member who attacked opens the war chest of the league and the result until the next prep day.
	/** The war as it stands for the player's alliance (without one only its days). */
	WarInfo War() const;
	/** BadId (no war in the content, or Base out of the roster), NoAlliance, NotAllowed (the alliance has too few
	 *  members, or there is no rival), Locked (Castle too low: castle), NotReady (not the battle days: they come in
	 *  timeSec), LimitReached (no attacks left), Nothing (the base has given all its stars). Base -1 checks the rest. */
	Check CheckWar(int Base) const;
	/** Starts an attack on a base with this army (as StartAttack, without stones or allies; Busy while a battle is on);
	 *  the battle runs elsewhere. */
	Error StartWar(int Base, const std::vector<int>& Squads, int32_t TitanId, const std::vector<int>& Spells);
	/** Applies a war attack: everyone comes back, no spell is spent; the stars count for the base. */
	Error FinishWar(int Stars, Reward* Out);
	/** Opens the war chest: NotReady (the war is not over, or a war battle is on), NotAllowed (the player did not attack
	 *  in this war in this alliance), Nothing (opened), InventoryFull. */
	Error ClaimWarChest(Reward* Out);
	bool WarChestClaimed() const { return bWarChest && WarNo == WarNoNow(); }
	/** The base the pending war attack is on (-1: none). */
	int WarBattleBase() const { return Pending == WarPending ? WarAt : -1; }

	// --- Conquest (master prompt 12.11) ---
	// A week of every few: the player's alliance and the nearest ones by league take tiles of a hex map, each from its
	// fortress. A tile next to an alliance's land can be attacked; each star takes Control and at none the tile is the
	// attacker's, with a truce. Held tiles give points every hour; the sanctum doubles its neighbours for its holder;
	// shrines strengthen their holder's squads. Computer alliances (and the player's computer members) march every hour;
	// the player spends a March Token an attack. After the week everyone who attacked opens the chest of the place.
	/** The Conquest as it stands for the player's alliance (without one only its days). */
	ConquestInfo Conquest() const;
	/** BadId (no Conquest in the content, or Tile out of the map), NoAlliance, Locked (Castle too low: castle), NotReady
	 *  (no Conquest on: it comes in timeSec), LimitReached (no March Token: the next in timeSec), Nothing (the tile is the
	 *  alliance's or a fortress), NotAllowed (not next to the alliance's land), NotReady (a truce: timeSec). Tile -1 checks
	 *  the rest. */
	Check CheckConquest(int Tile) const;
	/** Starts an attack on a tile with this army (as StartWar; a March Token goes); the battle runs elsewhere. */
	Error StartConquest(int Tile, const std::vector<int>& Squads, int32_t TitanId, const std::vector<int>& Spells);
	/** Applies a Conquest attack: everyone comes back, no spell is spent; the stars take the tile's Control. */
	Error FinishConquest(int Stars, Reward* Out);
	/** Opens the Conquest chest: NotReady (not over, or a Conquest battle is on), NotAllowed (the player did not attack in
	 *  this Conquest in this alliance), Nothing (opened), InventoryFull. */
	Error ClaimConquestChest(Reward* Out);
	bool ConquestChestClaimed() const;
	/** The tile the pending Conquest attack is on (-1: none). */
	int ConquestBattleTile() const { return Pending == ConquestPending ? ConqAt : -1; }

	// --- Season events (master prompt 13.2: Kingdom Rush, Training Grounds) ---
	// Each takes a week of every season from Castle 3. The nominal time of the constructions and research the player
	// starts, and of the squads leaving the Barracks, earns points; milestones are shares of a par set by the Castle
	// level at the event's first point, and the place among computer lords pays after the week. What is left unclaimed
	// is paid when the next event of the kind begins.
	int SeasonEventCount() const { return static_cast<int>(C.seasonEvents.list.size()); }
	/** The event of kind E that runs now or ran last (-1 before the first). */
	int64_t SeasonEventNo(int E) const;
	int64_t SeasonEventStart(int E, int64_t No) const;
	int64_t SeasonEventEnd(int E, int64_t No) const;
	/** Event E runs now (whatever the Castle). */
	bool SeasonEventOn(int E) const;
	/** The event the player's points of kind E belong to (-1: none yet). */
	int64_t SeasonEventMine(int E) const;
	/** The player's points in that event (0 without one). */
	int64_t SeasonEventPoints(int E) const;
	/** The par of that event (of the Castle now before the first point). */
	int64_t SeasonEventPar(int E) const;
	/** The points milestone I of event E asks for. */
	int64_t SeasonEventGoal(int E, int I) const;
	/** A computer lord's points in the player's event of kind E (or the current one) at Time. */
	int64_t SeasonLordPoints(int E, int Lord) const;
	/** The player's place in the bracket (the player before the lords it ties). */
	int SeasonEventPlace(int E) const;
	/** The rank reward of a place (index into ranks; -1: none). */
	int SeasonRankReward(int E, int Place) const;
	bool SeasonMilestoneClaimed(int E, int I) const;
	bool SeasonRankClaimed(int E) const;
	/** BadId, NotReady (too few points), Nothing (claimed), InventoryFull. */
	Error ClaimSeasonMilestone(int E, int I, Reward* Out);
	/** BadId, NotReady (the event is on), Nothing (claimed, no points or no reward for the place), InventoryFull. */
	Error ClaimSeasonRank(int E, Reward* Out);
	/** A squad's training time at its level, without relics (what season events count). */
	int64_t BaseTrainSec(int Troop) const;

	// --- Alliance help (master prompt 12.4) ---
	// The player asks the alliance to help with a running timer; computer members answer one by one, and each help takes
	// a share of the timer's whole time off (at least a minimum). A timer gets a few helps (more with the Alliance Camp),
	// never more than the alliance has members besides the player. The members ask too: helping them pays a little.
	/** Helps a timer asked for now could get: the content's base, more with the Alliance Camp, up to its maximum and to
	 *  the members (0 outside an alliance). */
	int HelpLimit() const;
	/** The time one help takes off a timer of this whole length. */
	int64_t HelpCut(int64_t Total) const;
	/** The asks of timers still running, in the order they were made (finished ones are dropped). */
	std::vector<HelpAsk> HelpAsks() const;
	/** The helps got lately, oldest first (the last 20). */
	const std::vector<HelpGot>& HelpLog() const { return HelpLogList; }
	/** BadId (no help in the content, or no such timer), NoAlliance, NotBusy (the timer is not running), Nothing (already
	 *  asked), NotAllowed (no member to help: an own alliance without recruits). */
	Check CheckAskHelp(HelpKind Kind, int32_t Id) const;
	Error AskHelp(HelpKind Kind, int32_t Id);
	/** The members' asks open now, oldest first. */
	std::vector<AllyAsk> AllyAsks() const;
	/** Helps the player gave today (UTC). */
	int HelpsGivenToday() const;
	/** BadId (no help in the content), NoAlliance, LimitReached (the day's helps given: timeSec to the next day), Nothing
	 *  (no open ask the player has not helped with). */
	Check CheckGiveHelp() const;
	/** Helps every open ask not helped yet, up to the day's limit; each pays the content's reward. Count: how many. */
	Error GiveHelp(int* Count, Reward* Out);

	// --- Main line of goals (master prompt 9.4 and 13.5) ---
	// One goal at a time, in order: build this, reach that level, win a battle... A goal that checks the kingdom's
	// state counts what is there now; an event goal counts actions done while it is the current goal.
	/** The current goal (index into the content's goals); the goals' count when the line is done. */
	int GoalIndex() const { return GoalAt; }
	/** The current goal's count so far (may pass its target). */
	int64_t GoalValue() const;
	bool GoalDone() const;
	/** Pays the current goal and brings the next: NotReady (not done yet), Nothing (the line is done). */
	Error ClaimGoal(Reward* Out);

	// --- Kingdom Power (master prompt 7.7) ---
	/** The kingdom's Power: its buildings, its strongest titans (with the battle content) and its research. */
	KingdomPowerInfo KingdomPower() const;
	/** A titan's battle Power with its relics, in the prompt's units (the simulation's / 100); 0 without the battle content. */
	int64_t TitanPowerOf(int32_t TitanId) const;

	// --- Achievements (master prompt 13.5) ---
	// Each counts actions over the whole game (from the version that started counting) or a fact of the kingdom, and
	// pays gems once per tier reached (bronze, silver, gold).
	/** Actions of one kind over the whole game. */
	int64_t Lifetime(QuestEvent E) const;
	int64_t AchievementValue(int I) const;
	/** Tiers already paid (0 up to the achievement's tiers). */
	int AchievementTier(int I) const;
	/** The next tier is reached and not paid yet. */
	bool AchievementReady(int I) const;
	/** Pays the next tier: BadId, Nothing (all tiers paid), NotReady. */
	Error ClaimAchievement(int I, Reward* Out);

	// --- Login calendars (master prompt 13.6) ---
	// A login is the first Visit of a UTC day. The newcomer calendar opens its day N on the N-th login and keeps it until
	// it is claimed. The monthly one runs in 28-day cycles from the first login: its N-th reward opens on the N-th login
	// of the cycle; days missed before today can be caught up for gems a few times a cycle; what is not claimed when the
	// cycle ends is lost.
	int LoginDays() const { return LoginCount; }
	bool NewcomerClaimed(int Day) const;
	bool NewcomerReady(int Day) const;
	/** Choice: the titan picked on a day with a choice. BadId, Nothing (claimed), NotReady, RosterFull, InventoryFull. */
	Error ClaimNewcomer(int Day, int Choice, Reward* Out);
	/** First UTC day (Unix time / 86 400) of the current cycle; -1 before the first login. */
	int64_t MonthStartDay() const { return MonthStart; }
	int MonthLogins() const { return MonthLoginCount; }
	int MonthClaimed() const { return MonthClaimCount; }
	int MonthCatchUps() const { return MonthCatchUpCount; }
	/** Days of the cycle before today without a login. */
	int MonthMissed() const;
	/** The next reward of the cycle: Nothing (all claimed or no calendar), NotReady, RosterFull, InventoryFull. */
	Error ClaimMonthly(Reward* Out);
	/** Counts a missed day as a login for gems: Nothing (none missed), LimitReached, NotEnough. */
	Error CatchUpMonthly();

	// --- Raids on the kingdom ---
	// AI raiders attack the city and captured lands, also while the game is closed (Advance plays every raid that
	// came in between, in time order). A raid is won or lost on the power of the two sides, by odds fitted to
	// battles of the simulation; the battle content gives each squad's and titan's power. Without it no raids come.
	void SetBattleContent(const SimContent* Battle) { BattleContent = Battle; }
	/** The player is in the game: raiders keep coming until awayHours after the last visit. */
	void Visit();
	int64_t LastVisit() const { return VisitAt; }
	/** When the next raid comes; 0 when none is due (no battle content, or the player is away too long). */
	int64_t NextRaidAt() const;
	int64_t CityShieldUntil() const { return CityShield; }
	/** Squads that defend the city: the Garrison's slots (filled with the player's squads, then its own guards). */
	int DefenseSlots() const;
	int GuardTier() const;
	/** The Garrison's bonus to the city's defense, in percent. */
	int64_t DefenseBonusPct() const;
	const std::vector<int>& GarrisonSquads() const { return GarrisonList; }
	int32_t GarrisonTitan() const { return GarrisonTitanId; }
	/** Sets the city's defense: squads (troop indices, one per slot) and a titan (0 for none). */
	Error SetGarrison(const std::vector<int>& Squads, int32_t TitanId);
	/** The defense of the city (LandId 0) or a captured land against the raiders that can come now. */
	RaidForecast Forecast(int32_t LandId) const;
	const std::vector<RaidReport>& Raids() const { return RaidLog; }
	/** Reports after this time are new to the player. */
	int64_t RaidsReadAt() const { return RaidsRead; }
	Error ReadRaids();
	/** Scouting before a battle (master prompt 9.3): the chance in permille that an attack wins, by the raid odds fitted
	 *  to simulated battles. Squads and titans are both sides' start Power as the battle counts it; the titans weigh
	 *  odds.titanPct %, the defender's AI profile scales its side as for raiders. */
	int64_t BattleWinPermille(int64_t AttackSquads, int64_t AttackTitans, int64_t DefenseSquads, int64_t DefenseTitans, const std::string& DefenseAi) const;
	/** One side's Power for those odds: squads plus titans weighted, scaled by the AI profile (100% for a player). */
	int64_t OddsPower(int64_t Squads, int64_t Titans, const std::string& Ai) const;
	/** The Marshal (master prompt 4.6 and 9.3): tips against the enemy's squads and titan for the squads taken (per troop,
	 *  allies' included) and the Marshal's Plan from the camp. Allowed (per troop, empty = all) keeps a trial's rule.
	 *  Empty without the battle content. */
	MarshalAdvice Marshal(const SideSetup& Enemy, const std::vector<int>& Taken, const std::vector<bool>& Allowed) const;

private:
	const LevelDef* CurrentLevel(const Building& B) const;
	const LevelDef* NextLevel(const Building& B) const;
	Check CheckLevel(int Def, const LevelDef& L, bool bNew, bool bNeedsBuilder) const;
	bool Pay(const ResArray& Cost, bool UseGems);
	void Settle(Building& B, int64_t At) const;
	void SettleLand(Land& L, int64_t At) const;
	void Complete(Building& B, int64_t At);
	void CompleteLand(Land& L, int64_t At);
	/** The level band of world lands at this Castle level: three levels from the low end (capped at the last level). */
	int LandBandLow(int Castle) const;
	int LandBandTop(int Castle) const;
	void RegenStones(int64_t Until);
	void AddResource(Res R, int64_t Amount);
	void RollSeason(int64_t At);
	int64_t LandRateNum(const Land& L) const;
	int64_t LandPeriodSec(const Land& L) const;
	WorldLand MakeWorldLand();
	void FillWorld();
	/** The lords on a new game or an old save without them; their lands up to what each holds. */
	void ResetRivals();
	void FillRivals();
	int PickLandType(uint64_t H, int Castle) const;
	WorldLand MakeRivalLand(int Rival);
	WorldLand* FindWorld(int32_t Id, bool* OutRival);
	int64_t ProducerRate(const Building& B) const;
	int64_t ProducerCap(const Building& B) const;
	const LevelDef* BuildingLevel(int Def) const;
	int64_t TrainSec(int Troop) const;
	void ProcessQueue(int64_t Until);
	void UnblockQueue(int64_t At);
	void ResetArmy();
	void ResetSpellsAndRelics();
	void AddTitanXp(TitanState& T, int64_t Xp, int* OutLevels);
	int64_t TitanTotalXp(const TitanState& T) const;
	int64_t GemsForSeconds(int64_t Seconds) const;
	int64_t CraftSec(int Spell) const;
	int64_t CraftGold(int Spell) const;
	void ProcessCraft(int64_t Until);
	void UnblockCraft(int64_t At);
	void SettleLands();
	void UnequipEverywhere(int32_t RelicId);
	RelicItem* MutableRelic(int32_t Id);
	/** Counts an attached economy relic's time up to now and settles its building (before anything changes). */
	void TouchRelic(RelicItem& R);
	/** A new relic item at full durability with its first effects open; 0 when the inventory is full. */
	int32_t AddRelic(int Kind, int Rarity);
	int RollRelicKind(uint64_t H) const;
	int RollRarity(const std::array<int64_t, 5>& Weights, uint64_t H) const;
	int RelicSlotCount(const RelicItem& R) const;
	/** Time cut by a relic stat on buildings of a def, tenths of a percent (capped). */
	int64_t DefRelicBonus(int Def, RelicStat Stat) const;
	int64_t CapBonus(RelicStat Stat, int64_t Value) const;
	int32_t SummonTitan(int Stars, uint64_t Pick);
	/** Adds a titan of this type to the Hall (its start skills set). */
	int32_t AddTitanOf(int Def);
	int BaseAlliances() const { return static_cast<int>(C.alliances.names.size()); }
	bool HasOwnAlliance() const { return AllianceIdx >= 0 && AllianceIdx == BaseAlliances(); }
	void ResetAlliances();
	/** A computer alliance's strength: the weekly VP of its average member. */
	int64_t AllianceStrength(int Index) const;
	int AllianceSize(int Index) const; // computer members (recruits so far for the own one)
	int64_t AllianceJoinVp(int Index) const;
	int AllianceTier(int Index) const;
	int64_t MemberVp(int Index, int Member, int64_t Season, int64_t At) const;
	int64_t AllianceVp(int Index, int64_t Season, int64_t At) const;
	int RecruitsAt(int64_t At) const;
	/** Ranks every league at the end of the season, pays the player's alliance and moves alliances up and down. */
	bool RollAllianceLeagues(int64_t End);
	/** Checks the army that goes to a battle, spends the stones and makes it the pending battle (Target: a world
	 *  land's id, -(site + 1) or a mission's id). */
	Error BeginBattle(int32_t Target, bool Capture, int64_t Stones, const std::vector<int>& Squads, int32_t TitanId, const std::vector<int>& Spells,
		bool Reinforce);
	/** Brings the pending army back: casualties, spells cast, the titan's XP (XpPct of a land battle of this level),
	 *  relic wear and rest; allies' squads leave. */
	void EndBattle(int Level, int Stars, int64_t XpPct, const std::vector<int>& SurvivorPct, const std::vector<int>& SpellsUsed, Reward& R);
	/** Counts an event for today's quests (a negative amount takes it back). */
	void NoteQuest(QuestEvent Event, int64_t Amount);
	/** Opens the titan's next skill slot with a skill of its class it does not have yet (tier I). */
	void AddSkill(TitanState& T, uint64_t H) const;
	/** The titan's first skill (and the ascension one), rolled from the seed and its id. */
	void StartSkills(TitanState& T) const;
	/** What a titan's working relics add to a stat before the cap. */
	int64_t TitanRelicSum(int32_t TitanId, RelicStat Stat) const;
	/** Opens the Codex entry of a titan kind just bound. */
	void NoteCodex(int Def);
	/** The titan kinds in the Hall now (bit = index into the content's titans). */
	uint64_t OwnedKinds() const;
	/** Brings the quests of the day Time is in (when the day has changed), and of the week. */
	void RollQuests();
	/** Whether the pool quest can come to this kingdom now. */
	bool QuestOpen(int Def) const;
	bool QuestOpen(const QuestDef& D) const;
	int64_t QuestTarget(const QuestDef& D) const;
	/** Picks Count quests of the pool open to this kingdom, by the seed, a salt and the period. */
	std::vector<DailyQuest> PickQuests(const QuestRules& Rules, uint64_t Salt, int64_t Period) const;
	/** Shared by daily and weekly quests. */
	Error ClaimQuestIn(const QuestRules& Rules, std::vector<DailyQuest>& List, int Slot, Reward* Out);
	Error RerollQuestIn(const QuestRules& Rules, std::vector<DailyQuest>& List, int& Used, uint64_t Salt, int Slot);
	Error ClaimChestIn(const QuestRules& Rules, const std::vector<DailyQuest>& List, bool& bOpened, uint64_t Salt, int64_t Period, Reward* Out);
	/** Pays a reward on top of full vaults (a gift is never lost; the vaults then collect nothing until it is spent). */
	void PayGift(const ResArray& Pay, Reward& R);
	/** The same with an event's relic (InventoryFull when it has no room, and nothing is paid); Scale multiplies gold
	 *  and food by the Castle's level. */
	Error PayEvent(const EventReward& Pay, uint64_t Salt, uint64_t Pick, Reward& R, bool Scale = false);
	/** Adds Warpath XP (battle XP up to the daily cap). */
	void AddWarpathXp(int64_t Xp, bool Battle);
	/** Starts the pass of Time's weeks; the old one's reached tiers and chests are paid first. */
	void RollWarpath();
	/** The last Assault event that opened by Time (it may be over) and the day an event opens. */
	int64_t AssaultNoNow() const;
	int64_t AssaultStartDay(int64_t No) const;
	/** Brings the state of a new Assault event once it opens. */
	void RollAssault();
	void ResetAssault();
	/** The Raid Boss of an alliance in an event: the alliance (BossKey), its computer members, the tier and theirs. */
	struct BossSnap
	{
		int64_t key = -1;
		int members = 0;
		int tier = 0;
		int home = 0;
	};
	/** The battles of the boss up to Time: the damage done (at most the pool), when it fell, the members' parts. */
	struct BossTally
	{
		int64_t pool = 0;
		int64_t total = 0;
		int64_t defeatedAt = 0;
		std::vector<BossMemberHits> members;
	};
	/** The alliance the player is in as a key (-1: none; an own alliance by when it was founded). */
	int64_t BossKey() const;
	int64_t BossDayOf(int64_t No) const { return No * 7 + 3; }
	/** The own alliance's pick for an event (0: the league's own). */
	int BossPickFor(int64_t No) const { return No >= BossPickFrom ? BossPickNew : BossPickOld; }
	BossSnap BossSnapNow(int64_t No) const;
	/** The saved snapshot when it is this event's and the alliance's, else the alliance as it is now. */
	BossSnap BossSnapOf() const;
	/** Whether the player's battles saved belong to the last event and the alliance the player is in. */
	bool BossMine() const;
	BossTally TallyBoss() const;
	void RollBoss();
	void ResetBoss();
	/** The Alliance War of an alliance: the alliance (BossKey), the rival, the roster, the league and the player's place. */
	struct WarSnap
	{
		int64_t key = -1;
		int opp = -1;
		int roster = 0;
		int league = 0;
		int me = -1;
	};
	/** The last war whose prep day came by Time, and a war's prep day. */
	int64_t WarNoNow() const;
	int64_t WarPrepDay(int64_t No) const { return No * 7 + C.allianceWar.prepDay - 3; }
	bool WarContent() const { return C.allianceWar.tiers > 0; }
	/** The war of the alliance the player is in now (key -1: none, or too few members, or no rival). */
	WarSnap WarSnapNow(int64_t No) const;
	/** The saved snapshot when it is this war's and the alliance's, else the alliance as it is now. */
	WarSnap WarSnapOf() const;
	/** Whether the player's attacks saved belong to this war and the alliance the player is in. */
	bool WarMine() const;
	/** The tier of a roster place. */
	int WarTier(int League, int Place, int Roster) const;
	void RollWar();
	void ResetWar();
	/** Weeks since the Monday before the epoch, for Time. */
	int64_t WeekOf() const { return (std::max<int64_t>(0, Time) / 86400 + 3) / 7; }
	/** Conquest: the alliances of an event (slot 0 the player's, with its key), the league and each slot's computer
	 *  members. */
	struct ConqSnap
	{
		int64_t key = -1;
		int league = 0;
		std::vector<int> ids;
		std::vector<int> members;
	};
	struct ConqHit
	{
		int64_t at = 0;
		int tile = 0;
		int stars = 0;
	};
	bool ConquestContent() const { return C.conquest.On(); }
	/** The first day of a Conquest, and the last Conquest that began by Time. */
	int64_t ConqStartDay(int64_t No) const { return (No * C.conquest.weeks + C.conquest.week) * 7 - 3; }
	int64_t ConqNoNow() const;
	/** The alliances of the player's alliance now (key -1: none). */
	ConqSnap ConqSnapNow() const;
	/** The saved alliances when they are this Conquest's and the alliance's, else the alliance as it is now. */
	ConqSnap ConqSnapOf() const;
	bool ConqMine() const;
	/** The map from the start to Until with the computer marches and the player's attacks (hits); fills tiles, points
	 *  and the player's stars and tiles taken. */
	void SimConquest(const ConqSnap& S, int64_t Start, int64_t Until, const std::vector<ConqHit>& Hits, ConquestInfo& Out) const;
	int ConqTokens(int64_t* NextIn) const;
	void ResetConquest();
	/** Season events: points for an act of Sec nominal seconds at At; the state of kind E for event No (made, paying
	 *  what the one before left, when it is another's); what is left unclaimed paid. */
	void NoteSeason(SeasonAct Act, int64_t Sec, int64_t At);
	void RollSeasonEvents();
	void OpenSeasonEvent(int E, int64_t No);
	void PaySeasonLeft(int E);
	int64_t SeasonNoAt(int E, int64_t At) const;
	// Raids (KingdomRaids.cpp)
	void ResetRaids(int64_t Now);
	void RunRaid(int64_t At);
	int RaidLevelLow() const;
	int RaidLevelHigh() const;
	/** Titans (indices) that raiders of this level can bring; empty when they come without one. */
	std::vector<int> RaiderTitans(int Level) const;
	int RaiderTitanLevel(int Level) const;
	/** The two sides of a raid on the city (LandId 0) or a land at a time; Pick chooses the raiders' titan. */
	void RaidArmies(int32_t LandId, int Level, uint64_t Pick, int64_t At, SideSetup& Raiders, SideSetup& Defense, int* OutSquads, int* OutGuards,
		int32_t* OutTitan) const;
	int64_t ArmyPower(const SideSetup& Side) const;
	/** A titan of the Hall as the battle takes it: stars, level, ascension and its relics' bonuses. */
	void TitanBattleSetup(const TitanState& T, TitanSetup& Out) const;
	/** A defense's power: its army's, with the Garrison's bonus on the city (LandId 0). */
	int64_t DefensePower(const SideSetup& Defense, int32_t LandId) const;
	/** Chance in permille that raiders of this power beat this defense. */
	int64_t RaidWinPermille(int64_t Raid, int64_t Defense) const;
	void ClearGuard(int32_t TitanId);

	const KingdomContent& C;
	int64_t Time = 0;
	ResArray Res_{};
	int64_t StonesAt = 0;
	std::vector<Building> BuildingList;
	std::vector<Land> LandList;
	std::vector<WorldLand> WorldList;
	std::vector<Rival> RivalList;
	std::vector<WorldLand> RivalLandList;
	uint64_t RivalCounter = 0;
	int ExpansionCount = 1;
	int ExtraSlots = 0;
	int32_t NextId = 1;
	uint64_t Seed = 1;
	uint64_t WorldCounter = 0;
	int32_t Pending = 0;
	bool PendingIsCapture = false;
	int64_t SeasonNo = 0;
	SeasonResult Last;
	// Alliances
	int AllianceIdx = -1;
	int64_t AllianceJoinedAt = 0;
	std::vector<int> AllianceTiers; // league of every computer alliance, then of the own one while it exists
	std::vector<int> AlliancePerm; // which name each computer alliance has (from the seed; not saved)
	std::string OwnName;
	std::string OwnTag;
	int64_t OwnFounded = 0;
	std::vector<Reinforcement> ReinforceList;
	int64_t ReinforceAt = 0;
	bool bReinforceAsked = false;
	std::vector<Reinforcement> PendingReinforce;
	// Army
	std::vector<int> TroopLevels;
	std::vector<int64_t> Army; // units by troop, squads away in battle included
	std::vector<int> Queue; // troop per queued unit
	std::vector<int64_t> QueuePaid; // food paid for each queued unit (refunded on cancel)
	int64_t QueueStart = 0;
	bool bQueueBlocked = false;
	int Research = -1;
	int64_t ResearchFrom = 0;
	int64_t ResearchUntil = 0;
	std::vector<int> PendingArmy;
	int32_t PendingTitanId = 0;
	// Spells
	std::vector<int64_t> SpellStockList;
	std::vector<int> SpellLevelList;
	std::vector<int> Craft; // spell per queued craft
	std::vector<int64_t> CraftPaid; // gold paid for each queued craft (refunded on cancel)
	int64_t CraftStart = 0;
	bool bCraftBlocked = false;
	std::vector<SpellResearchSlot> SpellLabs;
	std::vector<int> PendingSpellList;
	// Titans and relics
	std::vector<TitanState> TitanList;
	std::vector<RelicItem> RelicList;
	int64_t RelicAt = 0;
	uint64_t RelicCounter = 0;
	// Titan Monuments and XP Arenas: when each site opens again (by site index), and the shard drop counter.
	std::vector<int64_t> SiteReady;
	uint64_t SiteCounter = 0;
	// Campaigns: best stars by act and mission, opened star chests by act (bits).
	std::vector<std::vector<uint8_t>> MissionStarList;
	std::vector<uint32_t> ChestMask;
	std::vector<std::vector<uint8_t>> HeroicStarList; // the same on Heroic
	std::vector<uint32_t> HeroicChestMask;
	// Army loadouts.
	std::vector<Loadout> LoadoutList;
	// Divine gems held, and when the shrine began filling (0: no shrine built yet).
	int64_t DivineCount = 0;
	int64_t ShrineFrom = 0;
	// Titan Spire: the fixed floors won, the week of the top floors won and the highest of them, the floor fought.
	int SpireTop = 0;
	int64_t SpireWeek = -1;
	int SpireWeekTop = 0;
	int SpireAt = 0;
	int64_t ComebackSec = 0; // the absence that earned the gift waiting (0: none)
	int64_t ComebackAt = 0; // when it ended
	uint64_t CodexSeenMask = 0; // titan kinds ever bound (bit = index into the content's titans)
	uint64_t CodexPaidMask = 0; // entries paid
	uint64_t CodexPageMask = 0; // lineage pages paid
	int64_t HonorCount = 0;
	int64_t DonateDay = 0; // the UTC day of DonateCount
	int DonateCount = 0; // donations made that day
	int64_t ShopWeek = 0; // the week of ShopBoughtList
	std::vector<int> ShopBoughtList; // by shop item: bought that week
	// Alliance Tech: the player's points by alliance index, by tech.
	std::vector<std::pair<int, std::vector<int64_t>>> TechPtsList;
	const std::vector<int64_t>* MyTechPts(int Index) const;
	int64_t TechBase(int Index, int Tech) const;
	int64_t SilkCount = 0; // Banner Silk
	std::vector<int> BannerList; // by troop: the banner's level
	// War Scrolls held, and loot table rolls so far (repeat wins and sweeps).
	int64_t ScrollCount = 0;
	uint64_t LootRolls = 0;
	/** Rolls the missions' loot table into R (gold and food by level and stars). */
	void RollMissionLoot(int Level, int Stars, bool bHeroic, Reward& R);
	/** Whether Mission (HeroicBase + M on Heroic) is a mission of the act. */
	bool ValidMission(int Act, int Mission) const;
	/** Adds War Scrolls up to the most a kingdom holds; returns how many went in. */
	int64_t AddScrolls(int64_t N);
	// Daily quests: the day they belong to (days since the epoch, UTC), the quests, rerolls used, the chest, and a counter
	// for rerolls.
	int64_t QuestDay = -1;
	std::vector<DailyQuest> QuestList;
	int QuestRerolls = 0;
	bool bQuestChest = false;
	uint64_t QuestCounter = 0;
	// Weekly quests: the week (weeks since the Monday before the epoch), the quests, rerolls used, the chest
	int64_t WeekNo = -1;
	std::vector<DailyQuest> WeekList;
	int WeekRerolls = 0;
	bool bWeekChest = false;
	// Titan Trials: the week the state belongs to, battles fought on a day, the best score, milestones paid (bits), the
	// rank reward
	int64_t TrialWeek = -1;
	int64_t TrialDay = -1;
	int TrialTries = 0;
	int64_t TrialScore = 0;
	uint32_t TrialMask = 0;
	bool bTrialRank = false;
	// Warpath: the pass (weeks since the Monday before the epoch / weeks a pass), XP, tiers claimed (bits), bonus chests
	// opened, battle XP on a day, and what the end of the last pass paid
	int64_t WarpathNo = -1;
	int64_t WpXp = 0;
	uint64_t WpMask = 0;
	int WpBonus = 0;
	int64_t WpDay = -1;
	int64_t WpDayXp = 0;
	int64_t WpPaidNo = -1;
	ResArray WpPaid{};
	int WpPaidRelics = 0;
	// Assault: the event, whether its run began and is over, stages won, retries used, the Castle at the start, the
	// titan, the squads, boons taken and boons offered
	int64_t AssaultNo = -1;
	bool bAsRun = false;
	bool bAsOver = false;
	int AsStage = 0;
	int AsTries = 0;
	int AsBase = 1;
	int32_t AsTitan = 0;
	std::vector<AssaultSquad> AsSquads;
	std::vector<int> AsBoons;
	std::vector<int> AsOffer;
	// Raid Boss: the event, the alliance and tier it was rolled for, the player's battles (time, damage), the chest, the
	// day's battles, the own alliance's pick (old before the event BossPickFrom, new from it)
	int64_t BossNo = -1;
	BossSnap BossSaved;
	std::vector<std::pair<int64_t, int64_t>> BossHits;
	bool bBossChest = false;
	int64_t BossDay = -1;
	int BossTries = 0;
	int BossPickOld = 0;
	int BossPickNew = 0;
	int64_t BossPickFrom = 0;
	// Alliance War: the war and the alliance it was rolled for, the player's attacks (time, base, stars), the chest, the
	// war of the attacks spent and how many, the pending attack's base
	struct WarHit
	{
		int64_t at = 0;
		int base = 0;
		int stars = 0;
	};
	int64_t WarNo = -1;
	WarSnap WarSaved;
	std::vector<WarHit> WarHits;
	bool bWarChest = false;
	int64_t WarUsedNo = -1;
	int WarUsed = 0;
	int WarAt = -1;
	// Conquest: the event and the alliances it was rolled for, the player's attacks, the chest, the March Tokens (the
	// event they count in, how many at TokAt), the pending attack's tile; a cache of the last map worked out
	int64_t ConqNo = -1;
	ConqSnap ConqSaved;
	std::vector<ConqHit> ConqHits;
	bool bConqChest = false;
	int64_t ConqTokNo = -1;
	int ConqTok = 0;
	int64_t ConqTokAt = 0;
	int ConqAt = -1;
	struct ConqCache
	{
		uint64_t key = 0;
		bool valid = false;
		ConquestInfo info;
	};
	mutable ConqCache ConqMemo;
	// Season events, by kind: the event the points belong to, the Castle level its par was set at, the points, the
	// milestones paid (bits) and the rank reward paid
	struct SeasonEvState
	{
		int64_t no = -1;
		int castle = 0;
		int64_t points = 0;
		uint32_t mask = 0;
		bool rank = false;
	};
	std::vector<SeasonEvState> SeasonEvs;
	// Alliance help: the player's asks (the raw list: finished timers are dropped when time moves on), the helps got
	// lately, the members' asks helped (by no) and the helps given on a day
	std::vector<HelpAsk> AskList;
	std::vector<HelpGot> HelpLogList;
	std::vector<int64_t> HelpedNos;
	int64_t GiveDay = -1;
	int GiveCount = 0;
	/** Whether an ask's timer is still the one asked for and runs past At. */
	bool HelpLive(const HelpAsk& A, int64_t At) const;
	/** The timer's end now (0 if it is not running). */
	int64_t HelpEnd(HelpKind Kind, int32_t Id) const;
	/** Moves the timer's end to End. */
	void SetHelpEnd(const HelpAsk& A, int64_t End);
	/** The alliance's members other than the player at a moment (computer members, recruits of an own alliance). */
	int HelpMembers(int64_t At) const;
	/** The index into Members() of a computer member (0-based among them). */
	int HelpMemberIndex(int Member) const { return Member + (HasOwnAlliance() ? 1 : 0); }
	/** When help number Got of an ask comes (Got 0: the first). */
	int64_t HelpNextAt(const HelpAsk& A, int Got, int64_t From) const;
	/** Applies the help an ask gets at At. */
	void ApplyHelp(HelpAsk& A, int64_t At);
	/** Drops the asks whose timers have ended by At. */
	void PruneHelp(int64_t At);
	void ResetHelp();
	// Daily Trials: the day of the attempts, attempts by trial that day, best stars by trial (2 bits a tier), the
	// pending battle's trial and tier
	int64_t DailyDay = -1;
	std::vector<int> DailyTries;
	std::vector<uint32_t> DailyBest;
	int DailyAt = -1;
	int DailyAtTier = 0;
	/** Pays a Daily Trial tier into R (TitanId: the titan for XP and the lineage); Pick tells relic rolls apart. */
	Error PayDaily(int Trial, int Tier, int32_t TitanId, uint64_t Pick, Reward& R);
	/** Counts an attempt of a trial today. */
	void SpendDaily(int Trial);
	// Main line of goals
	int GoalAt = 0;
	int64_t GoalEvents = 0;
	// Achievements: counts by QuestEvent over the whole game, tiers paid by achievement
	std::array<int64_t, static_cast<size_t>(QuestEvent::Count)> LifetimeCounts{};
	std::vector<int> AchTiers;
	// Login calendars
	int LoginCount = 0;
	int64_t LoginDay = -1;
	uint32_t NewcomerMask = 0;
	int64_t MonthStart = -1;
	int MonthLoginCount = 0;
	int MonthClaimCount = 0;
	int MonthCatchUpCount = 0;
	void NoteLogin();
	bool MonthRunning() const;
	Error PayCalendarDay(const CalendarDay& D, int Choice, uint64_t H, Reward* Out);
	/** What a goal or an achievement counts now (Events: its event count). */
	int64_t CheckValue(GoalCheck Check, int BuildingDef, int Troop, int64_t Events) const;
	// Raids
	const SimContent* BattleContent = nullptr;
	int64_t NextRaid = 0;
	int64_t VisitAt = 0;
	uint64_t RaidCounter = 0;
	int64_t CityShield = 0;
	int64_t RaidsRead = 0;
	std::vector<int> GarrisonList; // troop per defending squad
	int32_t GarrisonTitanId = 0;
	std::vector<RaidReport> RaidLog; // oldest first
};
} // namespace kingdom
} // namespace dot
