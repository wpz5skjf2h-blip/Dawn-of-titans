// Source/DoTKingdom/Public/DoTKingdom/KingdomContent.h
// Kingdom content (Content/Data/kingdom): building level tables, expansions, lands, seasons, army, titans, start state.
// Numbers follow the original game where known (docs/Original_Reference.md).
#pragma once

#include "DoTSim/Content.h"

#include <array>
#include <string>
#include <utility>
#include <vector>

// UnrealBuildTool defines DOTKINGDOM_API for the DoTKingdom module; standalone builds leave it empty.
#ifndef DOTKINGDOM_API
#define DOTKINGDOM_API
#endif

namespace dot
{
namespace kingdom
{
/** Shards (from ruins) pay for titan ascension; victory points (VP) rank the player in a season. */
enum class Res : uint8_t { Gold, Food, Gems, Stones, Shards, Vp, Count };
constexpr int kResCount = static_cast<int>(Res::Count);
using ResArray = std::array<int64_t, kResCount>;
inline int ResIndex(Res R) { return static_cast<int>(R); }
DOTKINGDOM_API const char* ToString(Res R);
DOTKINGDOM_API bool ParseRes(const std::string& S, Res& Out);

/** What a building does for the economy. Everything else is described by its stats. */
enum class Role : uint8_t { Castle, Producer, Storage, Reserve, Portal, Builders, Other };

struct Requirement
{
	int building = -1;
	int level = 1;
	int count = 1;
};

struct LevelDef
{
	int level = 1;
	ResArray cost{};
	int64_t timeSec = 0;
	/** Gems to finish the whole build at once (the wiki's "Gems to Skip"). */
	int64_t skipGems = 0;
	int castle = 1;
	/** City expansion needed (Portal 3+ in the original). */
	int expansion = 0;
	std::vector<Requirement> needs;
	int64_t produce = 0; // per hour (producers)
	int64_t store = 0; // what a producer holds before it must be collected
	int64_t capacity = 0; // storage buildings
	int64_t protect = 0; // reserves
	int64_t stones = 0; // portal capacity
	int64_t builders = 0; // builder's yard
	int64_t landCapacity = 0; // castle
	int64_t queue = 0; // barracks: units in the training queue
	int64_t troops = 0; // army camp: units it holds
	int64_t slots = 0; // army camp: squads in one battle
	int64_t maxTroopLevel = 0; // armoury
	int64_t survival = 0; // medical school: share of a squad (%) that must survive for it to come back
	int64_t titans = 0; // hall of titans: roster size
	int64_t spellSlots = 0; // arcane tower: spell researches at once
	int64_t maxSpellLevel = 0; // arcane tower
	bool estimated = false;
};

struct BuildingDef
{
	std::string id;
	std::string name;
	std::string category;
	Role role = Role::Other;
	Res resource = Res::Gold;
	int width = 2;
	int height = 2;
	int unlockCastle = 1;
	/** (castle level, how many may exist) steps. */
	std::vector<std::pair<int, int>> counts;
	/** Instant purchases paid in gems (Builder's Yard): no builder, no timer. */
	bool instant = false;
	std::vector<LevelDef> levels;

	int MaxLevel() const { return static_cast<int>(levels.size()); }
	DOTKINGDOM_API int MaxCount(int CastleLevel) const;
};

struct ExpansionDef
{
	int64_t costGold = 0;
	int castle = 1;
	int x0 = 0;
	int y0 = 0;
	int x1 = 0;
	int y1 = 0;
};

struct LandTypeDef
{
	std::string id;
	std::string name;
	Res resource = Res::Gold;
	std::vector<int64_t> produce; // per period, by land level
	int64_t periodHours = 1; // ruins make one shard a day
	std::vector<int64_t> fillHours; // by land level; empty = the common table
	std::vector<int64_t> raidLoot; // of the land's resource, by land level; empty = the common table
	int castle = 1; // Castle level from which the type appears on the world map
	int64_t weight = 1; // how often it appears
};

/** A league at the end of a season: reached with this many VP, rewarded with gems and portal stones. */
struct LeagueDef
{
	std::string id;
	std::string name;
	int64_t vp = 0;
	int64_t gems = 0;
	int64_t stones = 0;
};

/** One level of a troop type (the original's Militia, Archers, Spearmen). Costs are Food. */
struct TroopLevelDef
{
	int level = 1;
	int64_t cost = 0; // per unit trained at this level
	int64_t trainSec = 0;
	int64_t upgradeCost = 0; // to reach the next level (Armoury research)
	int64_t upgradeSec = 0;
	int tier = 1; // battle simulation tier for this level
	bool estimated = false;
};

struct TroopType
{
	std::string id; // battle content troop id
	std::string name;
	int unlockCastle = 1;
	/** The envoy council (building) whose people send this troop, -1 for the Barracks' own; and the council level it needs.
	 *  Every troop trains in the Barracks queue, as in the original; a council troop also needs its council, whose level
	 *  caps the troop's level (stat maxTroopLevel on the council's levels). */
	int council = -1;
	int councilLevel = 1;
	std::vector<TroopLevelDef> levels;

