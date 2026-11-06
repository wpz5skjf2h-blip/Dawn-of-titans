// Source/DoTKingdom/Private/KingdomContent.cpp
#include "DoTKingdom/KingdomContent.h"

#include "DoTSim/Hash.h"
#include "DoTSim/Json.h"

#include <algorithm>

namespace dot
{
namespace kingdom
{
namespace
{
bool ReadJson(IContentSource& Source, const std::string& Path, JsonValue& Out, std::string& Error, StateWriter& HashInput)
{
	std::string Text;
	if (!Source.ReadText(Path, Text))
	{
		Error = "cannot read content file '" + Path + "'";
		return false;
	}
	for (const char Ch : Text)
	{
		HashInput.U8(static_cast<uint8_t>(Ch));
	}
	std::string ParseError;
	if (!ParseJson(Text, Out, ParseError))
	{
		Error = Path + ": " + ParseError;
		return false;
	}
	return true;
}

bool ParseRole(const std::string& S, Role& Out)
{
	static const char* const Names[] = {"castle", "producer", "storage", "reserve", "portal", "builders", "other"};
	for (int I = 0; I < 7; ++I)
	{
		if (S == Names[I])
		{
			Out = static_cast<Role>(I);
			return true;
		}
	}
	return false;
}

void ReadCost(const JsonValue* Cost, ResArray& Out)
{
	Out.fill(0);
	if (!Cost || !Cost->IsObject())
	{
		return;
	}
	for (size_t I = 0; I < Cost->keys.size(); ++I)
	{
		Res R = Res::Gold;
		if (ParseRes(Cost->keys[I], R))
		{
			Out[static_cast<size_t>(ResIndex(R))] = Cost->items[I].AsInt64(0);
		}
	}
}
/** An event's pay: resources, a relic's rarity (1-5, 0 none), divine gems and Honor. */
void ReadEventReward(const JsonValue* J, EventReward& R)
{
	ReadCost(J, R.res);
	if (J && J->IsObject())
	{
		R.relic = std::max(0, std::min(5, J->GetInt("relic", 0)));
		R.divine = std::max<int64_t>(0, J->GetInt64("divine", 0));
		R.honor = std::max<int64_t>(0, J->GetInt64("honor", 0));
		R.silk = std::max<int64_t>(0, J->GetInt64("silk", 0));
	}
}
void ReadInts(const JsonValue* List, std::vector<int64_t>& Out)
{
	Out.clear();
	if (List)
	{
		for (size_t I = 0; I < List->Size(); ++I)
		{
			Out.push_back((*List)[I].AsInt64(0));
		}
	}
}

bool LoadArmy(const JsonValue& J, KingdomContent& Out, std::string& Error)
{
	const JsonValue* Troops = J.Find("troops");
	const JsonValue* Titans = J.Find("titans");
	if (!Troops || Troops->Size() == 0 || !Titans || Titans->Size() == 0)
	{
		Error = "kingdom/army.json: needs troops and titans";
		return false;
	}
	for (size_t I = 0; I < Troops->Size(); ++I)
	{
		const JsonValue& TJ = (*Troops)[I];
		TroopType T;
		T.id = TJ.GetString("id");
		T.name = TJ.GetString("name", T.id);
		T.unlockCastle = TJ.GetInt("unlockCastle", 1);
		if (TJ.Has("council"))
		{
			T.council = Out.FindBuilding(TJ.GetString("council"));
			if (T.council < 0)
			{
				Error = "troop '" + T.id + "': unknown council '" + TJ.GetString("council") + "'";
				return false;
			}
			T.councilLevel = std::max(1, TJ.GetInt("councilLevel", 1));
		}
		if (const JsonValue* Levels = TJ.Find("levels"))
		{
			for (size_t K = 0; K < Levels->Size(); ++K)
			{
				const JsonValue& LJ = (*Levels)[K];
				TroopLevelDef L;
				L.level = static_cast<int>(K) + 1;
				L.cost = LJ.GetInt64("cost", 0);
				L.trainSec = std::max<int64_t>(1, LJ.GetInt64("trainSec", 60));
				L.upgradeCost = LJ.GetInt64("upgradeCost", 0);
				L.upgradeSec = LJ.GetInt64("upgradeSec", 0);
				L.tier = LJ.GetInt("tier", 1);
				L.estimated = LJ.GetString("src", "est") != "wiki";
				T.levels.push_back(L);
			}
		}
		if (T.levels.empty())
		{
			Error = "troop '" + T.id + "': no levels";
			return false;
		}
		Out.troops.push_back(T);
	}
	for (size_t I = 0; I < Titans->Size(); ++I)
	{
		const JsonValue& TJ = (*Titans)[I];
		TitanType T;
		T.id = TJ.GetString("id");
		T.name = TJ.GetString("name", T.id);
		T.cls = TJ.GetString("class");
		T.stars = std::max(1, std::min(4, TJ.GetInt("stars", 1)));
		Out.titans.push_back(T);
	}
	if (const JsonValue* Altars = J.Find("altars"))
	{
		for (size_t I = 0; I < Altars->Size(); ++I)
		{
			const JsonValue& AJ = (*Altars)[I];
			AltarDef A;
			A.id = AJ.GetString("id");
			A.name = AJ.GetString("name", A.id);
			A.gems = AJ.GetInt64("gems", 0);
			A.cooldownSec = AJ.GetInt64("cooldownSec", 0);
			A.relics = AJ.GetBool("relics", false);
			if (const JsonValue* W = AJ.Find("weights"))
			{
				for (size_t K = 0; K < W->Size() && K < (A.relics ? 5u : 4u); ++K)
				{
					A.weights[K] = std::max<int64_t>(0, (*W)[K].AsInt64(0));
				}
			}
			Out.altars.push_back(A);
		}
	}
	if (const JsonValue* X = J.Find("titanXp"))
	{
		ReadInts(X->Find("next"), Out.titanXpNext);
		ReadInts(X->Find("battle"), Out.titanBattleXp);
		std::vector<int64_t> Pct;
		ReadInts(X->Find("starsPct"), Pct);
		for (size_t K = 0; K < Pct.size() && K < 4; ++K)
		{
			Out.titanStarsPct[K] = Pct[K];
		}
		Out.titanRecoveryFromLevel = X->GetInt("recoveryFromLevel", 10);
		Out.titanRecoverySecPerLevel = X->GetInt64("recoverySecPerLevel", 120);
	}
	if (Out.titanXpNext.empty() || Out.titanBattleXp.empty())
	{
		Error = "kingdom/army.json: titanXp needs next and battle";
		return false;
	}
	if (const JsonValue* A = J.Find("ascension"))
	{
		std::vector<int64_t> Shards;
		std::vector<int64_t> Food;
		ReadInts(A->Find("shards"), Shards);
		ReadInts(A->Find("food"), Food);
		for (size_t K = 0; K < 4; ++K)
		{
			Out.ascendShards[K] = K < Shards.size() ? Shards[K] : Out.ascendShards[K];
			Out.ascendFood[K] = K < Food.size() ? Food[K] : Out.ascendFood[K];
		}
		Out.ascendLevels = std::max(0, A->GetInt("levels", 10));
		Out.ascendRelicSlots = std::max(0, A->GetInt("relicSlots", 1));
	}
	if (const JsonValue* F = J.Find("fusion"))
	{
		Out.fusionCastle = F->GetInt("castle", 7);
		Out.fusionFoodPerStar = F->GetInt64("foodPerStar", 2000);
		Out.fusionXpPerStarSq = F->GetInt64("xpPerStarSq", 150);
		Out.fusionKeepXpPct = F->GetInt64("keepXpPct", 50);
	}
	if (const JsonValue* Spells = J.Find("spells"))
	{
		for (size_t I = 0; I < Spells->Size(); ++I)
		{
			const JsonValue& SJ = (*Spells)[I];
			SpellType S;
			S.id = SJ.GetString("id");
			S.name = SJ.GetString("name", S.id);
			S.forge = std::max(1, SJ.GetInt("forge", 1));
			S.craftGold = SJ.GetInt64("craftGold", 0);
			S.craftSec = std::max<int64_t>(1, SJ.GetInt64("craftSec", 60));
			Out.spells.push_back(S);
		}
	}
	if (const JsonValue* R = J.Find("spellResearch"))
	{
		Out.spellGoldBase = R->GetInt64("goldBase", 20000);
		Out.spellGoldGrowthPct = static_cast<int64_t>(R->GetDouble("goldGrowth", 1.55) * 100 + 0.5);
		Out.spellSecBase = R->GetInt64("secBase", 7200);
		Out.spellSecGrowthPct = static_cast<int64_t>(R->GetDouble("secGrowth", 1.45) * 100 + 0.5);
		Out.spellCraftPctPerLevel = R->GetInt64("craftPctPerLevel", 15);
	}
	Out.battleSpellKinds = std::max(0, J.GetInt("battleSpellKinds", 3));
	if (const JsonValue* R = J.Find("relics"))
	{
		// Values are percents in the data (decimals allowed) and tenths of a percent in memory.
		auto Tenths = [](const JsonValue& V) { return static_cast<int64_t>(V.AsDouble(0) * 10 + (V.AsDouble(0) < 0 ? -0.5 : 0.5)); };
		if (const JsonValue* Effects = R->Find("effects"))
		{
			for (size_t I = 0; I < Effects->Size(); ++I)
			{
				const JsonValue& EJ = (*Effects)[I];
				RelicEffectDef E;
				E.id = EJ.GetString("id");
				E.name = EJ.GetString("name", E.id);
				if (!ParseRelicStat(EJ.GetString("stat"), E.stat))
				{
					Error = "relic effect '" + E.id + "': unknown stat '" + EJ.GetString("stat") + "'";
					return false;
				}
				if (const JsonValue* B = EJ.Find("base"))
				{
					for (size_t K = 0; K < B->Size() && K < 5; ++K)
					{
						E.base[K] = Tenths((*B)[K]);
					}
				}
				if (const JsonValue* Cap = EJ.Find("cap"))
				{
					E.cap = Tenths(*Cap);
				}
				Out.relicEffects.push_back(E);
			}
		}
		if (const JsonValue* Kinds = R->Find("kinds"))
		{
			for (size_t I = 0; I < Kinds->Size(); ++I)
			{
				const JsonValue& KJ = (*Kinds)[I];
				RelicKind K;
				K.id = KJ.GetString("id");
				K.name = KJ.GetString("name", K.id);
				K.economy = KJ.GetString("type", "combat") == "economy";
				K.battles = std::max(0, KJ.GetInt("battles", 0));
				K.weight = std::max<int64_t>(0, KJ.GetInt64("weight", 10));
				if (const JsonValue* Effects = KJ.Find("effects"))
				{
					for (size_t E = 0; E < Effects->Size(); ++E)
					{
						int Found = -1;
						for (size_t D = 0; D < Out.relicEffects.size(); ++D)
						{
							Found = Out.relicEffects[D].id == (*Effects)[E].AsString() ? static_cast<int>(D) : Found;
						}
						if (Found < 0)
						{
							Error = "relic kind '" + K.id + "': unknown effect '" + (*Effects)[E].AsString() + "'";
							return false;
						}
						K.effects.push_back(Found);
					}
				}
				if (const JsonValue* Buildings = KJ.Find("buildings"))
				{
					for (size_t B = 0; B < Buildings->Size(); ++B)
					{
						const int Def = Out.FindBuilding((*Buildings)[B].AsString());
						if (Def < 0)
						{
							Error = "relic kind '" + K.id + "': unknown building '" + (*Buildings)[B].AsString() + "'";
							return false;
						}
						K.buildings.push_back(Def);
					}
				}
				if (K.effects.empty() || K.effects.size() > 3 || K.economy == K.buildings.empty())
				{
					Error = "relic kind '" + K.id + "' needs 1-3 effects, and buildings exactly when it is an economy relic";
					return false;
				}
				Out.relicKinds.push_back(K);
			}
		}
		auto Read5 = [R](const char* Key, std::array<int64_t, 5>& Dest)
		{
			if (const JsonValue* L = R->Find(Key))
			{
				for (size_t K = 0; K < L->Size() && K < 5; ++K)
				{
					Dest[K] = (*L)[K].AsInt64(0);
				}
			}
		};
		auto Read5i = [R](const char* Key, std::array<int, 5>& Dest)
		{
			if (const JsonValue* L = R->Find(Key))
			{
				for (size_t K = 0; K < L->Size() && K < 5; ++K)
				{
					Dest[K] = std::max(0, std::min(3, (*L)[K].AsInt(1)));
				}
			}
		};
		Read5("durability", Out.relicDurability);
		Read5("hours", Out.relicHours);
		Read5("enhancePct", Out.relicEnhancePct);
		Read5("repairFood", Out.relicRepairFood);
		Read5("dailyRarity", Out.dailyRelicRarity);
		Read5i("startEffects", Out.relicStartEffects);
		Read5i("maxEffects", Out.relicMaxEffects);
		if (const JsonValue* T = R->Find("repairPct"))
		{
			for (size_t A = 0; A < T->Size() && A < 5; ++A)
			{
				for (size_t B = 0; B < (*T)[A].Size() && B < 5; ++B)
				{
					Out.relicRepairPct[A][B] = std::max<int64_t>(0, (*T)[A][B].AsInt64(0));
				}
			}
		}
		if (const JsonValue* D = R->Find("dropRarity"))
		{
			for (size_t A = 0; A < D->Size(); ++A)
			{
				std::array<int64_t, 5> W{};
				for (size_t B = 0; B < (*D)[A].Size() && B < 5; ++B)
				{
					W[B] = std::max<int64_t>(0, (*D)[A][B].AsInt64(0));
				}
				Out.relicDropRarity.push_back(W);
			}
		}
		if (const JsonValue* L = R->Find("slotLevels"))
		{
			Out.relicSlotLevels.clear();
			for (size_t K = 0; K < L->Size(); ++K)
			{
				Out.relicSlotLevels.push_back((*L)[K].AsInt(1));
			}
		}
		Out.relicLevels = std::max(1, R->GetInt("levels", 4));
		Out.relicStepPct = std::max<int64_t>(0, R->GetInt64("stepPct", 15));
		Out.relicRepairCastle = R->GetInt("repairCastle", 8);
		Out.maxRelics = R->GetInt("maxRelics", 200);
		Out.dailyRelicPct = R->GetInt64("dailyRelicPct", 60);
		Out.relicDropMax = std::max(0, R->GetInt("dropMax", 3));
	}
	if (Out.relicKinds.empty())
	{
		Error = "kingdom/army.json: relics need kinds";
		return false;
	}
	if (const JsonValue* S = J.Find("skills"))
	{
		SkillRules& Sk = Out.skills;
		Sk.slots = std::max(1, S->GetInt("slots", Sk.slots));
		Sk.openPct = std::max(0, std::min(100, S->GetInt("openPct", Sk.openPct)));
		Sk.upgradePct = std::max(0, std::min(100, S->GetInt("upgradePct", Sk.upgradePct)));
		const JsonValue* Kinds = S->Find("kinds");
		for (size_t I = 0; Kinds && I < Kinds->Size(); ++I)
		{
			const JsonValue& KJ = (*Kinds)[I];
			SkillKind K;
			K.id = KJ.GetString("id");
			K.name = KJ.GetString("name", K.id);
			const JsonValue* Tiers = KJ.Find("tiers");
			if (K.id.empty() || !ParseRelicStat(KJ.GetString("stat"), K.stat) || !Tiers || Tiers->Size() != 3)
			{
				Error = "kingdom/army.json: skill '" + K.id + "' needs an id, a titan stat and three tiers";
				return false;
			}
			for (size_t T = 0; T < 3; ++T)
			{
				const double V = (*Tiers)[T].AsDouble(0);
				K.tiers[T] = static_cast<int64_t>(V * 10 + 0.5);
			}
			Sk.kinds.push_back(K);
		}
		if (const JsonValue* Classes = S->Find("classes"))
		{
			for (size_t I = 0; I < Classes->keys.size(); ++I)
			{
				std::vector<int64_t> W(Sk.kinds.size(), 0);
				const JsonValue& CJ = Classes->items[I];
				for (size_t K = 0; K < Sk.kinds.size(); ++K)
				{
					W[K] = std::max<int64_t>(0, CJ.GetInt64(Sk.kinds[K].id.c_str(), 0));
				}
				Sk.classWeights.emplace_back(Classes->keys[I], W);
			}
		}
	}
	Out.landTitanPctPerLevel = J.GetInt64("landTitanPctPerLevel", 1);
	Out.skipSecPerGem = std::max<int64_t>(1, J.GetInt64("skipSecPerGem", 60));
	Out.startArmy.assign(Out.troops.size(), 0);
	if (const JsonValue* Start = J.Find("start"))
	{
		if (const JsonValue* A = Start->Find("army"))
		{
			for (size_t I = 0; I < A->keys.size(); ++I)
			{
				const int T = Out.FindTroop(A->keys[I]);
				if (T < 0)
				{
					Error = "kingdom/army.json: start army has unknown troop '" + A->keys[I] + "'";
					return false;
				}
				Out.startArmy[static_cast<size_t>(T)] = A->items[I].AsInt64(0);
			}
		}
		if (const JsonValue* L = Start->Find("titans"))
		{
			for (size_t I = 0; I < L->Size(); ++I)
			{
				StartTitan S;
				S.def = Out.FindTitan((*L)[I].GetString("id"));
				S.level = (*L)[I].GetInt("level", 1);
				if (S.def < 0)
				{
					Error = "kingdom/army.json: start titan '" + (*L)[I].GetString("id") + "' is not in the roster";
					return false;
				}
				Out.startTitans.push_back(S);
			}
		}
	}
	return true;
}
} // namespace

const char* ToString(Res R)
{
	switch (R)
	{
	case Res::Gold: return "gold";
	case Res::Food: return "food";
	case Res::Gems: return "gems";
	case Res::Stones: return "stones";
	case Res::Shards: return "shards";
	case Res::Vp: return "vp";
	default: return "?";
	}
}

const char* ToString(RelicStat S)
{
	static const char* const Names[] = {"atkPct", "hpPct", "armorPct", "critPct", "piercePct", "troopAtkPct", "troopHpPct", "xpPct",
		"producePct", "storePct", "trainPct", "craftPct", "researchPct", "restPct"};
	const int I = static_cast<int>(S);
	return I >= 0 && I < kRelicStatCount ? Names[I] : "?";
}

bool ParseRelicStat(const std::string& S, RelicStat& Out)
{
	for (int I = 0; I < kRelicStatCount; ++I)
	{
		if (S == ToString(static_cast<RelicStat>(I)))
		{
			Out = static_cast<RelicStat>(I);
			return true;
		}
	}
	return false;
}

namespace
{
const char* const QuestEventNames[] = {"win", "threeStars", "raid", "collectGold", "collectFood", "train", "upgrade", "craft", "stones", "site", "mission"};
static_assert(sizeof(QuestEventNames) / sizeof(QuestEventNames[0]) == static_cast<size_t>(QuestEvent::Count), "a name for every quest event");
} // namespace

const char* QuestEventName(QuestEvent E)
{
	return E < QuestEvent::Count ? QuestEventNames[static_cast<int>(E)] : "";
}

bool ParseQuestEvent(const std::string& S, QuestEvent& Out)
{
	for (int I = 0; I < static_cast<int>(QuestEvent::Count); ++I)
	{
		if (S == QuestEventNames[I])
		{
			Out = static_cast<QuestEvent>(I);
			return true;
		}
	}
	return false;
}

bool ParseGoalCheck(const std::string& S, GoalCheck& Out)
{
	static const char* const Names[] = {"castle", "build", "level", "army", "troopLevel", "lands", "landLevel", "guard", "garrison", "titans", "titanLevel",
		"relics", "skills", "spells", "spellLevel", "alliance", "missions", "event", "power"};
	static_assert(sizeof(Names) / sizeof(Names[0]) == static_cast<size_t>(GoalCheck::Count), "a name for every goal check");
	for (int I = 0; I < static_cast<int>(GoalCheck::Count); ++I)
	{
		if (S == Names[I])
		{
			Out = static_cast<GoalCheck>(I);
			return true;
		}
	}
	return false;
}

bool ParseRes(const std::string& S, Res& Out)
{
	for (int I = 0; I < kResCount; ++I)
	{
		if (S == ToString(static_cast<Res>(I)))
		{
			Out = static_cast<Res>(I);
			return true;
		}
	}
	return false;
}

int BuildingDef::MaxCount(int CastleLevel) const
{
	int Count = 0;
	for (const auto& Step : counts)
	{
		if (CastleLevel >= Step.first)
		{
			Count = Step.second;
		}
	}
	return Count;
}

int KingdomContent::FindBuilding(const std::string& Id) const
{
	for (size_t I = 0; I < buildings.size(); ++I)
	{
		if (buildings[I].id == Id)
		{
			return static_cast<int>(I);
		}
	}
	return -1;
}

int KingdomContent::FindTroop(const std::string& Id) const
{
	for (size_t I = 0; I < troops.size(); ++I)
	{
		if (troops[I].id == Id)
		{
			return static_cast<int>(I);
		}
	}
	return -1;
}

int KingdomContent::FindSpell(const std::string& Id) const
{
	for (size_t I = 0; I < spells.size(); ++I)
	{
		if (spells[I].id == Id)
		{
			return static_cast<int>(I);
		}
	}
	return -1;
}

int KingdomContent::FindRelicKind(const std::string& Id) const
{
	for (size_t I = 0; I < relicKinds.size(); ++I)
	{
		if (relicKinds[I].id == Id)
		{
			return static_cast<int>(I);
		}
	}
	return -1;
}

int KingdomContent::FindTitan(const std::string& Id) const
{
	for (size_t I = 0; I < titans.size(); ++I)
	{
		if (titans[I].id == Id)
		{
			return static_cast<int>(I);
		}
	}
	return -1;
}

bool LoadKingdomContent(IContentSource& Source, KingdomContent& Out, std::string& Error)
{
	Out = KingdomContent();
	StateWriter HashInput;
	JsonValue Rules;
	JsonValue Defs;
	JsonValue Army;
	if (!ReadJson(Source, "kingdom/kingdom.json", Rules, Error, HashInput) || !ReadJson(Source, "kingdom/buildings.json", Defs, Error, HashInput) ||
		!ReadJson(Source, "kingdom/army.json", Army, Error, HashInput))
	{
		return false;
	}

	// Buildings first (requirements refer to them by id, so ids are resolved in a second pass).
	const JsonValue* List = Defs.Find("buildings");
	if (!List || !List->IsArray() || List->Size() == 0)
	{
		Error = "kingdom/buildings.json: no buildings";
		return false;
	}
	for (size_t I = 0; I < List->Size(); ++I)
	{
		const JsonValue& J = (*List)[I];
		BuildingDef D;
		D.id = J.GetString("id");
		D.name = J.GetString("name", D.id);
		D.category = J.GetString("category", "other");
		if (!ParseRole(J.GetString("role", "other"), D.role))
		{
			Error = "building '" + D.id + "': unknown role";
			return false;
		}
		if (J.Has("resource") && !ParseRes(J.GetString("resource"), D.resource))
		{
			Error = "building '" + D.id + "': unknown resource";
			return false;
		}
		if (const JsonValue* Size = J.Find("size"))
		{
			if (Size->Size() == 2)
			{
				D.width = (*Size)[0].AsInt(2);
				D.height = (*Size)[1].AsInt(2);
			}
		}
		D.unlockCastle = J.GetInt("unlockCastle", 1);
		D.instant = J.GetBool("instant", false);
		if (const JsonValue* Counts = J.Find("counts"))
		{
			for (size_t K = 0; K < Counts->Size(); ++K)
			{
				const JsonValue& P = (*Counts)[K];
				if (P.Size() == 2)
				{
					D.counts.emplace_back(P[0].AsInt(1), P[1].AsInt(1));
				}
			}
		}
		Out.buildings.push_back(D);
	}
	for (size_t I = 0; I < List->Size(); ++I)
	{
		const JsonValue& J = (*List)[I];
		BuildingDef& D = Out.buildings[I];
		const JsonValue* Levels = J.Find("levels");
		if (!Levels || !Levels->IsArray() || Levels->Size() == 0)
		{
			Error = "building '" + D.id + "': no levels";
			return false;
		}
		for (size_t K = 0; K < Levels->Size(); ++K)
		{
			const JsonValue& LJ = (*Levels)[K];
			LevelDef L;
			L.level = static_cast<int>(K) + 1;
			ReadCost(LJ.Find("cost"), L.cost);
			L.timeSec = LJ.GetInt64("timeSec", 0);
			L.skipGems = LJ.GetInt64("skipGems", 0);
			L.castle = LJ.GetInt("castle", 1);
			L.expansion = LJ.GetInt("expansion", 0);
			L.produce = LJ.GetInt64("produce", 0);
			L.store = LJ.GetInt64("store", 0);
			L.capacity = LJ.GetInt64("capacity", 0);
			L.protect = LJ.GetInt64("protect", 0);
			L.stones = LJ.GetInt64("stones", 0);
			L.builders = LJ.GetInt64("builders", 0);
			L.landCapacity = LJ.GetInt64("landCapacity", 0);
			L.queue = LJ.GetInt64("queue", 0);
			L.troops = LJ.GetInt64("troops", 0);
			L.slots = LJ.GetInt64("slots", 0);
			L.maxTroopLevel = LJ.GetInt64("maxTroopLevel", 0);
			L.survival = LJ.GetInt64("survival", 0);
			L.titans = LJ.GetInt64("titans", 0);
			L.spellSlots = LJ.GetInt64("spellSlots", 0);
			L.maxSpellLevel = LJ.GetInt64("maxSpellLevel", 0);
			L.reinforcements = LJ.GetInt64("reinforcements", 0);
			L.defenseSlots = LJ.GetInt64("defenseSlots", 0);
			L.guardTier = LJ.GetInt64("guardTier", 0);
			L.defenseBonusPct = LJ.GetInt64("defenseBonusPct", 0);
			L.estimated = LJ.GetString("src", "est") != "wiki";
			if (const JsonValue* Reqs = LJ.Find("requires"))
			{
				for (size_t R = 0; R < Reqs->Size(); ++R)
				{
					Requirement Q;
					const std::string Id = (*Reqs)[R].GetString("building");
					Q.building = Out.FindBuilding(Id);
					Q.level = (*Reqs)[R].GetInt("level", 1);
					Q.count = (*Reqs)[R].GetInt("count", 1);
					if (Q.building < 0)
					{
						Error = "building '" + D.id + "': requirement on unknown building '" + Id + "'";
						return false;
					}
					L.needs.push_back(Q);
				}
			}
			D.levels.push_back(L);
		}
	}
	Out.castle = Out.FindBuilding("castle");
	Out.builderYard = Out.FindBuilding("builders_yard");
	Out.portal = Out.FindBuilding("portal");
	Out.barracks = Out.FindBuilding("barracks");
	Out.armyCamp = Out.FindBuilding("army_camp");
	Out.armoury = Out.FindBuilding("armoury");
	Out.medicalSchool = Out.FindBuilding("medical_school");
	Out.hallOfTitans = Out.FindBuilding("hall_of_titans");
	Out.garrison = Out.FindBuilding("garrison");
	Out.relicTemple = Out.FindBuilding("relic_temple");
	Out.spellForge = Out.FindBuilding("spell_forge");
	Out.arcaneTower = Out.FindBuilding("arcane_tower");
	Out.allianceCamp = Out.FindBuilding("alliance_camp");
	if (Out.castle < 0)
	{
		Error = "kingdom content has no castle";
		return false;
	}

	if (const JsonValue* Grid = Rules.Find("grid"))
	{
		Out.gridW = Grid->GetInt("width", 40);
		Out.gridH = Grid->GetInt("height", 40);
	}
	if (const JsonValue* Exp = Rules.Find("expansions"))
	{
		for (size_t I = 0; I < Exp->Size(); ++I)
		{
			const JsonValue& J = (*Exp)[I];
			ExpansionDef E;
			E.costGold = J.GetInt64("costGold", 0);
			E.castle = J.GetInt("castle", 1);
			if (const JsonValue* Area = J.Find("area"))
			{
				if (Area->Size() == 4)
				{
					E.x0 = (*Area)[0].AsInt(0);
					E.y0 = (*Area)[1].AsInt(0);
					E.x1 = (*Area)[2].AsInt(Out.gridW);
					E.y1 = (*Area)[3].AsInt(Out.gridH);
				}
			}
			Out.expansions.push_back(E);
		}
	}
	if (Out.expansions.empty())
	{
		Out.expansions.push_back({0, 1, 0, 0, Out.gridW, Out.gridH});
	}
	if (const JsonValue* Portal = Rules.Find("portal"))
	{
		Out.stoneRegenSec = Portal->GetInt64("regenSec", 420);
		Out.raidStones = Portal->GetInt64("raidCost", 3);
		Out.captureStones = Portal->GetInt64("captureCost", 6);
		Out.gemsPerStone = Portal->GetInt64("gemsPerStone", 4);
	}
	if (const JsonValue* Lands = Rules.Find("lands"))
	{
		Out.maxLands = Lands->GetInt("maxLands", 30);
		Out.worldSize = Lands->GetInt("worldSize", 6);
		if (const JsonValue* Slots = Lands->Find("extraSlotGems"))
		{
			for (size_t I = 0; I < Slots->Size(); ++I)
			{
				Out.extraSlotGems.push_back((*Slots)[I].AsInt64(0));
			}
		}
		if (const JsonValue* Fill = Lands->Find("fillHours"))
		{
			for (size_t I = 0; I < Fill->Size(); ++I)
			{
				Out.landFillHours.push_back((*Fill)[I].AsInt64(6));
			}
		}
		if (const JsonValue* Loot = Lands->Find("raidLoot"))
		{
			for (size_t I = 0; I < Loot->Size(); ++I)
			{
				Out.raidLoot.push_back((*Loot)[I].AsInt64(0));
			}
		}
		ReadInts(Lands->Find("victoryVp"), Out.victoryVp);
		ReadInts(Lands->Find("upgradeGold"), Out.landUpgradeGold);
		ReadInts(Lands->Find("upgradeHours"), Out.landUpgradeSec);
		for (int64_t& Sec : Out.landUpgradeSec)
		{
			Sec *= 3600;
		}
		if (const JsonValue* Types = Lands->Find("types"))
		{
			for (size_t I = 0; I < Types->Size(); ++I)
			{
				const JsonValue& J = (*Types)[I];
				LandTypeDef T;
				T.id = J.GetString("id");
				T.name = J.GetString("name", T.id);
				ParseRes(J.GetString("resource", "gold"), T.resource);
				ReadInts(J.Find("produce"), T.produce);
				T.periodHours = std::max<int64_t>(1, J.GetInt64("periodHours", 1));
				ReadInts(J.Find("fillHours"), T.fillHours);
				ReadInts(J.Find("raidLoot"), T.raidLoot);
				T.castle = std::max(1, J.GetInt("castle", 1));
				T.weight = std::max<int64_t>(0, J.GetInt64("weight", 1));
				if (const JsonValue* Swap = J.Find("swap"))
				{
					for (size_t K = 0; K < Swap->keys.size(); ++K)
					{
						T.swap.emplace_back(Swap->keys[K], Swap->items[K].AsString());
					}
				}
				if (T.produce.empty())
				{
					Error = "kingdom/kingdom.json: land type '" + T.id + "' needs produce";
					return false;
				}
				Out.landTypes.push_back(T);
			}
		}
	}
	if (Out.landTypes.empty() || Out.landFillHours.empty() || Out.raidLoot.empty())
	{
		Error = "kingdom/kingdom.json: lands need types, fillHours and raidLoot";
		return false;
	}
	if (const JsonValue* Battles = Rules.Find("battles"))
	{
		for (size_t I = 0; I < Battles->Size(); ++I)
		{
			const JsonValue& J = (*Battles)[I];
			BattleRowDef B;
			B.ai = J.GetString("ai", "veteran");
			B.tier = std::max(1, J.GetInt("tier", 1));
			if (const JsonValue* Squads = J.Find("squads"))
			{
				for (size_t K = 0; K < Squads->Size(); ++K)
				{
					B.squads.push_back((*Squads)[K].AsString());
				}
			}
			if (const JsonValue* Titan = J.Find("titan"))
			{
				B.titanLevel = std::max(1, Titan->GetInt("level", 1));
			}
			Out.battles.push_back(B);
		}
	}
	if (const JsonValue* Raids = Rules.Find("raids"))
	{
		RaidRules& R = Out.raids;
		R.castle = std::max(1, Raids->GetInt("castle", R.castle));
		R.firstSec = Raids->GetInt64("firstHours", R.firstSec / 3600) * 3600;
		if (const JsonValue* Every = Raids->Find("everyHours"))
		{
			if (Every->Size() == 2)
			{
				R.everyMinSec = std::max<int64_t>(1, (*Every)[0].AsInt64(6)) * 3600;
				R.everyMaxSec = std::max<int64_t>(R.everyMinSec, (*Every)[1].AsInt64(10) * 3600);
			}
		}
		R.awaySec = Raids->GetInt64("awayHours", R.awaySec / 3600) * 3600;
		R.cityPct = Raids->GetInt64("cityPct", R.cityPct);
		R.cityShieldSec = Raids->GetInt64("cityShieldHours", R.cityShieldSec / 3600) * 3600;
		R.landShieldSec = Raids->GetInt64("landShieldHours", R.landShieldSec / 3600) * 3600;
		R.stealPct = Raids->GetInt64("stealPct", R.stealPct);
		R.vpPct = Raids->GetInt64("vpPct", R.vpPct);
		R.landStealPct = Raids->GetInt64("landStealPct", R.landStealPct);
		R.defenseVpPct = Raids->GetInt64("defenseVpPct", R.defenseVpPct);
		ReadInts(Raids->Find("citySquads"), R.citySquads);
		ReadInts(Raids->Find("titanLevels"), R.titanLevels);
		if (const JsonValue* Guards = Raids->Find("guards"))
		{
			for (size_t K = 0; K < Guards->Size(); ++K)
			{
				R.guards.push_back((*Guards)[K].AsString());
			}
		}
		R.defaultSlots = std::max(1, Raids->GetInt("noGarrisonSlots", R.defaultSlots));
		R.defaultGuardTier = std::max(1, Raids->GetInt("noGarrisonTier", R.defaultGuardTier));
		R.defenderAi = Raids->GetString("defenderAi", R.defenderAi);
		if (const JsonValue* Odds = Raids->Find("odds"))
		{
			R.titanPct = std::max<int64_t>(0, Odds->GetInt64("titanPct", R.titanPct));
			if (const JsonValue* Ai = Odds->Find("aiPct"))
			{
				for (size_t K = 0; K < Ai->keys.size(); ++K)
				{
					R.aiPct.emplace_back(Ai->keys[K], std::max<int64_t>(1, Ai->items[K].AsInt64(100)));
				}
			}
			R.oddsKQuarters = std::max(1, std::min(32, static_cast<int>(Odds->GetDouble("k", R.oddsKQuarters / 4.0) * 4.0 + 0.5)));
			R.oddsC = std::max<int64_t>(1, Odds->GetInt64("c", R.oddsC));
			R.threeStarRatio = Odds->GetInt64("threeStars", R.threeStarRatio);
			R.twoStarRatio = Odds->GetInt64("twoStars", R.twoStarRatio);
		}
		R.log = std::max(1, Raids->GetInt("log", R.log));
	}
	if (const JsonValue* Season = Rules.Find("seasons"))
	{
		Out.seasonEpoch = Season->GetInt64("epoch", 0);
		Out.seasonSec = std::max<int64_t>(3600, Season->GetInt64("days", 7) * 86400);
		if (const JsonValue* Leagues = Season->Find("leagues"))
		{
			for (size_t I = 0; I < Leagues->Size(); ++I)
			{
				const JsonValue& J = (*Leagues)[I];
				LeagueDef L;
				L.id = J.GetString("id");
				L.name = J.GetString("name", L.id);
				L.vp = std::max<int64_t>(1, J.GetInt64("vp", 1));
				L.gems = J.GetInt64("gems", 0);
				L.stones = J.GetInt64("stones", 0);
				if (!Out.leagues.empty() && L.vp <= Out.leagues.back().vp)
				{
					Error = "kingdom/kingdom.json: leagues must need more VP one after another";
					return false;
				}
				Out.leagues.push_back(L);
			}
		}
	}
	if (const JsonValue* Rivals = Rules.Find("rivals"))
	{
		RivalRules& R = Out.rivals;
		if (const JsonValue* Lords = Rivals->Find("lords"))
		{
			for (size_t I = 0; I < Lords->Size(); ++I)
			{
				const JsonValue& J = (*Lords)[I];
				R.lords.push_back({J.GetString("name"), J.GetString("realm")});
			}
		}
		std::vector<int64_t> Offsets;
		ReadInts(Rivals->Find("castleOffsets"), Offsets);
		for (const int64_t O : Offsets)
		{
			R.castleOffsets.push_back(static_cast<int>(O));
		}
		R.castle = std::max(1, Rivals->GetInt("castle", R.castle));
		if (const JsonValue* Lands = Rivals->Find("lands"))
		{
			R.landsBase = std::max(1, Lands->GetInt("base", R.landsBase));
			R.landsPerCastle = std::max(1, Lands->GetInt("perCastle", R.landsPerCastle));
			R.landsMax = std::max(R.landsBase, Lands->GetInt("max", R.landsMax));
		}
		R.regrowSec = std::max<int64_t>(1, Rivals->GetInt64("regrowHours", R.regrowSec / 3600)) * 3600;
		R.shieldSec = std::max<int64_t>(0, Rivals->GetInt64("shieldHours", R.shieldSec / 3600)) * 3600;
		R.lootPct = std::max<int64_t>(0, Rivals->GetInt64("lootPct", R.lootPct));
		R.vpPct = std::max<int64_t>(0, Rivals->GetInt64("vpPct", R.vpPct));
		ReadInts(Rivals->Find("titanLevels"), R.titanLevels);
		if (R.lords.size() < R.castleOffsets.size())
		{
			Error = "kingdom/kingdom.json: rivals need at least one lord per castle offset";
			return false;
		}
	}
	if (const JsonValue* Sites = Rules.Find("sites"))
	{
		SiteRules& S = Out.sites;
		S.cooldownSec = std::max<int64_t>(0, Sites->GetInt64("cooldownHours", S.cooldownSec / 3600)) * 3600;
		S.stones = std::max<int64_t>(0, Sites->GetInt64("stones", S.stones));
		if (const JsonValue* Pct = Sites->Find("shardPct"))
		{
			for (size_t I = 0; I < Pct->Size() && I < S.shardPct.size(); ++I)
			{
				S.shardPct[I] = std::max<int64_t>(0, std::min<int64_t>(100, (*Pct)[I].AsInt64(0)));
			}
		}
		if (const JsonValue* SiteList = Sites->Find("list"))
		{
			for (size_t I = 0; I < SiteList->Size(); ++I)
			{
				const JsonValue& J = (*SiteList)[I];
				SiteDef D;
				D.id = J.GetString("id");
				D.name = J.GetString("name", D.id);
				D.arena = J.GetString("kind", "monument") == "arena";
				D.level = std::max(1, std::min(static_cast<int>(Out.battles.size()), J.GetInt("level", 1)));
				D.castle = std::max(1, J.GetInt("castle", 1));
				D.ai = J.GetString("ai");
				D.map = J.GetString("map");
				D.shards = std::max<int64_t>(0, J.GetInt64("shards", 0));
				D.xpPct = std::max<int64_t>(0, J.GetInt64("xpPct", 100));
				D.resetGems = std::max<int64_t>(0, J.GetInt64("resetGems", 0));
				if (D.id.empty() || Out.battles.empty())
				{
					Error = "kingdom/kingdom.json: sites need an id and the battles table";
					return false;
				}
				Out.sites.list.push_back(D);
			}
		}
	}
	if (const JsonValue* Camp = Rules.Find("campaigns"))
	{
		CampaignRules& G = Out.campaigns;
		G.stones = std::max<int64_t>(0, Camp->GetInt64("stones", G.stones));
		const std::string Guild = Camp->GetString("guild");
		G.guild = Guild.empty() ? -1 : Out.FindBuilding(Guild);
		if (!Guild.empty() && G.guild < 0)
		{
			Error = "kingdom/kingdom.json: campaigns.guild names an unknown building";
			return false;
		}
		if (const JsonValue* Acts = Camp->Find("acts"))
		{
			for (size_t I = 0; I < Acts->Size(); ++I)
			{
				const JsonValue& AJ = (*Acts)[I];
				ActDef A;
				A.id = AJ.GetString("id");
				A.castle = std::max(1, AJ.GetInt("castle", 1));
				if (const JsonValue* Missions = AJ.Find("missions"))
				{
					for (size_t M = 0; M < Missions->Size(); ++M)
					{
						const JsonValue& MJ = (*Missions)[M];
						MissionDef D;
						D.id = MJ.GetString("id");
						D.level = std::max(1, std::min(static_cast<int>(Out.battles.size()), MJ.GetInt("level", 1)));
						ReadCost(MJ.Find("first"), D.first);
						A.missions.push_back(D);
					}
				}
				if (const JsonValue* Chests = AJ.Find("chests"))
				{
					for (size_t K = 0; K < Chests->Size(); ++K)
					{
						const JsonValue& CJ = (*Chests)[K];
						ChestDef D;
						D.stars = std::max(1, CJ.GetInt("stars", 1));
						ReadCost(&CJ, D.res);
						D.relic = std::max(0, std::min(5, CJ.GetInt("relic", 0)));
						D.titan = std::max(0, std::min(4, CJ.GetInt("titan", 0)));
						A.chests.push_back(D);
					}
				}
				// Missions are stored as one byte of stars each and chests as bits of a 32-bit mask; Heroic missions and
				// chests are numbered from 50.
				if (A.id.empty() || A.missions.empty() || A.missions.size() > 50 || A.chests.size() > 32 || Out.battles.empty())
				{
					Error = "kingdom/kingdom.json: a campaign act needs an id, 1-50 missions, at most 32 chests and the battles table";
					return false;
				}
				G.acts.push_back(A);
			}
		}
		if (const JsonValue* SweepJ = Camp->Find("sweep"))
		{
			G.sweepFood = std::max<int64_t>(0, SweepJ->GetInt64("food", 0));
			G.scrollsStart = std::max<int64_t>(0, SweepJ->GetInt64("start", 0));
			G.scrollsMax = std::max<int64_t>(G.scrollsStart, SweepJ->GetInt64("max", G.scrollsStart));
		}
		if (const JsonValue* HeroJ = Camp->Find("heroic"))
		{
			HeroicRules& H = G.heroic;
			H.castle = std::max(0, std::min(100, HeroJ->GetInt("castle", 0)));
			H.level = std::max(0, std::min(static_cast<int>(Out.battles.size()), HeroJ->GetInt("level", 0)));
			H.firstPct = std::max(0, std::min(1000, HeroJ->GetInt("first", 100)));
			H.lootPct = std::max(0, std::min(1000, HeroJ->GetInt("loot", 100)));
			H.relicUp = std::max(0, std::min(4, HeroJ->GetInt("relicUp", 0)));
			H.chestPct = std::max(0, std::min(1000, HeroJ->GetInt("chest", 100)));
		}
		if (const JsonValue* LootJ = Camp->Find("loot"))
		{
			for (size_t I = 0; I < LootJ->Size(); ++I)
			{
				const JsonValue& EJ = (*LootJ)[I];
				MissionLoot L;
				L.weight = std::max(0, EJ.GetInt("weight", 0));
				ReadCost(&EJ, L.res);
				L.relic = std::max(0, std::min(5, EJ.GetInt("relic", 0)));
				if (L.weight > 0)
				{
					G.loot.push_back(L);
				}
			}
		}
	}
	// Daily and weekly quests share a shape.
	const auto ReadQuests = [&](const JsonValue* Qj, QuestRules& Q, const char* What) {
		if (!Qj)
		{
			return true;
		}
		Q.count = std::max(0, Qj->GetInt("count", Q.count));
		Q.rerolls = std::max(0, Qj->GetInt("rerolls", Q.rerolls));
		// (from Castle level, value) pairs, ascending by level.
		const auto ReadSteps = [](const JsonValue* StepsJ, std::vector<std::pair<int, int64_t>>& Steps) {
			for (size_t I = 0; StepsJ && I < StepsJ->Size(); ++I)
			{
				const JsonValue& Step = (*StepsJ)[I];
				if (Step.Size() == 2)
				{
					Steps.emplace_back(Step[0].AsInt(1), Step[1].AsInt64(0));
				}
			}
		};
		if (const JsonValue* Chest = Qj->Find("chest"))
		{
			ReadCost(Chest, Q.chest);
			Q.chestScrolls = std::max<int64_t>(0, Chest->GetInt64("scrolls", 0));
			std::vector<std::pair<int, int64_t>> Relic;
			ReadSteps(Chest->Find("relic"), Relic);
			for (const std::pair<int, int64_t>& R : Relic)
			{
				Q.chestRelic.emplace_back(R.first, static_cast<int>(std::max<int64_t>(0, std::min<int64_t>(5, R.second))));
			}
		}
		if (const JsonValue* Pool = Qj->Find("pool"))
		{
			for (size_t I = 0; I < Pool->Size(); ++I)
			{
				const JsonValue& J = (*Pool)[I];
				QuestDef D;
				D.id = J.GetString("id");
				D.castle = std::max(1, J.GetInt("castle", 1));
				const std::string Needs = J.GetString("needs");
				D.building = Needs.empty() ? -1 : Out.FindBuilding(Needs);
				ReadSteps(J.Find("target"), D.target);
				ReadCost(J.Find("reward"), D.reward);
				if (D.id.empty() || !ParseQuestEvent(J.GetString("event"), D.event) || D.target.empty() || (!Needs.empty() && D.building < 0))
				{
					Error = std::string("kingdom/kingdom.json: ") + What + " quest " + D.id + " needs an id, a known event and building, and targets";
					return false;
				}
				Q.pool.push_back(D);
			}
		}
		return true;
	};
	if (!ReadQuests(Rules.Find("dailyQuests"), Out.quests, "daily") || !ReadQuests(Rules.Find("weeklyQuests"), Out.weekly, "weekly"))
	{
		return false;
	}
	if (const JsonValue* Al = Rules.Find("alliances"))
	{
		AllianceRules& A = Out.alliances;
		A.joinVp = std::max<int64_t>(0, Al->GetInt64("joinVp", A.joinVp));
		A.joinVpMax = std::max(A.joinVp, Al->GetInt64("joinVpMax", A.joinVpMax));
		A.createGems = std::max<int64_t>(0, Al->GetInt64("createGems", A.createGems));
		if (const JsonValue* M = Al->Find("members"))
		{
			if (M->Size() == 2)
			{
				A.minMembers = std::max(1, (*M)[0].AsInt(A.minMembers));
				A.maxMembers = std::max(A.minMembers, (*M)[1].AsInt(A.maxMembers));
			}
		}
		A.groupSize = std::max(2, Al->GetInt("groupSize", A.groupSize));
		A.promote = std::max(0, std::min(A.groupSize / 2, Al->GetInt("promote", A.promote)));
		A.memberSec = std::max<int64_t>(0, Al->GetInt64("memberHours", A.memberSec / 3600) * 3600);
		A.recruitSec = std::max<int64_t>(60, Al->GetInt64("recruitHours", A.recruitSec / 3600) * 3600);
		A.recruits = std::max(0, std::min(A.maxMembers - 1, Al->GetInt("recruits", A.recruits)));
		A.placeStepPct = std::max(0, Al->GetInt("placeStepPct", A.placeStepPct));
		A.minPlacePct = std::max(0, std::min(100, Al->GetInt("minPlacePct", A.minPlacePct)));
		A.reinforceCooldownSec = std::max<int64_t>(0, Al->GetInt64("reinforceCooldownSec", A.reinforceCooldownSec));
		A.reinforceArriveSec = std::max<int64_t>(0, Al->GetInt64("reinforceArriveSec", A.reinforceArriveSec));
		if (const JsonValue* G = Al->Find("crystalGems"))
		{
			for (size_t I = 0; I < G->Size(); ++I)
			{
				A.crystalGems.push_back(std::max<int64_t>(0, (*G)[I].AsInt64(0)));
			}
		}
		if (const JsonValue* Leagues = Al->Find("leagues"))
		{
			for (size_t I = 0; I < Leagues->Size(); ++I)
			{
				const JsonValue& J = (*Leagues)[I];
				AllianceLeagueDef L;
				L.id = J.GetString("id");
				L.name = J.GetString("name", L.id);
				if (const JsonValue* V = J.Find("memberVp"))
				{
					if (V->Size() == 2)
					{
						L.memberVpMin = std::max<int64_t>(1, (*V)[0].AsInt64(1));
						L.memberVpMax = std::max(L.memberVpMin, (*V)[1].AsInt64(1));
					}
				}
				L.gems = std::max<int64_t>(0, J.GetInt64("gems", 0));
				L.stones = std::max<int64_t>(0, J.GetInt64("stones", 0));
				L.divine = std::max<int64_t>(0, J.GetInt64("divine", 0));
				L.crystal = std::max(0, std::min(4, J.GetInt("crystal", 0)));
				if (const JsonValue* P = J.Find("titanPct"))
				{
					if (P->Size() == 2)
					{
						L.titanPctFirst = std::max(0, std::min(100, (*P)[0].AsInt(0)));
						L.titanPctLast = std::max(0, std::min(100, (*P)[1].AsInt(0)));
					}
				}
				L.troopLevel = std::max(1, J.GetInt("troopLevel", 1));
				if (L.memberVpMin <= 0)
				{
					Error = "kingdom/kingdom.json: alliance league '" + L.id + "' needs memberVp";
					return false;
				}
				A.leagues.push_back(L);
			}
		}
		if (const JsonValue* Names = Al->Find("names"))
		{
			for (size_t I = 0; I < Names->Size(); ++I)
			{
				const JsonValue& N = (*Names)[I];
				if (N.Size() == 2)
				{
					A.names.emplace_back(N[0].AsString(), N[1].AsString());
				}
			}
		}
		if (const JsonValue* Members = Al->Find("memberNames"))
		{
			for (size_t I = 0; I < Members->Size(); ++I)
			{
				A.memberNames.push_back((*Members)[I].AsString());
			}
		}
		if (A.leagues.empty() || A.names.empty() || static_cast<int>(A.memberNames.size()) < A.maxMembers)
		{
			Error = "kingdom/kingdom.json: alliances need leagues, names and at least as many member names as members";
			return false;
		}
	}
	if (const JsonValue* Gems = Rules.Find("gemsForResources"))
	{
		for (size_t I = 0; I < Gems->Size(); ++I)
		{
			const JsonValue& P = (*Gems)[I];
			if (P.Size() == 2)
			{
				Out.gemsForResources.emplace_back(P[0].AsInt64(0), P[1].AsInt64(0));
			}
		}
	}
	if (const JsonValue* Start = Rules.Find("start"))
	{
		ReadCost(Start->Find("resources"), Out.startResources);
		if (const JsonValue* Placed = Start->Find("buildings"))
		{
			for (size_t I = 0; I < Placed->Size(); ++I)
			{
				const JsonValue& J = (*Placed)[I];
				StartBuilding S;
				S.building = Out.FindBuilding(J.GetString("id"));
				S.level = J.GetInt("level", 1);
				S.x = J.GetInt("x", 0);
				S.y = J.GetInt("y", 0);
				if (S.building < 0)
				{
					Error = "kingdom start: unknown building '" + J.GetString("id") + "'";
					return false;
				}
				Out.startBuildings.push_back(S);
			}
		}
	}
	if (!LoadArmy(Army, Out, Error))
	{
		return false;
	}
	// The main line of goals names troops, so it comes after the army.
	if (const JsonValue* Gj = Rules.Find("goals"))
	{
		for (size_t I = 0; I < Gj->Size(); ++I)
		{
			const JsonValue& J = (*Gj)[I];
			GoalDef D;
			D.id = J.GetString("id");
			const std::string Building = J.GetString("building");
			const std::string Troop = J.GetString("troop");
			D.building = Building.empty() ? -1 : Out.FindBuilding(Building);
			D.troop = Troop.empty() ? -1 : Out.FindTroop(Troop);
			D.n = std::max<int64_t>(1, J.GetInt64("n", 1));
			ReadCost(J.Find("reward"), D.reward);
			const bool bCheck = ParseGoalCheck(J.GetString("check"), D.check);
			const bool bEvent = !bCheck || D.check != GoalCheck::Event || ParseQuestEvent(J.GetString("event"), D.event);
			const bool bNeedsBuilding = D.check == GoalCheck::Build || D.check == GoalCheck::Level;
			const bool bNeedsTroop = D.check == GoalCheck::TroopLevel;
			if (D.id.empty() || !bCheck || !bEvent || bNeedsBuilding != (D.building >= 0) || bNeedsTroop != (D.troop >= 0) ||
				(!Building.empty() && D.building < 0) || (!Troop.empty() && D.troop < 0))
			{
				Error = "kingdom/kingdom.json: goal " + D.id + " needs an id, a known check (and event) and the building or troop it names";
				return false;
			}
			Out.goals.push_back(D);
		}
	}
	if (const JsonValue* Aj = Rules.Find("achievements"))
	{
		for (size_t I = 0; I < Aj->Size(); ++I)
		{
			const JsonValue& J = (*Aj)[I];
			AchievementDef D;
			D.id = J.GetString("id");
			const std::string Building = J.GetString("building");
			const std::string Troop = J.GetString("troop");
			D.building = Building.empty() ? -1 : Out.FindBuilding(Building);
			D.troop = Troop.empty() ? -1 : Out.FindTroop(Troop);
			bool bOk = !D.id.empty() && ParseGoalCheck(J.GetString("check", "event"), D.check);
			bOk = bOk && (D.check != GoalCheck::Event || ParseQuestEvent(J.GetString("event"), D.event));
			bOk = bOk && ((D.check == GoalCheck::Build || D.check == GoalCheck::Level) == (D.building >= 0));
			bOk = bOk && ((D.check == GoalCheck::TroopLevel) == (D.troop >= 0));
			if (const JsonValue* T = J.Find("tiers"))
			{
				for (size_t K = 0; K < T->Size(); ++K)
				{
					const JsonValue& Tier = (*T)[K];
					const int64_t N = Tier.Size() >= 1 ? Tier[0].AsInt64() : 0;
					const int64_t Gems = Tier.Size() >= 2 ? Tier[1].AsInt64() : 0;
					bOk = bOk && N >= 1 && Gems >= 0 && (D.tiers.empty() || N > D.tiers.back().first);
					D.tiers.emplace_back(N, Gems);
				}
			}
			bOk = bOk && !D.tiers.empty() && D.tiers.size() <= 3;
			for (const AchievementDef& Other : Out.achievements)
			{
				bOk = bOk && Other.id != D.id;
			}
			if (!bOk)
			{
				Error = "kingdom/kingdom.json: achievement " + D.id + " needs a unique id, a known check (and event or building) and 1-3 rising tiers";
				return false;
			}
			Out.achievements.push_back(D);
		}
	}
	if (const JsonValue* Cal = Rules.Find("calendar"))
	{
		const auto ReadDays = [&Out](const JsonValue* DayList, std::vector<CalendarDay>& Days) {
			for (size_t I = 0; DayList && I < DayList->Size(); ++I)
			{
				const JsonValue& J = (*DayList)[I];
				CalendarDay D;
				ReadCost(J.Find("res"), D.res);
				const std::string Titan = J.GetString("titan");
				D.titan = Titan.empty() ? -1 : Out.FindTitan(Titan);
				bool bOk = Titan.empty() || D.titan >= 0;
				if (const JsonValue* Choice = J.Find("choice"))
				{
					for (size_t K = 0; K < Choice->Size(); ++K)
					{
						const int T = Out.FindTitan((*Choice)[K].AsString());
						bOk = bOk && T >= 0;
						D.choice.push_back(T);
					}
				}
				D.relic = J.GetInt("relic", 0);
				bOk = bOk && D.relic >= 0 && D.relic <= 5 && !(D.titan >= 0 && !D.choice.empty());
				if (!bOk)
				{
					return false;
				}
				Days.push_back(D);
			}
			return true;
		};
		CalendarRules& Cr = Out.calendar;
		const JsonValue* Monthly = Cal->Find("monthly");
		if (!ReadDays(Cal->Find("newcomer"), Cr.newcomer) || !ReadDays(Monthly ? Monthly->Find("days") : nullptr, Cr.monthly) || Cr.newcomer.size() > 32)
		{
			Error = "kingdom/kingdom.json: a calendar day names an unknown titan, a titan and a choice, or a bad relic";
			return false;
		}
		if (Monthly)
		{
			Cr.catchUpGems = std::max<int64_t>(0, Monthly->GetInt64("catchUpGems", Cr.catchUpGems));
			Cr.catchUps = std::max(0, Monthly->GetInt("catchUps", Cr.catchUps));
		}
	}
	if (const JsonValue* Tj = Rules.Find("trials"))
	{
		TrialRules& T = Out.trials;
		T.castle = std::max(1, Tj->GetInt("castle", T.castle));
		T.days = std::max(1, std::min(7, Tj->GetInt("days", T.days)));
		T.attempts = std::max(1, Tj->GetInt("attempts", T.attempts));
		T.bracket = std::max(2, Tj->GetInt("bracket", T.bracket));
		const auto ReadReward = [](const JsonValue& J, EventReward& R) {
			ReadEventReward(&J, R);
		};
		bool bOk = true;
		for (const char* Key : {"milestones", "ranks"})
		{
			const JsonValue* Rows = Tj->Find(Key);
			for (size_t I = 0; Rows && I < Rows->Size(); ++I)
			{
				const JsonValue& J = (*Rows)[I];
				EventReward R;
				ReadReward(J, R);
				if (Key[0] == 'm')
				{
					const int64_t Score = J.GetInt64("score", 0);
					bOk = bOk && Score > 0 && (T.milestones.empty() || Score > T.milestones.back().first);
					T.milestones.emplace_back(Score, R);
				}
				else
				{
					const int Place = J.GetInt("place", 0);
					bOk = bOk && Place > 0 && Place <= T.bracket && (T.ranks.empty() || Place > T.ranks.back().first);
					T.ranks.emplace_back(Place, R);
				}
			}
		}
		const JsonValue* Trials = Tj->Find("list");
		for (size_t I = 0; Trials && I < Trials->Size(); ++I)
		{
			const JsonValue& J = (*Trials)[I];
			TrialDef D;
			D.id = J.GetString("id");
			D.noTitan = J.GetBool("noTitan", false);
			D.maxStars = std::max(0, J.GetInt("maxStars", 0));
			D.levelOffset = J.GetInt("levelOffset", 0);
			bOk = bOk && !D.id.empty() && J.Find("enemy") && J.Find("enemy")->Size() > 0;
			if (const JsonValue* Ts = J.Find("titans"))
			{
				for (size_t K = 0; K < Ts->Size(); ++K)
				{
					const int X = Out.FindTitan((*Ts)[K].AsString());
					bOk = bOk && X >= 0;
					D.titans.push_back(X);
				}
			}
			if (const JsonValue* Tr = J.Find("troops"))
			{
				for (size_t K = 0; K < Tr->Size(); ++K)
				{
					const int X = Out.FindTroop((*Tr)[K].AsString());
					bOk = bOk && X >= 0;
					D.troops.push_back(X);
				}
			}
			for (size_t K = 0; bOk && K < J.Find("enemy")->Size(); ++K)
			{
				D.enemy.push_back(Out.FindTroop((*J.Find("enemy"))[K].AsString()));
				bOk = D.enemy.back() >= 0;
			}
			const std::string EnemyTitan = J.GetString("enemyTitan");
			D.enemyTitan = EnemyTitan.empty() ? -1 : Out.FindTitan(EnemyTitan);
			bOk = bOk && (EnemyTitan.empty() || D.enemyTitan >= 0);
			T.list.push_back(D);
		}
		if (!bOk)
		{
			Error = "kingdom/kingdom.json: a trial needs an id, known titans and troops, an enemy army, and rewards in order";
			return false;
		}
	}
	// Titan lineages (shared by the Lineage Trial and the Codex): a titan in one lineage at most.
	Out.lineageOf.assign(Out.titans.size(), -1);
	if (const JsonValue* Lj = Rules.Find("lineages"))
	{
		bool bOk = Lj->Size() <= 32;
		for (size_t I = 0; I < Lj->Size() && bOk; ++I)
		{
			const JsonValue& J = (*Lj)[I];
			LineageDef L;
			L.id = J.GetString("id");
			L.name = J.GetString("name", L.id);
			const JsonValue* Ts = J.Find("titans");
			for (size_t K = 0; Ts && K < Ts->Size(); ++K)
			{
				const int X = Out.FindTitan((*Ts)[K].AsString());
				bOk = bOk && X >= 0 && Out.lineageOf[static_cast<size_t>(X)] < 0;
				if (X >= 0)
				{
					Out.lineageOf[static_cast<size_t>(X)] = static_cast<int>(I);
				}
				L.titans.push_back(X);
			}
			if (const JsonValue* Pj = J.Find("page"))
			{
				ReadEventReward(Pj, L.page);
			}
			bOk = bOk && !L.id.empty() && !L.titans.empty();
			for (const LineageDef& Other : Out.lineages)
			{
				bOk = bOk && Other.id != L.id;
			}
			Out.lineages.push_back(L);
		}
		if (!bOk)
		{
			Error = "kingdom/kingdom.json: lineages need distinct ids, at least one known titan each, and no titan in two lineages";
			return false;
		}
	}
	if (const JsonValue* Dj = Rules.Find("dailyTrials"))
	{
		DailyTrialRules& D = Out.dailyTrials;
		D.castle = std::max(1, Dj->GetInt("castle", D.castle));
		D.attempts = std::max(1, std::min(20, Dj->GetInt("attempts", D.attempts)));
		D.lineageBonusPct = std::max(0, std::min(1000, Dj->GetInt("lineageBonusPct", D.lineageBonusPct)));
		bool bOk = true;
		if (const JsonValue* Tiers = Dj->Find("tiers"))
		{
			for (size_t I = 0; I < Tiers->Size(); ++I)
			{
				const int Castle = std::max(1, (*Tiers)[I].GetInt("castle", D.castle));
				bOk = bOk && (D.tierCastle.empty() || Castle >= D.tierCastle.back());
				D.tierCastle.push_back(Castle);
			}
		}
		if (!Dj->Find("lineages"))
		{
			// The shared lineages, in their order.
			for (const LineageDef& L : Out.lineages)
			{
				D.lineages.push_back(L.titans);
			}
		}
		else if (const JsonValue* Lines = Dj->Find("lineages"))
		{
			for (size_t I = 0; I < Lines->Size(); ++I)
			{
				std::vector<int> Titans;
				const JsonValue* Ts = (*Lines)[I].Find("titans");
				for (size_t K = 0; Ts && K < Ts->Size(); ++K)
				{
					Titans.push_back(Out.FindTitan((*Ts)[K].AsString()));
					bOk = bOk && Titans.back() >= 0;
				}
				bOk = bOk && !Titans.empty();
				D.lineages.push_back(Titans);
			}
		}
		if (const JsonValue* Trials = Dj->Find("list"))
		{
			for (size_t I = 0; I < Trials->Size(); ++I)
			{
				const JsonValue& J = (*Trials)[I];
				DailyTrialDef T;
				T.id = J.GetString("id");
				T.day = J.GetInt("day", -1);
				const std::string Pay = J.GetString("pay");
				static const char* const PayNames[] = {"gold", "food", "titanXp", "relic", "shards", "study", "silk"};
				bool bPay = false;
				for (int K = 0; K < 7; ++K)
				{
					if (Pay == PayNames[K])
					{
						T.pay = static_cast<DailyPay>(K);
						bPay = true;
					}
				}
				bOk = bOk && !T.id.empty() && T.day >= 0 && T.day <= 5 && bPay;
				const JsonValue* Amounts = J.Find("amounts");
				for (size_t K = 0; Amounts && K < Amounts->Size(); ++K)
				{
					const int64_t A = (*Amounts)[K].AsInt64();
					bOk = bOk && A > 0 && (T.pay != DailyPay::Relic || A <= 5);
					T.amounts.push_back(A);
				}
				bOk = bOk && T.amounts.size() == D.tierCastle.size();
				for (const DailyTrialDef& Other : D.list)
				{
					bOk = bOk && Other.id != T.id;
				}
				D.list.push_back(T);
			}
		}
		if (!bOk || D.tierCastle.empty() || D.tierCastle.size() > 16 || D.list.size() > 16)
		{
			Error = "kingdom/kingdom.json: daily trials need 1 to 16 tiers of rising Castle, lineages of known titans, and up to 16 "
				"trials with distinct ids, a day 0-5, a known pay and a positive amount a tier (a relic rarity 1-5)";
			return false;
		}
	}
	if (const JsonValue* Wj = Rules.Find("warpath"))
	{
		WarpathRules& W = Out.warpath;
		W.weeks = std::max(1, Wj->GetInt("weeks", W.weeks));
		W.tierXp = std::max<int64_t>(1, Wj->GetInt64("tierXp", W.tierXp));
		W.skipGems = std::max<int64_t>(0, Wj->GetInt64("skipGems", W.skipGems));
		W.bonusXp = std::max<int64_t>(1, Wj->GetInt64("bonusXp", W.bonusXp));
		if (const JsonValue* Xp = Wj->Find("xp"))
		{
			W.dailyXp = std::max<int64_t>(0, Xp->GetInt64("daily", W.dailyXp));
			W.weeklyXp = std::max<int64_t>(0, Xp->GetInt64("weekly", W.weeklyXp));
			W.battleXp = std::max<int64_t>(0, Xp->GetInt64("battle", W.battleXp));
			W.battleCap = std::max<int64_t>(0, Xp->GetInt64("battleCap", W.battleCap));
			W.trialXp = std::max<int64_t>(0, Xp->GetInt64("trial", W.trialXp));
		}
		const auto ReadPay = [](const JsonValue& J, EventReward& R) {
			ReadEventReward(&J, R);
		};
		if (const JsonValue* B = Wj->Find("bonus"))
		{
			ReadPay(*B, W.bonus);
		}
		if (const JsonValue* Tiers = Wj->Find("tiers"))
		{
			for (size_t I = 0; I < Tiers->Size(); ++I)
			{
				EventReward R;
				ReadPay((*Tiers)[I], R);
				W.tiers.push_back(R);
			}
		}
		if (W.tiers.empty() || W.tiers.size() > 64)
		{
			Error = "kingdom/kingdom.json: the warpath needs 1 to 64 tiers";
			return false;
		}
	}
	if (const JsonValue* Aj = Rules.Find("assault"))
	{
		AssaultRules& A = Out.assault;
		A.castle = std::max(1, Aj->GetInt("castle", A.castle));
		A.weeks = std::max(1, std::min(52, Aj->GetInt("weeks", A.weeks)));
		A.week = std::max(0, std::min(A.weeks - 1, Aj->GetInt("week", A.week)));
		A.startDay = std::max(0, std::min(6, Aj->GetInt("startDay", A.startDay)));
		A.days = std::max(1, std::min(A.weeks * 7, Aj->GetInt("days", A.days)));
		A.retries = std::max(0, Aj->GetInt("retries", A.retries));
		A.boonChoices = std::max(1, Aj->GetInt("boonChoices", A.boonChoices));
		bool bOk = true;
		if (const JsonValue* G = Aj->Find("garrison"))
		{
			for (size_t I = 0; I < G->Size(); ++I)
			{
				A.garrison.push_back(Out.FindTroop((*G)[I].AsString()));
				bOk = bOk && A.garrison.back() >= 0;
			}
		}
		if (const JsonValue* Stages = Aj->Find("stages"))
		{
			for (size_t I = 0; I < Stages->Size(); ++I)
			{
				const JsonValue& J = (*Stages)[I];
				AssaultStage S;
				S.level = J.GetInt("level", 0);
				S.extra = std::max(0, std::min(8, J.GetInt("extra", 0)));
				S.fortPct = std::max(0, std::min(200, J.GetInt("fort", 0)));
				S.titan = J.GetBool("titan", false);
				if (const JsonValue* Pay = J.Find("reward"))
				{
					ReadEventReward(Pay, S.reward);
				}
				bOk = bOk && (S.extra == 0 || !A.garrison.empty());
				A.stages.push_back(S);
			}
		}
		if (const JsonValue* Boons = Aj->Find("boons"))
		{
			for (size_t I = 0; I < Boons->Size(); ++I)
			{
				const JsonValue& J = (*Boons)[I];
				AssaultBoon B;
				B.id = J.GetString("id");
				B.troopAtkPct = std::max(0, J.GetInt("atkPct", 0));
				B.troopHpPct = std::max(0, J.GetInt("hpPct", 0));
				B.revivePct = std::max(0, std::min(100, J.GetInt("revivePct", 0)));
				B.titanPct = std::max(0, J.GetInt("titanPct", 0));
				B.titanCritPct = std::max(0, J.GetInt("critPct", 0));
				bOk = bOk && !B.id.empty();
				A.boons.push_back(B);
			}
		}
		if (!bOk || A.stages.empty() || A.stages.size() > 32 || static_cast<int>(A.boons.size()) < A.boonChoices || A.boons.size() > 32)
		{
			Error = "kingdom/kingdom.json: the assault needs 1 to 32 stages, known garrison troops, and boons with ids (at least boonChoices)";
			return false;
		}
	}
	if (const JsonValue* Bj = Rules.Find("raidBoss"))
	{
		RaidBossRules& B = Out.raidBoss;
		B.castle = std::max(1, Bj->GetInt("castle", B.castle));
		B.days = std::max(1, std::min(6, Bj->GetInt("days", B.days)));
		B.attempts = std::max(1, Bj->GetInt("attempts", B.attempts));
		B.battleSec = std::max(10, std::min(600, Bj->GetInt("battleSec", B.battleSec)));
		B.hpPct = std::max(0, std::min(5000, Bj->GetInt("hpPct", B.hpPct)));
		B.poolPct = std::max<int64_t>(1, Bj->GetInt64("poolPct", B.poolPct));
		B.memberHit = std::max<int64_t>(0, std::min<int64_t>(1000, Bj->GetInt64("memberHit", B.memberHit)));
		B.memberStepPct = std::max(0, std::min(90, Bj->GetInt("memberStepPct", B.memberStepPct)));
		const auto ReadReward = [](const JsonValue& J, EventReward& R) {
			ReadEventReward(&J, R);
		};
		bool bOk = true;
		if (const JsonValue* Tiers = Bj->Find("tiers"))
		{
			for (size_t I = 0; I < Tiers->Size(); ++I)
			{
				const JsonValue& J = (*Tiers)[I];
				RaidBossTier T;
				T.level = std::max(1, J.GetInt("level", T.level));
				T.stars = std::max(1, std::min(5, J.GetInt("stars", T.stars)));
				T.escort = std::max(0, std::min(6, J.GetInt("escort", T.escort)));
				T.troopTier = std::max(1, std::min(5, J.GetInt("troopTier", T.troopTier)));
				if (const JsonValue* Chest = J.Find("chest"))
				{
					ReadReward(*Chest, T.chest);
				}
				B.tiers.push_back(T);
			}
		}
		if (const JsonValue* Lt = Bj->Find("leagueTier"))
		{
			for (size_t I = 0; I < Lt->Size(); ++I)
			{
				const int T = (*Lt)[I].AsInt();
				bOk = bOk && T >= 1 && T <= static_cast<int>(B.tiers.size());
				B.leagueTier.push_back(T);
			}
		}
		if (const JsonValue* Ph = Bj->Find("phases"))
		{
			for (size_t I = 0; I < Ph->Size(); ++I)
			{
				const int P = (*Ph)[I].AsInt();
				bOk = bOk && P >= 1 && P <= 999 && (B.phases.empty() || P < B.phases.back());
				B.phases.push_back(P);
			}
		}
		B.husk = Bj->GetString("husk");
		bOk = bOk && (B.phases.empty() || !B.husk.empty());
		if (const JsonValue* Bosses = Bj->Find("bosses"))
		{
			for (size_t I = 0; I < Bosses->Size(); ++I)
			{
				const JsonValue& J = (*Bosses)[I];
				RaidBossDef D;
				D.id = J.GetString("id");
				D.titan = Out.FindTitan(J.GetString("titan"));
				D.escort = J.GetString("escort");
				bOk = bOk && !D.id.empty() && D.titan >= 0;
				B.bosses.push_back(D);
			}
		}
		if (const JsonValue* Ms = Bj->Find("milestones"))
		{
			for (size_t I = 0; I < Ms->Size(); ++I)
			{
				const JsonValue& J = (*Ms)[I];
				EventReward R;
				ReadReward(J, R);
				const int64_t Dmg = J.GetInt64("dmg", 0);
				bOk = bOk && Dmg > 0 && Dmg <= 1000 && (B.milestones.empty() || Dmg > B.milestones.back().first);
				B.milestones.emplace_back(Dmg, R);
			}
		}
		if (!bOk || B.bosses.empty() || B.tiers.empty() || B.tiers.size() > 32 || B.milestones.size() > 32)
		{
			Error = "kingdom/kingdom.json: the raid boss needs bosses with ids and known titans, 1 to 32 tiers, leagueTier within them, "
				"falling phases (1-999) with a husk troop and milestones of rising damage (1-1000)";
			return false;
		}
	}
	if (const JsonValue* Wj = Rules.Find("allianceWar"))
	{
		AllianceWarRules& W = Out.allianceWar;
		W.castle = std::max(1, Wj->GetInt("castle", W.castle));
		W.rosterStep = std::max(1, std::min(10, Wj->GetInt("rosterStep", W.rosterStep)));
		W.rosterMax = std::max(W.rosterStep, std::min(60, Wj->GetInt("rosterMax", W.rosterMax)));
		W.minMembers = std::max(W.rosterStep, std::min(W.rosterMax, Wj->GetInt("minMembers", W.minMembers)));
		W.prepDay = std::max(0, std::min(6, Wj->GetInt("prepDay", W.prepDay)));
		W.battleDays = std::max(1, std::min(5, Wj->GetInt("battleDays", W.battleDays)));
		W.attacks = std::max(1, std::min(5, Wj->GetInt("attacks", W.attacks)));
		W.idlePct = std::max(0, std::min(100, Wj->GetInt("idlePct", W.idlePct)));
		W.onePct = std::max(0, std::min(100 - W.idlePct, Wj->GetInt("onePct", W.onePct)));
		const auto ReadWarReward = [](const JsonValue* J, EventReward& R) {
			if (J)
			{
				ReadEventReward(J, R);
			}
		};
		bool bOk = true;
		if (const JsonValue* Sp = Wj->Find("starPct"))
		{
			bOk = Sp->IsArray() && Sp->Size() == W.starPct.size();
			for (size_t I = 0; bOk && I < W.starPct.size(); ++I)
			{
				W.starPct[I] = std::max(-100, std::min(100, (*Sp)[I].AsInt(0)));
			}
		}
		if (const JsonValue* Tiers = Wj->Find("tiers"))
		{
			W.tiers = static_cast<int>(Tiers->Size());
		}
		if (const JsonValue* Lt = Wj->Find("leagueTier"))
		{
			for (size_t I = 0; I < Lt->Size(); ++I)
			{
				const int T = (*Lt)[I].AsInt();
				bOk = bOk && T >= 1 && T <= W.tiers;
				W.leagueTier.push_back(T);
			}
		}
		if (const JsonValue* Chests = Wj->Find("chests"))
		{
			for (size_t I = 0; I < Chests->Size(); ++I)
			{
				const JsonValue& J = (*Chests)[I];
				WarChest Ch;
				ReadWarReward(J.Find("win"), Ch.win);
				ReadWarReward(J.Find("draw"), Ch.draw);
				ReadWarReward(J.Find("lose"), Ch.lose);
				W.chests.push_back(Ch);
			}
		}
		ReadWarReward(Wj->Find("star"), W.star);
		if (!bOk || W.tiers < 1 || W.tiers > 32 || W.leagueTier.empty() || W.chests.size() != W.leagueTier.size() || 1 + W.battleDays > 6)
		{
			Error = "kingdom/kingdom.json: the alliance war needs 1 to 32 tiers, leagueTier within them, a chest for each league, "
				"six starPct numbers and a prep day and battle days within a week";
			return false;
		}
	}
	if (const JsonValue* Lj = Rules.Find("loadouts"))
	{
		Out.loadouts.max = std::max(0, std::min(20, Lj->GetInt("max", 0)));
	}
	if (const JsonValue* Dj = Rules.Find("divine"))
	{
		DivineRules& D = Out.divine;
		D.building = Out.FindBuilding(Dj->GetString("building", "eternal_shrine"));
		D.perDay = std::max<int64_t>(0, std::min<int64_t>(100000, Dj->GetInt64("perDay", 0)));
		D.cap = std::max<int64_t>(D.perDay, std::min<int64_t>(10000000, Dj->GetInt64("cap", D.perDay)));
		if (const JsonValue* A = Dj->Find("altars"))
		{
			for (size_t I = 0; I < A->Size(); ++I)
			{
				const JsonValue& J = (*A)[I];
				DivineAltar Alt;
				Alt.id = J.GetString("id");
				Alt.name = J.GetString("name", Alt.id);
				Alt.titanStars = std::max(0, std::min(4, J.GetInt("titan", 0)));
				Alt.relicRarity = Alt.titanStars > 0 ? 0 : std::max(0, std::min(5, J.GetInt("relic", 0)));
				Alt.price = std::max<int64_t>(1, J.GetInt64("price", 1));
				if (Alt.titanStars > 0 || Alt.relicRarity > 0)
				{
					D.altars.push_back(Alt);
				}
			}
		}
		if (D.building < 0)
		{
			D = DivineRules();
		}
	}
	if (const JsonValue* Sj = Rules.Find("spire"))
	{
		SpireRules& S = Out.spire;
		S.castle = std::max(1, std::min(100, Sj->GetInt("castle", 1)));
		S.perBand = std::max(2, std::min(50, Sj->GetInt("perBand", S.perBand)));
		S.bracket = std::max(2, std::min(200, Sj->GetInt("bracket", S.bracket)));
		const auto ReadPay = [](const JsonValue* J, EventReward& R) {
			if (J)
			{
				ReadEventReward(J, R);
			}
		};
		if (const JsonValue* B = Sj->Find("bands"))
		{
			for (size_t I = 0; I < B->Size() && I < 100; ++I)
			{
				const JsonValue& J = (*B)[I];
				SpireBand Band;
				Band.pay = std::max<int64_t>(0, J.GetInt64("pay", 0));
				ReadPay(J.Find("middle"), Band.middle);
				ReadPay(J.Find("top"), Band.top);
				S.bands.push_back(Band);
			}
		}
		S.weekly = std::max(0, std::min(S.Floors(), Sj->GetInt("weekly", S.weekly)));
		if (S.bands.empty())
		{
			S = SpireRules();
		}
	}
	if (const JsonValue* Cj = Rules.Find("comeback"))
	{
		ComebackRules& Cb = Out.comeback;
		Cb.days = std::max(0, std::min(365, Cj->GetInt("days", 0)));
		Cb.castle = std::max(1, std::min(100, Cj->GetInt("castle", 1)));
		ReadEventReward(Cj, Cb.gift);
		Cb.scrolls = std::max<int64_t>(0, Cj->GetInt64("scrolls", 0));
	}
	if (const JsonValue* Xj = Rules.Find("codex"))
	{
		// The Codex keeps its entries in a 64-bit mask, so it needs lineages and 64 titans at most.
		CodexRules& X = Out.codex;
		X.on = !Out.lineages.empty() && Out.titans.size() <= 64;
		if (const JsonValue* Gj = Xj->Find("entryGems"))
		{
			for (size_t I = 0; I < Gj->Size() && I < X.entryGems.size(); ++I)
			{
				X.entryGems[I] = std::max<int64_t>(0, (*Gj)[I].AsInt64());
			}
		}
		X.power = std::max<int64_t>(0, std::min<int64_t>(1000000, Xj->GetInt64("power", 0)));
		X.level = std::max(1, std::min(100, Xj->GetInt("level", X.level)));
		bool bOk = true;
		if (const JsonValue* Rj = Xj->Find("resonance"))
		{
			for (size_t I = 0; I < Rj->Size() && I < 8; ++I)
			{
				const JsonValue& J = (*Rj)[I];
				ResonanceTier T;
				T.count = J.GetInt("count", 0);
				T.hpPct = std::max(0, std::min(100, J.GetInt("hp", 0)));
				T.atkPct = std::max(0, std::min(100, J.GetInt("atk", 0)));
				T.armorPct = std::max(0, std::min(100, J.GetInt("armor", 0)));
				bOk = bOk && T.count >= 1 && (X.resonance.empty() || T.count > X.resonance.back().count);
				X.resonance.push_back(T);
			}
		}
		if (!bOk)
		{
			Error = "kingdom/kingdom.json: codex resonance tiers need rising counts from 1";
			return false;
		}
	}
	if (const JsonValue* Oj = Rules.Find("honor"))
	{
		HonorRules& O = Out.honor;
		O.on = true;
		O.castle = std::max(1, std::min(100, Oj->GetInt("castle", O.castle)));
		if (const JsonValue* Dj = Oj->Find("donate"))
		{
			ReadCost(Dj, O.donateCost);
			O.donatePerDay = std::max(0, std::min(100, Dj->GetInt("perDay", 0)));
			O.donateHonor = std::max<int64_t>(0, Dj->GetInt64("honor", 0));
		}
		bool bOk = true;
		if (const JsonValue* Sj = Oj->Find("shop"))
		{
			for (size_t I = 0; I < Sj->Size() && I < 32; ++I)
			{
				const JsonValue& J = (*Sj)[I];
				ShopItemDef D;
				D.id = J.GetString("id");
				D.price = J.GetInt64("price", 0);
				D.weekly = J.GetInt("weekly", 1);
				D.castle = std::max(1, std::min(100, J.GetInt("castle", 1)));
				ReadEventReward(J.Find("pay"), D.pay);
				D.pay.honor = 0;
				if (const JsonValue* Pj = J.Find("pay"))
				{
					D.scrolls = std::max<int64_t>(0, Pj->GetInt64("scrolls", 0));
				}
				bOk = bOk && !D.id.empty() && D.price > 0 && D.weekly >= 1 && D.weekly <= 100;
				for (const ShopItemDef& Other : O.shop)
				{
					bOk = bOk && Other.id != D.id;
				}
				O.shop.push_back(D);
			}
		}
		if (!bOk)
		{
			Error = "kingdom/kingdom.json: honor shop items need distinct ids, a price over 0 and 1-100 a week";
			return false;
		}
	}
	if (const JsonValue* Bj = Rules.Find("banners"))
	{
		BannerRules& B = Out.banners;
		B.castle = std::max(1, std::min(100, Bj->GetInt("castle", B.castle)));
		B.pctPerLevel = std::max(0, std::min(100, Bj->GetInt("pct", B.pctPerLevel)));
		B.perkPct = std::max(0, std::min(100, Bj->GetInt("perkPct", 0)));
		ReadInts(Bj->Find("silk"), B.silk);
		ReadInts(Bj->Find("gold"), B.gold);
		std::vector<int64_t> Perks;
		ReadInts(Bj->Find("perkLevels"), Perks);
		bool bOk = !B.silk.empty() && B.silk.size() == B.gold.size() && B.silk.size() <= 100;
		for (size_t I = 0; I < B.silk.size(); ++I)
		{
			bOk = bOk && B.silk[I] >= 0 && B.gold[I] >= 0;
		}
		for (const int64_t L : Perks)
		{
			bOk = bOk && L >= 1 && L <= static_cast<int64_t>(B.silk.size());
			B.perkLevels.push_back(static_cast<int>(L));
		}
		if (!bOk)
		{
			Error = "kingdom/kingdom.json: banners need silk and gold costs of the same length (1-100 levels) and perk levels within them";
			return false;
		}
		B.on = true;
	}
	if (const JsonValue* Hj = Rules.Find("allianceHelp"))
	{
		AllianceHelpRules& H = Out.allianceHelp;
		H.on = true;
		H.base = std::max(0, std::min(50, Hj->GetInt("base", H.base)));
		H.hallStep = std::max(1, std::min(20, Hj->GetInt("hallStep", H.hallStep)));
		H.max = std::max(H.base, std::min(50, Hj->GetInt("max", H.max)));
		H.cutPct = std::max(0, std::min(100, Hj->GetInt("cutPct", H.cutPct)));
		H.minCutSec = std::max<int64_t>(0, std::min<int64_t>(86400, Hj->GetInt64("minCutSec", H.minCutSec)));
		H.openHours = std::max(1, std::min(24, Hj->GetInt("openHours", H.openHours)));
		H.dailyCap = std::max(0, std::min(1000, Hj->GetInt("dailyCap", H.dailyCap)));
		bool bOk = true;
		const auto ReadSpan = [&bOk](const JsonValue* J, int64_t Lo, int64_t Hi, int64_t& OutLo, int64_t& OutHi) {
			if (!J)
			{
				return;
			}
			bOk = bOk && J->IsArray() && J->Size() == 2;
			if (bOk)
			{
				OutLo = (*J)[0].AsInt64(-1);
				OutHi = (*J)[1].AsInt64(-1);
				bOk = OutLo >= Lo && OutLo <= OutHi && OutHi <= Hi;
			}
		};
		ReadSpan(Hj->Find("firstSec"), 0, 86400, H.firstSec[0], H.firstSec[1]);
		ReadSpan(Hj->Find("gapSec"), 1, 86400, H.gapSec[0], H.gapSec[1]);
		int64_t AskLo = H.asksPerHour[0];
		int64_t AskHi = H.asksPerHour[1];
		ReadSpan(Hj->Find("asksPerHour"), 0, 8, AskLo, AskHi);
		H.asksPerHour = {{static_cast<int>(AskLo), static_cast<int>(AskHi)}};
		if (const JsonValue* Rw = Hj->Find("reward"))
		{
			ReadEventReward(Rw, H.reward);
		}
		if (!bOk)
		{
			Error = "kingdom/kingdom.json: alliance help needs firstSec, gapSec and asksPerHour as [low, high] pairs "
				"(0-86400 s, 1-86400 s, 0-8)";
			return false;
		}
	}
	if (const JsonValue* Pj = Rules.Find("power"))
	{
		PowerRules& P = Out.power;
		P.building = std::max<int64_t>(0, std::min<int64_t>(100000, Pj->GetInt64("building", P.building)));
		P.castleMul = std::max<int64_t>(0, std::min<int64_t>(100, Pj->GetInt64("castleMul", P.castleMul)));
		P.titans = std::max(0, std::min(50, Pj->GetInt("titans", P.titans)));
		P.research = std::max<int64_t>(0, std::min<int64_t>(100000, Pj->GetInt64("research", P.research)));
	}
	Out.hash = HashToHex(HashInput.Hash());
	return true;
}
} // namespace kingdom
} // namespace dot
