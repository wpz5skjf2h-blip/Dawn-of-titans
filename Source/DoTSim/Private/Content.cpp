// Source/DoTSim/Private/Content.cpp
#include "DoTSim/Content.h"

#include "DoTSim/Hash.h"

#include <cmath>

namespace dot
{
namespace
{
const char* const kFormationNames[] = {"Line", "Column", "Wedge", "Square", "Loose"};
const char* const kStanceNames[] = {"Aggressive", "Hold", "Skirmish"};
const char* const kDefenderClassNames[] = {"Light", "Heavy", "Pike", "Ranged", "Cavalry", "Siege", "Support", "Titan", "Structure"};

int SecToTicks(double Seconds)
{
	return static_cast<int>(std::llround(Seconds * kTicksPerSecond));
}

template <typename TEnum, size_t N>
bool ParseEnumName(const std::string& S, const char* const (&Names)[N], TEnum& Out)
{
	for (size_t I = 0; I < N; ++I)
	{
		if (S == Names[I])
		{
			Out = static_cast<TEnum>(I);
			return true;
		}
	}
	return false;
}

bool ParseDefenderClass(const std::string& S, DefenderClass& Out) { return ParseEnumName(S, kDefenderClassNames, Out); }

bool ParseMoveClass(const std::string& S, MoveClass& Out)
{
	static const char* const Names[] = {"Infantry", "Cavalry", "Siege", "Titan"};
	return ParseEnumName(S, Names, Out);
}

bool ParseTitanClass(const std::string& S, TitanClass& Out)
{
	static const char* const Names[] = {"Juggernaut", "Ravager", "Warden", "Mystic", "Hunter"};
	return ParseEnumName(S, Names, Out);
}

bool ParseSizeClass(const std::string& S, SizeClass& Out)
{
	static const char* const Names[] = {"Medium", "Large", "Colossal"};
	return ParseEnumName(S, Names, Out);
}

bool ParseRarity(const std::string& S, Rarity& Out)
{
	static const char* const Names[] = {"Common", "Rare", "Epic", "Legendary", "Mythic"};
	return ParseEnumName(S, Names, Out);
}

bool ParseEffectType(const std::string& S, EffectType& Out)
{
	static const char* const Names[] = {"Damage", "Heal", "Knockback", "Stun", "StatMod", "Burn", "Morale", "RaiseDead"};
	return ParseEnumName(S, Names, Out);
}

bool ParseStatType(const std::string& S, StatType& Out)
{
	static const char* const Names[] = {"MoveSpeedPct", "AttackSpeedPct", "DamageTakenPct", "ArmorAdd"};
	return ParseEnumName(S, Names, Out);
}

bool ParseShape(const std::string& S, TargetShape& Out)
{
	static const char* const Names[] = {"Self", "Circle", "Cone", "Line"};
	return ParseEnumName(S, Names, Out);
}

bool ParseDelivery(const std::string& S, Delivery& Out)
{
	static const char* const Names[] = {"Instant", "Projectile", "Leap", "Zone", "Delayed"};
	return ParseEnumName(S, Names, Out);
}

bool ParseHeuristic(const std::string& S, AiHeuristic& Out)
{
	static const char* const Names[] = {"MaxUnitsInArea", "MaxUnitsInLine", "MaxUnitsInCone", "SelfDefense", "AlliesInCombat", "AlliesDamaged", "Corpses"};
	return ParseEnumName(S, Names, Out);
}

bool ParseFeatureType(const std::string& S, FeatureType& Out)
{
	static const char* const Names[] = {"Hill", "Forest", "ShallowWater", "DeepWater", "Rock", "Road", "Ruins"};
	return ParseEnumName(S, Names, Out);
}

Fix64 CosOfDegrees(double Degrees)
{
	return fx::Cos(fx::DegToRad(Fix64::FromDouble(Degrees)));
}

// --- Rules overrides --------------------------------------------------------

void BindFix(const JsonValue& J, const char* Key, Fix64& Value)
{
	if (const JsonValue* V = J.Find(Key))
	{
		Value = V->AsFix(Value);
	}
}

void BindInt(const JsonValue& J, const char* Key, int& Value)
{
	if (const JsonValue* V = J.Find(Key))
	{
		Value = V->AsInt(Value);
	}
}

/** Struct field "<name>Ticks" is configured in JSON as "<name>Sec". */
void BindTicks(const JsonValue& J, const char* SecKey, int& Ticks)
{
	if (const JsonValue* V = J.Find(SecKey))
	{
		Ticks = SecToTicks(V->AsDouble(Ticks / static_cast<double>(kTicksPerSecond)));
	}
}

void ApplyRules(const JsonValue& R, RulesDef& D)
{
	BindTicks(R, "countdownSec", D.countdownTicks);
	BindTicks(R, "deploySec", D.deployTicks);
	BindFix(R, "valorStart", D.valorStart);
	BindTicks(R, "valorIntervalSec", D.valorIntervalTicks);
	BindFix(R, "valorMax", D.valorMax);
	BindFix(R, "valorOnClashStagger", D.valorOnClashStagger);
	BindFix(R, "moraleLossPerPctLost", D.moraleLossPerPctLost);
	BindFix(R, "moraleChargedFlank", D.moraleChargedFlank);
	BindFix(R, "moraleTitanHit", D.moraleTitanHit);
	BindTicks(R, "moraleTitanHitCooldownSec", D.moraleTitanHitCooldownTicks);
	BindFix(R, "moraleFriendlyRout", D.moraleFriendlyRout);
	BindFix(R, "moraleFriendlyRoutRadius", D.moraleFriendlyRoutRadius);
	BindFix(R, "moraleEnemyTitanDown", D.moraleEnemyTitanDown);
	BindFix(R, "moraleOwnTitanDown", D.moraleOwnTitanDown);
	BindFix(R, "moraleRegenPerSec", D.moraleRegenPerSec);
	BindTicks(R, "moraleRegenDelaySec", D.moraleRegenDelayTicks);
	BindFix(R, "moraleRoutRegenPerSec", D.moraleRoutRegenPerSec);
	BindFix(R, "moraleRally", D.moraleRally);
	BindFix(R, "moraleShaken", D.moraleShaken);
	BindFix(R, "moraleWavering", D.moraleWavering);
	BindFix(R, "shakenAtkMult", D.shakenAtkMult);
	BindFix(R, "waveringAtkMult", D.waveringAtkMult);
	BindFix(R, "waveringSpeedMult", D.waveringSpeedMult);
	BindFix(R, "gravity", D.gravity);
	BindTicks(R, "knockdownSec", D.knockdownTicks);
	BindTicks(R, "airborneDamageSec", D.airborneDamageTicks);
	BindFix(R, "landingDamagePct", D.landingDamagePct);
	BindFix(R, "sweepKnockH", D.sweepKnockH);
	BindFix(R, "sweepKnockV", D.sweepKnockV);
	BindFix(R, "smashKnockH", D.smashKnockH);
	BindFix(R, "smashKnockV", D.smashKnockV);
	BindFix(R, "stompKnockH", D.stompKnockH);
	BindFix(R, "stompKnockV", D.stompKnockV);
	BindInt(R, "stompTriggerCount", D.stompTriggerCount);
	BindFix(R, "stompTriggerRadius", D.stompTriggerRadius);
	BindFix(R, "stompRadius", D.stompRadius);
	BindFix(R, "stompCoef", D.stompCoef);
	BindTicks(R, "stompCooldownSec", D.stompCooldownTicks);
	BindTicks(R, "stompWindupSec", D.stompWindupTicks);
	BindInt(R, "stompChainTrigger", D.stompChainTrigger);
	BindFix(R, "grabRadius", D.grabRadius);
	BindFix(R, "throwRange", D.throwRange);
	BindFix(R, "throwSplash", D.throwSplash);
	BindFix(R, "throwCoef", D.throwCoef);
	BindTicks(R, "grabCooldownSec", D.grabCooldownTicks);
	BindTicks(R, "throwFlightSec", D.throwFlightTicks);
	BindFix(R, "titanChargeMinDistance", D.titanChargeMinDistance);
	BindFix(R, "titanChargeSpeedBonus", D.titanChargeSpeedBonus);
	BindTicks(R, "titanChargeSec", D.titanChargeTicks);
	BindTicks(R, "titanChargeCooldownSec", D.titanChargeCooldownTicks);
	BindFix(R, "titanTrampleCoef", D.titanTrampleCoef);
	BindFix(R, "footstepCoef", D.footstepCoef);
	BindTicks(R, "footstepKnockdownSec", D.footstepKnockdownTicks);
	BindFix(R, "rageStart", D.rageStart);
	BindFix(R, "rageRegenPerSec", D.rageRegenPerSec);
	BindFix(R, "ragePerKill", D.ragePerKill);
	BindFix(R, "rageMaxKillPerSec", D.rageMaxKillPerSec);
	BindFix(R, "ragePerPctHpLost", D.ragePerPctHpLost);
	BindFix(R, "poiseFactorTroops", D.poiseFactorTroops);
	BindFix(R, "poiseFactorTitans", D.poiseFactorTitans);
	BindTicks(R, "staggerSec", D.staggerTicks);
	BindFix(R, "staggerDamageTakenPct", D.staggerDamageTakenPct);
	BindFix(R, "poiseRegenPctPerSec", D.poiseRegenPctPerSec);
	BindTicks(R, "poiseRegenDelaySec", D.poiseRegenDelayTicks);
	BindTicks(R, "comboResetSec", D.comboResetTicks);
	BindTicks(R, "titanWindupSec", D.titanWindupTicks);
	BindFix(R, "smashRadius", D.smashRadius);
	BindFix(R, "smashCoef", D.smashCoef);
	BindFix(R, "titanVsTitanMult", D.titanVsTitanMult);
	BindFix(R, "fallWidth", D.fallWidth);
	BindFix(R, "fallFlatDamage", D.fallFlatDamage);
	BindFix(R, "fallPctMaxHp", D.fallPctMaxHp);
	BindTicks(R, "fallObstacleSec", D.fallObstacleTicks);
	BindFix(R, "clashTriggerExtra", D.clashTriggerExtra);
	BindTicks(R, "clashLockSec", D.clashLockTicks);
	BindTicks(R, "clashWindupSec", D.clashWindupTicks);
	BindTicks(R, "clashStrikeSec", D.clashStrikeTicks);
	BindInt(R, "clashPerfectMs", D.clashPerfectMs);
	BindInt(R, "clashGoodMs", D.clashGoodMs);
	BindFix(R, "clashWinCoef", D.clashWinCoef);
	BindFix(R, "clashWinPerfectCoef", D.clashWinPerfectCoef);
	BindFix(R, "clashLoseCoef", D.clashLoseCoef);
	BindFix(R, "clashBothMissCoef", D.clashBothMissCoef);
	BindFix(R, "clashPushback", D.clashPushback);
	BindFix(R, "clashPoisePct", D.clashPoisePct);
	BindTicks(R, "clashStaggerSec", D.clashStaggerTicks);
	BindFix(R, "clashStaggerDamageTakenPct", D.clashStaggerDamageTakenPct);
	BindTicks(R, "clashPairCooldownSec", D.clashPairCooldownTicks);
	BindFix(R, "clashShockwaveRadius", D.clashShockwaveRadius);
	BindFix(R, "clashShockwavePct", D.clashShockwavePct);
	BindFix(R, "varianceMin", D.varianceMin);
	BindFix(R, "varianceMax", D.varianceMax);
	BindFix(R, "flankMult", D.flankMult);
	BindFix(R, "rearMult", D.rearMult);
	BindFix(R, "elevationThreshold", D.elevationThreshold);
	BindFix(R, "elevationRangedMult", D.elevationRangedMult);
	BindFix(R, "elevationRangeBonus", D.elevationRangeBonus);
	BindFix(R, "uphillMeleeMult", D.uphillMeleeMult);
	BindFix(R, "engageDistance", D.engageDistance);
	BindInt(R, "maxAttackersPerSoldier", D.maxAttackersPerSoldier);
	BindInt(R, "pairingIntervalTicks", D.pairingIntervalTicks);
	BindFix(R, "looseAreaDamageMult", D.looseAreaDamageMult);
	BindFix(R, "looseMeleeDealtMult", D.looseMeleeDealtMult);
	BindFix(R, "looseAccuracyMult", D.looseAccuracyMult);
	BindFix(R, "columnSpeedMult", D.columnSpeedMult);
	BindFix(R, "columnFlankTakenMult", D.columnFlankTakenMult);
	BindFix(R, "squareSpeedMult", D.squareSpeedMult);
	BindFix(R, "wedgeChargeMult", D.wedgeChargeMult);
	BindFix(R, "wedgeArmorAdd", D.wedgeArmorAdd);
	BindTicks(R, "formationChangeSec", D.formationChangeTicks);
	BindFix(R, "formationChangeDamageMult", D.formationChangeDamageMult);
	BindTicks(R, "chargeCommandSec", D.chargeCommandTicks);
	BindFix(R, "chargeCommandSpeedBonus", D.chargeCommandSpeedBonus);
	BindTicks(R, "chargeCommandCooldownSec", D.chargeCommandCooldownTicks);
	BindFix(R, "chargeSpeedThreshold", D.chargeSpeedThreshold);
	BindFix(R, "aggressiveRadius", D.aggressiveRadius);
	BindFix(R, "leashRadius", D.leashRadius);
	BindFix(R, "holdRadius", D.holdRadius);
	BindFix(R, "skirmishTrigger", D.skirmishTrigger);
	BindFix(R, "skirmishRetreat", D.skirmishRetreat);
	BindTicks(R, "cycleChargeMeleeSec", D.cycleChargeMeleeTicks);
	BindFix(R, "cycleChargeDistance", D.cycleChargeDistance);
	BindFix(R, "forestAccuracyMult", D.forestAccuracyMult);
	BindFix(R, "ruinsRangedTakenMult", D.ruinsRangedTakenMult);
	BindFix(R, "forestHideRadius", D.forestHideRadius);
	BindFix(R, "roadSpeedMult", D.roadSpeedMult);
	BindFix(R, "forestCavalrySpeedMult", D.forestCavalrySpeedMult);
	BindFix(R, "fordSpeedMult", D.fordSpeedMult);
	BindFix(R, "titanFordSpeedMult", D.titanFordSpeedMult);
	BindFix(R, "titanDeepSpeedMult", D.titanDeepSpeedMult);
	BindFix(R, "burnDps", D.burnDps);
}

// --- Loaders -----------------------------------------------------------------

bool LoadJsonFile(IContentSource& Source, const std::string& Path, JsonValue& Out, std::string& Error, StateWriter& HashInput)
{
	std::string Text;
	if (!Source.ReadText(Path, Text))
	{
		Error = "cannot read content file '" + Path + "'";
		return false;
	}
	for (const char C : Text)
	{
		HashInput.U8(static_cast<uint8_t>(C));
	}
	std::string ParseError;
	if (!ParseJson(Text, Out, ParseError))
	{
		Error = Path + ": " + ParseError;
		return false;
	}
	return true;
}

bool LoadTroops(const JsonValue& Root, SimContent& C, std::string& Error)
{
	const JsonValue* List = Root.Find("troops");
	if (!List || !List->IsArray())
	{
		Error = "troops.json: missing 'troops' array";
		return false;
	}
	for (size_t I = 0; I < List->Size(); ++I)
	{
		const JsonValue& J = (*List)[I];
		TroopDef T;
		T.id = J.GetString("id");
		T.name = J.GetString("name", T.id);
		T.shortName = J.GetString("short", T.name);
		if (!ParseDefenderClass(J.GetString("class"), T.cls) || !ParseMoveClass(J.GetString("move", "Infantry"), T.move))
		{
			Error = "troop '" + T.id + "': bad class or move";
			return false;
		}
		T.squadSize = J.GetInt("squadSize", 20);
		T.hp = J.GetFix("hp");
		T.armor = J.GetFix("armor");
		T.atk = J.GetFix("atk");
		T.attackIntervalTicks = SecToTicks(J.GetDouble("attackInterval", 1.0));
		T.range = J.GetFix("range", fx::Milli(1500));
		T.minRange = J.GetFix("minRange");
		T.moveSpeed = J.GetFix("moveSpeed", fx::Int(3));
		T.collisionRadius = J.GetFix("collisionRadius", fx::Milli(400));
		T.knockbackMult = J.GetFix("knockbackMult", Fix64::One());
		T.critChance = J.GetFix("critChance", fx::Pct(5));
		T.critDamage = J.GetFix("critDamage", fx::Milli(1500));
		T.spacing = J.GetFix("spacing", fx::Milli(1400));
		if (const JsonValue* R = J.Find("ranged"))
		{
			T.ranged = true;
			T.projectileSpeed = R->GetFix("projectileSpeed", fx::Int(30));
			T.accuracyNear = R->GetFix("accuracyNear", fx::Pct(75));
			T.accuracyFar = R->GetFix("accuracyFar", fx::Pct(40));
			T.accuracyNearRange = R->GetFix("accuracyNearRange", fx::Int(15));
			T.accuracyVsTitan = R->GetFix("accuracyVsTitan", fx::Pct(95));
		}
		if (const JsonValue* S = J.Find("specials"))
		{
			if (const JsonValue* V = S->Find("shieldBlock"))
			{
				T.shieldBlock = true;
				T.shieldBlockReduction = V->GetFix("reduction", fx::Pct(40));
			}
			if (const JsonValue* V = S->Find("brace"))
			{
				T.brace = true;
				T.braceDamageMult = V->GetFix("chargeDamageTakenMult", fx::Int(2));
				T.braceTitanChargeMult = V->GetFix("titanChargeDamageMult", fx::Pct(50));
			}
			if (const JsonValue* V = S->Find("strengthInNumbers"))
			{
				T.strengthInNumbers = true;
				T.strengthInNumbersBonus = V->GetFix("atkBonus", fx::Pct(10));
			}
			if (const JsonValue* V = S->Find("charge"))
			{
				T.chargeMult = V->GetFix("firstHitMult", fx::Milli(2500));
				T.chargeKnockdownTicks = SecToTicks(V->GetDouble("knockdown", 1.0));
			}
			if (const JsonValue* V = S->Find("trample"))
			{
				T.trample = true;
				T.trampleDamage = V->GetFix("damage", fx::Int(30));
			}
			if (const JsonValue* V = S->Find("firebomb"))
			{
				T.firebomb = true;
				T.splashRadius = V->GetFix("splashRadius", fx::Int(3));
				T.splashEdgeMult = V->GetFix("edgeMult", fx::Pct(50));
				T.fireRadius = V->GetFix("fireRadius", fx::Milli(2500));
				T.fireDps = V->GetFix("fireDps", fx::Int(8));
				T.fireTicks = SecToTicks(V->GetDouble("fireDuration", 4.0));
				T.fireFriendlyMult = V->GetFix("friendlyMult", fx::Pct(50));
			}
			if (const JsonValue* V = S->Find("chains"))
			{
				T.chains = true;
				T.chainRange = V->GetFix("range", fx::Int(18));
				T.chainIntervalTicks = SecToTicks(V->GetDouble("interval", 2.5));
				T.chainMinAlive = V->GetInt("minAlive", 3);
				T.chainMoveSlow = V->GetFix("moveSlow", fx::Pct(8));
				T.chainAttackSlow = V->GetFix("attackSlow", fx::Pct(5));
				T.chainTicks = SecToTicks(V->GetDouble("duration", 10.0));
				T.chainBindAt = V->GetInt("bindAt", 5);
				T.chainBindTicks = SecToTicks(V->GetDouble("bindDuration", 2.5));
				T.chainImmuneTicks = SecToTicks(V->GetDouble("immunity", 12.0));
			}
			if (const JsonValue* V = S->Find("sanctuary"))
			{
				T.support = true;
				T.auraRadius = V->GetFix("radius", fx::Int(8));
				T.auraHealPctPerSec = V->GetFix("healPctPerSec", fx::Milli(15));
				T.auraMoralePerSec = V->GetFix("moralePerSec", fx::Int(3));
				T.auraArmor = V->GetFix("armor", fx::Pct(10));
				T.keepAway = V->GetFix("keepAway", fx::Int(8));
			}
		}
		if (const JsonValue* F = J.Find("formations"))
		{
			for (size_t K = 0; K < F->Size(); ++K)
			{
				Formation Form;
				if (ParseFormation((*F)[K].AsString(), Form))
				{
					T.formationMask |= 1u << static_cast<unsigned>(Form);
				}
			}
		}
		T.formationMask |= 1u << static_cast<unsigned>(Formation::Line);
		ParseStance(J.GetString("defaultStance", "Aggressive"), T.defaultStance);
		if (const JsonValue* Tiers = J.Find("tiers"))
		{
			for (size_t K = 0; K < Tiers->Size(); ++K)
			{
				const JsonValue& TJ = (*Tiers)[K];
				TroopTierDef Tier;
				Tier.name = TJ.GetString("name", T.name);
				Tier.hpMult = TJ.GetFix("hpMult", Fix64::One());
				Tier.atkMult = TJ.GetFix("atkMult", Fix64::One());
				Tier.armorAdd = TJ.GetFix("armorAdd");
				T.tiers.push_back(Tier);
			}
		}
		if (T.tiers.empty())
		{
			T.tiers.push_back(TroopTierDef());
		}
		const std::string Row = J.GetString("counterRow", T.id);
		T.counterRow = C.counters.FindRow(Row);
		if (T.counterRow < 0)
		{
			Error = "troop '" + T.id + "': counter row '" + Row + "' not found";
			return false;
		}
		C.troopIndex[T.id] = static_cast<int>(C.troops.size());
		C.troops.push_back(T);
	}
	return true;
}

bool LoadCounters(const JsonValue& Root, SimContent& C, std::string& Error)
{
	const JsonValue* Rows = Root.Find("rows");
	if (!Rows || !Rows->IsObject())
	{
		Error = "counters.json: missing 'rows' object";
		return false;
	}
	for (size_t I = 0; I < Rows->keys.size(); ++I)
	{
		const JsonValue& Row = Rows->items[I];
		if (!Row.IsArray() || Row.Size() != static_cast<size_t>(kDefenderClassCount))
		{
			Error = "counters.json: row '" + Rows->keys[I] + "' must have 9 values";
			return false;
		}
		std::array<Fix64, kDefenderClassCount> Values;
		for (size_t K = 0; K < Values.size(); ++K)
		{
			Values[K] = Row[K].AsFix(Fix64::One());
		}
		C.counters.rowIds.push_back(Rows->keys[I]);
		C.counters.rows.push_back(Values);
	}
	C.titanCounterRow = C.counters.FindRow("titan");
	if (C.titanCounterRow < 0)
	{
		Error = "counters.json: missing 'titan' row";
		return false;
	}
	return true;
}

bool LoadTitans(const JsonValue& Root, SimContent& C, std::string& Error)
{
	C.titanClasses.assign(static_cast<size_t>(TitanClass::Count), TitanClassDef());
	C.sizes.assign(static_cast<size_t>(SizeClass::Count), SizeDef());
	if (const JsonValue* Classes = Root.Find("classes"))
	{
		for (size_t I = 0; I < Classes->Size(); ++I)
		{
			const JsonValue& J = (*Classes)[I];
			TitanClassDef D;
			if (!ParseTitanClass(J.GetString("class"), D.cls))
			{
				Error = "titans.json: bad class '" + J.GetString("class") + "'";
				return false;
			}
			D.hp = J.GetFix("hp");
			D.atk = J.GetFix("atk");
			D.armor = J.GetFix("armor");
			D.attackIntervalTicks = SecToTicks(J.GetDouble("attackInterval", 2.0));
			D.sweepRadius = J.GetFix("sweepRadius", fx::Int(6));
			D.sweepHalfArcCos = CosOfDegrees(J.GetDouble("sweepArcDeg", 120.0) * 0.5);
			D.poise = J.GetFix("poise", fx::Int(200));
			D.moveSpeed = J.GetFix("moveSpeed", fx::Int(5));
			D.turnRateDeg = J.GetFix("turnRateDeg", fx::Int(60));
			D.critChance = J.GetFix("critChance", fx::Pct(5));
			D.critDamage = J.GetFix("critDamage", fx::Milli(1500));
			D.rangedAttack = J.GetBool("rangedAttack", false);
			D.attackRange = J.GetFix("attackRange");
			D.splashRadius = J.GetFix("splashRadius");
			D.aoeFactor = J.GetFix("aoeFactor", fx::Int(6));
			D.vsTitanMult = J.GetFix("vsTitanMult", Fix64::One());
			D.pierce = J.GetInt("pierce", 0);
			C.titanClasses[static_cast<size_t>(D.cls)] = D;
		}
	}
	if (const JsonValue* Sizes = Root.Find("sizes"))
	{
		for (size_t I = 0; I < Sizes->Size(); ++I)
		{
			const JsonValue& J = (*Sizes)[I];
			SizeDef D;
			if (!ParseSizeClass(J.GetString("size"), D.size))
			{
				Error = "titans.json: bad size";
				return false;
			}
			D.height = J.GetFix("height", fx::Int(18));
			D.capsuleRadius = J.GetFix("capsuleRadius", fx::Int(4));
			D.footRadius = J.GetFix("footRadius", fx::Milli(3200));
			D.stepLength = J.GetFix("stepLength", fx::Milli(8500));
			D.canWadeDeepWater = J.GetBool("canWadeDeepWater", true);
			C.sizes[static_cast<size_t>(D.size)] = D;
		}
	}
	const JsonValue* List = Root.Find("titans");
	if (!List || !List->IsArray())
	{
		Error = "titans.json: missing 'titans' array";
		return false;
	}
	for (size_t I = 0; I < List->Size(); ++I)
	{
		const JsonValue& J = (*List)[I];
		TitanDef T;
		T.id = J.GetString("id");
		T.name = J.GetString("name", T.id);
		T.title = J.GetString("title");
		T.lineage = J.GetString("lineage");
		if (!ParseTitanClass(J.GetString("class"), T.cls) || !ParseRarity(J.GetString("rarity", "Common"), T.rarity) ||
			!ParseSizeClass(J.GetString("size", "Large"), T.size))
		{
			Error = "titan '" + T.id + "': bad class, rarity or size";
			return false;
		}
		if (const JsonValue* V = J.Find("variance"))
		{
			T.varianceHp = V->GetFix("hp", Fix64::One());
			T.varianceAtk = V->GetFix("atk", Fix64::One());
			T.variancePoise = V->GetFix("poise", Fix64::One());
		}
		T.moveSpeedOverride = J.GetFix("moveSpeed");
		if (const JsonValue* A = J.Find("abilities"))
		{
			for (size_t K = 0; K < A->Size() && K < 2; ++K)
			{
				const int Index = C.FindAbility((*A)[K].AsString());
				if (Index < 0)
				{
					Error = "titan '" + T.id + "': unknown ability '" + (*A)[K].AsString() + "'";
					return false;
				}
				T.abilities[K] = Index;
			}
		}
		if (const JsonValue* P = J.Find("passives"))
		{
			T.rangedArmorBonus = P->GetFix("rangedArmorBonus");
			if (const JsonValue* L = P->Find("chainLightning"))
			{
				T.chainLightningEvery = L->GetInt("every", 3);
				T.chainLightningTargets = L->GetInt("targets", 4);
				T.chainLightningCoef = L->GetFix("coef", fx::Pct(60));
			}
		}
		C.titanIndex[T.id] = static_cast<int>(C.titans.size());
		C.titans.push_back(T);
	}
	return true;
}

bool LoadAbilities(const JsonValue& Root, SimContent& C, std::string& Error)
{
	const JsonValue* List = Root.Find("abilities");
	if (!List || !List->IsArray())
	{
		Error = "abilities.json: missing 'abilities' array";
		return false;
	}
	for (size_t I = 0; I < List->Size(); ++I)
	{
		const JsonValue& J = (*List)[I];
		AbilityDef A;
		A.id = J.GetString("id");
		A.name = J.GetString("name", A.id);
		A.isSpell = J.GetBool("spell", false);
		A.rageCost = J.GetInt("rage", 0);
		A.valorCost = J.GetInt("valor", 0);
		A.cooldownTicks = SecToTicks(J.GetDouble("cooldown", 0.0));
		A.castTicks = SecToTicks(J.GetDouble("castTime", 0.0));
		if (!ParseShape(J.GetString("shape", "Circle"), A.shape) || !ParseDelivery(J.GetString("delivery", "Instant"), A.delivery))
		{
			Error = "ability '" + A.id + "': bad shape or delivery";
			return false;
		}
		A.range = J.GetFix("range");
		A.radius = J.GetFix("radius");
		A.width = J.GetFix("width");
		A.coneHalfAngleCos = CosOfDegrees(J.GetDouble("coneAngleDeg", 60.0) * 0.5);
		A.projectileSpeed = J.GetFix("projectileSpeed", fx::Int(30));
		A.returns = J.GetBool("returns", false);
		A.delayTicks = SecToTicks(J.GetDouble("delay", 0.0));
		A.zoneDurationTicks = SecToTicks(J.GetDouble("zoneDuration", 0.0));
		A.zonePulseTicks = SecToTicks(J.GetDouble("zonePulse", 1.0));
		A.leapTicks = SecToTicks(J.GetDouble("leapTime", 0.8));
		A.perLevelPct = J.GetFix("perLevelPct", fx::Pct(8));
		if (const JsonValue* Ai = J.Find("ai"))
		{
			ParseHeuristic(Ai->GetString("heuristic", "MaxUnitsInArea"), A.ai);
			A.aiMinTargets = Ai->GetInt("minTargets", 6);
			A.aiHpBelow = Ai->GetFix("hpBelow", fx::Pct(60));
			A.aiPreferTitan = Ai->GetBool("preferTitan", false);
		}
		if (const JsonValue* Effects = J.Find("effects"))
		{
			for (size_t K = 0; K < Effects->Size(); ++K)
			{
				const JsonValue& E = (*Effects)[K];
				EffectDef D;
				if (!ParseEffectType(E.GetString("type"), D.type))
				{
					Error = "ability '" + A.id + "': bad effect type";
					return false;
				}
				D.affectsMask = 0;
				if (const JsonValue* Aff = E.Find("affects"))
				{
					for (size_t M = 0; M < Aff->Size(); ++M)
					{
						const std::string& S = (*Aff)[M].AsString();
						if (S == "EnemyTroops") D.affectsMask |= affects::EnemyTroops;
						else if (S == "EnemyTitans") D.affectsMask |= affects::EnemyTitans;
						else if (S == "AllyTroops") D.affectsMask |= affects::AllyTroops;
						else if (S == "AllyTitans") D.affectsMask |= affects::AllyTitans;
						else if (S == "Self") D.affectsMask |= affects::Self;
					}
				}
				D.coef = E.GetFix("coef");
				D.flat = E.GetFix("flat");
				D.pctMaxHp = E.GetFix("pctMaxHp");
				D.durationTicks = SecToTicks(E.GetDouble("duration", 0.0));
				ParseStatType(E.GetString("stat", "MoveSpeedPct"), D.stat);
				D.magnitude = E.GetFix("magnitude");
				D.horizontal = E.GetFix("horizontal");
				D.vertical = E.GetFix("vertical");
				D.hits = E.GetInt("hits", 1);
				D.poiseDamage = E.GetFix("poiseDamage");
				if (D.type == EffectType::RaiseDead)
				{
					D.summonTroop = C.FindTroop(E.GetString("troop"));
					if (D.summonTroop < 0)
					{
						Error = "ability '" + A.id + "': unknown troop to raise";
						return false;
					}
					D.summonMax = E.GetInt("max", 12);
					D.summonMaxPerLevel = E.GetInt("maxPerLevel", 0);
					D.corpseAgeTicks = SecToTicks(E.GetDouble("corpseAge", 15.0));
				}
				A.effects.push_back(D);
			}
		}
		C.abilityIndex[A.id] = static_cast<int>(C.abilities.size());
		C.abilities.push_back(A);
	}
	return true;
}

bool LoadAiProfiles(const JsonValue& Root, SimContent& C, std::string& Error)
{
	const JsonValue* List = Root.Find("profiles");
	if (!List || !List->IsArray())
	{
		Error = "ai_profiles.json: missing 'profiles'";
		return false;
	}
	for (size_t I = 0; I < List->Size(); ++I)
	{
		const JsonValue& J = (*List)[I];
		AiProfileDef P;
		P.id = J.GetString("id");
		P.reactionTicks = SecToTicks(J.GetDouble("reaction", 1.2));
		P.decisionTicks = SecToTicks(J.GetDouble("decisionInterval", 1.0));
		if (P.decisionTicks < 1)
		{
			P.decisionTicks = 1;
		}
		P.bestCounterChance = J.GetFix("bestCounterChance", fx::Pct(60));
		P.flanking = J.GetInt("flanking", 1);
		P.focusFire = J.GetInt("focusFire", 1);
		P.spellSkill = J.GetInt("spellSkill", 1);
		P.formationMicro = J.GetBool("formationMicro", false);
		if (const JsonValue* Clash = J.Find("clash"))
		{
			P.clashPerfect = Clash->GetFix("perfect", fx::Pct(15));
			P.clashGood = Clash->GetFix("good", fx::Pct(50));
		}
		C.aiIndex[P.id] = static_cast<int>(C.aiProfiles.size());
		C.aiProfiles.push_back(P);
	}
	return true;
}

bool ReadVec(const JsonValue* J, FixVec2& Out)
{
	if (!J || !J->IsArray() || J->Size() < 2)
	{
		return false;
	}
	Out = FixVec2((*J)[0].AsFix(), (*J)[1].AsFix());
	return true;
}

bool LoadMap(const JsonValue& J, SimContent& C, std::string& Error)
{
	MapDef M;
	M.id = J.GetString("id");
	M.name = J.GetString("name", M.id);
	M.width = J.GetFix("width", fx::Int(240));
	M.depth = J.GetFix("depth", fx::Int(160));
	M.cellSize = J.GetFix("cellSize", fx::Int(2));
	const JsonValue* Deploy = J.Find("deploy");
	if (!Deploy)
	{
		Error = "map '" + M.id + "': missing deploy zones";
		return false;
	}
	const char* SideNames[2] = {"attacker", "defender"};
	for (int S = 0; S < 2; ++S)
	{
		const JsonValue* Zone = Deploy->Find(SideNames[S]);
		if (!Zone || !ReadVec(Zone->Find("min"), M.deployMin[S]) || !ReadVec(Zone->Find("max"), M.deployMax[S]))
		{
			Error = "map '" + M.id + "': bad deploy zone";
			return false;
		}
	}
	if (const JsonValue* Features = J.Find("features"))
	{
		for (size_t I = 0; I < Features->Size(); ++I)
		{
			const JsonValue& F = (*Features)[I];
			MapFeature Feature;
			if (!ParseFeatureType(F.GetString("type"), Feature.type))
			{
				Error = "map '" + M.id + "': bad feature type '" + F.GetString("type") + "'";
				return false;
			}
			if (const JsonValue* Circle = F.Find("circle"))
			{
				Feature.circle = true;
				Feature.center = FixVec2(Circle->GetFix("x"), Circle->GetFix("z"));
				Feature.radius = Circle->GetFix("r");
			}
			else if (const JsonValue* Rect = F.Find("rect"))
			{
				Feature.circle = false;
				Feature.min = FixVec2(Rect->GetFix("x0"), Rect->GetFix("z0"));
				Feature.max = FixVec2(Rect->GetFix("x1"), Rect->GetFix("z1"));
			}
			else
			{
				Error = "map '" + M.id + "': feature needs circle or rect";
				return false;
			}
			Feature.height = F.GetFix("height");
			M.features.push_back(Feature);
		}
	}
	C.mapIndex[M.id] = static_cast<int>(C.maps.size());
	C.maps.push_back(M);
	return true;
}

int FindIn(const std::map<std::string, int>& Index, const std::string& Id)
{
	const auto It = Index.find(Id);
	return It == Index.end() ? -1 : It->second;
}
} // namespace

const char* ToString(Formation F) { return kFormationNames[static_cast<int>(F)]; }
const char* ToString(Stance S) { return kStanceNames[static_cast<int>(S)]; }
const char* ToString(DefenderClass C) { return kDefenderClassNames[static_cast<int>(C)]; }
bool ParseFormation(const std::string& S, Formation& Out) { return ParseEnumName(S, kFormationNames, Out); }
bool ParseStance(const std::string& S, Stance& Out) { return ParseEnumName(S, kStanceNames, Out); }

int CounterMatrix::FindRow(const std::string& Id) const
{
	for (size_t I = 0; I < rowIds.size(); ++I)
	{
		if (rowIds[I] == Id)
		{
			return static_cast<int>(I);
		}
	}
	return -1;
}

int SimContent::FindTroop(const std::string& Id) const { return FindIn(troopIndex, Id); }
int SimContent::FindTitan(const std::string& Id) const { return FindIn(titanIndex, Id); }
int SimContent::FindAbility(const std::string& Id) const { return FindIn(abilityIndex, Id); }
int SimContent::FindMap(const std::string& Id) const { return FindIn(mapIndex, Id); }
int SimContent::FindAiProfile(const std::string& Id) const { return FindIn(aiIndex, Id); }

bool LoadContent(IContentSource& Source, SimContent& Out, std::string& Error)
{
	Out = SimContent();
	StateWriter HashInput;
	JsonValue Manifest;
	if (!LoadJsonFile(Source, "manifest.json", Manifest, Error, HashInput))
	{
		return false;
	}
	Out.version = Manifest.GetString("contentVersion", "dev");
	const JsonValue* Files = Manifest.Find("files");
	if (!Files)
	{
		Error = "manifest.json: missing 'files'";
		return false;
	}

	JsonValue Doc;
	// Order matters: counters before troops, abilities before titans.
	if (!LoadJsonFile(Source, Files->GetString("rules", "rules.json"), Doc, Error, HashInput))
	{
		return false;
	}
	if (const JsonValue* R = Doc.Find("rules"))
	{
		ApplyRules(*R, Out.rules);
	}
	if (const JsonValue* P = Doc.Find("progression"))
	{
		const JsonValue* RarityMult = P->Find("rarityMult");
		const JsonValue* StarMult = P->Find("starMult");
		for (int I = 0; I < static_cast<int>(Rarity::Count); ++I)
		{
			Out.progression.rarityMult[I] = (RarityMult && static_cast<size_t>(I) < RarityMult->Size()) ? (*RarityMult)[static_cast<size_t>(I)].AsFix(Fix64::One()) : Fix64::One();
		}
		for (int I = 0; I < 7; ++I)
		{
			Out.progression.starMult[I] = (StarMult && static_cast<size_t>(I) < StarMult->Size()) ? (*StarMult)[static_cast<size_t>(I)].AsFix(Fix64::One()) : Fix64::One();
		}
		Out.progression.levelGrowth = P->GetFix("levelGrowth", fx::Milli(45));
	}

	if (!LoadJsonFile(Source, Files->GetString("counters", "counters.json"), Doc, Error, HashInput) || !LoadCounters(Doc, Out, Error))
	{
		return false;
	}
	if (!LoadJsonFile(Source, Files->GetString("troops", "troops.json"), Doc, Error, HashInput) || !LoadTroops(Doc, Out, Error))
	{
		return false;
	}
	if (!LoadJsonFile(Source, Files->GetString("abilities", "abilities.json"), Doc, Error, HashInput) || !LoadAbilities(Doc, Out, Error))
	{
		return false;
	}
	if (!LoadJsonFile(Source, Files->GetString("titans", "titans.json"), Doc, Error, HashInput) || !LoadTitans(Doc, Out, Error))
	{
		return false;
	}
	if (!LoadJsonFile(Source, Files->GetString("ai", "ai_profiles.json"), Doc, Error, HashInput) || !LoadAiProfiles(Doc, Out, Error))
	{
		return false;
	}
	if (const JsonValue* Maps = Files->Find("maps"))
	{
		for (size_t I = 0; I < Maps->Size(); ++I)
		{
			if (!LoadJsonFile(Source, (*Maps)[I].AsString(), Doc, Error, HashInput) || !LoadMap(Doc, Out, Error))
			{
				return false;
			}
		}
	}
	if (const JsonValue* Scenarios = Files->Find("scenarios"))
	{
		for (size_t I = 0; I < Scenarios->Size(); ++I)
		{
			Out.scenarioFiles.push_back((*Scenarios)[I].AsString());
		}
	}
	if (Out.maps.empty())
	{
		Error = "content has no maps";
		return false;
	}
	Out.hash = HashToHex(HashInput.Hash());
	return true;
}
} // namespace dot
