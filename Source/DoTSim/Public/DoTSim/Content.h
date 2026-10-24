// Source/DoTSim/Public/DoTSim/Content.h
// Immutable content definitions used by the simulation, loaded from JSON
// (Content/Data). All tuning numbers live in data, never in simulation code.
#pragma once

#include "DoTSim/Fix64.h"
#include "DoTSim/Json.h"

#include <array>
#include <map>
#include <string>
#include <vector>

namespace dot
{
enum class DefenderClass : uint8_t { Light, Heavy, Pike, Ranged, Cavalry, Siege, Support, Titan, Structure, Count };
constexpr int kDefenderClassCount = static_cast<int>(DefenderClass::Count);

enum class MoveClass : uint8_t { Infantry, Cavalry, Siege, Titan, Count };
constexpr int kMoveClassCount = static_cast<int>(MoveClass::Count);

enum class Formation : uint8_t { Line, Column, Wedge, Square, Loose, Count };
enum class Stance : uint8_t { Aggressive, Hold, Skirmish, Count };
enum class TitanClass : uint8_t { Juggernaut, Ravager, Warden, Mystic, Hunter, Count };
enum class SizeClass : uint8_t { Medium, Large, Colossal, Count };
enum class Rarity : uint8_t { Common, Rare, Epic, Legendary, Mythic, Count };

DOTSIM_API const char* ToString(Formation F);
DOTSIM_API const char* ToString(Stance S);
DOTSIM_API const char* ToString(DefenderClass C);
DOTSIM_API bool ParseFormation(const std::string& S, Formation& Out);
DOTSIM_API bool ParseStance(const std::string& S, Stance& Out);

struct TroopTierDef
{
	std::string name;
	Fix64 hpMult = Fix64::One();
	Fix64 atkMult = Fix64::One();
	Fix64 armorAdd;
};

struct TroopDef
{
	std::string id;
	std::string name;
	std::string shortName;
	DefenderClass cls = DefenderClass::Light;
	MoveClass move = MoveClass::Infantry;
	int squadSize = 20;
	Fix64 hp;
	Fix64 armor;
	Fix64 atk;
	int attackIntervalTicks = 30;
	Fix64 range;
	Fix64 minRange;
	Fix64 moveSpeed;
	Fix64 collisionRadius;
	Fix64 knockbackMult = Fix64::One();
	Fix64 critChance;
	Fix64 critDamage = Fix64::One();
	Fix64 spacing;
	// Ranged
	bool ranged = false;
	Fix64 projectileSpeed;
	Fix64 accuracyNear;
	Fix64 accuracyFar;
	Fix64 accuracyNearRange;
	Fix64 accuracyVsTitan;
	// Specials (4.9)
	bool shieldBlock = false;
	Fix64 shieldBlockReduction;
	bool brace = false;
	Fix64 braceDamageMult;
	Fix64 braceTitanChargeMult = Fix64::One();
	bool strengthInNumbers = false;
	Fix64 strengthInNumbersBonus;
	Fix64 chargeMult; // 0 = no charge bonus
	int chargeKnockdownTicks = 0;
	bool trample = false;
	Fix64 trampleDamage;
	// Grenadiers: shots burst in a splash and leave a burning patch.
	bool firebomb = false;
	Fix64 splashRadius;
	Fix64 splashEdgeMult = fx::Pct(50);
	Fix64 fireRadius;
	Fix64 fireDps;
	int fireTicks = 0;
	Fix64 fireFriendlyMult = fx::Pct(50);
	// Chainbreakers: harpoon chains slow an enemy titan and bind it at bindAt chains.
	bool chains = false;
	Fix64 chainRange;
	int chainIntervalTicks = 75;
	int chainMinAlive = 3;
	Fix64 chainMoveSlow;
	Fix64 chainAttackSlow;
	int chainTicks = 300;
	int chainBindAt = 5;
	int chainBindTicks = 75;
	int chainImmuneTicks = 360;
	// War Priests: never attack; heal, steady and armor nearby allies.
	bool support = false;
	Fix64 auraRadius;
	Fix64 auraHealPctPerSec;
	Fix64 auraMoralePerSec;
	Fix64 auraArmor;
	Fix64 keepAway;
	// Envoy council troops carry the original's elements as on-hit effects.
	// Pikemen (ice): a melee hit chills the target squad (or titan, at frostTitanMult) for frostTicks.
	bool frost = false;
	Fix64 frostMoveSlow;
	Fix64 frostAttackSlow;
	int frostTicks = 0;
	Fix64 frostTitanMult = fx::Pct(25);
	// Goliaths (earth): the club also hits soldiers around the target and knocks them down.
	bool cleave = false;
	Fix64 cleaveRadius;
	Fix64 cleaveMult = fx::Pct(60);
	int cleaveTargets = 4;
	int cleaveKnockdownTicks = 0;
	// Panthers (poison): a melee hit poisons the soldier for poisonTicks.
	bool poison = false;
	Fix64 poisonDps;
	int poisonTicks = 0;
	/** How many enemy soldiers may fight one soldier of this troop at once (0 = rules.maxAttackersPerSoldier). */
	int maxAttackers = 0;
	uint32_t formationMask = 0;
	Stance defaultStance = Stance::Aggressive;
	std::vector<TroopTierDef> tiers;
	int counterRow = -1;

