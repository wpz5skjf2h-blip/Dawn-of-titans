// Source/DoTKingdom/Public/DoTKingdom/KingdomContent.h
// Kingdom content (Content/Data/kingdom): building level tables, expansions, lands, seasons, army, titans, relics, start state.
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
	int64_t defenseSlots = 0; // garrison: squads that defend the city
	int64_t guardTier = 0; // garrison: tier of its own guards in the slots left empty
	int64_t defenseBonusPct = 0; // garrison: the city's defense counts this much more
	int64_t maxSpellLevel = 0; // arcane tower
	int64_t reinforcements = 0; // alliance camp: squads allies may send
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
	/** Defenders that differ on this land type (battle troop id -> the one that stands instead). */
	std::vector<std::pair<std::string, std::string>> swap;
};

/** A league at the end of a season: reached with this many VP, rewarded with gems and portal stones. */
/** The defenders of a world land of one level (kingdom.json "battles"); AI raiders of that level bring the same. */
struct BattleRowDef
{
	std::string ai; // AI profile
	int tier = 1;
	std::vector<std::string> squads; // battle troop ids
	int titanLevel = 0; // 0 = no titan
};

/** Raids on the kingdom by AI raiders (kingdom.json "raids"). */
struct RaidRules
{
	int castle = 3; // raiders come from this Castle level
	int64_t firstSec = 86400; // a new kingdom is left alone this long
	int64_t everyMinSec = 6 * 3600; // time between raids, random within
	int64_t everyMaxSec = 10 * 3600;
	int64_t awaySec = 48 * 3600; // raiders stop coming this long after the player's last visit
	int64_t cityPct = 40; // chance the city is the target when a land is open too
	int64_t cityShieldSec = 12 * 3600; // after a lost defense
	int64_t landShieldSec = 12 * 3600;
	int64_t stealPct = 20; // of the gold and food above the reserves, at three stars
	int64_t vpPct = 5; // of VP for a lost city defense, at three stars
	int64_t landStealPct = 100; // of what a land holds, at three stars
	int64_t defenseVpPct = 50; // VP for a held defense: share of a land battle's VP at the raiders' level
	std::vector<int64_t> citySquads; // raiders' squads on the city by their level
	std::vector<int64_t> titanLevels; // the raiders' titan by their level (raiders are players: they always bring one)
	std::vector<std::string> guards; // Garrison guard troops, cycled over the empty slots
	int defaultSlots = 3; // without a Garrison
	int defaultGuardTier = 1;
	std::string defenderAi = "veteran";
	/**
	 * How a raid goes, fitted to simulated battles: each side's power counts titans at titanPct and the raiders' at
	 * their AI's percentage; at the ratio x = raiders / defense the raiders win with x^k / (x^k + c^k). Ratios and c in
	 * hundredths, k in quarters. When they win they take three stars from threeStarRatio, else two (one below
	 * twoStarRatio, where a defending titan still stands).
	 */
	int64_t titanPct = 100;
	std::vector<std::pair<std::string, int64_t>> aiPct;
	int oddsKQuarters = 16;
	int64_t oddsC = 100;
	int64_t threeStarRatio = 90;
	int64_t twoStarRatio = 60;
	int log = 10; // reports kept
};

struct LeagueDef
{
	std::string id;
	std::string name;
	int64_t vp = 0;
	int64_t gems = 0;
	int64_t stones = 0;
};

/** An alliance league: how strong its computer alliances are, what it pays by place and what its allies send. */
struct AllianceLeagueDef
{
	std::string id;
	std::string name;
	/** Weekly VP of a computer member: alliances of this league start with a strength in this range. */
	int64_t memberVpMin = 0;
	int64_t memberVpMax = 0;
	/** First place's reward to every member; each lower place gets placeStepPct less (down to minPlacePct). */
	int64_t gems = 0;
	int64_t stones = 0;
	/** Rarity of the crystal every member gets (a relic, or a titan by chance). */
	int crystal = 1;
	/** The crystal's titan chance in percent at first and at last place (linear between). */
	int titanPctFirst = 0;
	int titanPctLast = 0;
	/** Level of the troops allies of this league send to the Alliance Camp (capped by the player's Armoury). */
	int troopLevel = 1;
};

