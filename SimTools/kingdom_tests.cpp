// SimTools/kingdom_tests.cpp
// Tests for the kingdom economy (Source/DoTKingdom): content numbers from the original game,
// timers, production, limits, requirements, portal stones, lands (ruins and victory-point lands too), seasons,
// army, titans (ascension too), relic items (combat and economy, effects, repair), alliances with their leagues and
// reinforcements, and save/load determinism.
#include "ToolCommon.h"

#include "DoTKingdom/Kingdom.h"

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

namespace
{
int GFailures = 0;
int GChecks = 0;

#define KD_CHECK(Cond)                                                                 \
	do                                                                                 \
	{                                                                                  \
		++GChecks;                                                                     \
		if (!(Cond))                                                                   \
		{                                                                              \
			++GFailures;                                                               \
			std::printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #Cond);             \
		}                                                                              \
	} while (0)

using namespace dot;
using namespace dot::kingdom;

const KingdomContent& Content()
{
	static KingdomContent C;
	static bool bLoaded = false;
	if (!bLoaded)
	{
		dottools::FileContentSource Source(dottools::DefaultContentDir());
		std::string Error;
		if (!LoadKingdomContent(Source, C, Error))
		{
			std::printf("kingdom content error: %s\n", Error.c_str());
		}
		bLoaded = true;
	}
	return C;
}

int Def(const char* Id) { return Content().FindBuilding(Id); }

int32_t FindId(const Kingdom& K, const char* Id)
{
	for (const Building& B : K.Buildings())
	{
		if (B.def == Def(Id))
		{
			return B.id;
		}
	}
	return 0;
}

const int64_t T0 = 1760000000; // an arbitrary Unix time
const int64_t Hour = 3600;

void TestContentMatchesOriginal()
{
	const KingdomContent& C = Content();
	KD_CHECK(C.buildings.size() == 25); // the original's 21 and four envoy councils
	KD_CHECK(C.buildings[static_cast<size_t>(Def("castle"))].MaxLevel() == 25);
	const BuildingDef& Mine = C.buildings[static_cast<size_t>(Def("gold_mine"))];
	// Gold Mine level 10 in the wiki: 895/h, holds 21 480, costs 441 400 gold, 20 h, Castle 12.
	const LevelDef& L10 = Mine.levels[9];
	KD_CHECK(L10.produce == 895 && L10.store == 21480);
	KD_CHECK(L10.cost[0] == 441400 && L10.timeSec == 20 * Hour && L10.castle == 12);
	KD_CHECK(!L10.estimated);
	KD_CHECK(Mine.MaxCount(12) == 8 && Mine.MaxCount(16) == 9);
	// Castle 10: 264 800 gold, 19 h 40 m, needs Garrison 9 and Army Camp 6.
	const LevelDef& C10 = C.buildings[static_cast<size_t>(Def("castle"))].levels[9];
	KD_CHECK(C10.cost[0] == 264800 && C10.timeSec == 19 * Hour + 40 * 60 && C10.skipGems == 863);
	KD_CHECK(C10.needs.size() == 2 && C10.needs[0].building == Def("garrison") && C10.needs[0].level == 9);
	// Portal: 18 stones at level 1, 30 at level 7; level 3 needs the first expansion.
	const BuildingDef& Portal = C.buildings[static_cast<size_t>(Def("portal"))];
	KD_CHECK(Portal.levels[0].stones == 18 && Portal.levels[6].stones == 30 && Portal.levels[2].expansion == 1);
	// Builder's Yard: the second builder costs 1 500 gems, the fifth 20 000.
	const BuildingDef& Yard = C.buildings[static_cast<size_t>(Def("builders_yard"))];
	KD_CHECK(Yard.instant && Yard.levels[1].cost[static_cast<size_t>(ResIndex(Res::Gems))] == 1500 && Yard.levels[4].builders == 5);
	KD_CHECK(C.expansions.size() == 4 && C.expansions[1].costGold == 33000 && C.expansions[3].costGold == 1431000);
	KD_CHECK(C.stoneRegenSec == 420 && C.raidStones == 3 && C.captureStones == 6);
}

void TestNewGameAndProduction()
{
	Kingdom K(Content());
	K.NewGame(T0, 7);
	KD_CHECK(K.CastleLevel() == 1);
	KD_CHECK(K.Builders() == 1 && K.BusyBuilders() == 0);
	KD_CHECK(K.Capacity(Res::Gold) == 3000 && K.Capacity(Res::Food) == 3000);
	KD_CHECK(K.Amount(Res::Stones) == 18 && K.Capacity(Res::Stones) == 18);
	KD_CHECK(K.LandCapacity() == 3);
	KD_CHECK(static_cast<int>(K.World().size()) == Content().worldSize);
	const int32_t Mine = FindId(K, "gold_mine");
	K.Advance(T0 + Hour);
	KD_CHECK(K.Uncollected(*K.Find(Mine)) == 100); // 100 per hour at level 1
	K.Advance(T0 + Hour + 1800);
	KD_CHECK(K.Uncollected(*K.Find(Mine)) == 150);
	K.Advance(T0 + 10 * Hour);
	KD_CHECK(K.Uncollected(*K.Find(Mine)) == 600); // capped by the mine's own storage
	const int64_t Before = K.Amount(Res::Gold);
	int64_t Got = 0;
	KD_CHECK(K.Collect(Mine, &Got) == Error::None);
	KD_CHECK(Got == 500 && K.Amount(Res::Gold) == 3000 && Before == 2500); // the vault holds 3 000
	KD_CHECK(K.Uncollected(*K.Find(Mine)) == 100);
	KD_CHECK(K.Collect(Mine) == Error::StorageFull);
}

void TestUpgradesBuildersAndRequirements()
{
	Kingdom K(Content());
	K.NewGame(T0, 7);
	const int32_t Castle = FindId(K, "castle");
	const int32_t Mine = FindId(K, "gold_mine");
	KD_CHECK(K.CheckUpgrade(Mine).error == Error::CastleTooLow);
	KD_CHECK(K.CheckUpgrade(Castle).error == Error::None);
	KD_CHECK(K.Upgrade(Castle, false) == Error::None);
	KD_CHECK(K.Amount(Res::Gold) == 500);
	KD_CHECK(K.BusyBuilders() == 1);
	KD_CHECK(K.CheckUpgrade(FindId(K, "gold_vault")).error == Error::CastleTooLow); // Vault 2 needs Castle 3
	K.Advance(T0 + 299);
	KD_CHECK(K.CastleLevel() == 1);
	K.Advance(T0 + 300); // Castle 2 takes 5 minutes
	KD_CHECK(K.CastleLevel() == 2 && K.BusyBuilders() == 0);
	// Speed-up costs a share of the wiki's skip price for the time that is left.
	K.Advance(T0 + 10 * Hour);
	KD_CHECK(K.CollectAll() == Error::None);
	KD_CHECK(K.Upgrade(Mine, false) == Error::None); // 800 gold, 5 minutes
	KD_CHECK(K.CheckUpgrade(FindId(K, "farm")).error == Error::NoBuilder); // one builder at the start
	const int64_t Gems = K.Amount(Res::Gems);
	const int64_t Skip = K.SkipCost(Mine);
	KD_CHECK(Skip >= 1);
	KD_CHECK(K.Speedup(Mine) == Error::None);
	KD_CHECK(K.Amount(Res::Gems) == Gems - Skip);
	KD_CHECK(K.Find(Mine)->level == 2 && !K.Find(Mine)->upgrading);
	// Castle 5 needs Garrison 4 and Army Camp 3 (wiki).
	Kingdom K2(Content());
	K2.NewGame(T0, 7);
	const KingdomContent& Ct = Content();
	(void)Ct;
	for (int Level = 1; Level < 4; ++Level)
	{
		const int32_t Id = FindId(K2, "castle");
		K2.Advance(K2.Now() + 30 * 24 * Hour);
		K2.CollectAll();
		KD_CHECK(K2.Upgrade(Id, true) == Error::None);
		K2.Advance(K2.Now() + 24 * Hour);
	}
	KD_CHECK(K2.CastleLevel() == 4);
	const Check Ck = K2.CheckUpgrade(FindId(K2, "castle"));
	KD_CHECK(Ck.error == Error::NeedRequirement && Ck.missing.building == Def("garrison") && Ck.missing.level == 4);
}

void TestBuildLimitsAndPlacement()
{
	Kingdom K(Content());
	K.NewGame(T0, 7);
	const int Farm = Def("farm");
	KD_CHECK(K.MaxCountOf(Farm) == 1 && K.CheckBuild(Farm).error == Error::LimitReached);
	KD_CHECK(K.CheckBuild(Def("medical_school")).error == Error::Locked);
	KD_CHECK(K.Upgrade(FindId(K, "castle"), false) == Error::None);
	K.Advance(T0 + 400);
	KD_CHECK(K.MaxCountOf(Farm) == 2);
	KD_CHECK(!K.CanPlace(Farm, 17, 17, 0)); // on the castle
	KD_CHECK(!K.CanPlace(Farm, 2, 2, 0)); // outside the first city area
	KD_CHECK(K.CanPlace(Farm, 8, 28, 0));
	int32_t Id = 0;
	KD_CHECK(K.Build(Farm, 8, 28, false, &Id) == Error::None);
	KD_CHECK(K.Find(Id) && K.Find(Id)->level == 0 && K.Find(Id)->upgrading);
	K.Advance(T0 + 400 + 120);
	KD_CHECK(K.Find(Id)->level == 1);
	KD_CHECK(K.Move(Id, 29, 28) == Error::BadPlacement); // a 4x4 farm would leave the area
	KD_CHECK(K.Move(Id, 27, 8) == Error::None);
	// Expansion 2 costs 33 000 gold (wiki) and needs Castle 4 (estimate).
	KD_CHECK(K.CheckExpansion().error == Error::CastleTooLow);
}

void TestStonesAttacksAndLands()
{
	Kingdom K(Content());
	K.NewGame(T0, 11);
	const WorldLand Target = K.World()[0];
	KD_CHECK(K.StartAttack(Target.id, false) == Error::None);
	KD_CHECK(K.Amount(Res::Stones) == 15);
	KD_CHECK(K.FinishAttack(Target.id, true, 3) == Error::NoAttack); // capture flag must match
	Reward R;
	KD_CHECK(K.FinishAttack(Target.id, false, 3, &R) == Error::None);
	// A land rewards raiders with what it produces (wiki 1.8), and every victory brings VP.
	const Res Kind = Content().landTypes[static_cast<size_t>(Target.type)].resource;
	const Res Other = Kind == Res::Gold ? Res::Food : Res::Gold;
	KD_CHECK(R.res[static_cast<size_t>(ResIndex(Kind))] > 0 && R.res[static_cast<size_t>(ResIndex(Other))] == 0);
	KD_CHECK(R.res[static_cast<size_t>(ResIndex(Res::Vp))] == Content().victoryVp[static_cast<size_t>(Target.level - 1)]);
	KD_CHECK(static_cast<int>(K.World().size()) == Content().worldSize);
	K.Advance(T0 + 420);
	KD_CHECK(K.Amount(Res::Stones) == 16); // one stone every 7 minutes
	K.Advance(T0 + 420 * 10);
	KD_CHECK(K.Amount(Res::Stones) == 18);
	const WorldLand Land1 = K.World()[1];
	KD_CHECK(K.StartAttack(Land1.id, true) == Error::None);
	KD_CHECK(K.FinishAttack(Land1.id, true, 1, &R) == Error::None);
	KD_CHECK(R.landId != 0 && K.Lands().size() == 1);
	const int64_t Rate = K.LandRate(*K.FindLand(R.landId));
	K.Advance(K.Now() + Hour);
	KD_CHECK(K.UncollectedLand(*K.FindLand(R.landId)) == Rate);
	KD_CHECK(K.ProductionPerHour(Content().landTypes[static_cast<size_t>(Land1.type)].resource) >= Rate + 100);
	// A lost battle gives nothing and keeps the land on the map.
	const WorldLand Land2 = K.World()[0];
	KD_CHECK(K.StartAttack(Land2.id, false) == Error::None);
	KD_CHECK(K.FinishAttack(Land2.id, false, 0, &R) == Error::None);
	KD_CHECK(R.res[0] == 0 && K.World()[0].id == Land2.id);
	// Land slots: three at Castle 1.
	KD_CHECK(K.RefillStones() == Error::None);
	for (int I = 0; I < 2; ++I)
	{
		const WorldLand W = K.World()[0];
		KD_CHECK(K.StartAttack(W.id, true) == Error::None);
		KD_CHECK(K.FinishAttack(W.id, true, 2) == Error::None);
	}
	KD_CHECK(K.Lands().size() == 3);
	KD_CHECK(K.StartAttack(K.World()[0].id, true) == Error::NoLandSlot);
}

void TestSaveLoadDeterminism()
{
	Kingdom A(Content());
	A.NewGame(T0, 99);
	A.Upgrade(FindId(A, "castle"), false);
	A.Advance(T0 + 7 * Hour + 13);
	A.CollectAll();
	A.StartAttack(A.World()[2].id, true);
	A.FinishAttack(A.World()[2].id, true, 3);
	A.Advance(A.Now() + 5 * Hour + 7);
	const std::string Saved = A.Save();
	Kingdom B(Content());
	std::string Error;
	KD_CHECK(B.Load(Saved, Error));
	KD_CHECK(B.Hash() == A.Hash());
	KD_CHECK(B.Save() == Saved);
	// The same actions on both copies give the same state.
	A.Advance(A.Now() + 3 * Hour);
	B.Advance(B.Now() + 3 * Hour);
	A.CollectAll();
	B.CollectAll();
	KD_CHECK(A.Hash() == B.Hash());
	// The clock never runs backwards.
	const uint64_t H = A.Hash();
	A.Advance(A.Now() - 100);
	KD_CHECK(A.Hash() == H);
}

void TestPortalUpgradeMidway()
{
	// Stones regenerate against the capacity in force at each moment.
	Kingdom K(Content());
	K.NewGame(T0, 3);
	for (int I = 0; I < 6; ++I)
	{
		K.StartAttack(K.World()[0].id, false);
		K.FinishAttack(K.World()[0].id, false, 0);
	}
	KD_CHECK(K.Amount(Res::Stones) == 0);
	K.Advance(T0 + 420 * 30);
	KD_CHECK(K.Amount(Res::Stones) == 18);
}

/** Save-text edits for setting up states that take weeks to reach (the save format is compact JSON). */
std::string Replace(std::string S, const std::string& From, const std::string& To)
{
	const size_t P = S.find(From);
	if (P == std::string::npos)
	{
		std::printf("  (pattern not found: %s)\n", From.c_str());
		return S;
	}
	return S.replace(P, From.size(), To);
}

std::string WithBuilding(const std::string& Save, const char* Id, int Level, int X)
{
	return Replace(Save, "\"buildings\":[",
		"\"buildings\":[{\"id\":" + std::to_string(900 + X) + ",\"def\":\"" + Id + "\",\"level\":" + std::to_string(Level) + ",\"x\":" + std::to_string(X) +
			",\"y\":36},");
}

int Troop(const char* Id) { return Content().FindTroop(Id); }

void TestArmyContent()
{
	const KingdomContent& C = Content();
	// Wiki: Militia 135 food per unit (L1), 215 (L2), 2 640 (L7); upgrades 3 500 (L1) ... 111 500 (L7); trains in 1 minute.
	const TroopType& Mil = C.troops[static_cast<size_t>(Troop("troop.militia"))];
	KD_CHECK(Mil.levels[0].cost == 135 && Mil.levels[1].cost == 215 && Mil.levels[6].cost == 2640);
	KD_CHECK(Mil.levels[0].upgradeCost == 3500 && Mil.levels[6].upgradeCost == 111500 && Mil.levels[0].trainSec == 60);
	const TroopType& Spear = C.troops[static_cast<size_t>(Troop("troop.spearmen"))];
	KD_CHECK(Spear.levels[0].cost == 145 && Spear.levels[0].upgradeCost == 6600 && Spear.levels[0].trainSec == 300);
	const TroopType& Arch = C.troops[static_cast<size_t>(Troop("troop.archers"))];
	KD_CHECK(Arch.levels[0].cost == 200 && Arch.levels[0].upgradeCost == 8800);
	// Army Camp: 10 units and 3 battle slots at level 1, 225 units at 8, 14 slots at 13; Barracks queue 6 -> 20.
	const BuildingDef& Camp = C.buildings[static_cast<size_t>(Def("army_camp"))];
	KD_CHECK(Camp.levels[0].troops == 10 && Camp.levels[0].slots == 3 && Camp.levels[7].troops == 225 && Camp.levels[12].slots == 14);
	const BuildingDef& Barracks = C.buildings[static_cast<size_t>(Def("barracks"))];
	KD_CHECK(Barracks.levels[0].queue == 6 && Barracks.levels[8].queue == 20);
	const BuildingDef& Med = C.buildings[static_cast<size_t>(Def("medical_school"))];
	KD_CHECK(Med.levels[0].survival == 85 && Med.levels[10].survival == 75);
	// Titans: rarity R1..R4, max level 10 + 10 x stars (wiki 1.8); altars for 1 000 and 4 000 gems; fusion at Castle 7.
	KD_CHECK(C.titans.size() == 10);
	KD_CHECK(C.titans[static_cast<size_t>(C.FindTitan("titan.thor"))].MaxLevel() == 50);
	KD_CHECK(C.titans[static_cast<size_t>(C.FindTitan("titan.talos"))].MaxLevel() == 20);
	KD_CHECK(C.altars.size() == 4 && C.altars[0].cooldownSec == 86400 && C.altars[1].gems == 1000 && C.altars[2].gems == 4000);
	KD_CHECK(C.altars[0].weights[3] == 0); // R4 only from the paid altars
	KD_CHECK(C.fusionCastle == 7);
}

void TestEnvoyCouncils()
{
	const KingdomContent& C = Content();
	// Four councils, one per people, one of each; a council's people send their troops to the Barracks queue.
	struct Expect { const char* Troop; const char* Council; int Level; int Castle; };
	const Expect Table[] = {{"troop.grenadiers", "council_sunreach", 1, 5}, {"troop.priests", "council_sunreach", 3, 9},
		{"troop.pikemen", "council_rimeheim", 1, 6}, {"troop.chainbreakers", "council_rimeheim", 3, 10},
		{"troop.goliaths", "council_jade", 1, 7}, {"troop.ballista", "council_jade", 3, 11}, {"troop.panthers", "council_mistmoor", 1, 8}};
	for (const Expect& E : Table)
	{
		const TroopType& T = C.troops[static_cast<size_t>(Troop(E.Troop))];
		KD_CHECK(T.council == Def(E.Council) && T.councilLevel == E.Level && T.unlockCastle == E.Castle);
		const BuildingDef& B = C.buildings[static_cast<size_t>(Def(E.Council))];
		KD_CHECK(B.category == "councils" && B.MaxLevel() == 10 && B.MaxCount(25) == 1);
		// The council level that sends the troop opens no earlier than the troop's Castle level.
		KD_CHECK(B.levels[static_cast<size_t>(E.Level - 1)].castle == E.Castle);
	}
	KD_CHECK(C.buildings[static_cast<size_t>(Def("council_mistmoor"))].unlockCastle == 8); // wiki: Panthers at Castle 8
	for (const char* Id : {"troop.militia", "troop.archers", "troop.spearmen", "troop.swordsmen", "troop.outriders", "troop.knights"})
	{
		KD_CHECK(C.troops[static_cast<size_t>(Troop(Id))].council < 0);
	}

	// Castle 9 with Barracks 3, an Armoury 10 and lots of food; one grenadier unit trained before the councils existed.
	Kingdom K(Content());
	K.NewGame(T0, 31);
	std::string Save = WithBuilding(K.Save(), "armoury", 10, 30);
	Save = Replace(Save, "\"def\":\"castle\",\"level\":1", "\"def\":\"castle\",\"level\":9");
	Save = Replace(Save, "\"food\":" + std::to_string(K.Amount(Res::Food)), "\"food\":5000000");
	Save = Replace(Save, "\"gold\":" + std::to_string(K.Amount(Res::Gold)), "\"gold\":5000000");
	Save = Replace(Save, "\"troop.grenadiers\":0", "\"troop.grenadiers\":1");
	Kingdom L(Content());
	std::string Error;
	KD_CHECK(L.Load(Save, Error));
	const int Gren = Troop("troop.grenadiers");
	const int Pri = Troop("troop.priests");
	KD_CHECK(L.ArmyCount(Gren) == 1); // old units stay in the camp
	Check Ck = L.CheckTrain(Gren);
	KD_CHECK(Ck.error == Error::NeedRequirement && Ck.missing.building == Def("council_sunreach") && Ck.missing.level == 1);
	KD_CHECK(L.CouncilLevel(Gren) == 0 && L.CouncilLevel(Troop("troop.militia")) == -1);
	KD_CHECK(L.TroopLevelCap(Gren) == 1 && L.TroopLevelCap(Troop("troop.militia")) == 11);
	// A council needs Barracks 3 first.
	Check B = L.CheckBuild(Def("council_sunreach"));
	KD_CHECK(B.error == Error::NeedRequirement && B.missing.building == Def("barracks") && B.missing.level == 3);
	// The Mistmoor council (panthers) opens only at Castle 8; this kingdom is at 9, so all four can be built.
	KD_CHECK(L.CheckBuild(Def("council_mistmoor")).error != Error::CastleTooLow);

	// With the council at level 1: grenadiers train, war priests wait for council level 3, the troop level is capped at 3.
	Save = WithBuilding(Replace(L.Save(), "\"def\":\"barracks\",\"level\":1", "\"def\":\"barracks\",\"level\":3"), "council_sunreach", 1, 20);
	Kingdom M(Content());
	KD_CHECK(M.Load(Save, Error));
	KD_CHECK(M.CheckBuild(Def("council_sunreach")).error == Error::LimitReached);
	KD_CHECK(M.CheckTrain(Gren).error == Error::None && M.Train(Gren) == Error::None);
	Ck = M.CheckTrain(Pri);
	KD_CHECK(Ck.error == Error::NeedRequirement && Ck.missing.building == Def("council_sunreach") && Ck.missing.level == 3);
	KD_CHECK(M.CouncilLevel(Gren) == 1 && M.TroopLevelCap(Gren) == 3);
	KD_CHECK(M.UpgradeTroop(Gren, false) == Error::None);
	M.Advance(M.Now() + 30 * 86400);
	KD_CHECK(M.UpgradeTroop(Gren, false) == Error::None);
	M.Advance(M.Now() + 30 * 86400);
	KD_CHECK(M.TroopLevel(Gren) == 3);
	Ck = M.CheckTroopUpgrade(Gren);
	KD_CHECK(Ck.error == Error::NeedRequirement && Ck.missing.building == Def("council_sunreach") && Ck.missing.level == 2);
	KD_CHECK(M.ArmyCount(Gren) == 2);

	// Council level 3: war priests too, and the troop level cap rises to 7.
	Save = Replace(M.Save(), "\"def\":\"council_sunreach\",\"level\":1", "\"def\":\"council_sunreach\",\"level\":3");
	Kingdom N(Content());
	KD_CHECK(N.Load(Save, Error));
	KD_CHECK(N.CheckTrain(Pri).error == Error::None && N.TroopLevelCap(Gren) == 7 && N.CheckTroopUpgrade(Gren).error == Error::None);
	// Another council does not help: pikemen still need the Rimeheim council.
	KD_CHECK(N.CheckTrain(Troop("troop.pikemen")).missing.building == Def("council_rimeheim"));
	Kingdom Back(Content());
	KD_CHECK(Back.Load(N.Save(), Error) && Back.Hash() == N.Hash());
}

void TestTrainingQueue()
{
	Kingdom K(Content());
	K.NewGame(T0, 5);
	const int Mil = Troop("troop.militia");
	KD_CHECK(K.ArmyCount(Mil) == 3 && K.ArmyTotal() == 4 && K.ArmyCapacity() == 10 && K.BattleSlots() == 3);
	KD_CHECK(K.QueueCapacity() == 6 && K.SurvivalPct() == 100 && K.MaxTroopLevel() == 1);
	KD_CHECK(K.CheckTrain(Troop("troop.spearmen")).error == Error::Locked); // Spearmen from Castle 3 (estimate)
	KD_CHECK(Content().troops.size() == 13);
	for (const char* Id : {"troop.swordsmen", "troop.grenadiers", "troop.outriders", "troop.priests", "troop.chainbreakers", "troop.knights",
			 "troop.pikemen", "troop.goliaths", "troop.panthers", "troop.ballista"})
	{
		const int T = Troop(Id);
		KD_CHECK(T >= 0 && Content().troops[static_cast<size_t>(T)].unlockCastle > 3);
		KD_CHECK(T >= 0 && K.CheckTrain(T).error == Error::Locked);
	}
	const int64_t Food = K.Amount(Res::Food);
	for (int I = 0; I < 6; ++I)
	{
		KD_CHECK(K.Train(Mil) == Error::None);
	}
	KD_CHECK(K.Amount(Res::Food) == Food - 6 * 135);
	KD_CHECK(K.Train(Mil) == Error::QueueFull);
	K.Advance(T0 + 59);
	KD_CHECK(K.ArmyCount(Mil) == 3);
	K.Advance(T0 + 60);
	KD_CHECK(K.ArmyCount(Mil) == 4 && K.TrainQueue().size() == 5);
	K.Advance(T0 + 360);
	KD_CHECK(K.ArmyTotal() == 10 && K.TrainQueue().empty());
	// The camp is full: the next unit waits and comes out as soon as there is room.
	KD_CHECK(K.Train(Mil) == Error::None);
	K.Advance(T0 + 2 * Hour);
	KD_CHECK(K.QueueBlocked() && K.ArmyTotal() == 10);
	KD_CHECK(K.DismissTroops(Mil, 1) == Error::None);
	KD_CHECK(K.ArmyTotal() == 10 && K.TrainQueue().empty() && !K.QueueBlocked());
	// Cancelling refunds the Food; skipping finishes the queue for gems.
	KD_CHECK(K.DismissTroops(Mil, 2) == Error::None);
	KD_CHECK(K.Train(Mil) == Error::None && K.Train(Mil) == Error::None);
	const int64_t Before = K.Amount(Res::Food);
	KD_CHECK(K.CancelTrain(1) == Error::None);
	KD_CHECK(K.Amount(Res::Food) == Before + 135 && K.TrainQueue().size() == 1);
	const int64_t Gems = K.Amount(Res::Gems);
	const int64_t Skip = K.TrainSkipCost();
	KD_CHECK(Skip >= 1 && K.SkipTraining() == Error::None);
	KD_CHECK(K.Amount(Res::Gems) == Gems - Skip && K.TrainQueue().empty() && K.ArmyTotal() == 9);
}

void TestTroopResearch()
{
	Kingdom K(Content());
	K.NewGame(T0, 6);
	const int Mil = Troop("troop.militia");
	KD_CHECK(K.CheckTroopUpgrade(Mil).error == Error::NeedRequirement); // no Armoury yet
	std::string Save = WithBuilding(K.Save(), "armoury", 1, 30);
	Save = Replace(Save, "\"food\":2500", "\"food\":50000");
	Kingdom L(Content());
	std::string Error;
	KD_CHECK(L.Load(Save, Error));
	KD_CHECK(L.MaxTroopLevel() == 2);
	const Check Ck = L.CheckTroopUpgrade(Mil);
	KD_CHECK(Ck.error == Error::None && Ck.cost[1] == 3500);
	KD_CHECK(L.UpgradeTroop(Mil, false) == Error::None);
	KD_CHECK(L.CheckTroopUpgrade(Troop("troop.archers")).error == Error::Researching); // one research at a time
	L.Advance(L.Now() + Ck.timeSec - 1);
	KD_CHECK(L.TroopLevel(Mil) == 1);
	L.Advance(L.Now() + 1);
	KD_CHECK(L.TroopLevel(Mil) == 2 && L.ResearchTroop() < 0);
	KD_CHECK(L.CheckTrain(Mil).cost[1] == 215); // level 2 units cost 215 food (wiki)
	const Check Next = L.CheckTroopUpgrade(Mil);
	KD_CHECK(Next.error == Error::NeedRequirement && Next.missing.building == Def("armoury") && Next.missing.level == 2);
}

void TestCasualtiesTitansRelicsFusion()
{
	Kingdom K(Content());
	K.NewGame(T0, 21);
	const int Mil = Troop("troop.militia");
	const int Arch = Troop("troop.archers");
	KD_CHECK(K.Titans().size() == 2);
	const int32_t Perun = K.Titans()[0].id;
	KD_CHECK(K.FindTitan(Perun)->level == 5);
	const WorldLand W = K.World()[0];
	KD_CHECK(K.StartAttack(W.id, false, {Mil, Mil, Mil, Arch}, Perun) == Error::TooManySquads);
	KD_CHECK(K.StartAttack(W.id, false, {Mil, Arch, Arch}, Perun) == Error::NoTroops);
	KD_CHECK(K.StartAttack(W.id, false, {Mil, Mil, Arch}, Perun) == Error::None);
	KD_CHECK(K.PendingSquads().size() == 3 && K.PendingTitan() == Perun);
	Reward R;
	// Without a Medical School only untouched squads come back.
	KD_CHECK(K.FinishAttack(W.id, false, 2, {100, 60, 0}, &R) == Error::None);
	KD_CHECK(R.squadsBack == 1 && R.squadsLost == 2 && K.ArmyCount(Mil) == 2 && K.ArmyCount(Arch) == 0);
	KD_CHECK(R.titanId == Perun && R.titanXp == Content().titanBattleXp[static_cast<size_t>(W.level - 1)] * 120 / 100);
	KD_CHECK(K.FindTitan(Perun)->xp == R.titanXp && K.FindTitan(Perun)->recoverUntil == 0);
	KD_CHECK(K.CheckAltar(0).error == Error::NeedRequirement); // no Relic Temple yet

	// Castle 7 with a Relic Temple, a Medical School and plenty of gems, gold and food.
	std::string Save = WithBuilding(WithBuilding(K.Save(), "relic_temple", 1, 30), "medical_school", 1, 34);
	Save = Replace(Save, "\"def\":\"castle\",\"level\":1", "\"def\":\"castle\",\"level\":7");
	Save = Replace(Save, "\"gems\":500", "\"gems\":60000");
	Save = Replace(Save, "\"gold\":" + std::to_string(K.Amount(Res::Gold)), "\"gold\":900000");
	Save = Replace(Save, "\"food\":" + std::to_string(K.Amount(Res::Food)), "\"food\":900000");
	Kingdom L(Content());
	std::string Error;
	KD_CHECK(L.Load(Save, Error));
	L.Advance(L.Now() + 60);
	KD_CHECK(L.CastleLevel() == 7 && L.SurvivalPct() == 85);
	int32_t Id = 0;
	int32_t RelicId = 0;
	KD_CHECK(L.OpenAltar(0, &Id, &RelicId) == Error::None);
	KD_CHECK((Id && L.FindTitan(Id) && L.Titans().size() == 3) || (RelicId && L.FindRelic(RelicId) && L.Titans().size() == 2)); // a titan or a relic
	KD_CHECK(L.CheckAltar(0).error == Error::NotReady); // once a day
	L.Advance(L.Now() + 24 * Hour);
	KD_CHECK(L.CheckAltar(0).error == Error::None);
	std::vector<int32_t> Fodder;
	bool bRare = true;
	for (int I = 0; I < 12; ++I)
	{
		KD_CHECK(L.OpenAltar(2, &Id) == Error::None);
		bRare = bRare && Content().titans[static_cast<size_t>(L.FindTitan(Id)->def)].stars >= 3;
		Fodder.push_back(Id);
	}
	KD_CHECK(bRare); // the gold altar gives R3 or R4
	KD_CHECK(L.Amount(Res::Gems) == 60000 - 12 * 4000);
	// Fusion: the sacrificed titans' energy goes to the target.
	KD_CHECK(L.CheckFusion(Perun, Fodder).cost[1] >= 12 * 3 * Content().fusionFoodPerStar); // fusion costs food (wiki)
	std::vector<int32_t> Some(Fodder.begin(), Fodder.begin() + 1);
	KD_CHECK(L.CheckFusion(Perun, Some).error == Error::None);
	KD_CHECK(L.FuseTitans(Perun, Some) == Error::None);
	KD_CHECK(!L.FindTitan(Some[0]) && L.FindTitan(Perun)->level > 5);
	for (size_t I = 1; I < Fodder.size(); ++I)
	{
		L.FuseTitans(Perun, {Fodder[I]});
	}
	const int Level = L.FindTitan(Perun)->level;
	KD_CHECK(Level > 10 && Level <= 30); // R2: max level 30
	// Battle with the Medical School: 90% comes back, 80% does not (threshold 85%); the titan rests afterwards.
	const WorldLand W2 = L.World()[0];
	KD_CHECK(L.StartAttack(W2.id, false, {Mil, Mil}, Perun) == Error::None);
	KD_CHECK(L.FinishAttack(W2.id, false, 1, {90, 80}, &R) == Error::None);
	KD_CHECK(R.squadsBack == 1 && R.squadsLost == 1 && L.ArmyCount(Mil) == 1);
	KD_CHECK(L.FindTitan(Perun)->recoverUntil > L.Now());
	KD_CHECK(L.StartAttack(L.World()[0].id, false, {}, Perun) == Error::TitanRecovering);
	KD_CHECK(L.TitanSkipCost(Perun) >= 1 && L.SkipRecovery(Perun) == Error::None);
	KD_CHECK(L.StartAttack(L.World()[0].id, false, {}, Perun) == Error::None);
}

void TestArmySaveAndMigration()
{
	Kingdom A(Content());
	A.NewGame(T0, 44);
	const int Mil = Troop("troop.militia");
	A.Train(Mil);
	A.Train(Mil);
	A.Advance(T0 + 70);
	A.StartAttack(A.World()[1].id, true, {Mil, Mil}, A.Titans()[1].id);
	const std::string Saved = A.Save();
	Kingdom B(Content());
	std::string Error;
	KD_CHECK(B.Load(Saved, Error));
	KD_CHECK(B.Hash() == A.Hash() && B.Save() == Saved);
	KD_CHECK(B.TrainQueue().size() == 1 && B.PendingSquads().size() == 2);
	A.Advance(A.Now() + 600);
	B.Advance(B.Now() + 600);
	KD_CHECK(A.Hash() == B.Hash());
	// A save from before the army (format 1) loads with the starting army and titans.
	Kingdom C(Content());
	KD_CHECK(C.Load("{\"formatVersion\":1,\"time\":1760000000,\"seed\":5,\"nextId\":20,\"resources\":{\"gold\":100},"
					"\"buildings\":[{\"id\":1,\"def\":\"castle\",\"level\":2,\"x\":17,\"y\":17}]}", Error));
	KD_CHECK(C.CastleLevel() == 2 && C.ArmyCount(Mil) == 3 && C.Titans().size() == 2 && C.TroopLevel(Mil) == 1);
	KD_CHECK(C.SpellStock(0) == 0 && C.SpellLevel(0) == 1 && C.Relics().empty());
	KD_CHECK(C.Titans()[0].id >= 20);
}

int SpellIx(const char* Id) { return Content().FindSpell(Id); }

void TestSpellsRelicsLandTitans()
{
	const KingdomContent& Ct = Content();
	// Wiki: the Spell Forge crafts spells; level 1 has a queue of 2 and holds 10, needs Castle 5; level 9: 10 and 65, Castle 21.
	const BuildingDef& Forge = Ct.buildings[static_cast<size_t>(Def("spell_forge"))];
	KD_CHECK(Forge.unlockCastle == 5 && Forge.levels[0].queue == 2 && Forge.levels[0].capacity == 10 && Forge.levels[0].castle == 5);
	KD_CHECK(Forge.levels[8].queue == 10 && Forge.levels[8].capacity == 65 && Forge.levels[8].castle == 21);
	KD_CHECK(Ct.spells.size() == 8 && SpellIx("spell.fireball") >= 0 && SpellIx("spell.acid_cloud") >= 0 && SpellIx("spell.raise_dead") >= 0);

	Kingdom K(Content());
	K.NewGame(T0, 31);
	KD_CHECK(K.CheckCraft(SpellIx("spell.fireball")).error == Error::NeedRequirement); // no Spell Forge yet
	const int32_t Perun = K.Titans()[0].id;
	std::string Save = WithBuilding(WithBuilding(K.Save(), "spell_forge", 2, 30), "arcane_tower", 1, 34);
	Save = Replace(Save, "\"def\":\"castle\",\"level\":1", "\"def\":\"castle\",\"level\":8");
	Save = Replace(Save, "\"gold\":2500", "\"gold\":900000");
	Save = Replace(Save, "\"food\":2500", "\"food\":40000");
	Save = Replace(Save, "\"relics\":[],\"relicAt\"",
		"\"relics\":[{\"id\":800,\"kind\":\"hp\",\"rarity\":3,\"durability\":1},{\"id\":801,\"kind\":\"atk\",\"rarity\":1,\"durability\":10},"
		"{\"id\":802,\"kind\":\"atk\",\"rarity\":1,\"durability\":10},{\"id\":803,\"kind\":\"armor\",\"rarity\":3,\"durability\":18},"
		"{\"id\":804,\"kind\":\"armor\",\"rarity\":3,\"durability\":18},{\"id\":805,\"kind\":\"armor\",\"rarity\":3,\"durability\":18}],\"relicAt\"");
	Kingdom L(Content());
	std::string Error;
	KD_CHECK(L.Load(Save, Error));
	KD_CHECK(L.Relics().size() == 6 && L.SpellCapacity() > 10);
	// Relics from before format 5: the effects a new relic of their rarity has, durability within the new table.
	KD_CHECK(L.FindRelic(800)->levels == std::vector<int>({1, 0, 0}) && L.FindRelic(803)->durability == 12);
	// Crafting: queue by Forge level, one spell after another, into the stock.
	const int Fire = SpellIx("spell.fireball");
	KD_CHECK(L.CheckCraft(SpellIx("spell.lightning")).error == Error::NeedRequirement); // Lightning from Forge 3 (estimate)
	for (int I = 0; I < L.CraftQueueCapacity(); ++I)
	{
		KD_CHECK(L.CraftSpell(Fire) == Error::None);
	}
	KD_CHECK(L.CraftSpell(Fire) == Error::QueueFull);
	L.Advance(L.Now() + Ct.spells[static_cast<size_t>(Fire)].craftSec);
	KD_CHECK(L.SpellStock(Fire) == 1);
	L.Advance(L.Now() + 24 * Hour);
	KD_CHECK(L.SpellStock(Fire) == L.CraftQueueCapacity() && L.CraftQueue().empty());
	// Arcane Tower: one research at a time at level 1; level 2 crafting costs 15% more.
	const int64_t Craft1 = L.CheckCraft(Fire).cost[0];
	KD_CHECK(L.UpgradeSpell(Fire, false) == Error::None);
	KD_CHECK(L.CheckSpellUpgrade(SpellIx("spell.shield")).error == Error::Researching);
	L.Advance(L.Now() + 30 * 24 * Hour);
	KD_CHECK(L.SpellLevel(Fire) == 2 && L.SpellResearch().empty());
	KD_CHECK(L.CheckCraft(Fire).cost[0] == Craft1 * 115 / 100);
	// Relics: one slot at level 5; an R3 health relic gives +8% health (tenths of a percent), its other effects locked.
	KD_CHECK(L.EquipRelic(Perun, 800) == Error::None);
	KD_CHECK(L.EquipRelic(Perun, 801) == Error::LimitReached);
	KD_CHECK(L.TitanRelicBonus(Perun, RelicStat::Hp) == 80 && L.TitanRelicBonus(Perun, RelicStat::Armor) == 0 && L.TitanRelicBonus(Perun, RelicStat::Atk) == 0);
	// A battle with a spell: the cast comes out of the stock and the relic wears out.
	const int64_t Stock = L.SpellStock(Fire);
	const WorldLand W = L.World()[0];
	KD_CHECK(L.StartAttack(W.id, true, {}, Perun, {Fire, Fire}) == Error::BadId);
	KD_CHECK(L.StartAttack(W.id, true, {}, Perun, {Fire}) == Error::None);
	Reward R;
	KD_CHECK(L.FinishAttack(W.id, true, 2, {}, {1}, &R) == Error::None);
	KD_CHECK(L.SpellStock(Fire) == Stock - 1 && R.relicsBroken == 1 && L.TitanRelicBonus(Perun, RelicStat::Hp) == 0);
	// The won battle (two stars) dropped two relics (the original since 1.8: one to three per victory).
	KD_CHECK(R.relics.size() == 2 && R.relicsLost == 0 && L.Relics().size() == 8);
	// Repair (Castle 8) costs food (wiki); each sacrifice adds by the wiki's table: R1 for an R3 relic 10%, R3 40%.
	KD_CHECK(L.RepairChancePct(800, {801, 802}) == 20 && L.RepairChancePct(800, {803, 804}) == 80);
	RepairResult Rep;
	const int64_t Food = L.Amount(Res::Food);
	KD_CHECK(Food >= Ct.relicRepairFood[2]);
	KD_CHECK(L.RepairChancePct(800, {803, 804, 805}) == 100 && L.CanUnlockWith(800, {803, 804, 805}) && !L.CanUnlockWith(800, {801}));
	KD_CHECK(L.RepairRelic(800, {803, 804, 805}, &Rep) == Error::None && Rep.repaired);
	KD_CHECK(L.Amount(Res::Food) == Food - Ct.relicRepairFood[2]);
	// A success may improve the health effect or open the next one (R3 sacrifices may open it).
	const RelicItem& Fixed = *L.FindRelic(800);
	const int64_t HpNow = Rep.slot == 0 ? 92 : 80;
	KD_CHECK(Fixed.durability == 12 && !L.FindRelic(803) && L.TitanRelicBonus(Perun, RelicStat::Hp) == HpNow);
	KD_CHECK(Rep.slot < 0 || (Rep.slot == 0 && Fixed.levels[0] == 2) || (Rep.slot == 1 && Rep.unlocked && Fixed.levels[1] == 1));
	// A titan on a land adds 1% per titan level (wiki).
	KD_CHECK(R.landId != 0);
	const int64_t Base = L.LandRate(*L.FindLand(R.landId));
	KD_CHECK(L.AssignTitan(R.landId, Perun) == Error::None);
	const int Lv = L.FindTitan(Perun)->level;
	KD_CHECK(L.LandRate(*L.FindLand(R.landId)) == Base * (100 + Lv) / 100);
	KD_CHECK(L.RecallTitan(R.landId) == Error::None && L.LandRate(*L.FindLand(R.landId)) == Base);
	// Round trip keeps everything.
	L.AssignTitan(R.landId, Perun);
	L.CraftSpell(Fire);
	const std::string Saved = L.Save();
	Kingdom M(Content());
	KD_CHECK(M.Load(Saved, Error) && M.Hash() == L.Hash() && M.Save() == Saved);
}
/** A relic item in a save (format 5). */
std::string RelicJson(int Id, const char* Kind, int Rarity, int64_t Durability, const char* Levels)
{
	return "{\"id\":" + std::to_string(Id) + ",\"kind\":\"" + Kind + "\",\"rarity\":" + std::to_string(Rarity) + ",\"durability\":" +
		std::to_string(Durability) + ",\"levels\":" + Levels + "}";
}

void TestComplexRelics()
{
	const KingdomContent& Ct = Content();
	// The original's relics since 1.8: combat relics for titans, economy relics for buildings, one to three effects,
	// durability R1 20 / R2 16 / R3 12 / R4 8 battles, the repair table per sacrificed relic, an inventory of 200.
	KD_CHECK(Ct.relicKinds.size() == 15 && Ct.relicEffects.size() == 14 && Ct.maxRelics == 200);
	KD_CHECK(Ct.relicDurability[0] == 20 && Ct.relicDurability[1] == 16 && Ct.relicDurability[2] == 12 && Ct.relicDurability[3] == 8);
	KD_CHECK(Ct.relicRepairPct[0][0] == 50 && Ct.relicRepairPct[1][2] == 80 && Ct.relicRepairPct[3][2] == 20 && Ct.relicRepairPct[3][3] == 40);
	const int Pickaxe = Ct.FindRelicKind("pickaxe");
	KD_CHECK(Pickaxe >= 0 && Ct.relicKinds[static_cast<size_t>(Pickaxe)].economy && Ct.relicKinds[static_cast<size_t>(Ct.FindRelicKind("scroll"))].battles == 2);
	KD_CHECK(Ct.altars.size() == 4 && Ct.altars[3].relics && Ct.altars[3].gems == 1000 && Ct.altars[3].weights[0] == 0);

	Kingdom K(Content());
	K.NewGame(T0, 41);
	const int32_t Perun = K.Titans()[0].id;
	const int32_t Talos = K.Titans()[1].id;
	std::string Save = WithBuilding(K.Save(), "relic_temple", 1, 34);
	// An empty level-10 Gold Mine.
	Save = Replace(Save, "\"buildings\":[", "\"buildings\":[{\"id\":930,\"def\":\"gold_mine\",\"level\":10,\"x\":30,\"y\":30,\"lastAt\":" + std::to_string(T0) + "},");
	Save = Replace(Save, "\"def\":\"castle\",\"level\":1", "\"def\":\"castle\",\"level\":8");
	Save = Replace(Save, "\"gems\":500", "\"gems\":60000");
	Save = Replace(Save, "\"food\":2500", "\"food\":900000");
	Save = Replace(Save, "\"def\":\"titan.perun\",\"level\":5", "\"def\":\"titan.perun\",\"level\":20");
	std::string Relics = RelicJson(700, "pickaxe", 3, 3600, "[1,0]") + "," + RelicJson(701, "atk", 1, 20, "[1]") + "," + RelicJson(702, "atk", 1, 20, "[1]") +
		"," + RelicJson(703, "hp", 1, 20, "[1]") + "," + RelicJson(704, "scroll", 3, 2, "[1]") + "," + RelicJson(705, "bugle", 2, 100000, "[1]") +
		"," + RelicJson(706, "hp", 2, 16, "[1,0]") + "," + RelicJson(707, "hp", 2, 16, "[4,4]") + "," + RelicJson(708, "hp", 2, 16, "[4,0]");
	for (int I = 0; I < 30; ++I)
	{
		Relics += "," + RelicJson(710 + I, "armor", 1, 20, "[1]"); // R1 fodder
	}
	for (int I = 0; I < 12; ++I)
	{
		Relics += "," + RelicJson(750 + I, "eye", 2, 16, "[1,0]"); // R2 fodder
	}
	Save = Replace(Save, "\"relics\":[],\"relicAt\"", "\"relics\":[" + Relics + "],\"relicAt\"");
	Kingdom L(Content());
	std::string Error;
	KD_CHECK(L.Load(Save, Error));
	KD_CHECK(L.Relics().size() == 51 && L.FindTitan(Perun)->level == 20 && L.RelicSlots(*L.FindTitan(Perun)) == 2);
	const int32_t Mine = 930;
	const int32_t Farm = FindId(L, "farm");
	const int32_t Barracks = FindId(L, "barracks");

	// Economy relics attach to the buildings of their kind only, combat relics to titans only.
	KD_CHECK(L.AttachRelic(Farm, 700) == Error::WrongRelic && L.AttachRelic(Mine, 701) == Error::WrongRelic);
	KD_CHECK(L.EquipRelic(Perun, 700) == Error::WrongRelic);
	// A pickaxe (R3, +12% production) on a level-10 Gold Mine (895 an hour, wiki) for its last hour, then without it.
	const int64_t Before = L.ProductionPerHour(Res::Gold);
	KD_CHECK(L.Uncollected(*L.Find(Mine)) == 0);
	KD_CHECK(L.AttachRelic(Mine, 700) == Error::None && L.BuildingRelic(Mine)->id == 700);
	KD_CHECK(L.ProductionPerHour(Res::Gold) == Before - 895 + 895 * 1120 / 1000);
	L.Advance(L.Now() + 3 * Hour);
	KD_CHECK(L.RelicLeft(*L.FindRelic(700)) == 0 && L.ProductionPerHour(Res::Gold) == Before);
	KD_CHECK(L.Uncollected(*L.Find(Mine)) == 895 * 1120 / 1000 + 2 * 895);

	// A titan cannot wear two identical relics (the original); two different ones fit two slots at level 20.
	KD_CHECK(L.EquipRelic(Perun, 701) == Error::None && L.EquipRelic(Perun, 702) == Error::SameRelic && L.EquipRelic(Perun, 703) == Error::None);
	KD_CHECK(L.TitanRelicBonus(Perun, RelicStat::Atk) == 30 && L.TitanRelicBonus(Perun, RelicStat::Hp) == 30);

	// An XP relic (two battles) adds its share to the titan's battle XP; the victory drops one relic per star.
	KD_CHECK(L.EquipRelic(Talos, 704) == Error::None);
	const WorldLand W = L.World()[0];
	KD_CHECK(L.StartAttack(W.id, false, {}, Talos) == Error::None);
	Reward R;
	KD_CHECK(L.FinishAttack(W.id, false, 3, {}, &R) == Error::None);
	const int64_t BaseXp = Ct.titanBattleXp[static_cast<size_t>(W.level - 1)] * Ct.titanStarsPct[3] / 100;
	KD_CHECK(R.titanXp == BaseXp * 1400 / 1000 && L.FindRelic(704)->durability == 1);
	KD_CHECK(R.relics.size() == 3 && L.Relics().size() == 54);

	// A relic on the Barracks trains every troop faster (R2: 8%).
	const int Mil = Troop("troop.militia");
	KD_CHECK(L.CheckTrain(Mil).timeSec == 60);
	KD_CHECK(L.AttachRelic(Barracks, 705) == Error::None && L.CheckTrain(Mil).timeSec == 56);

	// Repair by the wiki's table: an R1 sacrifice adds 20% for an R2 relic, an R2 one 40%.
	KD_CHECK(L.RepairChancePct(706, {710, 711}) == 40 && L.RepairChancePct(706, {750}) == 40 && L.RepairChancePct(706, {750, 751, 752}) == 100);
	KD_CHECK(L.EnhanceChancePct(706) == 70);
	// At its best and whole, a relic cannot be repaired; a locked effect needs a sacrifice of at least its rarity.
	KD_CHECK(L.RepairRelic(707, {710}) == Error::Nothing && L.RepairRelic(708, {710}) == Error::Nothing);
	KD_CHECK(!L.CanUnlockWith(706, {710}) && L.CanUnlockWith(706, {750}));
	// A whole relic can be repaired for the improvement (the original). R1 sacrifices only raise the open effect,
	// and once it is at its best they cannot do anything more.
	int Improved = 0;
	int G = 0;
	for (; G < 6 && L.FindRelic(706)->levels[0] < Ct.relicLevels; ++G)
	{
		std::vector<int32_t> Group;
		for (int I = 0; I < 5; ++I)
		{
			Group.push_back(710 + 5 * G + I);
		}
		RepairResult Rep;
		const std::vector<int> LevelsBefore = L.FindRelic(706)->levels;
		KD_CHECK(L.RepairRelic(706, Group, &Rep) == Error::None && Rep.repaired && !Rep.unlocked);
		const std::vector<int>& Now = L.FindRelic(706)->levels;
		KD_CHECK(Now[1] == 0 && (Rep.slot < 0 ? Now == LevelsBefore : Now[0] == LevelsBefore[0] + 1));
		Improved += Rep.slot >= 0 ? 1 : 0;
	}
	KD_CHECK(L.FindRelic(706)->levels[0] == 1 + Improved && L.FindRelic(706)->levels[0] == Ct.relicLevels);
	KD_CHECK(L.TitanRelicBonus(Perun, RelicStat::Hp) == 30 && L.RelicEffectValue(*L.FindRelic(706), 0) == 50 * 145 / 100);
	KD_CHECK(G == 6 || L.RepairRelic(706, {710 + 5 * G}) == Error::Nothing);
	// R2 sacrifices can open the second effect.
	bool bOpened = false;
	for (int Q = 0; Q < 4; ++Q)
	{
		RepairResult Rep;
		KD_CHECK(L.RepairRelic(706, {750 + 3 * Q, 751 + 3 * Q, 752 + 3 * Q}, &Rep) == Error::None && Rep.repaired);
		bOpened = bOpened || Rep.unlocked;
	}
	KD_CHECK(bOpened == (L.FindRelic(706)->levels[1] > 0));

	// The Altar of Power sells a relic item for 1 000 gems (1.8).
	int32_t Titan = 0;
	int32_t Relic = 0;
	const int64_t Gems = L.Amount(Res::Gems);
	KD_CHECK(L.OpenAltar(3, &Titan, &Relic) == Error::None && Titan == 0 && Relic != 0 && L.FindRelic(Relic)->rarity >= 2);
	KD_CHECK(L.Amount(Res::Gems) == Gems - 1000);

	// Round trip keeps attached relics, effect levels and the time an economy relic has left.
	const std::string Saved = L.Save();
	Kingdom M(Content());
	KD_CHECK(M.Load(Saved, Error) && M.Hash() == L.Hash() && M.Save() == Saved);
	KD_CHECK(M.BuildingRelic(Barracks) && M.CheckTrain(Mil).timeSec == 56);

	// A full inventory: no altar relics, and the battle's drops are lost.
	std::string Many = RelicJson(1000, "atk", 1, 20, "[1]");
	for (int I = 1; I < Ct.maxRelics; ++I)
	{
		Many += "," + RelicJson(1000 + I, "atk", 1, 20, "[1]");
	}
	const std::string Full = Replace(K.Save(), "\"relics\":[],\"relicAt\"", "\"relics\":[" + Many + "],\"relicAt\"");
	Kingdom N(Content());
	KD_CHECK(N.Load(WithBuilding(Full, "relic_temple", 1, 34), Error) && static_cast<int>(N.Relics().size()) == Ct.maxRelics);
	KD_CHECK(N.CheckAltar(3).error == Error::InventoryFull);
	const WorldLand W2 = N.World()[0];
	KD_CHECK(N.StartAttack(W2.id, false, {}, N.Titans()[0].id) == Error::None);
	Reward R2;
	KD_CHECK(N.FinishAttack(W2.id, false, 2, {}, &R2) == Error::None && R2.relics.empty() && R2.relicsLost == 2);
}

int LandTypeIx(const char* Id)
{
	const KingdomContent& C = Content();
	for (size_t I = 0; I < C.landTypes.size(); ++I)
	{
		if (C.landTypes[I].id == Id)
		{
			return static_cast<int>(I);
		}
	}
	return -1;
}

int64_t ResOf(const Reward& R, Res Kind) { return R.res[static_cast<size_t>(ResIndex(Kind))]; }

void TestRuinsAndVictoryPointLands()
{
	const KingdomContent& Ct = Content();
	// Wiki: lands are gold, farm, ruins (shards) and victory-point lands (forest, lake, mountain);
	// VP lands fill up in 4 hours; captured ruins make one shard per 24 hours (version 1.8).
	KD_CHECK(LandTypeIx("ruins") >= 0 && LandTypeIx("forest") >= 0 && LandTypeIx("lake") >= 0 && LandTypeIx("mountain") >= 0);
	const LandTypeDef& Ruins = Ct.landTypes[static_cast<size_t>(LandTypeIx("ruins"))];
	KD_CHECK(Ruins.resource == Res::Shards && Ruins.periodHours == 24 && Ruins.produce[0] == 1 && Ruins.produce[8] == 1);
	for (const char* Id : {"forest", "lake", "mountain"})
	{
		const LandTypeDef& T = Ct.landTypes[static_cast<size_t>(LandTypeIx(Id))];
		KD_CHECK(T.resource == Res::Vp && T.fillHours.size() == 1 && T.fillHours[0] == 4);
	}
	// A new kingdom (Castle 1) sees only gold and farm lands; the others come with the Castle.
	Kingdom K(Content());
	K.NewGame(T0, 41);
	for (const WorldLand& W : K.World())
	{
		KD_CHECK(Ct.landTypes[static_cast<size_t>(W.type)].castle <= 1);
	}
	std::string Save = Replace(K.Save(), "\"def\":\"castle\",\"level\":1", "\"def\":\"castle\",\"level\":6");
	Save = Replace(Save, "\"world\":[",
		"\"world\":[{\"id\":700,\"type\":\"ruins\",\"level\":2},{\"id\":701,\"type\":\"forest\",\"level\":3},"
		"{\"id\":702,\"type\":\"gold_land\",\"level\":1},{\"id\":703,\"type\":\"ruins\",\"level\":4},");
	Kingdom L(Content());
	std::string Error;
	KD_CHECK(L.Load(Save, Error));
	// A raid on ruins brings shards (at least one for any victory) and VP for the win.
	Reward R;
	KD_CHECK(L.StartAttack(700, false) == Error::None);
	KD_CHECK(L.FinishAttack(700, false, 1, &R) == Error::None);
	KD_CHECK(ResOf(R, Res::Shards) == 1 && L.Amount(Res::Shards) == 1);
	KD_CHECK(ResOf(R, Res::Vp) == Ct.victoryVp[1] / 2 && ResOf(R, Res::Gold) == 0 && ResOf(R, Res::Food) == 0);
	// Capturing a forest: it makes VP by the hour and is full in 4 hours.
	KD_CHECK(L.StartAttack(701, true) == Error::None);
	KD_CHECK(L.FinishAttack(701, true, 3, &R) == Error::None && R.landId != 0);
	const int32_t Forest = R.landId;
	KD_CHECK(ResOf(R, Res::Vp) == Ct.victoryVp[2]);
	KD_CHECK(L.LandRate(*L.FindLand(Forest)) == 7 && L.LandStoreCap(*L.FindLand(Forest)) == 28);
	L.Advance(L.Now() + Hour);
	KD_CHECK(L.UncollectedLand(*L.FindLand(Forest)) == 7);
	L.Advance(L.Now() + 10 * Hour);
	KD_CHECK(L.UncollectedLand(*L.FindLand(Forest)) == 28);
	const int64_t Vp = L.Amount(Res::Vp);
	int64_t Got = 0;
	KD_CHECK(L.CollectLand(Forest, &Got) == Error::None && Got == 28 && L.Amount(Res::Vp) == Vp + 28);
	KD_CHECK(L.ProductionPerHour(Res::Vp) == 7);
	// Captured ruins: one shard a day, three at most (estimate); a titan guard speeds them up by 1% a level.
	KD_CHECK(L.StartAttack(703, true) == Error::None);
	KD_CHECK(L.FinishAttack(703, true, 2, &R) == Error::None && R.landId != 0);
	const int32_t Ruin = R.landId;
	KD_CHECK(L.LandRate(*L.FindLand(Ruin)) == 1 && L.LandStoreCap(*L.FindLand(Ruin)) == 3);
	L.Advance(L.Now() + 24 * Hour - 1);
	KD_CHECK(L.UncollectedLand(*L.FindLand(Ruin)) == 0);
	L.Advance(L.Now() + 1);
	KD_CHECK(L.UncollectedLand(*L.FindLand(Ruin)) == 1);
	KD_CHECK(L.CollectLand(Ruin) == Error::None && L.Amount(Res::Shards) == 2);
	const int32_t Perun = L.Titans()[0].id; // level 5: a shard every 24 h / 1.05
	KD_CHECK(L.AssignTitan(Ruin, Perun) == Error::None);
	L.Advance(L.Now() + 23 * Hour);
	KD_CHECK(L.UncollectedLand(*L.FindLand(Ruin)) == 1);
	L.Advance(L.Now() + 30 * 24 * Hour);
	KD_CHECK(L.UncollectedLand(*L.FindLand(Ruin)) == 3);
	// Shards and VP survive a save round trip.
	const std::string Saved = L.Save();
	Kingdom M(Content());
	KD_CHECK(M.Load(Saved, Error) && M.Hash() == L.Hash() && M.Save() == Saved);
}

void TestSeasons()
{
	const KingdomContent& Ct = Content();
	KD_CHECK(Ct.seasonSec == 7 * 86400 && Ct.leagues.size() == 6 && Ct.leagues[1].vp == 400);
	Kingdom K(Content());
	K.NewGame(T0, 43);
	const int64_t Season = K.Season();
	KD_CHECK(Season == K.SeasonOf(T0) && K.SeasonStart(Season) <= T0 && T0 < K.SeasonStart(Season + 1));
	KD_CHECK(K.League(99) == -1 && K.League(100) == 0 && K.League(450) == 1 && K.League(1000000) == 5);
	// 450 VP reach the Silver league: at the end of the season VP returns to zero and the league pays.
	std::string Error;
	Kingdom L(Content());
	KD_CHECK(L.Load(Replace(K.Save(), "\"shards\":0,\"vp\":0", "\"shards\":0,\"vp\":450"), Error));
	KD_CHECK(L.Amount(Res::Vp) == 450);
	for (int I = 0; I < 4; ++I)
	{
		L.StartAttack(L.World()[0].id, false);
		L.FinishAttack(L.World()[0].id, false, 0); // lost battles: 12 stones spent, no VP
	}
	const int64_t End = L.SeasonStart(Season + 1);
	L.Advance(End - 1);
	KD_CHECK(L.Amount(Res::Vp) == 450 && L.LastSeason().season == -1);
	const int64_t Gems = L.Amount(Res::Gems);
	const int64_t Stones = L.Amount(Res::Stones);
	L.Advance(End);
	KD_CHECK(L.Amount(Res::Vp) == 0 && L.Season() == Season + 1);
	const SeasonResult& Last = L.LastSeason();
	KD_CHECK(Last.season == Season && Last.vp == 450 && Last.league == 1 && Last.gems == Ct.leagues[1].gems);
	KD_CHECK(L.Amount(Res::Gems) == Gems + Ct.leagues[1].gems);
	KD_CHECK(Last.stones == std::min(Ct.leagues[1].stones, L.Capacity(Res::Stones) - Stones) && L.Amount(Res::Stones) == Stones + Last.stones);
	// Weeks away: every season is closed in turn, empty ones leave the last result alone.
	L.Advance(End + 3 * Ct.seasonSec + 5);
	KD_CHECK(L.Season() == Season + 4 && L.LastSeason().season == Season);
	const std::string Saved = L.Save();
	Kingdom M(Content());
	KD_CHECK(M.Load(Saved, Error) && M.Hash() == L.Hash() && M.Save() == Saved && M.LastSeason().league == 1);
	// A version 3 save (before seasons, shards and VP) starts in the current season with nothing to pay.
	std::string V3 = Replace(K.Save(), "\"formatVersion\":6", "\"formatVersion\":3");
	const size_t S0 = V3.find("\"season\":");
	const size_t S1 = V3.find("\"resources\":");
	KD_CHECK(S0 != std::string::npos && S1 > S0);
	V3.erase(S0, S1 - S0);
	V3 = Replace(V3, ",\"shards\":0,\"vp\":0", "");
	Kingdom O(Content());
	KD_CHECK(O.Load(V3, Error) && O.Season() == Season && O.LastSeason().season == -1 && O.Amount(Res::Vp) == 0);
	KD_CHECK(O.Save().find("\"formatVersion\":6") != std::string::npos && O.Hash() == K.Hash());
}

void TestAscension()
{
	const KingdomContent& Ct = Content();
	// Wiki (Ascension): R1 needs 5 shards and 180 000 food ... R4 100 shards and 4 800 000; ten more levels.
	KD_CHECK(Ct.ascendShards[0] == 5 && Ct.ascendShards[3] == 100 && Ct.ascendFood[0] == 180000 && Ct.ascendFood[3] == 4800000);
	KD_CHECK(Ct.ascendLevels == 10 && Ct.ascendRelicSlots == 1);
	Kingdom K(Content());
	K.NewGame(T0, 47);
	std::string Save = Replace(K.Save(), "\"def\":\"titan.talos\",\"level\":5", "\"def\":\"titan.talos\",\"level\":20");
	std::string Error;
	Kingdom L(Content());
	KD_CHECK(L.Load(Save, Error));
	const int32_t Perun = L.Titans()[0].id;
	const int32_t Talos = L.Titans()[1].id;
	KD_CHECK(L.FindTitan(Talos)->level == 20 && L.TitanMaxLevel(*L.FindTitan(Talos)) == 20);
	KD_CHECK(L.CheckAscend(Perun).error == Error::NotMaxLevel); // only a max-level titan ascends
	Check Ck = L.CheckAscend(Talos);
	KD_CHECK(Ck.error == Error::NotEnough && Ck.gemsForMissing == -1); // shards cannot be bought
	KD_CHECK(Ck.cost[static_cast<size_t>(ResIndex(Res::Shards))] == 5 && Ck.cost[static_cast<size_t>(ResIndex(Res::Food))] == 180000);
	KD_CHECK(L.Ascend(Talos, true) == Error::NotEnough);
	// At the cap a battle's XP is lost.
	L.StartAttack(L.World()[0].id, false, {}, Talos);
	L.FinishAttack(L.World()[0].id, false, 3);
	KD_CHECK(L.FindTitan(Talos)->xp == 0 && L.FindTitan(Talos)->level == 20);
	const int Slots = L.RelicSlots(*L.FindTitan(Talos));
	// With the shards in hand the missing food can be bought with gems.
	Save = Replace(L.Save(), "\"shards\":0", "\"shards\":7");
	Save = Replace(Save, "\"gems\":500", "\"gems\":5000");
	Kingdom A(Content());
	KD_CHECK(A.Load(Save, Error));
	Ck = A.CheckAscend(Talos);
	KD_CHECK(Ck.error == Error::NotEnough && Ck.gemsForMissing > 0);
	KD_CHECK(A.Ascend(Talos, false) == Error::NotEnough);
	const int64_t Gems = A.Amount(Res::Gems);
	KD_CHECK(A.Ascend(Talos, true) == Error::None);
	KD_CHECK(A.Amount(Res::Shards) == 2 && A.Amount(Res::Gems) == Gems - Ck.gemsForMissing && A.Amount(Res::Food) == 0);
	const kingdom::TitanState& T = *A.FindTitan(Talos);
	KD_CHECK(T.ascended && A.TitanMaxLevel(T) == 30 && A.RelicSlots(T) == Slots + 1);
	KD_CHECK(A.CheckAscend(Talos).error == Error::MaxLevel);
	// Past the old cap XP counts again.
	A.Advance(A.Now() + 24 * Hour);
	A.StartAttack(A.World()[0].id, false, {}, Talos);
	A.FinishAttack(A.World()[0].id, false, 3);
	KD_CHECK(A.FindTitan(Talos)->xp > 0);
	const std::string Saved = A.Save();
	Kingdom M(Content());
	KD_CHECK(M.Load(Saved, Error) && M.Hash() == A.Hash() && M.Save() == Saved && M.FindTitan(Talos)->ascended);
}
} // namespace