	bool AllowsFormation(Formation F) const { return (formationMask & (1u << static_cast<unsigned>(F))) != 0; }
	bool IsCavalry() const { return cls == DefenderClass::Cavalry; }
};

struct CounterMatrix
{
	std::vector<std::string> rowIds;
	std::vector<std::array<Fix64, kDefenderClassCount>> rows;

	DOTSIM_API int FindRow(const std::string& Id) const;
	Fix64 Get(int Row, DefenderClass Defender) const
	{
		if (Row < 0 || Row >= static_cast<int>(rows.size()))
		{
			return Fix64::One();
		}
		return rows[static_cast<size_t>(Row)][static_cast<size_t>(Defender)];
	}
};

struct TitanClassDef
{
	TitanClass cls = TitanClass::Juggernaut;
	Fix64 hp;
	Fix64 atk;
	Fix64 armor;
	int attackIntervalTicks = 60;
	Fix64 sweepRadius;
	Fix64 sweepHalfArcCos; // cos(arc / 2)
	Fix64 poise;
	Fix64 moveSpeed;
	Fix64 turnRateDeg;
	Fix64 critChance;
	Fix64 critDamage = Fix64::One();
	bool rangedAttack = false;
	Fix64 attackRange;
	Fix64 splashRadius;
	Fix64 aoeFactor = Fix64::One();
	Fix64 vsTitanMult = Fix64::One();
	int pierce = 0;
};

struct SizeDef
{
	SizeClass size = SizeClass::Large;
	Fix64 height;
	Fix64 capsuleRadius;
	Fix64 footRadius;
	Fix64 stepLength;
	bool canWadeDeepWater = true;
};

struct TitanDef
{
	std::string id;
	std::string name;
	std::string title;
	std::string lineage;
	TitanClass cls = TitanClass::Juggernaut;
	Rarity rarity = Rarity::Common;
	SizeClass size = SizeClass::Large;
	Fix64 varianceHp = Fix64::One();
	Fix64 varianceAtk = Fix64::One();
	Fix64 variancePoise = Fix64::One();
	Fix64 moveSpeedOverride; // 0 = class value
	int abilities[2] = {-1, -1};
	// Passives
	Fix64 rangedArmorBonus;
	int chainLightningEvery = 0;
	int chainLightningTargets = 0;
	Fix64 chainLightningCoef;
};

enum class EffectType : uint8_t { Damage, Heal, Knockback, Stun, StatMod, Burn, Morale, RaiseDead };
enum class StatType : uint8_t { MoveSpeedPct, AttackSpeedPct, DamageTakenPct, ArmorAdd, Count };
constexpr int kStatTypeCount = static_cast<int>(StatType::Count);

namespace affects
{
constexpr uint8_t EnemyTroops = 1;
constexpr uint8_t EnemyTitans = 2;
constexpr uint8_t AllyTroops = 4;
constexpr uint8_t AllyTitans = 8;
constexpr uint8_t Self = 16;
} // namespace affects

struct EffectDef
{
	EffectType type = EffectType::Damage;
	uint8_t affectsMask = affects::EnemyTroops;
	Fix64 coef; // x caster ATK
	Fix64 flat;
	Fix64 pctMaxHp; // Heal / Damage as % of target max HP
	int durationTicks = 0;
	StatType stat = StatType::MoveSpeedPct;
	Fix64 magnitude;
	Fix64 horizontal; // knockback
	Fix64 vertical;
	int hits = 1;
	Fix64 poiseDamage;
	// RaiseDead: fallen soldiers in the area rise as a squad of this troop for durationTicks.
	int summonTroop = -1;
	int summonMax = 0;
	int summonMaxPerLevel = 0;
	int corpseAgeTicks = 0;
};

enum class TargetShape : uint8_t { Self, Circle, Cone, Line };
enum class Delivery : uint8_t { Instant, Projectile, Leap, Zone, Delayed };
enum class AiHeuristic : uint8_t { MaxUnitsInArea, MaxUnitsInLine, MaxUnitsInCone, SelfDefense, AlliesInCombat, AlliesDamaged, Corpses };

struct AbilityDef
{
	std::string id;
	std::string name;
	bool isSpell = false;
	int rageCost = 0;
	int valorCost = 0;
	int cooldownTicks = 0;
	int castTicks = 0;
	TargetShape shape = TargetShape::Circle;
	Fix64 range;
	Fix64 radius;
	Fix64 width;
	Fix64 coneHalfAngleCos;
	Delivery delivery = Delivery::Instant;
	Fix64 projectileSpeed;
	bool returns = false;
	int delayTicks = 0;
	int zoneDurationTicks = 0;
	int zonePulseTicks = 30;
	int leapTicks = 24;
	std::vector<EffectDef> effects;
	Fix64 perLevelPct;
	AiHeuristic ai = AiHeuristic::MaxUnitsInArea;
	int aiMinTargets = 6;
	Fix64 aiHpBelow;
	bool aiPreferTitan = false;
};

enum class FeatureType : uint8_t { Hill, Forest, ShallowWater, DeepWater, Rock, Road, Ruins };

struct MapFeature
{
	FeatureType type = FeatureType::Hill;
	bool circle = true;
	FixVec2 center;
	Fix64 radius;
	FixVec2 min;
	FixVec2 max;
	Fix64 height;
};

struct MapDef
{
	std::string id;
	std::string name;
	Fix64 width;
	Fix64 depth;
	Fix64 cellSize;
	std::vector<MapFeature> features;
	FixVec2 deployMin[2];
	FixVec2 deployMax[2];
};

struct AiProfileDef
{
	std::string id;
	int reactionTicks = 36;
	int decisionTicks = 30;
	Fix64 bestCounterChance;
	int flanking = 0;
	int focusFire = 0;
	int spellSkill = 0;
	bool formationMicro = false;
	Fix64 clashPerfect;
	Fix64 clashGood;
};

struct ProgressionDef
{
	Fix64 rarityMult[static_cast<int>(Rarity::Count)];
	Fix64 starMult[7];
	Fix64 levelGrowth;
};

/** Global battle rules (Section 4). Defaults equal the master-prompt values; rules.json overrides them. */
struct RulesDef
{
	int countdownTicks = 45;
	int deployTicks = 900;
	// Valor
	Fix64 valorStart = fx::Int(3);
	int valorIntervalTicks = 180;
	Fix64 valorMax = fx::Int(10);
	Fix64 valorOnClashStagger = fx::Int(2);
	// Morale
	Fix64 moraleLossPerPctLost = fx::Milli(1200);
	Fix64 moraleChargedFlank = fx::Int(15);
	Fix64 moraleTitanHit = fx::Int(10);
	int moraleTitanHitCooldownTicks = 90;
	Fix64 moraleFriendlyRout = fx::Int(10);
	Fix64 moraleFriendlyRoutRadius = fx::Int(20);
	Fix64 moraleEnemyTitanDown = fx::Int(25);
	Fix64 moraleOwnTitanDown = fx::Int(20);
	Fix64 moraleRegenPerSec = fx::Int(4);
	int moraleRegenDelayTicks = 90;
	Fix64 moraleRoutRegenPerSec = fx::Int(6);
	Fix64 moraleRally = fx::Int(30);
	Fix64 moraleShaken = fx::Int(50);
	Fix64 moraleWavering = fx::Int(25);
	Fix64 shakenAtkMult = fx::Pct(90);
	Fix64 waveringAtkMult = fx::Pct(75);
	Fix64 waveringSpeedMult = fx::Pct(90);
	// Knockback
	Fix64 gravity = fx::Milli(9810);
	int knockdownTicks = 36;
	int airborneDamageTicks = 24;
	Fix64 landingDamagePct = fx::Pct(10);
	Fix64 sweepKnockH = fx::Int(9);
	Fix64 sweepKnockV = fx::Int(4);
	Fix64 smashKnockH = fx::Int(3);
	Fix64 smashKnockV = fx::Int(10);
	Fix64 stompKnockH = fx::Int(7);
	Fix64 stompKnockV = fx::Int(6);
	// Titan built-in actions
	int stompTriggerCount = 10;
	Fix64 stompTriggerRadius = fx::Int(7);
	Fix64 stompRadius = fx::Int(8);
	Fix64 stompCoef = fx::Pct(60);
	int stompCooldownTicks = 300;
	int stompWindupTicks = 12;
	int stompChainTrigger = 3;
	Fix64 grabRadius = fx::Int(6);
	Fix64 throwRange = fx::Int(35);
	Fix64 throwSplash = fx::Int(4);
	Fix64 throwCoef = fx::Pct(80);
	int grabCooldownTicks = 450;
	int throwFlightTicks = 30;
	Fix64 titanChargeMinDistance = fx::Int(25);
	Fix64 titanChargeSpeedBonus = fx::Pct(35);
	int titanChargeTicks = 150;
	int titanChargeCooldownTicks = 600;
	Fix64 titanTrampleCoef = fx::Pct(40);
	Fix64 footstepCoef = fx::Pct(15);
	int footstepKnockdownTicks = 30;
	// Rage / poise
	Fix64 rageStart = fx::Int(30);
	Fix64 rageRegenPerSec = fx::Int(3);
	Fix64 ragePerKill = fx::Int(1);
	Fix64 rageMaxKillPerSec = fx::Int(10);
	Fix64 ragePerPctHpLost = fx::Int(1);
	Fix64 poiseFactorTroops = fx::Pct(2);
	Fix64 poiseFactorTitans = fx::Pct(10);
	int staggerTicks = 75;
	Fix64 staggerDamageTakenPct = fx::Pct(25);
	Fix64 poiseRegenPctPerSec = fx::Pct(10);
	int poiseRegenDelayTicks = 60;
	// Combo / fall
	int comboResetTicks = 90;
	int titanWindupTicks = 12;
	Fix64 smashRadius = fx::Int(4);
	Fix64 smashCoef = fx::Milli(1500);
	/** All titan-to-titan damage (basic attacks, abilities, Clash) is multiplied by this (TTK guardrail 4.20). */
	Fix64 titanVsTitanMult = fx::Int(5);
	Fix64 fallWidth = fx::Int(5);
	Fix64 fallFlatDamage = fx::Int(400);
	Fix64 fallPctMaxHp = fx::Pct(20);
	int fallObstacleTicks = 600;
	// Clash (4.11.9)
	Fix64 clashTriggerExtra = fx::Int(3);
	int clashLockTicks = 18;
	int clashWindupTicks = 30;
	int clashStrikeTicks = 18;
	int clashPerfectMs = 90;
	int clashGoodMs = 220;
	Fix64 clashWinCoef = fx::Int(2);
	Fix64 clashWinPerfectCoef = fx::Milli(2500);
	Fix64 clashLoseCoef = fx::Pct(50);
	Fix64 clashBothMissCoef = fx::Pct(75);
	Fix64 clashPushback = fx::Int(4);
	Fix64 clashPoisePct = fx::Pct(35);
	int clashStaggerTicks = 90;
	Fix64 clashStaggerDamageTakenPct = fx::Pct(50);
	int clashPairCooldownTicks = 450;
	Fix64 clashShockwaveRadius = fx::Int(10);
	Fix64 clashShockwavePct = fx::Pct(5);
	// Troop combat
	Fix64 varianceMin = fx::Pct(95);
	Fix64 varianceMax = fx::Pct(105);
	Fix64 flankMult = fx::Milli(1250);
	Fix64 rearMult = fx::Milli(1500);
	Fix64 elevationThreshold = fx::Int(2);
	Fix64 elevationRangedMult = fx::Milli(1100);
	Fix64 elevationRangeBonus = fx::Pct(15);
	Fix64 uphillMeleeMult = fx::Pct(90);
	Fix64 engageDistance = fx::Int(3);
	int maxAttackersPerSoldier = 3;
	int pairingIntervalTicks = 6;
	Fix64 looseAreaDamageMult = fx::Pct(50);
	Fix64 looseMeleeDealtMult = fx::Pct(85);
	Fix64 looseAccuracyMult = fx::Pct(80);
	Fix64 columnSpeedMult = fx::Milli(1100);
	Fix64 columnFlankTakenMult = fx::Milli(1250);
	Fix64 squareSpeedMult = fx::Pct(60);
	Fix64 wedgeChargeMult = fx::Milli(1200);
	Fix64 wedgeArmorAdd = -fx::Pct(10);
	int formationChangeTicks = 45;
	Fix64 formationChangeDamageMult = fx::Pct(70);
	int chargeCommandTicks = 120;
	Fix64 chargeCommandSpeedBonus = fx::Pct(40);
	int chargeCommandCooldownTicks = 360;
	Fix64 chargeSpeedThreshold = fx::Int(6);
	Fix64 aggressiveRadius = fx::Int(25);
	Fix64 leashRadius = fx::Int(40);
	Fix64 holdRadius = fx::Int(6);
	Fix64 skirmishTrigger = fx::Int(12);
	Fix64 skirmishRetreat = fx::Int(15);
	int cycleChargeMeleeTicks = 60;
	Fix64 cycleChargeDistance = fx::Int(20);
	Fix64 forestAccuracyMult = fx::Pct(70);
	Fix64 ruinsRangedTakenMult = fx::Pct(75);
	Fix64 forestHideRadius = fx::Int(25);
	Fix64 roadSpeedMult = fx::Milli(1150);
	Fix64 forestCavalrySpeedMult = fx::Pct(60);
	Fix64 fordSpeedMult = fx::Pct(50);
	Fix64 titanFordSpeedMult = fx::Pct(80);
	Fix64 titanDeepSpeedMult = fx::Pct(60);
	Fix64 burnDps = fx::Int(10);
};

struct SimContent
{
	std::string version;
	std::string hash;
	RulesDef rules;
	ProgressionDef progression;
	std::vector<TroopDef> troops;
	std::vector<TitanClassDef> titanClasses; // indexed by TitanClass
	std::vector<SizeDef> sizes; // indexed by SizeClass
	std::vector<TitanDef> titans;
	std::vector<AbilityDef> abilities;
	std::vector<MapDef> maps;
	std::vector<AiProfileDef> aiProfiles;
	CounterMatrix counters;
	int titanCounterRow = -1;
	std::vector<std::string> scenarioFiles;

	std::map<std::string, int> troopIndex;
	std::map<std::string, int> titanIndex;
	std::map<std::string, int> abilityIndex;
	std::map<std::string, int> mapIndex;
	std::map<std::string, int> aiIndex;

	DOTSIM_API int FindTroop(const std::string& Id) const;
	DOTSIM_API int FindTitan(const std::string& Id) const;
	DOTSIM_API int FindAbility(const std::string& Id) const;
	DOTSIM_API int FindMap(const std::string& Id) const;
	DOTSIM_API int FindAiProfile(const std::string& Id) const;
};

/** Abstract file access so Unreal (FFileHelper/pak files) and tools (stdio) can both load content. */
class DOTSIM_API IContentSource
{
public:
	virtual ~IContentSource() = default;
	virtual bool ReadText(const std::string& RelativePath, std::string& OutText) = 0;
};

/** Loads manifest.json and every file it lists. */
DOTSIM_API bool LoadContent(IContentSource& Source, SimContent& Out, std::string& Error);
} // namespace dot