	int MaxLevel() const { return static_cast<int>(levels.size()); }
};

/** A titan the kingdom can own; the battle stats live in the battle content (titans.json). */
struct TitanType
{
	std::string id;
	std::string name;
	std::string cls; // the original's class name
	int stars = 1; // rarity R1..R4
	int MaxLevel() const { return 10 + 10 * stars; }
};

/** A Relic Temple altar: a summon with odds by rarity, for gems or free on a cooldown. */
struct AltarDef
{
	std::string id;
	std::string name;
	int64_t gems = 0;
	int64_t cooldownSec = 0;
	std::array<int64_t, 4> weights{}; // R1..R4
};

/** A spell the Spell Forge crafts into stock (battle content id). */
struct SpellType
{
	std::string id;
	std::string name;
	int forge = 1; // Spell Forge level that opens it
	int64_t craftGold = 0;
	int64_t craftSec = 60;
};

struct RelicKind
{
	std::string id;
	std::string name;
	int stat = 0; // 0 damage, 1 health, 2 armor
};

struct StartTitan
{
	int def = -1;
	int level = 1;
};

struct StartBuilding
{
	int building = -1;
	int level = 1;
	int x = 0;
	int y = 0;
};

struct KingdomContent
{
	std::string hash;
	int gridW = 40;
	int gridH = 40;
	std::vector<BuildingDef> buildings;
	std::vector<ExpansionDef> expansions;
	int64_t stoneRegenSec = 420;
	int64_t raidStones = 3;
	int64_t captureStones = 6;
	int64_t gemsPerStone = 4;
	int maxLands = 30;
	std::vector<int64_t> extraSlotGems;
	std::vector<int64_t> landFillHours;
	std::vector<LandTypeDef> landTypes;
	std::vector<int64_t> raidLoot;
	/** VP for any won land battle, by land level (scaled by stars like loot). */
	std::vector<int64_t> victoryVp;
	int worldSize = 6;
	/** Seasons: VP resets to zero at the end of each one and the reached league pays its reward. */
	int64_t seasonEpoch = 0;
	int64_t seasonSec = 7 * 86400;
	std::vector<LeagueDef> leagues;
	/** (amount, gems) points; gems for missing resources are interpolated linearly. */
	std::vector<std::pair<int64_t, int64_t>> gemsForResources;
	ResArray startResources{};
	std::vector<StartBuilding> startBuildings;
	int castle = -1;
	int builderYard = -1;
	int portal = -1;
	int barracks = -1;
	int armyCamp = -1;
	int armoury = -1;
	int medicalSchool = -1;
	int hallOfTitans = -1;
	int relicTemple = -1;
	int spellForge = -1;
	int arcaneTower = -1;

	// Army and titans (kingdom/army.json).
	std::vector<TroopType> troops;
	std::vector<TitanType> titans;
	std::vector<AltarDef> altars;
	std::vector<int64_t> titanXpNext; // XP from level L to L + 1, index L - 1
	std::vector<int64_t> titanBattleXp; // by land level
	std::array<int64_t, 4> titanStarsPct{{30, 100, 120, 150}}; // share of battle XP by stars won
	int titanRecoveryFromLevel = 10;
	int64_t titanRecoverySecPerLevel = 120;
	/** Ascension of a max-level titan: shards and food by rarity, extra levels and relic slots. */
	std::array<int64_t, 4> ascendShards{{5, 25, 50, 100}};
	std::array<int64_t, 4> ascendFood{{180000, 820000, 2300000, 4800000}};
	int ascendLevels = 10;
	int ascendRelicSlots = 1;
	int fusionCastle = 7;
	int64_t fusionFoodPerStar = 2000; // wiki: fusion costs food; the amount is our estimate
	int64_t fusionXpPerStarSq = 150;
	int64_t fusionKeepXpPct = 50;
	std::vector<SpellType> spells;
	int64_t spellGoldBase = 20000;
	int64_t spellGoldGrowthPct = 155; // research price growth per level, percent
	int64_t spellSecBase = 7200;
	int64_t spellSecGrowthPct = 145;
	int64_t spellCraftPctPerLevel = 15;
	int battleSpellKinds = 3;
	std::vector<RelicKind> relicKinds;
	std::array<int64_t, 5> relicBonusPct{{3, 5, 8, 12, 16}};
	std::array<int64_t, 5> relicDurability{{10, 14, 18, 24, 30}};
	std::vector<int> relicSlotLevels{1, 20, 30, 40, 50};
	int relicRepairCastle = 8;
	std::array<int64_t, 5> relicRepairWeight{{10, 20, 35, 50, 70}};
	std::array<int64_t, 5> relicRepairFood{{1000, 2500, 6000, 15000, 30000}}; // by the repaired relic's rarity
	int maxRelics = 100;
	int64_t dailyRelicPct = 60;
	std::array<int64_t, 5> dailyRelicRarity{{60, 35, 5, 0, 0}};
	int64_t landTitanPctPerLevel = 1;
	int64_t skipSecPerGem = 60;
	std::vector<int64_t> startArmy; // units by troop
	std::vector<StartTitan> startTitans;

	DOTKINGDOM_API int FindBuilding(const std::string& Id) const;
	DOTKINGDOM_API int FindTroop(const std::string& Id) const;
	DOTKINGDOM_API int FindTitan(const std::string& Id) const;
	DOTKINGDOM_API int FindSpell(const std::string& Id) const;
};

/** Loads kingdom/kingdom.json, kingdom/buildings.json and kingdom/army.json. */
DOTKINGDOM_API bool LoadKingdomContent(IContentSource& Source, KingdomContent& Out, std::string& Error);
} // namespace kingdom
} // namespace dot
