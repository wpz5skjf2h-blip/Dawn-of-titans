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

bool ParseQuestEvent(const std::string& S, QuestEvent& Out)
{
	static const char* const Names[] = {"win", "threeStars", "raid", "collectGold", "collectFood", "train", "upgrade", "craft", "stones", "site", "mission"};
	for (int I = 0; I < static_cast<int>(QuestEvent::Count); ++I)
	{
		if (S == Names[I])
		{
			Out = static_cast<QuestEvent>(I);
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
				// Missions are stored as one byte of stars each and chests as bits of a 32-bit mask.
				if (A.id.empty() || A.missions.empty() || A.missions.size() > 99 || A.chests.size() > 32 || Out.battles.empty())
				{
					Error = "kingdom/kingdom.json: a campaign act needs an id, 1-99 missions, at most 32 chests and the battles table";
					return false;
				}
				G.acts.push_back(A);
			}
		}
	}
	if (const JsonValue* Qj = Rules.Find("dailyQuests"))
	{
		QuestRules& Q = Out.quests;
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
					Error = "kingdom/kingdom.json: daily quest " + D.id + " needs an id, a known event and building, and targets";
					return false;
				}
				Q.pool.push_back(D);
			}
		}
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
	Out.hash = HashToHex(HashInput.Hash());
	return true;
}
} // namespace kingdom
} // namespace dot