/** A Titan Monument or an XP Arena (the original since 1.8): a battle of fixed difficulty on the world map that can be
 *  fought once a day (or again for gems). A monument may drop shards, an arena gives the titan extra XP and nothing else. */
struct SiteDef
{
	std::string id;
	std::string name;
	bool arena = false;
	int level = 1; // the land battle row its defenders come from
	int castle = 1; // Castle level that opens it
	std::string ai; // AI profile of the defenders; empty = the row's
	std::string map; // battlefield (read by the clients)
	int64_t shards = 0; // monuments: shards when the drop comes
	int64_t xpPct = 100; // arenas: titan XP in percent of a land battle's
	int64_t resetGems = 0; // gems to fight again before the cooldown ends
};

/** Titan Monuments and XP Arenas (kingdom.json "sites"). */
struct SiteRules
{
	int64_t cooldownSec = 24 * 3600;
	int64_t stones = 3;
	/** A monument's shard chance in percent by stars won (1, 2, 3). */
	std::array<int64_t, 3> shardPct{{40, 60, 80}};
	std::vector<SiteDef> list;
};

/** A campaign mission: the land battle row its enemy comes from (the clients build the army) and its first-clear reward. */
struct MissionDef
{
	std::string id;
	int level = 1;
	ResArray first{}; // resources for the first win
};

/** A star chest of a campaign act: opens at so many stars won in the act's missions. */
struct ChestDef
{
	int stars = 0;
	ResArray res{};
	int relic = 0; // a relic of this rarity (0 = none)
	int titan = 0; // a titan of this rarity while the Hall of Titans has room, else a relic of it (0 = none)
};

/** A campaign act: missions played in order; the next act opens when its last mission is won. */
struct ActDef
{
	std::string id;
	int castle = 1; // Castle level that opens it (besides the Adventurers' Guild)
	std::vector<MissionDef> missions;
	std::vector<ChestDef> chests;
};

/** Campaigns of the Adventurers' Guild (kingdom.json "campaigns"). */
struct CampaignRules
{
	int64_t stones = 2; // portal stones per mission battle
	int guild = -1; // building index that opens the campaigns (-1: none needed)
	std::vector<ActDef> acts;
};

/** What a daily quest counts. */
enum class QuestEvent : uint8_t
{
	Win, // battles won (lands, sites, missions)
	ThreeStars, // battles won with three stars
	Raid, // attacks on world lands (raids and captures)
	CollectGold, // gold collected from mines and lands
	CollectFood, // food collected from farms and lands
	Train, // squads queued in the Barracks
	Upgrade, // constructions and upgrades started (buildings, lands, troops, spells)
	Craft, // spells queued in the Spell Forge
	Stones, // portal stones spent on battles
	Site, // battles at Titan Monuments and XP Arenas
	Mission, // campaign missions fought
	Count
};
DOTKINGDOM_API bool ParseQuestEvent(const std::string& S, QuestEvent& Out);

/** A daily quest of the pool: its target by Castle level and its reward. */
struct QuestDef
{
	std::string id;
	QuestEvent event = QuestEvent::Win;
	int castle = 1; // Castle level from which it can come
	int building = -1; // a building it needs (-1: none)
	std::vector<std::pair<int, int64_t>> target; // (from Castle level, amount), ascending
	ResArray reward{};
};

/** Daily quests (kingdom.json "dailyQuests"). */
struct QuestRules
{
	int count = 5; // quests a day
	int rerolls = 2; // free rerolls a day
	ResArray chest{}; // the daily chest for claiming every quest
	std::vector<std::pair<int, int>> chestRelic; // (from Castle level, relic rarity), ascending
	std::vector<QuestDef> pool;
};

