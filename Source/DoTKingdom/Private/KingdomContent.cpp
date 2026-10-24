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
			if (const JsonValue* W = AJ.Find("weights"))
			{
				for (size_t K = 0; K < W->Size() && K < 4; ++K)
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
		if (const JsonValue* Kinds = R->Find("kinds"))
		{
			static const char* const Stats[] = {"atkPct", "hpPct", "armorPct"};
			for (size_t I = 0; I < Kinds->Size(); ++I)
			{
				RelicKind K;
				K.id = (*Kinds)[I].GetString("id");
				K.name = (*Kinds)[I].GetString("name", K.id);
				const std::string Stat = (*Kinds)[I].GetString("stat");
				for (int S = 0; S < 3; ++S)
				{
					K.stat = Stat == Stats[S] ? S : K.stat;
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
		Read5("bonusPct", Out.relicBonusPct);
		Read5("durability", Out.relicDurability);
		Read5("repairWeight", Out.relicRepairWeight);
		Read5("repairFood", Out.relicRepairFood);
		Read5("dailyRarity", Out.dailyRelicRarity);
		if (const JsonValue* L = R->Find("slotLevels"))
		{
			Out.relicSlotLevels.clear();
			for (size_t K = 0; K < L->Size(); ++K)
			{
				Out.relicSlotLevels.push_back((*L)[K].AsInt(1));
			}
		}
		Out.relicRepairCastle = R->GetInt("repairCastle", 8);
		Out.maxRelics = R->GetInt("maxRelics", 100);
		Out.dailyRelicPct = R->GetInt64("dailyRelicPct", 60);
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
	Out.relicTemple = Out.FindBuilding("relic_temple");
	Out.spellForge = Out.FindBuilding("spell_forge");
	Out.arcaneTower = Out.FindBuilding("arcane_tower");
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
