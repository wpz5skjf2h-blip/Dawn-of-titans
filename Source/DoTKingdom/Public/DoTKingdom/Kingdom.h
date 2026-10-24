// Source/DoTKingdom/Public/DoTKingdom/Kingdom.h
// The player's kingdom: city buildings, builders, timers, resources, expansions, lands, portal stones,
// the army (training, troop levels, casualties), spells (Spell Forge stock, Arcane Tower levels) and the titan
// roster (XP, relic items, fusion, titans guarding lands).
// Deterministic and engine-free like the battle simulation: integer math only, time is passed in
// explicitly (seconds), so a server can replay the same actions and reach the same state.
#pragma once

#include "DoTKingdom/KingdomContent.h"

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
	int64_t carry = 0;
	int64_t lastAt = 0;
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
	std::vector<int32_t> relics; // equipped relic ids
};

/** A relic item: raises one of a titan's stats (two from rarity 3), wears out with every battle. */
struct RelicItem
{
	int32_t id = 0;
	int kind = 0;
	int rarity = 1; // R1..R5
	int durability = 0; // battles left; 0 = broken until repaired
};

/** One Arcane Tower research slot. */
struct SpellResearchSlot
{
	int spell = -1;
	int64_t from = 0;
	int64_t until = 0;
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
	int64_t LandRate(const Land& L) const;
	Check CheckBuild(int Def) const;
	Check CheckUpgrade(int32_t Id) const;
	Check CheckExpansion() const;
	int64_t SkipCost(int32_t Id) const;
	int64_t GemsForResources(const ResArray& Missing) const;
	bool CanPlace(int Def, int X, int Y, int32_t IgnoreId) const;
	uint64_t Hash() const;

	// --- Army (Barracks, Army Camp, Armoury, Medical School) ---
	int TroopLevel(int Troop) const;
	int64_t ArmyCount(int Troop) const;
	int64_t ArmyTotal() const;
	int64_t ArmyCapacity() const;
	int BattleSlots() const;
	int QueueCapacity() const;
	int MaxTroopLevel() const;
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
	int64_t RelicStatPct(const RelicItem& R, int Stat) const;
	/** Bonus percent from a titan's working relics: Stat 0 damage, 1 health, 2 armor. */
	int64_t TitanBonusPct(int32_t TitanId, int Stat) const;
	int RepairChancePct(int32_t Target, const std::vector<int32_t>& Sacrifices) const;

	// --- Titans (Hall of Titans, Relic Temple, fusion) ---
	const std::vector<TitanState>& Titans() const { return TitanList; }
	const TitanState* FindTitan(int32_t Id) const;
	int TitanCapacity() const;
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
	/** Spends portal stones for a raid (3) or capture (6) of a world land; the battle runs elsewhere. */
	Error StartAttack(int32_t WorldId, bool Capture);
	/** The same with the army that goes: squads (troop indices, one per squad) and a titan (0 for none). */
	Error StartAttack(int32_t WorldId, bool Capture, const std::vector<int>& Squads, int32_t TitanId);
	/** The same with spells from the stock (distinct spell indices; every charge in stock goes along). */
	Error StartAttack(int32_t WorldId, bool Capture, const std::vector<int>& Squads, int32_t TitanId, const std::vector<int>& Spells);
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
	Error UnequipRelic(int32_t RelicId);
	Error DiscardRelic(int32_t RelicId);
	/** Sacrifices relics for a chance to restore the target's durability (from Castle 8). */
	Error RepairRelic(int32_t Target, const std::vector<int32_t>& Sacrifices, bool* OutRepaired = nullptr);
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

private:
	const LevelDef* CurrentLevel(const Building& B) const;
	const LevelDef* NextLevel(const Building& B) const;
	Check CheckLevel(int Def, const LevelDef& L, bool bNew, bool bNeedsBuilder) const;
	bool Pay(const ResArray& Cost, bool UseGems);
	void Settle(Building& B, int64_t At) const;
	void SettleLand(Land& L, int64_t At) const;
	void Complete(Building& B, int64_t At);
	void RegenStones(int64_t Until);
	void AddResource(Res R, int64_t Amount);
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
};
} // namespace kingdom
} // namespace dot
