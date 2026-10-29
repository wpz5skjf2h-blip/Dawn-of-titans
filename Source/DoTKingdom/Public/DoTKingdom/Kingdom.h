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

/** A neutral land on the world map that can be raided or captured. */
struct WorldLand
{
	int32_t id = 0;
	int type = 0;
	int level = 1;
};

/** A titan the player owns. */
struct TitanState
{
	int32_t id = 0;
	int def = 0;
	int level = 1;
	int64_t xp = 0; // towards the next level
	int64_t recoverUntil = 0; // resting after a battle until this time
	bool ascended = false; // ten more levels and one more relic slot
	std::vector<int32_t> relics; // equipped relic ids
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
	/** Fuses titans into the target (their XP moves to it). */
	Error FuseTitans(int32_t Target, const std::vector<int32_t>& Sacrifices);
	Error SkipRecovery(int32_t TitanId);
	/** Ascends a max-level titan: ten more levels and one more relic slot. Missing Food can be bought with gems. */
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
	/** The pending battle's site (-1 when the pending battle is on a world land, a mission or there is none). */
	int PendingSite() const { return Pending < 0 && -Pending < MissionBase ? -Pending - 1 : -1; }

	// --- Campaigns of the Adventurers' Guild ---
	// Acts of missions fought in order (the master prompt's campaign; the original opens campaigns in the Adventurers'
	// Guild). A mission costs portal stones and gives titan XP like a land battle of its level; the first win also
	// pays its first-clear reward. Stars won in an act open its star chests.
	int ActCount() const { return static_cast<int>(C.campaigns.acts.size()); }
	int MissionCount(int Act) const;
	/** Best stars won in the mission (0 = not won yet). */
	int MissionStars(int Act, int Mission) const;
	/** Stars won in the act's missions. */
	int ActStars(int Act) const;
	bool ChestClaimed(int Act, int Chest) const;
	/** NeedRequirement (no Adventurers' Guild), Locked (Castle too low, or the mission or act before is not won yet:
	 *  castle 0), NeedStones; the cost is the portal stones. */
	Check CheckMission(int Act, int Mission) const;
	/** Spends portal stones for a mission battle with this army (as StartAttack); the battle runs elsewhere. */
	Error StartMission(int Act, int Mission, const std::vector<int>& Squads, int32_t TitanId, const std::vector<int>& Spells, bool Reinforce);
	/** Applies a mission's result as on a land (casualties, spells, titan XP), keeps the best stars and pays the
	 *  first-clear reward on the first win. */
	Error FinishMission(int Act, int Mission, int Stars, const std::vector<int>& SurvivorPct, const std::vector<int>& SpellsUsed, Reward* Out);
	/** Opens a star chest: NotEnough (too few stars), Nothing (already open). */
	Error ClaimChest(int Act, int Chest, Reward* Out);
	/** The pending battle's mission as Act * 100 + Mission (-1 when it is not a mission). */
	int PendingMission() const { return -Pending >= MissionBase ? -Pending - MissionBase : -1; }

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