/** A kingdom of this seed with this much VP this season. */
Kingdom WithVp(uint64_t Seed, int64_t Vp, const std::string& Extra = std::string())
{
	Kingdom K(Content());
	K.NewGame(T0, Seed);
	std::string Save = Replace(K.Save(), "\"shards\":0,\"vp\":0", "\"shards\":0,\"vp\":" + std::to_string(Vp));
	if (!Extra.empty())
	{
		Save = WithBuilding(Save, Extra.c_str(), 3, 2);
	}
	Kingdom L(Content());
	std::string Error;
	KD_CHECK(L.Load(Save, Error));
	return L;
}

int LeagueSize(const Kingdom& K, int Tier) { return static_cast<int>(K.LeagueTable(Tier).size()); }

void TestAlliances()
{
	const KingdomContent& Ct = Content();
	const AllianceRules& A = Ct.alliances;
	// The original: alliances open at 1 000 VP; ours: 6 leagues of 8 alliances, 100 gems to found one.
	KD_CHECK(A.joinVp == 1000 && A.createGems == 100 && A.leagues.size() == 6 && A.names.size() == 48 && A.groupSize == 8);
	KD_CHECK(A.memberNames.size() == 79 && A.minMembers == 8 && A.maxMembers == 30 && A.promote == 2);
	KD_CHECK(A.leagues[0].crystal == 1 && A.leagues[5].crystal == 4); // rarity 4 crystals only at the top
	const BuildingDef& Camp = Ct.buildings[static_cast<size_t>(Def("alliance_camp"))];
	KD_CHECK(Ct.allianceCamp == Def("alliance_camp") && Camp.levels[0].reinforcements == 1 && Camp.levels[9].reinforcements == 10);

	// A new kingdom is in no alliance; every league starts with eight computer alliances.
	Kingdom K(Content());
	K.NewGame(T0, 61);
	KD_CHECK(K.MyAlliance() == -1 && K.AllianceCount() == 48);
	for (int T = 0; T < 6; ++T)
	{
		KD_CHECK(LeagueSize(K, T) == 8);
	}
	std::vector<std::string> Seen;
	for (int I = 0; I < K.AllianceCount(); ++I)
	{
		const AllianceInfo Info = K.Alliance(I);
		KD_CHECK(Info.members >= 8 && Info.members <= 30 && !Info.name.empty() && !Info.tag.empty() && !Info.own);
		KD_CHECK(Info.joinVp >= 1000 && Info.joinVp <= 8000 && Info.joinVp % 500 == 0);
		KD_CHECK(std::find(Seen.begin(), Seen.end(), Info.name) == Seen.end());
		Seen.push_back(Info.name);
		// Names never repeat inside an alliance.
		const std::vector<AllianceMember> M = K.Members(I);
		KD_CHECK(static_cast<int>(M.size()) == Info.members && M[0].role == 0);
		for (size_t X = 0; X < M.size(); ++X)
		{
			for (size_t Y = X + 1; Y < M.size(); ++Y)
			{
				KD_CHECK(M[X].name != M[Y].name);
			}
		}
	}
	// Another seed meets other neighbours.
	Kingdom Other(Content());
	Other.NewGame(T0, 62);
	KD_CHECK(Other.Alliance(0).name != K.Alliance(0).name || Other.Alliance(1).name != K.Alliance(1).name);
	// Members play through the week: VP only grow, and the season's start is quiet.
	const int64_t Season = K.Season();
	const int64_t Start = K.SeasonStart(Season);
	const int64_t End = K.SeasonStart(Season + 1);
	KD_CHECK(K.Alliance(47).vp >= 0);
	Kingdom Mid(Content());
	std::string Error;
	KD_CHECK(Mid.Load(K.Save(), Error));
	Mid.Advance(Start + (End - Start) / 2);
	Kingdom Late(Content());
	KD_CHECK(Late.Load(K.Save(), Error));
	Late.Advance(End - 60);
	KD_CHECK(Late.Alliance(47).vp > Mid.Alliance(47).vp && Mid.Alliance(47).vp > 0);
	// Stronger leagues gather more VP over a week than weaker ones (on average).
	int64_t Low = 0;
	int64_t High = 0;
	for (const int I : Late.LeagueTable(0))
	{
		Low += Late.Alliance(I).vp;
	}
	for (const int I : Late.LeagueTable(5))
	{
		High += Late.Alliance(I).vp;
	}
	KD_CHECK(High > 5 * Low);

	// Below 1 000 VP nothing opens.
	KD_CHECK(K.CheckJoin(0).error == Error::NeedVp && K.CheckCreate().error == Error::NeedVp && K.JoinAlliance(0) == Error::NeedVp);
	KD_CHECK(K.CheckReinforce().error == Error::NoAlliance && K.LeaveAlliance() == Error::NoAlliance);
	// At 1 000 VP the alliances that ask 1 000 take the player.
	Kingdom L = WithVp(61, 1000);
	int Easy = -1;
	int Strong = -1;
	int Full = -1;
	for (int I = 0; I < L.AllianceCount(); ++I)
	{
		const AllianceInfo Info = L.Alliance(I);
		if (Easy < 0 && Info.joinVp == 1000 && Info.members < 30 && Info.tier == 0)
		{
			Easy = I;
		}
		Strong = Info.joinVp > 1000 ? I : Strong;
		Full = Info.members == 30 ? I : Full;
	}
	KD_CHECK(Easy >= 0 && Strong >= 0);
	KD_CHECK(L.CheckJoin(Strong).error == Error::NeedVp && L.CheckJoin(Strong).cost[static_cast<size_t>(ResIndex(Res::Vp))] == L.Alliance(Strong).joinVp);
	if (Full >= 0)
	{
		Kingdom Rich = WithVp(61, 8000);
		KD_CHECK(Rich.CheckJoin(Full).error == Error::AllianceFull);
	}
	const int Before = L.Alliance(Easy).members;
	const int64_t VpBefore = L.Alliance(Easy).vp;
	KD_CHECK(L.JoinAlliance(Easy) == Error::None && L.MyAlliance() == Easy && L.AllianceSince() == T0);
	KD_CHECK(L.Alliance(Easy).members == Before + 1 && L.Alliance(Easy).vp == VpBefore + 1000); // the player's VP count too
	KD_CHECK(L.Members(Easy).back().name.empty() && L.Members(Easy).back().vp == 1000);
	KD_CHECK(L.CheckJoin(0).error == Error::InAlliance && L.CheckCreate().error == Error::InAlliance);
	// Last week's VP open alliances too: the season can end with fewer VP than it started.
	KD_CHECK(L.AllianceRating() == 1000);

	// The season ends: every league is ranked, two go up and two come down, the alliance pays its members.
	const int Tier = L.Alliance(Easy).tier;
	std::vector<int> TiersBefore;
	for (int I = 0; I < L.AllianceCount(); ++I)
	{
		TiersBefore.push_back(L.Alliance(I).tier);
	}
	L.Advance(End - 1);
	const std::vector<int> Table = L.LeagueTable(Tier);
	int Place = 0;
	for (size_t P = 0; P < Table.size(); ++P)
	{
		Place = Table[P] == Easy ? static_cast<int>(P) + 1 : Place;
	}
	const SeasonResult Expect = L.LeagueReward(Tier, Place, 8);
	const int64_t Gems = L.Amount(Res::Gems);
	const size_t Titans = L.Titans().size();
	const size_t Relics = L.Relics().size();
	L.Advance(End);
	const SeasonResult& R = L.LastSeason();
	KD_CHECK(R.season == Season && R.alliance == Easy && R.allianceTier == Tier && R.place == Place && R.groupSize == 8 && R.vp == 1000);
	KD_CHECK(R.gems == Expect.gems && R.gems == A.leagues[static_cast<size_t>(Tier)].gems * std::max(30, 100 - 10 * (Place - 1)) / 100);
	KD_CHECK(R.crystal == A.leagues[static_cast<size_t>(Tier)].crystal);
	KD_CHECK((R.crystalTitan != 0) + (R.crystalRelic != 0) + (R.crystalGems != 0) == 1);
	KD_CHECK(L.Titans().size() == Titans + (R.crystalTitan ? 1 : 0) && L.Relics().size() == Relics + (R.crystalRelic ? 1 : 0));
	KD_CHECK(L.Amount(Res::Gems) == Gems + R.gems + R.crystalGems); // the personal league does not pay as well
	if (R.crystalTitan)
	{
		KD_CHECK(Ct.titans[static_cast<size_t>(L.FindTitan(R.crystalTitan)->def)].stars == R.crystal);
	}
	if (R.crystalRelic)
	{
		KD_CHECK(L.FindRelic(R.crystalRelic)->rarity == R.crystal);
	}
	KD_CHECK(R.move == (Place <= 2 && Tier < 5 ? 1 : (Place >= 7 && Tier > 0 ? -1 : 0)));
	KD_CHECK(L.Alliance(Easy).tier == Tier + R.move && L.Amount(Res::Vp) == 0 && L.AllianceRating() == 1000);
	int Moved = 0;
	for (int I = 0; I < L.AllianceCount(); ++I)
	{
		Moved += L.Alliance(I).tier != TiersBefore[static_cast<size_t>(I)] ? 1 : 0;
	}
	KD_CHECK(Moved == 2 * 2 * 5); // five borders, two each way
	for (int T = 0; T < 6; ++T)
	{
		KD_CHECK(LeagueSize(L, T) == 8);
	}
	// A save keeps the alliance, its league and every other alliance's.
	const std::string Saved = L.Save();
	Kingdom M(Content());
	KD_CHECK(M.Load(Saved, Error) && M.Hash() == L.Hash() && M.Save() == Saved && M.MyAlliance() == Easy && M.LastSeason().alliance == Easy);
	// The same actions give the same leagues.
	Kingdom N = WithVp(61, 1000);
	N.JoinAlliance(Easy);
	N.Advance(End);
	KD_CHECK(N.Hash() == L.Hash());

	// Joining in the last day: the personal league pays this time (Gold league for 1 000 VP).
	Kingdom J = WithVp(61, 1000);
	J.Advance(End - 3600);
	KD_CHECK(J.JoinAlliance(Easy) == Error::None);
	const int64_t JGems = J.Amount(Res::Gems);
	J.Advance(End);
	KD_CHECK(J.LastSeason().alliance == -1 && J.LastSeason().league == 2 && J.Amount(Res::Gems) == JGems + Ct.leagues[2].gems);
	KD_CHECK(J.MyAlliance() == Easy); // still a member for the next season

	// An own alliance: 100 gems, the lowest league, a recruit every 6 hours.
	Kingdom O = WithVp(63, 1200);
	KD_CHECK(O.CreateAlliance("", "ТЕСТ") == Error::BadId && O.CreateAlliance("Слишком длинное имя альянса, чтобы его принять", "Т") == Error::BadId);
	KD_CHECK(O.CreateAlliance("Дом", "Слишком") == Error::BadId);
	const int64_t OGems = O.Amount(Res::Gems);
	KD_CHECK(O.CreateAlliance("  Дом Рассвета ", "ДОМ") == Error::None && O.Amount(Res::Gems) == OGems - 100);
	const int Own = O.MyAlliance();
	KD_CHECK(Own == 48 && O.AllianceCount() == 49 && LeagueSize(O, 0) == 9);
	AllianceInfo Mine = O.Alliance(Own);
	KD_CHECK(Mine.own && Mine.name == "Дом Рассвета" && Mine.tag == "ДОМ" && Mine.tier == 0 && Mine.members == 1 && Mine.vp == 1200);
	KD_CHECK(O.Members(Own).size() == 1 && O.Members(Own)[0].role == 0);
	O.Advance(T0 + 6 * Hour);
	KD_CHECK(O.Alliance(Own).members == 2);
	O.Advance(T0 + 30 * Hour);
	KD_CHECK(O.Alliance(Own).members == 6 && O.Members(Own).size() == 6);
	const std::string OSave = O.Save();
	Kingdom P(Content());
	KD_CHECK(P.Load(OSave, Error) && P.Hash() == O.Hash() && P.Save() == OSave && P.Alliance(Own).name == "Дом Рассвета");
	KD_CHECK(O.LeaveAlliance() == Error::None && O.MyAlliance() == -1 && O.AllianceCount() == 48 && LeagueSize(O, 0) == 8);

	// Reinforcements: the Alliance Camp (level 3 holds two squads) and an alliance.
	Kingdom Q = WithVp(64, 1000, "alliance_camp");
	KD_CHECK(Q.ReinforceCapacity() == 2 && Q.CheckReinforce().error == Error::NoAlliance);
	int Pick = -1;
	for (int I = 0; I < Q.AllianceCount() && Pick < 0; ++I)
	{
		Pick = Q.CheckJoin(I).error == Error::None ? I : -1;
	}
	KD_CHECK(Pick >= 0 && Q.JoinAlliance(Pick) == Error::None);
	KD_CHECK(Q.CheckReinforce().error == Error::None && Q.RequestReinforcements() == Error::None);
	KD_CHECK(Q.Reinforcements().size() == 2 && Q.ReinforcementsReady() == 0 && Q.CheckReinforce().error == Error::StorageFull);
	for (const Reinforcement& Rf : Q.Reinforcements())
	{
		KD_CHECK(Ct.troops[static_cast<size_t>(Rf.troop)].unlockCastle <= Q.CastleLevel());
		KD_CHECK(Rf.level == std::min(A.leagues[static_cast<size_t>(Q.Alliance(Pick).tier)].troopLevel, Q.MaxTroopLevel()));
		KD_CHECK(Rf.from >= 0 && Rf.from < Q.Alliance(Pick).members - 1);
	}
	KD_CHECK(Q.Reinforcements()[0].at == T0 + 60 && Q.Reinforcements()[1].at == T0 + 120);
	Q.Advance(T0 + 90);
	KD_CHECK(Q.ReinforcementsReady() == 1);
	// Squads that have arrived go to battle once; the one on its way stays.
	KD_CHECK(Q.StartAttack(Q.World()[0].id, false, {}, Q.Titans()[0].id, {}, true) == Error::None);
	KD_CHECK(Q.PendingReinforcements().size() == 1 && Q.Reinforcements().size() == 1);
	const std::string During = Q.Save();
	Kingdom S(Content());
	KD_CHECK(S.Load(During, Error) && S.Hash() == Q.Hash() && S.PendingReinforcements().size() == 1);
	Reward Rw;
	KD_CHECK(Q.FinishAttack(Q.World()[0].id, false, 3, {}, &Rw) == Error::None && Rw.reinforcements == 1 && Q.PendingReinforcements().empty());
	// The camp has room again, but a request waits 20 minutes after the last one.
	Q.Advance(T0 + 600);
	KD_CHECK(Q.CheckReinforce().error == Error::NotReady && Q.CheckReinforce().timeSec == 600);
	Q.Advance(T0 + 1200);
	KD_CHECK(Q.CheckReinforce().error == Error::None && Q.RequestReinforcements() == Error::None && Q.Reinforcements().size() == 2);
	// The newcomer queues behind the squad already on its way... which arrived at T0 + 120.
	KD_CHECK(Q.Reinforcements()[1].at == T0 + 1200 + 60);
	// Leaving: allies take their squads back.
	KD_CHECK(Q.LeaveAlliance() == Error::None && Q.Reinforcements().empty());
	// An own alliance without recruits has nobody to send troops yet.
	Kingdom U = WithVp(65, 1000, "alliance_camp");
	KD_CHECK(U.CreateAlliance("Одиночки", "ОДН") == Error::None && U.CheckReinforce().error == Error::Nothing);

	// A save from before alliances (format 5) starts outside any alliance.
	std::string V5 = Replace(K.Save(), "\"formatVersion\":6", "\"formatVersion\":5");
	const size_t A0 = V5.find(",\"alliance\":{");
	const size_t A1 = V5.find(",\"resources\":");
	KD_CHECK(A0 != std::string::npos && A1 > A0);
	V5.erase(A0, A1 - A0);
	Kingdom V(Content());
	KD_CHECK(V.Load(V5, Error) && V.MyAlliance() == -1 && V.AllianceCount() == 48 && V.Hash() == K.Hash());
}

