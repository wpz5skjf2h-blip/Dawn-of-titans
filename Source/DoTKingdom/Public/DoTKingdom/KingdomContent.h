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
	int64_t divine = 0; // divine gems (the original pays them in its top leagues)
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

/** Neighbouring lords on the world map (kingdom.json "rivals"): computer kingdoms in place of the original's players,
 *  each an island with its captured lands around it. Their lands can be raided (then shielded for a while) or taken
 *  (then they win another one back after a while); the lord's titan guards every one of them. */
struct RivalRules
{
	struct Lord
	{
		std::string name;
		std::string realm;
	};
	std::vector<Lord> lords; // the seed picks one per rival
	std::vector<int> castleOffsets; // one per rival: its Castle against the player's
	int castle = 3; // their lands can be attacked from this Castle
	int landsBase = 2; // lands a lord holds: base, one more every landsPerCastle Castle levels, at most landsMax
	int landsPerCastle = 6;
	int landsMax = 4;
	int64_t regrowSec = 8 * 3600; // a taken land comes back after this
	int64_t shieldSec = 8 * 3600; // a raided land is left alone this long
	int64_t lootPct = 100; // raid loot of a lord's land against a neutral one
	int64_t vpPct = 100;
	std::vector<int64_t> titanLevels; // the lord's titan on a land whose battle row has none, by land level
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

/** An entry of the missions' loot table: a repeat win or a sweep rolls one by weight. */
struct MissionLoot
{
	int weight = 0;
	ResArray res{}; // gold and food are per mission level
	int relic = 0; // a relic of this rarity (0 = none)
};

/** Campaigns of the Adventurers' Guild (kingdom.json "campaigns"). */
struct CampaignRules
{
	int64_t stones = 2; // portal stones per mission battle
	int guild = -1; // building index that opens the campaigns (-1: none needed)
	std::vector<ActDef> acts;
	// Sweeps (master prompt 4.19): a mission won on 3 stars is resolved at once for a War Scroll and its march food.
	int64_t sweepFood = 0; // food per mission level (0: no sweeps)
	int64_t scrollsStart = 0; // War Scrolls a new kingdom (or a save from before them) has
	int64_t scrollsMax = 0; // the most a kingdom holds
	std::vector<MissionLoot> loot; // rolled by repeat wins and sweeps (empty: titan XP only)
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
/** The event's name in content and saves ("win", "collectGold"...). */
DOTKINGDOM_API const char* QuestEventName(QuestEvent E);

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

/** Daily quests (kingdom.json "dailyQuests"); weekly quests use the same shape ("weeklyQuests", counts per week). */
struct QuestRules
{
	int count = 5; // quests a day
	int rerolls = 2; // free rerolls a day
	ResArray chest{}; // the daily chest for claiming every quest
	int64_t chestScrolls = 0; // War Scrolls in the chest
	std::vector<std::pair<int, int>> chestRelic; // (from Castle level, relic rarity), ascending
	std::vector<QuestDef> pool;
};

/** What a goal of the main line checks: a fact of the kingdom, or (Event) actions done while it is the current goal. */
enum class GoalCheck : uint8_t
{
	Castle, // the Castle's level
	Build, // buildings of one kind (built, any level)
	Level, // the highest level of one kind of building
	Army, // squads in the army
	TroopLevel, // the Armoury level of one troop
	Lands, // lands held
	LandLevel, // the highest level of a land held
	Guard, // lands with a titan on guard
	Garrison, // defenders in the Garrison (the titan and squads)
	Titans, // titans in the Hall
	TitanLevel, // the highest titan level
	Relics, // relics worn by titans
	Skills, // the most skill slots open on one titan
	Spells, // spells in stock
	SpellLevel, // the highest spell level
	Alliance, // 1 in an alliance
	Missions, // campaign missions won
	Event, // actions of one kind (QuestEvent) since the goal came up
	Power, // Kingdom Power (with the battle content)
	Count
};
DOTKINGDOM_API bool ParseGoalCheck(const std::string& S, GoalCheck& Out);

/** A goal of the main line (kingdom.json "goals", master prompt 13.5): the check, the count it needs and its reward. */
struct GoalDef
{
	std::string id;
	GoalCheck check = GoalCheck::Castle;
	QuestEvent event = QuestEvent::Win; // for GoalCheck::Event
	int building = -1; // for Build and Level
	int troop = -1; // for TroopLevel
	int64_t n = 1;
	ResArray reward{};
};

/** An achievement (kingdom.json "achievements", master prompt 13.5): actions over the whole game (Event) or a fact of
 *  the kingdom, in up to three tiers (bronze, silver, gold), each paying gems once. */
struct AchievementDef
{
	std::string id;
	GoalCheck check = GoalCheck::Event;
	QuestEvent event = QuestEvent::Win; // for GoalCheck::Event
	int building = -1; // for Build and Level
	int troop = -1; // for TroopLevel
	std::vector<std::pair<int64_t, int64_t>> tiers; // (count, gems), ascending
};

/** A day of a login calendar: resources (gold and food times the Castle's level), a titan, a choice of titans, a relic. */
struct CalendarDay
{
	ResArray res{};
	int titan = -1; // a titan of the roster
	std::vector<int> choice; // titans to choose one from
	int relic = 0; // a relic of this rarity
};

/** Login calendars (kingdom.json "calendar", master prompt 13.6): seven days for a new lord, and a 28-day cycle whose
 *  missed days can be caught up for gems a few times a cycle. */
struct CalendarRules
{
	std::vector<CalendarDay> newcomer;
	std::vector<CalendarDay> monthly;
	int64_t catchUpGems = 10;
	int catchUps = 3;
};

/** A reward of an event: resources and a relic of this rarity (0: none). */
struct EventReward
{
	ResArray res{};
	int relic = 0;
	int64_t divine = 0; // divine gems
};

/** One Titan Trial (master prompt 10.4): a fixed rule for the player's side and a fixed enemy army. */
struct TrialDef
{
	std::string id;
	std::vector<int> titans; // titans allowed (indices); empty: any titan
	bool noTitan = false; // the player goes without a titan
	int maxStars = 0; // highest titan rarity allowed (0: any)
	std::vector<int> troops; // the player's troops allowed (indices); empty: any
	int levelOffset = 0; // the battle's level is the Castle's plus this
	std::vector<int> enemy; // the enemy's troops (indices), repeated over its squads
	int enemyTitan = -1; // the enemy's titan (index; -1: the battle level's own, if any)
};

/** Titan Trials (kingdom.json "trials"): a weekly solo event with a score, milestones and a computer bracket. */
struct TrialRules
{
	int castle = 4; // Castle level they open at
	int days = 3; // open from Monday for this many days
	int attempts = 3; // battles a day
	int bracket = 100; // players in a bracket, the kingdom included
	std::vector<std::pair<int64_t, EventReward>> milestones; // (best score, reward), ascending
	std::vector<std::pair<int, EventReward>> ranks; // (worst place that gets it, reward), ascending
	std::vector<TrialDef> list; // one a week, in turn
};

/** What a Daily Trial pays (its amount a tier). */
enum class DailyPay : uint8_t { Gold, Food, TitanXp, Relic, Shards, Study };

/** One Daily Trial (master prompt 10.3): its weekday and its reward by tier. */
struct DailyTrialDef
{
	std::string id;
	int day = 0; // 0 Monday ... 5 Saturday (every trial is open on Sunday)
	DailyPay pay = DailyPay::Gold;
	std::vector<int64_t> amounts; // one a tier: resources, titan XP, relic rarity or research minutes
};

/** Daily Trials (kingdom.json "dailyTrials"): a trial a weekday, all on Sunday, tiers opened one after another. */
struct DailyTrialRules
{
	int castle = 3; // Castle level they open at
	int attempts = 3; // battles and sweeps a day, each trial
	int lineageBonusPct = 50; // more shards with a titan of the week's lineage
	std::vector<int> tierCastle; // Castle level of each tier
	std::vector<std::vector<int>> lineages; // titans (indices) of each lineage, one a week in turn
	std::vector<DailyTrialDef> list;
};

/** Warpath (kingdom.json "warpath", master prompt 13.4): the free track of a pass over a few weeks. Quests, battles and
 *  events fill it with XP; each tier reached pays its reward (gold and food grow with the Castle). */
struct WarpathRules
{
	int weeks = 4; // a pass lasts this many weeks from a Monday
	int64_t tierXp = 1000;
	int64_t dailyXp = 100; // a daily quest claimed
	int64_t weeklyXp = 500; // a weekly quest claimed
	int64_t battleXp = 10; // a battle fought
	int64_t battleCap = 300; // battle XP a day
	int64_t trialXp = 300; // a Titan Trial milestone claimed
	int64_t skipGems = 150; // gems for the next tier
	int64_t bonusXp = 2000; // past the last tier, a bonus chest per this much XP
	EventReward bonus;
	std::vector<EventReward> tiers; // at most 64
};

/** One Assault stage: its battle level against the Castle's at the run's start, garrison squads on top of that level's
 *  army, the defenders' fortification (health %), an enemy titan, and what winning it pays. */
struct AssaultStage
{
	int level = 0;
	int extra = 0;
	int fortPct = 0;
	bool titan = false;
	EventReward reward;
};

/** An Assault boon, picked after a won stage; boons stack over the run. */
struct AssaultBoon
{
	std::string id;
	int troopAtkPct = 0;
	int troopHpPct = 0;
	int revivePct = 0; // of every squad's losses, at once
	int titanPct = 0; // the titan's attack and health
	int titanCritPct = 0;
};

/** Assault (kingdom.json "assault", master prompt 10.5): a monthly gauntlet of sieges fought by one army whose losses
 *  carry over. */
struct AssaultRules
{
	int castle = 5; // Castle level it opens at
	int weeks = 4; // a cycle of this many weeks from a Monday
	int week = 1; // the week of the cycle it opens in (0-based)
	int startDay = 4; // the day of that week it opens (0 = Monday)
	int days = 5; // open for this many days
	int retries = 3; // lost stages a run can fight again
	int boonChoices = 3; // boons offered after a won stage
	std::vector<int> garrison; // troops (indices) of the extra squads, in turn
	std::vector<AssaultStage> stages;
	std::vector<AssaultBoon> boons;
};

/** A Raid Boss of the weekly list: a titan with an escort on its own battlefield. */
struct RaidBossDef
{
	std::string id;
	int titan = -1; // titan type (index)
	std::string escort; // the escort's battle troop id (any troop of the battle content, the Hollow's too)
};

/** A Raid Boss tier: the boss's level and stars, its escort and the chest when it falls. */
struct RaidBossTier
{
	int level = 10;
	int stars = 1;
	int escort = 1; // escort squads
	int troopTier = 1; // their battle tier
	EventReward chest;
};

/** Raid Boss (kingdom.json "raidBoss", master prompt 12.10): a weekly alliance event with a shared pool of health. */
struct RaidBossRules
{
	int castle = 5; // Castle level it opens at
	int days = 2; // open from Sunday for this many days
	int attempts = 3; // battles a day
	int battleSec = 120;
	int hpPct = 300; // the boss's health over a titan's of its level
	int64_t poolPct = 100; // the shared pool for each member, in % of the boss's health
	int64_t memberHit = 250; // a computer member's battle at its league's tier, in tenths of a percent of the boss's health
	int memberStepPct = 20; // less per tier above the league's (more below, up to twice)
	std::vector<int> leagueTier; // the tier a computer alliance picks, by league
	std::vector<int> phases; // pool left (‰), descending: each one reached adds a husk squad to the boss's side
	std::string husk; // the husks' battle troop id
	std::vector<RaidBossDef> bosses; // one a week, in turn
	std::vector<RaidBossTier> tiers;
	std::vector<std::pair<int64_t, EventReward>> milestones; // (damage in one battle, reward), ascending
};

/** An Alliance War chest of a league: for a win, a draw and a loss. */
struct WarChest
{
	EventReward win;
	EventReward draw;
	EventReward lose;
};

/** Alliance War (kingdom.json "allianceWar", master prompt 12.9): a weekly war of the player's alliance against a
 *  computer alliance of its league. A prep day, then battle days with a few attacks for each member of the roster on
 *  the other side's bases; a base gives up to 3 stars, and a new attack only adds stars over its best. */
struct AllianceWarRules
{
	int castle = 6; // Castle level the player attacks from
	int minMembers = 10; // members an alliance needs for a war, the player included
	int rosterStep = 5; // the roster is the members rounded down to this
	int rosterMax = 30;
	int prepDay = 3; // weekday of the prep day (Monday 0)
	int battleDays = 2;
	int attacks = 2; // each member's attacks in a war
	int idlePct = 15; // computer members who make no attack
	int onePct = 25; // computer members who make only one
	std::array<int, 6> starPct{{35, 20, 65, 15, 85, 10}}; // 3, 2 and 1 star or more: % at an equal tier and per tier over
	int tiers = 0; // base tiers (the web gives them their armies)
	std::vector<int> leagueTier; // tier of the bases in the middle of the roster, by league
	std::vector<WarChest> chests; // by league
	EventReward star; // for each star of the player's attacks (gold and food times the Castle)
};

/** Army loadouts (kingdom.json "loadouts"): how many a kingdom keeps (0: none). */
struct LoadoutRules
{
	int max = 0;
};

/** A Divine Shrine altar (kingdom.json "divine"): a titan of the player's choice among those of its stars, or a relic of
 *  the player's choice of kind at its rarity, for divine gems. */
struct DivineAltar
{
	std::string id;
	std::string name;
	int titanStars = 0; // 1-4: a titan of this rarity, or
	int relicRarity = 0; // 1-5: a relic of this rarity
	int64_t price = 0;
};

/** Divine gems (kingdom.json "divine", the original's Eternal Shrine): the shrine makes perDay a day and keeps up to
 *  cap until they are collected; its altars sell for them. */
struct DivineRules
{
	int building = -1; // the shrine (a building def; -1: no divine gems)
	int64_t perDay = 0;
	int64_t cap = 0;
	std::vector<DivineAltar> altars;
};

/** Alliance help (kingdom.json "allianceHelp", master prompt 12.4): allies take time off the player's timers one help
 *  at a time, and the player helps theirs for a small reward. */
struct AllianceHelpRules
{
	bool on = false; // the block is in the content
	int base = 5; // helps one timer can get
	int hallStep = 2; // one more for every this many Alliance Camp levels (the prompt's Alliance Hall)
	int max = 15;
	int cutPct = 1; // of the timer's whole time, for each help
	int64_t minCutSec = 60;
	std::array<int64_t, 2> firstSec{{30, 120}}; // from the ask to the first help
	std::array<int64_t, 2> gapSec{{20, 300}}; // between two helps
	std::array<int, 2> asksPerHour{{1, 3}}; // asks the members post each hour
	int openHours = 2; // how long a member's ask stays open
	int dailyCap = 50; // helps the player can give a day
	EventReward reward; // for each help given (gold and food times the Castle)
};

/** Kingdom Power (kingdom.json "power", master prompt 7.7): what a building, a titan and a research level add. */
struct PowerRules
{
	int64_t building = 10; // × L^1.5 for each building
	int64_t castleMul = 5; // the Castle counts this many times
	int titans = 5; // the strongest titans counted
	int64_t research = 25; // per level researched
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
	RivalRules rivals;
	CampaignRules campaigns;
	QuestRules quests;
	QuestRules weekly; // weekly quests (kingdom.json "weeklyQuests"), the same shape; a week starts on Monday 00:00 UTC
	std::vector<GoalDef> goals; // the main line, in order
	std::vector<AchievementDef> achievements;
	CalendarRules calendar;
	TrialRules trials;
	DailyTrialRules dailyTrials;
	WarpathRules warpath;
	AssaultRules assault;
	RaidBossRules raidBoss;
	AllianceWarRules allianceWar;
	AllianceHelpRules allianceHelp;
	LoadoutRules loadouts;
	DivineRules divine;
	PowerRules power;
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