/** Alliances and their leagues (kingdom.json "alliances"). */
struct AllianceRules
{
	/** VP a player needs to join or create one (the original's 1 000) and the most a strong alliance asks. */
	int64_t joinVp = 1000;
	int64_t joinVpMax = 8000;
	int64_t createGems = 100;
	int minMembers = 8;
	int maxMembers = 30;
	int groupSize = 8;
	/** Alliances that go up from each league (and as many that come down from the one above). */
	int promote = 2;
	/** Time in the alliance before the season ends for its reward to be paid; else the personal league pays. */
	int64_t memberSec = 86400;
	/** An own alliance: one recruit joins every recruitSec, up to `recruits`. */
	int64_t recruitSec = 6 * 3600;
	int recruits = 24;
	int placeStepPct = 10;
	int minPlacePct = 30;
	/** Gems instead of a crystal's relic when the relic inventory is full, by rarity. */
	std::vector<int64_t> crystalGems;
	int64_t reinforceCooldownSec = 1200;
	int64_t reinforceArriveSec = 60;
	std::vector<AllianceLeagueDef> leagues;
	/** Computer alliances: name and tag. */
	std::vector<std::pair<std::string, std::string>> names;
	std::vector<std::string> memberNames;
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
	/** The Altar of Power (1.8) sells relic items instead of titans; its odds run R1..R5. */
	bool relics = false;
	std::array<int64_t, 5> weights{}; // R1..R4 for titans, R1..R5 for relics
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

/** What a relic effect raises. Titan effects reach the battle; building effects work in the kingdom. */
enum class RelicStat : uint8_t
{
	Atk, // titan damage
	Hp, // titan health
	Armor, // titan armor
	Crit, // titan critical hit chance, percentage points
	Pierce, // share of the target's armor the titan ignores
	TroopAtk, // damage of the titan's army
	TroopHp, // health of the titan's army
	Xp, // titan XP from battles
	Produce, // production of the building (Gold Mine, Farm)
	Store, // what the producer holds before it must be collected
	Train, // shorter training (Barracks: every troop; an envoy council: its own troops)
	Craft, // shorter spell crafting (Spell Forge)
	Research, // shorter research (Armoury, Arcane Tower)
	Rest, // shorter titan rest after battles (Hall of Titans)
	Count
};
constexpr int kRelicStatCount = static_cast<int>(RelicStat::Count);
DOTKINGDOM_API const char* ToString(RelicStat S);
DOTKINGDOM_API bool ParseRelicStat(const std::string& S, RelicStat& Out);

/** One relic effect: its stat and the value at level 1 by rarity, in tenths of a percent. */
struct RelicEffectDef
{
	std::string id;
	std::string name;
	RelicStat stat = RelicStat::Atk;
	std::array<int64_t, 5> base{};
	/** The most all of a titan's (or building's) relics may add to the stat, tenths of a percent; 0 = no cap. */
	int64_t cap = 0;
};

/** A titan skill (the original: a titan's skills come from its class's pool, one per slot, and stay there for good;
 *  fusion can raise them). Its tier, I to III, sets the bonus. */
struct SkillKind
{
	std::string id;
	std::string name;
	RelicStat stat = RelicStat::Atk;
	std::array<int64_t, 3> tiers{}; // tenths of a percent (crit: of a percentage point)
};

/** Titan skills (army.json "skills"). */
struct SkillRules
{
	int slots = 4; // skill slots before ascension; the first is open from the start, fusion opens the rest
	int openPct = 35; // chance a fused titan opens the next slot (it needs as many stars as the slots open)
	int upgradePct = 25; // otherwise the chance it raises a skill's tier, up to its own stars
	std::vector<SkillKind> kinds;
	/** Weight of each kind by titan class (army.json titans' "class"); a class not listed weighs all kinds alike. */
	std::vector<std::pair<std::string, std::vector<int64_t>>> classWeights;
};

/** A relic item kind (the original 1.8 has combat relics for titans and economy relics for buildings). */
struct RelicKind
{
	std::string id;
	std::string name;
	/** Economy relics attach to buildings and last a time; combat relics are worn by titans and last battles. */
	bool economy = false;
	/** Effect slots in order (RelicEffectDef indices); the first ones open first. */
	std::vector<int> effects;
	/** Economy relics: the buildings (defs) that take them. */
	std::vector<int> buildings;
	/** Battles a combat relic lasts, overriding the rarity table (XP relics: 2); 0 = the table. */
	int battles = 0;
	int64_t weight = 10; // how often drops and altars give this kind
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
	/** Upgrading a captured land: gold and seconds by the level reached (index L - 1, so the first entry is unused). */
	std::vector<int64_t> landUpgradeGold;
	std::vector<int64_t> landUpgradeSec;
	/** World land battles by land level (the clients build the battle from them). */
	std::vector<BattleRowDef> battles;
	RaidRules raids;
	/** Seasons: VP resets to zero at the end of each one and the reached league pays its reward. */
	int64_t seasonEpoch = 0;
	int64_t seasonSec = 7 * 86400;
	std::vector<LeagueDef> leagues;
	AllianceRules alliances;
	SiteRules sites;
	CampaignRules campaigns;
	QuestRules quests;
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
	int garrison = -1;
	int relicTemple = -1;
	int spellForge = -1;
	int arcaneTower = -1;
	int allianceCamp = -1;

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
	SkillRules skills;
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
	std::vector<RelicEffectDef> relicEffects;
	std::vector<RelicKind> relicKinds;
	/** Effect levels 1..relicLevels; every level above 1 adds relicStepPct of the level-1 value. */
	int relicLevels = 4;
	int64_t relicStepPct = 15;
	/** Effects open on a new relic and at most, by rarity (capped by the kind's slots). */
	std::array<int, 5> relicStartEffects{{1, 1, 1, 2, 3}};
	std::array<int, 5> relicMaxEffects{{1, 2, 3, 3, 3}};
	std::array<int64_t, 5> relicDurability{{20, 16, 12, 8, 6}}; // battles, combat relics
	std::array<int64_t, 5> relicHours{{24, 36, 48, 72, 96}}; // time, economy relics
	std::vector<int> relicSlotLevels{1, 20, 30, 40, 50};
	int relicRepairCastle = 8;
	/** Repair chance each sacrificed relic adds, percent: [target rarity - 1][sacrifice rarity - 1]. */
	std::array<std::array<int64_t, 5>, 5> relicRepairPct{};
	/** Chance that a successful repair also improves an effect (or opens a locked one), by the target's rarity. */
	std::array<int64_t, 5> relicEnhancePct{{80, 70, 60, 50, 40}};
	std::array<int64_t, 5> relicRepairFood{{1000, 2500, 6000, 15000, 30000}}; // by the repaired relic's rarity
	int maxRelics = 200;
	int64_t dailyRelicPct = 60;
	std::array<int64_t, 5> dailyRelicRarity{{60, 35, 5, 0, 0}};
	/** Won land battles drop up to this many relics (one per star), rarity odds by land level. */
	int relicDropMax = 3;
	std::vector<std::array<int64_t, 5>> relicDropRarity;
	int64_t landTitanPctPerLevel = 1;
	int64_t skipSecPerGem = 60;
	std::vector<int64_t> startArmy; // units by troop
	std::vector<StartTitan> startTitans;

	DOTKINGDOM_API int FindBuilding(const std::string& Id) const;
	DOTKINGDOM_API int FindTroop(const std::string& Id) const;
	DOTKINGDOM_API int FindTitan(const std::string& Id) const;
	DOTKINGDOM_API int FindSpell(const std::string& Id) const;
	DOTKINGDOM_API int FindRelicKind(const std::string& Id) const;
};

/** Loads kingdom/kingdom.json, kingdom/buildings.json and kingdom/army.json. */
DOTKINGDOM_API bool LoadKingdomContent(IContentSource& Source, KingdomContent& Out, std::string& Error);
} // namespace kingdom
} // namespace dot