int main()
{
	struct
	{
		const char* Name;
		void (*Fn)();
	} Tests[] = {
		{"ContentMatchesOriginal", TestContentMatchesOriginal},
		{"NewGameAndProduction", TestNewGameAndProduction},
		{"UpgradesBuildersAndRequirements", TestUpgradesBuildersAndRequirements},
		{"BuildLimitsAndPlacement", TestBuildLimitsAndPlacement},
		{"StonesAttacksAndLands", TestStonesAttacksAndLands},
		{"SaveLoadDeterminism", TestSaveLoadDeterminism},
		{"PortalUpgradeMidway", TestPortalUpgradeMidway},
		{"ArmyContent", TestArmyContent},
		{"TrainingQueue", TestTrainingQueue},
		{"EnvoyCouncils", TestEnvoyCouncils},
		{"TroopResearch", TestTroopResearch},
		{"CasualtiesTitansRelicsFusion", TestCasualtiesTitansRelicsFusion},
		{"ArmySaveAndMigration", TestArmySaveAndMigration},
		{"SpellsRelicsLandTitans", TestSpellsRelicsLandTitans},
		{"ComplexRelics", TestComplexRelics},
		{"RuinsAndVictoryPointLands", TestRuinsAndVictoryPointLands},
		{"Seasons", TestSeasons},
		{"Ascension", TestAscension},
		{"Alliances", TestAlliances},
	};
	for (const auto& T : Tests)
	{
		const int Before = GFailures;
		T.Fn();
		std::printf("[%s] %s\n", GFailures == Before ? "  OK  " : " FAIL ", T.Name);
	}
	std::printf("\n%d checks, %d failures\n", GChecks, GFailures);
	return GFailures == 0 ? 0 : 1;
}
