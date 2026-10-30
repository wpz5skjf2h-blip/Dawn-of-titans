// SimTools/kingdom_tests.cpp
// Tests for the kingdom economy (Source/DoTKingdom): content numbers from the original game,
// timers, production, limits, requirements, portal stones, lands (ruins, victory-point lands, upgrades), seasons,
// army, titans (ascension too), relic items (combat and economy, effects, repair), raids on the kingdom, alliances with their leagues and
// reinforcements, and save/load determinism.
#include "ToolCommon.h"

#include "DoTKingdom/Kingdom.h"
#include "DoTSim/BattleSimulation.h"

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

void TestLandUpgrades()
{
	const KingdomContent& Ct = Content();
	KD_CHECK(Ct.landUpgradeGold.size() == 9 && Ct.landUpgradeSec.size() == 9 && Ct.landUpgradeSec[2] == 2 * Hour);
	Kingdom K(Content());
	K.NewGame(T0, 53);
	KD_CHECK(K.MaxLandLevel() == 3); // Castle 1: the world map offers levels 1-3
	// Castle 6 (map band 2-4) with a level 2 gold land and level 2 ruins.
	const std::string Lands = "\"lands\":[{\"id\":800,\"type\":\"gold_land\",\"level\":2,\"stored\":0,\"carry\":0,\"lastAt\":" + std::to_string(T0) +
		",\"titan\":0},{\"id\":801,\"type\":\"ruins\",\"level\":2,\"stored\":0,\"carry\":0,\"lastAt\":" + std::to_string(T0) + ",\"titan\":0}]";
	std::string Save = Replace(K.Save(), "\"lands\":[]", Lands);
	Save = Replace(Save, "\"def\":\"castle\",\"level\":1", "\"def\":\"castle\",\"level\":6");
	Save = Replace(Save, "\"gold\":2500", "\"gold\":20000");
	Kingdom L(Content());
	std::string Error;
	KD_CHECK(L.Load(Save, Error));
	KD_CHECK(L.MaxLandLevel() == 4);
	// Ruins make one shard a day at every level: nothing to gain, so no upgrade.
	KD_CHECK(L.CheckLandUpgrade(801).error == Error::MaxLevel);
	Check Ck = L.CheckLandUpgrade(800);
	KD_CHECK(Ck.error == Error::None && Ck.cost[static_cast<size_t>(ResIndex(Res::Gold))] == Ct.landUpgradeGold[2] && Ck.timeSec == 2 * Hour);
	KD_CHECK(L.UpgradeLand(800, false) == Error::None);
	KD_CHECK(L.Amount(Res::Gold) == 20000 - Ct.landUpgradeGold[2]);
	// A builder works on it: the city has to wait, the land cannot be upgraded twice or abandoned meanwhile.
	KD_CHECK(L.BusyBuilders() == 1 && L.CheckUpgrade(FindId(L, "gold_mine")).error == Error::NoBuilder);
	KD_CHECK(L.CheckLandUpgrade(800).error == Error::Busy && L.AbandonLand(800) == Error::Busy);
	KD_CHECK(L.SpeedupLand(801) == Error::NotBusy);
	// It keeps producing at level 2 (230/h) until the upgrade is done, then at level 3 (300/h).
	L.Advance(T0 + Hour);
	KD_CHECK(L.FindLand(800)->level == 2 && L.UncollectedLand(*L.FindLand(800)) == 230);
	L.Advance(T0 + 2 * Hour);
	KD_CHECK(L.FindLand(800)->level == 3 && !L.FindLand(800)->upgrading && L.BusyBuilders() == 0);
	KD_CHECK(L.UncollectedLand(*L.FindLand(800)) == 460);
	L.Advance(T0 + 3 * Hour);
	KD_CHECK(L.UncollectedLand(*L.FindLand(800)) == 760 && L.LandRate(*L.FindLand(800)) == 300);
	// Missing gold can be bought with gems; the rest of the time too.
	Ck = L.CheckLandUpgrade(800);
	KD_CHECK(Ck.error == Error::NotEnough && Ck.gemsForMissing > 0 && Ck.timeSec == 4 * Hour);
	KD_CHECK(L.UpgradeLand(800, false) == Error::NotEnough);
	const int64_t Gems = L.Amount(Res::Gems);
	KD_CHECK(L.UpgradeLand(800, true) == Error::None);
	KD_CHECK(L.Amount(Res::Gems) == Gems - Ck.gemsForMissing && L.Amount(Res::Gold) == 0);
	// A save in the middle of an upgrade loads to the same state and goes on the same way.
	L.Advance(L.Now() + Hour);
	const std::string Mid = L.Save();
	Kingdom M(Content());
	KD_CHECK(M.Load(Mid, Error) && M.Hash() == L.Hash() && M.Save() == Mid && M.FindLand(800)->upgrading);
	const int64_t Skip = L.LandSkipCost(800);
	KD_CHECK(Skip == (3 * Hour + Ct.skipSecPerGem - 1) / Ct.skipSecPerGem);
	KD_CHECK(L.SpeedupLand(800) == Error::None && L.FindLand(800)->level == 4 && L.Amount(Res::Gems) == Gems - Ck.gemsForMissing - Skip);
	M.Advance(M.Now() + 3 * Hour);
	KD_CHECK(M.FindLand(800)->level == 4 && M.LandRate(*M.FindLand(800)) == 390);
	// Level 4 is the top of Castle 6's band; Castle 8 opens level 5.
	Ck = L.CheckLandUpgrade(800);
	KD_CHECK(Ck.error == Error::CastleTooLow && Ck.castle == 8);
	Kingdom N(Content());
	KD_CHECK(N.Load(Replace(L.Save(), "\"def\":\"castle\",\"level\":6", "\"def\":\"castle\",\"level\":8"), Error));
	KD_CHECK(N.CheckLandUpgrade(800).error == Error::NotEnough && N.CheckLandUpgrade(800).castle == 0);
	KD_CHECK(N.AbandonLand(800) == Error::None && N.Lands().size() == 1);
}
void TestTitanMonumentsAndXpArenas()
{
	const KingdomContent& Ct = Content();
	KD_CHECK(Ct.sites.list.size() == 7 && Ct.sites.cooldownSec == 24 * Hour && Ct.sites.stones == 3);
	KD_CHECK(Ct.sites.shardPct[0] == 40 && Ct.sites.shardPct[2] == 80);
	const SiteDef& Bronze = Ct.sites.list[0];
	const SiteDef& Trial = Ct.sites.list[1];
	KD_CHECK(Bronze.id == "monument_bronze" && !Bronze.arena && Bronze.level == 3 && Bronze.castle == 5 && Bronze.shards == 1);
	KD_CHECK(Trial.id == "arena_trial" && Trial.arena && Trial.level == 4 && Trial.ai == "warlord" && Trial.xpPct == 300);
	Kingdom K(Content());
	K.NewGame(T0, 61);
	KD_CHECK(K.SiteCount() == 7 && K.CheckSite(1).error == Error::Locked && K.CheckSite(1).castle == 4);
	KD_CHECK(K.StartSiteAttack(1, {}, 0, {}, false) == Error::Locked && K.CheckSite(7).error == Error::BadId);
	// Castle 9 opens the first three; plenty of gems for resets.
	std::string Save = Replace(K.Save(), "\"def\":\"castle\",\"level\":1", "\"def\":\"castle\",\"level\":9");
	Save = Replace(Save, "\"gems\":500", "\"gems\":100000");
	Kingdom L(Content());
	std::string Error;
	KD_CHECK(L.Load(Save, Error));
	KD_CHECK(L.CheckSite(0).error == Error::None && L.CheckSite(2).error == Error::None && L.CheckSite(3).error == Error::Locked);
	const int32_t Perun = L.Titans()[0].id;
	const int Militia = Ct.FindTroop("troop.militia");
	const int64_t Stones = L.Amount(Res::Stones);
	const int64_t Vp = L.Amount(Res::Vp);
	// A monument: fought once, it is closed for a day whatever the result.
	KD_CHECK(L.StartSiteAttack(0, {Militia}, Perun, {}, false) == Error::None);
	KD_CHECK(L.Amount(Res::Stones) == Stones - 3 && L.PendingSite() == 0 && L.PendingAttack() == -1);
	KD_CHECK(L.SiteReadyAt(0) == T0 + 24 * Hour && L.CheckSite(0).error == Error::NotReady && L.CheckSite(0).timeSec == 24 * Hour);
	KD_CHECK(L.FinishSiteAttack(1, 3, {100}, {}, nullptr) == Error::NoAttack);
	KD_CHECK(L.FinishAttack(L.World()[0].id, false, 3, {100}, {}, nullptr) == Error::NoAttack);
	// A save in the middle of the battle loads to the same state.
	const std::string Mid = L.Save();
	Kingdom M(Content());
	KD_CHECK(M.Load(Mid, Error) && M.Hash() == L.Hash() && M.Save() == Mid && M.PendingSite() == 0 && M.SiteReadyAt(0) == T0 + 24 * Hour);
	Reward R;
	KD_CHECK(L.FinishSiteAttack(0, 3, {100}, {}, &R) == Error::None && L.PendingAttack() == 0);
	KD_CHECK(R.squadsBack == 1 && R.titanId == Perun && R.titanXp == Ct.titanBattleXp[2] * Ct.titanStarsPct[3] / 100);
	KD_CHECK(R.res[static_cast<size_t>(ResIndex(Res::Vp))] == L.VictoryVp(3) && L.Amount(Res::Vp) == Vp + L.VictoryVp(3));
	KD_CHECK(R.relics.empty() && (R.res[static_cast<size_t>(ResIndex(Res::Shards))] == 0 || R.res[static_cast<size_t>(ResIndex(Res::Shards))] == 1));
	// Gems open it again before the day is over; an open site has nothing to reset.
	KD_CHECK(L.SiteResetGems(0) == 30 && L.SiteResetGems(2) == 0 && L.ResetSite(2) == Error::Nothing);
	const int64_t Gems = L.Amount(Res::Gems);
	KD_CHECK(L.ResetSite(0) == Error::None && L.Amount(Res::Gems) == Gems - 30 && L.CheckSite(0).error == Error::None);
	// Shards drop by the stars' chance: about 80% at three stars, 40% at one.
	for (const int Stars : {3, 1})
	{
		int Drops = 0;
		for (int I = 0; I < 200; ++I)
		{
			L.ResetSite(0);
			L.RefillStones();
			const int64_t Shards = L.Amount(Res::Shards);
			KD_CHECK(L.StartSiteAttack(0, {}, 0, {}, false) == Error::None && L.FinishSiteAttack(0, Stars, {}, {}, nullptr) == Error::None);
			Drops += L.Amount(Res::Shards) > Shards ? 1 : 0;
			KD_CHECK(L.Amount(Res::Shards) == Shards || L.Amount(Res::Shards) == Shards + 1);
		}
		KD_CHECK(Stars == 3 ? Drops >= 140 && Drops <= 180 : Drops >= 60 && Drops <= 100);
	}
	// A lost battle gives nothing, and the site stays closed.
	L.ResetSite(0);
	L.RefillStones();
	const int64_t Before = L.Amount(Res::Vp);
	KD_CHECK(L.StartSiteAttack(0, {}, 0, {}, false) == Error::None && L.FinishSiteAttack(0, 0, {}, {}, &R) == Error::None);
	KD_CHECK(L.Amount(Res::Vp) == Before && L.CheckSite(0).error == Error::NotReady);
	// An XP Arena: three times a land battle's XP for the titan, no VP, shards or relics.
	L.Advance(L.Now() + 24 * Hour);
	L.RefillStones();
	const int64_t Shards = L.Amount(Res::Shards);
	const int64_t Vp2 = L.Amount(Res::Vp);
	KD_CHECK(L.FindTitan(Perun)->recoverUntil <= L.Now());
	KD_CHECK(L.StartSiteAttack(1, {Militia}, Perun, {}, false) == Error::None && L.FinishSiteAttack(1, 2, {100}, {}, &R) == Error::None);
	KD_CHECK(R.titanXp == Ct.titanBattleXp[3] * 300 / 100 * Ct.titanStarsPct[2] / 100);
	KD_CHECK(L.Amount(Res::Shards) == Shards && L.Amount(Res::Vp) == Vp2 && R.relics.empty());
	// The titan cannot fight while it rests, and the stones must be there.
	L.ResetSite(1);
	Kingdom N(Content());
	KD_CHECK(N.Load(Replace(L.Save(), "\"stones\":" + std::to_string(L.Amount(Res::Stones)), "\"stones\":2"), Error));
	KD_CHECK(N.CheckSite(1).error == Error::NeedStones && N.StartSiteAttack(1, {}, 0, {}, false) == Error::NeedStones);
	// An old save without sites loads with every site open.
	Kingdom O(Content());
	const std::string Old = Replace(Save, "\"sites\":{\"counter\":0,\"ready\":{}}", "\"x\":0");
	KD_CHECK(Old != Save && O.Load(Old, Error) && O.CheckSite(0).error == Error::None);
}

void TestCampaigns()
{
	const KingdomContent& Ct = Content();
	const CampaignRules& G = Ct.campaigns;
	const size_t GemsI = static_cast<size_t>(ResIndex(Res::Gems));
	const size_t FoodI = static_cast<size_t>(ResIndex(Res::Food));
	KD_CHECK(G.stones == 2 && G.guild == Def("adventurers_guild") && G.acts.size() == 2);
	KD_CHECK(G.acts[0].id == "act1" && G.acts[0].castle == 8 && G.acts[1].id == "act2" && G.acts[1].castle == 12);
	const MissionDef& First = G.acts[0].missions[0];
	KD_CHECK(First.id == "a1m1" && First.level == 3 && First.first[GemsI] == 10 && First.first[FoodI] == 5000);
	KD_CHECK(G.acts[0].chests[2].stars == 36 && G.acts[0].chests[2].titan == 2 && G.acts[0].chests[2].res[GemsI] == 150 && G.acts[0].chests[1].relic == 3);
	// The master prompt: 12 missions and a boss per act, 10-30 gems for a first clear, star chests at 12 / 24 / 36 of 39.
	for (const ActDef& A : G.acts)
	{
		KD_CHECK(A.missions.size() == 13 && A.chests.size() == 3 && A.chests[0].stars == 12 && A.chests[1].stars == 24 && A.chests[2].stars == 36);
		for (size_t M = 0; M < A.missions.size(); ++M)
		{
			KD_CHECK(A.missions[M].first[GemsI] >= 10 && A.missions[M].first[GemsI] <= 40);
			KD_CHECK(M == 0 || A.missions[M].level >= A.missions[M - 1].level);
		}
	}

	Kingdom K(Content());
	K.NewGame(T0, 61);
	KD_CHECK(K.ActCount() == 2 && K.MissionCount(0) == 13 && K.MissionCount(2) == 0 && K.ActStars(0) == 0 && !K.ChestClaimed(0, 0));
	// Without the Adventurers' Guild nothing opens.
	const Check NoGuild = K.CheckMission(0, 0);
	KD_CHECK(NoGuild.error == Error::NeedRequirement && NoGuild.missing.building == G.guild && NoGuild.missing.level == 1);
	KD_CHECK(K.StartMission(0, 0, {}, 0, {}, false) == Error::NeedRequirement && K.CheckMission(0, 13).error == Error::BadId);
	// With the Guild the act still waits for its Castle.
	const std::string Save = WithBuilding(K.Save(), "adventurers_guild", 1, 2);
	const auto AtCastle = [&](int Castle) { return Replace(Save, "\"def\":\"castle\",\"level\":1", "\"def\":\"castle\",\"level\":" + std::to_string(Castle)); };
	std::string Error;
	Kingdom L(Content());
	KD_CHECK(L.Load(AtCastle(7), Error) && L.CheckMission(0, 0).error == Error::Locked && L.CheckMission(0, 0).castle == 8);
	// Act II needs its Castle and Act I won to the end.
	KD_CHECK(L.Load(AtCastle(12), Error) && L.CheckMission(1, 0).error == Error::Locked && L.CheckMission(1, 0).castle == 0);
	Kingdom P(Content());
	KD_CHECK(P.Load(AtCastle(8), Error));
	KD_CHECK(P.CheckMission(0, 0).error == Error::None && P.CheckMission(0, 0).cost[static_cast<size_t>(ResIndex(Res::Stones))] == 2);
	KD_CHECK(P.CheckMission(0, 1).error == Error::Locked && P.CheckMission(0, 1).castle == 0 && P.CheckMission(1, 0).castle == 12);
	const int32_t Perun = P.Titans()[0].id;
	const int Militia = Troop("troop.militia");
	const int64_t Stones = P.Amount(Res::Stones);
	KD_CHECK(P.StartMission(0, 1, {}, 0, {}, false) == Error::Locked);
	KD_CHECK(P.StartMission(0, 0, {Militia}, Perun, {}, false) == Error::None);
	KD_CHECK(P.Amount(Res::Stones) == Stones - 2 && P.PendingMission() == 0 && P.PendingSite() == -1 && P.PendingAttack() == -Kingdom::MissionBase);
	KD_CHECK(P.FinishMission(0, 1, 3, {100}, {}, nullptr) == Error::NoAttack && P.FinishSiteAttack(0, 3, {100}, {}, nullptr) == Error::NoAttack);
	// A save in the middle of the mission loads to the same state.
	const std::string Mid = P.Save();
	Kingdom M(Content());
	KD_CHECK(M.Load(Mid, Error) && M.Hash() == P.Hash() && M.Save() == Mid && M.PendingMission() == 0);
	// A lost mission pays nothing and opens nothing; the titan still gains XP as in a land battle of its level.
	Reward R;
	KD_CHECK(P.FinishMission(0, 0, 0, {100}, {}, &R) == Error::None && P.PendingAttack() == 0 && !R.firstClear && R.res[GemsI] == 0);
	KD_CHECK(P.MissionStars(0, 0) == 0 && P.CheckMission(0, 1).error == Error::Locked);
	KD_CHECK(R.titanId == Perun && R.titanXp == Ct.titanBattleXp[2] * Ct.titanStarsPct[0] / 100 && R.squadsBack == 1);
	// The first win pays the mission's reward and opens the next one.
	P.RefillStones();
	const int64_t Gems = P.Amount(Res::Gems);
	const int64_t Food = P.Amount(Res::Food);
	KD_CHECK(P.StartMission(0, 0, {Militia}, 0, {}, false) == Error::None && P.FinishMission(0, 0, 2, {100}, {}, &R) == Error::None);
	KD_CHECK(R.firstClear && R.res[GemsI] == 10 && P.Amount(Res::Gems) == Gems + 10 && R.res[FoodI] == P.Amount(Res::Food) - Food && R.res[FoodI] > 0);
	KD_CHECK(R.relics.empty() && P.MissionStars(0, 0) == 2 && P.ActStars(0) == 2 && P.CheckMission(0, 1).error == Error::None);
	// Repeats give the titan XP only; the best stars stay.
	KD_CHECK(P.StartMission(0, 0, {}, 0, {}, false) == Error::None && P.FinishMission(0, 0, 1, {}, {}, &R) == Error::None);
	KD_CHECK(!R.firstClear && R.res[GemsI] == 0 && R.res[FoodI] == 0 && P.MissionStars(0, 0) == 2);
	KD_CHECK(P.StartMission(0, 0, {}, 0, {}, false) == Error::None && P.FinishMission(0, 0, 3, {}, {}, &R) == Error::None && P.MissionStars(0, 0) == 3);
	KD_CHECK(P.ClaimChest(0, 0, &R) == Error::NotEnough && P.ClaimChest(0, 3, &R) == Error::BadId);
	// Through the act with three stars: Act II waits for Castle 12 only.
	for (int Mi = 1; Mi < 13; ++Mi)
	{
		P.RefillStones();
		KD_CHECK(P.StartMission(0, Mi, {}, 0, {}, false) == Error::None && P.FinishMission(0, Mi, 3, {}, {}, &R) == Error::None && R.firstClear);
	}
	KD_CHECK(P.ActStars(0) == 39 && P.CheckMission(1, 0).error == Error::Locked && P.CheckMission(1, 0).castle == 12);
	// The chests: resources, a relic of their rarity, a titan while the Hall has room.
	const int64_t Gems2 = P.Amount(Res::Gems);
	KD_CHECK(P.ClaimChest(0, 0, &R) == Error::None && R.res[GemsI] == 50 && P.Amount(Res::Gems) == Gems2 + 50 && P.ChestClaimed(0, 0));
	KD_CHECK(P.ClaimChest(0, 0, &R) == Error::Nothing);
	const size_t Relics = P.Relics().size();
	KD_CHECK(P.ClaimChest(0, 1, &R) == Error::None && R.relics.size() == 1 && P.Relics().size() == Relics + 1 && P.FindRelic(R.relics[0])->rarity == 3);
	const size_t Titans = P.Titans().size();
	const bool bRoom = static_cast<int>(Titans) < P.TitanCapacity();
	KD_CHECK(P.ClaimChest(0, 2, &R) == Error::None && P.ChestClaimed(0, 2));
	KD_CHECK(bRoom ? R.newTitan != 0 && P.Titans().size() == Titans + 1 && Ct.titans[static_cast<size_t>(P.FindTitan(R.newTitan)->def)].stars == 2
				   : R.newTitan == 0 && R.relics.size() == 1);
	// Stars and chests survive a save; the hash covers them.
	Kingdom Q(Content());
	KD_CHECK(Q.Load(P.Save(), Error) && Q.Hash() == P.Hash() && Q.Save() == P.Save() && Q.ActStars(0) == 39 && Q.ChestClaimed(0, 1) && !Q.ChestClaimed(1, 0));
	// An old save without campaigns loads with nothing played.
	Kingdom O(Content());
	const std::string Old = Replace(AtCastle(8), "\"campaigns\":{}", "\"x\":0");
	KD_CHECK(Old != AtCastle(8) && O.Load(Old, Error) && O.ActStars(0) == 0 && O.CheckMission(0, 0).error == Error::None);
}

/** The save with its daily quests replaced (they are the last key of a save). */
std::string WithQuests(const std::string& Save, const std::string& Quests)
{
	const size_t P = Save.find("\"quests\":{");
	return P == std::string::npos ? Save : Save.substr(0, P) + "\"quests\":" + Quests + "}";
}

int QuestSlot(const Kingdom& K, const char* Id)
{
	for (size_t I = 0; I < K.Quests().size(); ++I)
	{
		if (Content().quests.pool[static_cast<size_t>(K.Quests()[I].def)].id == Id)
		{
			return static_cast<int>(I);
		}
	}
	return -1;
}

void TestDailyQuests()
{
	const KingdomContent& Ct = Content();
	const QuestRules& Q = Ct.quests;
	const size_t GemsI = static_cast<size_t>(ResIndex(Res::Gems));
	const size_t StonesI = static_cast<size_t>(ResIndex(Res::Stones));
	// The master prompt (3.4): five daily quests and a daily chest; about 20 gems a day of the free player's 40 (8.6).
	KD_CHECK(Q.count == 5 && Q.rerolls == 2 && Q.pool.size() == 11 && Q.chest[GemsI] == 10 && Q.chest[StonesI] == 6);
	KD_CHECK(Q.chestRelic.size() == 2 && Q.chestRelic[0] == std::make_pair(3, 1) && Q.chestRelic[1] == std::make_pair(12, 2));
	int64_t Most = 0;
	for (const QuestDef& D : Q.pool)
	{
		KD_CHECK(!D.target.empty() && D.target.front().first <= D.castle && D.reward[GemsI] >= 2 && D.reward[GemsI] <= 3);
		Most = std::max(Most, D.reward[GemsI]);
	}
	KD_CHECK(Q.count * Most + Q.chest[GemsI] <= 25);
	const auto PoolIndex = [&](const char* Id) {
		for (size_t I = 0; I < Q.pool.size(); ++I)
		{
			if (Q.pool[I].id == Id)
			{
				return static_cast<int>(I);
			}
		}
		return -1;
	};
	const int Craft = PoolIndex("q_craft");
	KD_CHECK(Craft >= 0 && Q.pool[static_cast<size_t>(Craft)].event == QuestEvent::Craft && Q.pool[static_cast<size_t>(Craft)].building == Def("spell_forge"));

	// A new kingdom gets five different quests open at Castle 1, for the day it started in.
	Kingdom K(Ct);
	K.NewGame(T0, 7);
	const int64_t DayEnd = (T0 / 86400 + 1) * 86400;
	const auto Defs = [](const Kingdom& X) {
		std::vector<int> Out;
		for (const DailyQuest& A : X.Quests())
		{
			Out.push_back(A.def);
		}
		return Out;
	};
	KD_CHECK(K.Quests().size() == 5 && K.QuestRerollsLeft() == 2 && !K.QuestChestClaimed() && K.QuestDayEnd() == DayEnd);
	for (size_t I = 0; I < K.Quests().size(); ++I)
	{
		const DailyQuest& A = K.Quests()[I];
		const QuestDef& D = Q.pool[static_cast<size_t>(A.def)];
		KD_CHECK(D.castle == 1 && D.building < 0 && A.progress == 0 && !A.claimed && A.target == K.QuestTarget(A.def) && A.target == D.target.front().second);
		for (size_t J = 0; J < I; ++J)
		{
			KD_CHECK(K.Quests()[J].def != A.def);
		}
	}
	// The seed and the day decide them; they stay through the day and change with it.
	Kingdom Same(Ct);
	Same.NewGame(T0 + 600, 7);
	KD_CHECK(Defs(Same) == Defs(K));
	bool bOtherSeed = false;
	for (uint64_t S = 8; S < 16 && !bOtherSeed; ++S)
	{
		Kingdom Other(Ct);
		Other.NewGame(T0, S);
		bOtherSeed = Defs(Other) != Defs(K);
	}
	KD_CHECK(bOtherSeed);
	Same.Advance(DayEnd - 1);
	KD_CHECK(Defs(Same) == Defs(K) && Same.QuestDayEnd() == DayEnd);
	bool bNewDay = false;
	for (int D = 0; D < 4; ++D)
	{
		Same.Advance(DayEnd + D * 86400);
		KD_CHECK(Same.QuestDayEnd() == DayEnd + (D + 1) * 86400 && Same.Quests().size() == 5);
		bNewDay = bNewDay || Defs(Same) != Defs(K);
	}
	KD_CHECK(bNewDay);

	// Progress from what the kingdom does: these quests by hand.
	const std::string List = "{\"day\":" + std::to_string(T0 / 86400) + ",\"rerolls\":0,\"chest\":false,\"counter\":0,\"list\":["
		"{\"id\":\"q_train\",\"target\":3,\"progress\":0,\"claimed\":false},"
		"{\"id\":\"q_upgrade\",\"target\":2,\"progress\":0,\"claimed\":false},"
		"{\"id\":\"q_gold\",\"target\":500,\"progress\":0,\"claimed\":false},"
		"{\"id\":\"q_win\",\"target\":2,\"progress\":0,\"claimed\":false},"
		"{\"id\":\"q_stones\",\"target\":9,\"progress\":0,\"claimed\":false}]}";
	std::string Error;
	Kingdom P(Ct);
	KD_CHECK(P.Load(WithQuests(K.Save(), List), Error) && P.Quests().size() == 5 && QuestSlot(P, "q_train") == 0 && QuestSlot(P, "q_stones") == 4);
	const int Mil = Troop("troop.militia");
	KD_CHECK(P.Train(Mil) == Error::None && P.Train(Mil) == Error::None && P.Quests()[0].progress == 2);
	KD_CHECK(P.CancelTrain(1) == Error::None && P.Quests()[0].progress == 1); // a cancelled squad does not count
	KD_CHECK(P.Train(Mil) == Error::None && P.Train(Mil) == Error::None && P.Train(Mil) == Error::None && P.Quests()[0].progress == 4);
	KD_CHECK(P.ClaimQuest(1, nullptr) == Error::NotReady && P.ClaimQuest(5, nullptr) == Error::BadId);
	P.Advance(T0 + 6 * Hour);
	int64_t Got = 0;
	KD_CHECK(P.Collect(FindId(P, "gold_mine"), &Got) == Error::None && Got > 0 && P.Quests()[2].progress == Got && P.Quests()[3].progress == 0);
	KD_CHECK(P.Collect(FindId(P, "farm"), &Got) == Error::None && P.Quests()[2].progress < 500 + 600);
	KD_CHECK(P.Upgrade(FindId(P, "castle"), false) == Error::None && P.Quests()[1].progress == 1);
	const WorldLand Land = P.World()[0];
	KD_CHECK(P.StartAttack(Land.id, false) == Error::None && P.Quests()[4].progress == 3 && P.Quests()[3].progress == 0);
	KD_CHECK(P.FinishAttack(Land.id, false, 3) == Error::None && P.Quests()[3].progress == 1);
	KD_CHECK(P.StartAttack(P.World()[0].id, false) == Error::None && P.FinishAttack(P.World()[0].id, false, 0) == Error::None);
	KD_CHECK(P.Quests()[3].progress == 1 && P.Quests()[4].progress == 6); // a lost battle is no win
	// A done quest pays its reward once.
	const QuestDef& TrainDef = Q.pool[static_cast<size_t>(P.Quests()[0].def)];
	const int64_t Gems = P.Amount(Res::Gems);
	Reward R;
	KD_CHECK(P.ClaimQuest(0, &R) == Error::None && P.Quests()[0].claimed && R.res[GemsI] == TrainDef.reward[GemsI] && P.Amount(Res::Gems) == Gems + TrainDef.reward[GemsI]);
	KD_CHECK(R.res[StonesI] == TrainDef.reward[StonesI] && P.ClaimQuest(0, &R) == Error::Nothing);
	KD_CHECK(P.Train(Mil) == Error::None && P.Quests()[0].progress == 4); // a claimed quest counts no more
	KD_CHECK(P.ClaimQuest(2, &R) == Error::None && P.ClaimQuestChest(&R) == Error::NotReady);
	// Rerolls swap an unfinished quest for one not on the list, twice a day, the same way on every client.
	KD_CHECK(P.RerollQuest(0) == Error::Nothing && P.RerollQuest(9) == Error::BadId);
	Kingdom Twin(Ct);
	KD_CHECK(Twin.Load(P.Save(), Error) && Twin.Hash() == P.Hash());
	const std::vector<int> Before = Defs(P);
	KD_CHECK(P.RerollQuest(3) == Error::None && P.Quests()[3].def != Before[3] && P.Quests()[3].progress == 0 && P.QuestRerollsLeft() == 1);
	KD_CHECK(std::count(Before.begin(), Before.end(), P.Quests()[3].def) == 0 && Q.pool[static_cast<size_t>(P.Quests()[3].def)].castle == 1);
	KD_CHECK(Twin.RerollQuest(3) == Error::None && Twin.Hash() == P.Hash());
	KD_CHECK(P.RerollQuest(1) == Error::None && P.QuestRerollsLeft() == 0 && P.RerollQuest(4) == Error::LimitReached);
	// Saved and loaded as they are; the hash covers them.
	Kingdom S(Ct);
	KD_CHECK(S.Load(P.Save(), Error) && S.Hash() == P.Hash() && S.Save() == P.Save() && S.QuestRerollsLeft() == 0 && S.Quests()[0].claimed);

	// The daily chest opens when every quest is claimed; a relic from Castle 3, rarity 2 from Castle 12.
	const auto AllDone = [&](int64_t Day) {
		return "{\"day\":" + std::to_string(Day) + ",\"rerolls\":1,\"chest\":false,\"counter\":5,\"list\":["
			"{\"id\":\"q_train\",\"target\":3,\"progress\":3,\"claimed\":false},{\"id\":\"q_win\",\"target\":2,\"progress\":7,\"claimed\":false},"
			"{\"id\":\"q_gold\",\"target\":500,\"progress\":500,\"claimed\":true},{\"id\":\"q_gone\",\"target\":1,\"progress\":1,\"claimed\":false}]}";
	};
	Kingdom Ch(Ct);
	KD_CHECK(Ch.Load(WithQuests(K.Save(), AllDone(T0 / 86400)), Error) && Ch.Quests().size() == 3 && Ch.QuestRerollsLeft() == 1); // unknown ids are dropped
	KD_CHECK(Ch.ClaimQuest(0, nullptr) == Error::None && Ch.ClaimQuestChest(&R) == Error::NotReady && Ch.ClaimQuest(1, nullptr) == Error::None);
	const int64_t Gems2 = Ch.Amount(Res::Gems);
	const size_t Relics = Ch.Relics().size();
	KD_CHECK(Ch.ClaimQuestChest(&R) == Error::None && R.res[GemsI] == 10 && Ch.Amount(Res::Gems) == Gems2 + 10 && R.relics.empty() && Ch.Relics().size() == Relics);
	KD_CHECK(Ch.QuestChestClaimed() && Ch.ClaimQuestChest(&R) == Error::Nothing);
	const std::string Save12 = Replace(WithBuilding(K.Save(), "relic_temple", 1, 34), "\"def\":\"castle\",\"level\":1", "\"def\":\"castle\",\"level\":12");
	Kingdom Hi(Ct);
	KD_CHECK(Hi.Load(WithQuests(Save12, AllDone(T0 / 86400)), Error) && Hi.ClaimQuest(0, nullptr) == Error::None && Hi.ClaimQuest(1, nullptr) == Error::None);
	KD_CHECK(Hi.ClaimQuestChest(&R) == Error::None && R.relics.size() == 1 && Hi.FindRelic(R.relics[0])->rarity == 2);
	// Higher Castles get bigger targets and their own quests (the Spell Forge's craft quest).
	KD_CHECK(K.QuestTarget(PoolIndex("q_gold")) == 500 && Hi.QuestTarget(PoolIndex("q_gold")) == 20000);
	KD_CHECK(Hi.QuestTarget(Craft) == 3 && K.QuestTarget(Craft) == 2 && Hi.QuestTarget(99) == 0);
	// Yesterday's quests are gone on the next Advance.
	Kingdom Old(Ct);
	KD_CHECK(Old.Load(WithQuests(K.Save(), AllDone(T0 / 86400 - 1)), Error) && Old.Quests().size() == 3);
	Old.Advance(T0 + 60);
	KD_CHECK(Old.Quests().size() == 5 && Old.QuestRerollsLeft() == 2 && !Old.QuestChestClaimed() && Defs(Old) == Defs(K));
	// A save from before daily quests rolls today's on the next Advance.
	Kingdom Pre(Ct);
	const std::string Before2 = K.Save();
	const std::string NoQuests = Before2.substr(0, Before2.find(",\"quests\":{")) + "}";
	KD_CHECK(NoQuests.size() < Before2.size() && Pre.Load(NoQuests, Error) && Pre.Quests().empty());
	Pre.Advance(T0 + 60);
	KD_CHECK(Defs(Pre) == Defs(K));
}

/** The save with these titans added: (id, def, stars-of-fodder ignored) as JSON objects. */
std::string WithTitans(const std::string& Save, const std::vector<std::pair<int32_t, std::string>>& Titans, const std::string& Extra = "")
{
	std::string List;
	for (const std::pair<int32_t, std::string>& T : Titans)
	{
		List += "{\"id\":" + std::to_string(T.first) + ",\"def\":\"" + T.second + "\",\"level\":1,\"xp\":0,\"recoverUntil\":0,\"ascended\":false,\"relics\":[]" + Extra + "},";
	}
	return Replace(Save, "\"titans\":[", "\"titans\":[" + List);
}

void TestTitanSkills()
{
	const KingdomContent& Ct = Content();
	const SkillRules& Sk = Ct.skills;
	// The original: skills by class in slots, opened by fusion (1-star titans for the 2nd slot, 2-star for the 3rd,
	// 3-star for the 4th), a new slot with ascension (wiki, Strategy and Ascension).
	KD_CHECK(Sk.slots == 4 && Sk.openPct == 35 && Sk.upgradePct == 25 && Sk.kinds.size() == 7 && Sk.classWeights.size() == 7);
	const auto KindOf = [&](const char* Id) {
		for (size_t I = 0; I < Sk.kinds.size(); ++I)
		{
			if (Sk.kinds[I].id == Id)
			{
				return static_cast<int>(I);
			}
		}
		return -1;
	};
	const int Cuirass = KindOf("cuirass");
	const int Precision = KindOf("precision");
	KD_CHECK(Cuirass >= 0 && Sk.kinds[static_cast<size_t>(Cuirass)].stat == RelicStat::Armor && Sk.kinds[static_cast<size_t>(Cuirass)].tiers[2] == 80);
	KD_CHECK(Precision >= 0 && Sk.kinds[static_cast<size_t>(Precision)].stat == RelicStat::Crit && Sk.kinds[static_cast<size_t>(Precision)].tiers[0] == 20);
	for (const TitanType& T : Ct.titans)
	{
		KD_CHECK(std::any_of(Sk.classWeights.begin(), Sk.classWeights.end(), [&T](const std::pair<std::string, std::vector<int64_t>>& W) { return W.first == T.cls; }));
	}

	// Every titan starts with one skill of tier I that its class can roll; the seed decides it.
	Kingdom K(Ct);
	K.NewGame(T0, 31);
	for (const kingdom::TitanState& T : K.Titans())
	{
		KD_CHECK(T.skills.size() == 1 && T.skills[0].tier == 1 && K.SkillSlots(T) == 4);
		const SkillKind& Kd = Sk.kinds[static_cast<size_t>(T.skills[0].kind)];
		KD_CHECK(K.TitanSkillBonus(T.id, Kd.stat) == Kd.tiers[0] && K.TitanBonus(T.id, Kd.stat) == K.TitanRelicBonus(T.id, Kd.stat) + Kd.tiers[0]);
	}
	Kingdom Same(Ct);
	Same.NewGame(T0 + 99, 31);
	KD_CHECK(Same.Titans()[0].skills[0].kind == K.Titans()[0].skills[0].kind && Same.Titans()[1].skills[0].kind == K.Titans()[1].skills[0].kind);

	// Fusion opens the slots in order, each needing fodder of enough stars, and raises tiers up to the fodder's stars.
	std::string Save = Replace(K.Save(), "\"def\":\"castle\",\"level\":1", "\"def\":\"castle\",\"level\":7");
	Save = Replace(Save, "\"food\":" + std::to_string(K.Amount(Res::Food)), "\"food\":9000000");
	std::vector<std::pair<int32_t, std::string>> Add = {{700, "titan.thor"}};
	for (int32_t I = 0; I < 16; ++I)
	{
		Add.emplace_back(710 + I, "titan.talos");
		Add.emplace_back(730 + I, "titan.atlas");
		Add.emplace_back(750 + I, "titan.surtr");
	}
	std::string Error;
	Kingdom F(Ct);
	KD_CHECK(F.Load(WithTitans(Save, Add), Error) && F.FindTitan(700) && F.FindTitan(700)->skills.size() == 1); // old saves roll the start skill
	const auto Fuse = [&](int32_t First) {
		for (int32_t I = 0; I < 16; ++I)
		{
			KD_CHECK(F.CheckFusion(700, {First + I}).error == Error::None && F.FuseTitans(700, {First + I}) == Error::None);
		}
	};
	const auto MaxTier = [&] {
		int Max = 0;
		for (const TitanSkill& S : F.FindTitan(700)->skills)
		{
			Max = std::max(Max, S.tier);
		}
		return Max;
	};
	Fuse(710); // 1-star titans: the 2nd slot, no tier above I
	KD_CHECK(F.FindTitan(700)->skills.size() == 2 && MaxTier() == 1);
	Fuse(730); // 2-star titans: the 3rd slot, tiers up to II
	KD_CHECK(F.FindTitan(700)->skills.size() == 3 && MaxTier() == 2);
	Fuse(750); // 3-star titans: the 4th slot, tiers up to III
	const kingdom::TitanState& Thor = *F.FindTitan(700);
	KD_CHECK(Thor.skills.size() == 4 && MaxTier() == 3 && F.FindTitan(700)->level < F.TitanMaxLevel(Thor));
	for (size_t I = 0; I < Thor.skills.size(); ++I)
	{
		for (size_t J = 0; J < I; ++J)
		{
			KD_CHECK(Thor.skills[I].kind != Thor.skills[J].kind); // a kind once per titan
		}
	}
	// The skills reach the battle bonus; save and load keep them and the hash covers them.
	int64_t SkillSum = 0;
	for (int S = 0; S <= static_cast<int>(RelicStat::TroopHp); ++S)
	{
		SkillSum += F.TitanSkillBonus(700, static_cast<RelicStat>(S));
	}
	int64_t Expect = 0;
	for (const TitanSkill& S : Thor.skills)
	{
		Expect += F.SkillValue(S);
	}
	KD_CHECK(SkillSum == Expect && SkillSum > 0);
	Kingdom G(Ct);
	KD_CHECK(G.Load(F.Save(), Error) && G.Hash() == F.Hash() && G.Save() == F.Save() && G.FindTitan(700)->skills.size() == 4);
	const std::string Changed = Replace(F.Save(), "\"skills\":[[\"", "\"skills\":[[\"x");
	Kingdom U(Ct);
	KD_CHECK(Changed != F.Save() && U.Load(Changed, Error) && U.FindTitan(700)->skills.size() == 3); // kinds the content lacks are dropped

	// Bonuses share the relics' caps; tiers past III and extra skills in a save are cut.
	Kingdom P(Ct);
	KD_CHECK(P.Load(WithTitans(K.Save(), {{800, "titan.talos"}}, ",\"skills\":[[\"precision\",9],[\"cuirass\",2],[\"might\",1],[\"vigor\",1],[\"banner\",1]]"), Error));
	KD_CHECK(P.FindTitan(800)->skills.size() == 4 && P.FindTitan(800)->skills[0].tier == 3 && P.TitanBonus(800, RelicStat::Crit) == 40);
	KD_CHECK(P.TitanBonus(800, RelicStat::Armor) == 50 && P.TitanSkillBonus(800, RelicStat::TroopHp) == 0);

	// Ascension opens the fifth slot with a new skill; an ascended titan from an old save gets it too.
	std::string Asc = Replace(WithTitans(K.Save(), {{801, "titan.talos"}}), "\"id\":801,\"def\":\"titan.talos\",\"level\":1", "\"id\":801,\"def\":\"titan.talos\",\"level\":20");
	Asc = Replace(Asc, "\"shards\":0", "\"shards\":5");
	Asc = Replace(Asc, "\"food\":" + std::to_string(K.Amount(Res::Food)), "\"food\":200000");
	Kingdom A(Ct);
	KD_CHECK(A.Load(Asc, Error) && A.FindTitan(801)->skills.size() == 1 && A.Ascend(801, false) == Error::None);
	KD_CHECK(A.FindTitan(801)->skills.size() == 2 && A.SkillSlots(*A.FindTitan(801)) == 5 && A.FindTitan(801)->skills[1].tier == 1);
	Kingdom O(Ct);
	const std::string AscOld = Replace(WithTitans(K.Save(), {{802, "titan.talos"}}), "\"id\":802,\"def\":\"titan.talos\",\"level\":1,\"xp\":0,\"recoverUntil\":0,\"ascended\":false",
		"\"id\":802,\"def\":\"titan.talos\",\"level\":25,\"xp\":0,\"recoverUntil\":0,\"ascended\":true");
	KD_CHECK(O.Load(AscOld, Error) && O.FindTitan(802)->skills.size() == 2 && O.FindTitan(802)->skills[0].kind != O.FindTitan(802)->skills[1].kind);
	// A slot fusion opens after ascension goes before the ascension skill, which stays last.
	std::vector<std::pair<int32_t, std::string>> Fodder = {{802, "titan.talos"}};
	for (int32_t I = 0; I < 12; ++I)
	{
		Fodder.emplace_back(810 + I, "titan.talos");
	}
	const std::string AscFuse = Replace(WithTitans(Save, Fodder), "\"id\":802,\"def\":\"titan.talos\",\"level\":1,\"xp\":0,\"recoverUntil\":0,\"ascended\":false",
		"\"id\":802,\"def\":\"titan.talos\",\"level\":21,\"xp\":0,\"recoverUntil\":0,\"ascended\":true");
	Kingdom Q(Ct);
	KD_CHECK(Q.Load(AscFuse, Error) && Q.FindTitan(802)->skills.size() == 2);
	const int AscKind = Q.FindTitan(802)->skills[1].kind;
	for (int32_t I = 0; I < 12 && Q.FindTitan(802)->skills.size() < 3; ++I)
	{
		KD_CHECK(Q.FuseTitans(802, {810 + I}) == Error::None);
	}
	KD_CHECK(Q.FindTitan(802)->skills.size() == 3 && Q.FindTitan(802)->skills[2].kind == AscKind);
}

const SimContent& Battle()
{
	static SimContent S;
	static bool bLoaded = false;
	if (!bLoaded)
	{
		std::string Error;
		if (!dottools::LoadContentDir(dottools::DefaultContentDir(), S, Error))
		{
			std::printf("battle content error: %s\n", Error.c_str());
		}
		bLoaded = true;
	}
	return S;
}

/** The save with its raids object replaced (it is the last key of a save). */
std::string WithRaids(const std::string& Save, const std::string& Raids)
{
	const size_t P = Save.find("\"raids\":{");
	return P == std::string::npos ? Save : Save.substr(0, P) + "\"raids\":" + Raids + "}";
}

void TestRaids()
{
	const KingdomContent& Ct = Content();
	const RaidRules& R = Ct.raids;
	KD_CHECK(R.castle == 3 && R.vpPct == 5 && R.titanLevels.size() == 9 && Ct.battles.size() == 9 && R.oddsKQuarters == 18);
	const BuildingDef& G = Ct.buildings[static_cast<size_t>(Def("garrison"))];
	KD_CHECK(G.levels[0].defenseSlots == 3 && G.levels.back().defenseSlots == 8 && G.levels.back().guardTier == 3 && G.levels.back().defenseBonusPct == 22);
	// The power the raids count is the battle's own start power.
	{
		BattleSetup S;
		std::string Error;
		KD_CHECK(dottools::LoadScenario(dottools::DefaultContentDir(), "scenarios/m0_sandbox.json", S, Error));
		BattleSimulation Sim(Battle());
		KD_CHECK(Sim.Init(S, Error));
		for (int Side = 0; Side < 2; ++Side)
		{
			int64_t Sum = TitanPower(Battle(), S.sides[Side].titan);
			for (const SquadSetup& Q : S.sides[Side].squads)
			{
				Sum += SquadPower(Battle(), Q);
			}
			KD_CHECK(Sum == Sim.GetSide(static_cast<dot::Side>(Side)).startPower);
		}
	}

	// Without battle content no raids come.
	Kingdom K(Content());
	K.NewGame(T0, 61);
	KD_CHECK(K.NextRaidAt() == 0 && K.DefenseSlots() == 2);
	K.Advance(T0 + 10 * 24 * Hour);
	KD_CHECK(K.Raids().empty());

	// Castle 6 (raiders of levels 2-4), a Garrison 5, a gold reserve, gold, food and VP; the start titans and army.
	Kingdom N(Content());
	N.NewGame(T0, 61);
	std::string Base = WithBuilding(WithBuilding(N.Save(), "garrison", 5, 30), "gold_reserve", 1, 34);
	Base = Replace(Base, "\"def\":\"castle\",\"level\":1", "\"def\":\"castle\",\"level\":6");
	Base = Replace(Base, "\"gold\":2500", "\"gold\":12000");
	Base = Replace(Base, "\"food\":2500", "\"food\":10000");
	Base = Replace(Base, "\"shards\":0,\"vp\":0", "\"shards\":0,\"vp\":1000");
	std::string Error;
	Kingdom A(Content());
	KD_CHECK(A.Load(Base, Error));
	A.SetBattleContent(&Battle());
	KD_CHECK(A.NextRaidAt() == T0 + 24 * Hour && A.DefenseSlots() == 4 && A.GuardTier() == 1 && A.DefenseBonusPct() == 4);
	const int32_t Perun = A.Titans()[0].id;
	const int32_t Talos = A.Titans()[1].id;
	// Raiders always bring a titan: guards alone do not stand a chance.
	RaidForecast F = A.Forecast(0);
	KD_CHECK(F.active && F.levelLow == 2 && F.levelHigh == 4 && F.squads == 0 && F.guards == 4 && F.titanId == 0 && F.holdPct < 5);
	// The city's defense: up to four squads that are at home and a titan.
	const int Mil = Troop("troop.militia");
	const int Arc = Troop("troop.archers");
	KD_CHECK(A.SetGarrison({Mil, Mil, Mil, Mil}, 0) == Error::NoTroops);
	KD_CHECK(A.SetGarrison({Mil, Mil, Arc, Mil, Mil}, 0) == Error::TooManySquads);
	KD_CHECK(A.SetGarrison({Mil, 99}, 0) == Error::BadId);
	KD_CHECK(A.SetGarrison({Mil, Arc}, 4242) == Error::BadId);
	KD_CHECK(A.SetGarrison({Mil, Mil, Arc}, Perun) == Error::None && A.GarrisonTitan() == Perun);
	RaidForecast G1 = A.Forecast(0);
	KD_CHECK(G1.squads == 3 && G1.guards == 1 && G1.titanId == Perun && G1.defense > F.defense && G1.holdPct > F.holdPct);
	// Squads and a titan away in the player's own attack do not defend: guards take the squads' places.
	KD_CHECK(A.StartAttack(A.World()[0].id, false, {Mil, Mil}, Perun) == Error::None);
	RaidForecast Away = A.Forecast(0);
	KD_CHECK(Away.titanId == 0 && Away.squads == 2 && Away.guards == 2 && Away.holdPct < G1.holdPct);
	// A titan guards one place: on a land it leaves the Garrison, and back.
	const std::string WithLand = Replace(Base, "\"lands\":[]",
		"\"lands\":[{\"id\":800,\"type\":\"gold_land\",\"level\":3,\"stored\":0,\"carry\":0,\"lastAt\":" + std::to_string(T0) + ",\"titan\":0}]");
	Kingdom B(Content());
	KD_CHECK(B.Load(WithLand, Error));
	B.SetBattleContent(&Battle());
	KD_CHECK(B.SetGarrison({}, Perun) == Error::None && B.AssignTitan(800, Perun) == Error::None && B.GarrisonTitan() == 0);
	KD_CHECK(B.Forecast(800).titanId == Perun && B.SetGarrison({}, Perun) == Error::None && B.FindLand(800)->titanId == 0);
	// A titan resting after a battle does not defend.
	Kingdom Rest(Content());
	KD_CHECK(Rest.Load(Replace(B.Save(), "\"recoverUntil\":0", "\"recoverUntil\":" + std::to_string(T0 + Hour)), Error));
	Rest.SetBattleContent(&Battle());
	KD_CHECK(Rest.Titans()[0].recoverUntil == T0 + Hour && Rest.Forecast(0).titanId == 0);

	// A lost defense of the city: a fifth of the gold and food above the reserves at three stars, 5% of VP, a shield.
	Kingdom C(Content());
	KD_CHECK(C.Load(Base, Error));
	C.SetBattleContent(&Battle());
	C.Advance(T0 + 24 * Hour);
	KD_CHECK(C.Raids().size() == 1);
	const RaidReport& Lost = C.Raids()[0];
	KD_CHECK(Lost.at == T0 + 24 * Hour && Lost.landId == 0 && Lost.stars == 3 && Lost.titanId == 0 && Lost.raidPower > Lost.defensePower);
	KD_CHECK(Lost.lost[static_cast<size_t>(ResIndex(Res::Gold))] == (12000 - 2000) * 20 / 100);
	KD_CHECK(Lost.lost[static_cast<size_t>(ResIndex(Res::Food))] == 10000 * 20 / 100);
	KD_CHECK(Lost.lost[static_cast<size_t>(ResIndex(Res::Vp))] == 50 && C.Amount(Res::Vp) == 950 && C.Amount(Res::Gold) == 10000);
	KD_CHECK(C.CityShieldUntil() == Lost.shieldUntil && Lost.shieldUntil == Lost.at + 12 * Hour);
	// The shield keeps the next raid off (the city is the only target), and the report is new until read.
	C.Advance(Lost.at + 12 * Hour - 1);
	KD_CHECK(C.Raids().size() == 1 && C.RaidsReadAt() < Lost.at && C.ReadRaids() == Error::None && C.RaidsReadAt() == C.Now());

	// A held defense: a strong titan on guard at Castle 3 (raiders of levels 1-3); VP for the victory.
	Kingdom D(Content());
	KD_CHECK(D.Load(Replace(Replace(Base, "\"def\":\"titan.perun\",\"level\":5", "\"def\":\"titan.perun\",\"level\":30"),
		"\"def\":\"castle\",\"level\":6", "\"def\":\"castle\",\"level\":3"), Error));
	D.SetBattleContent(&Battle());
	KD_CHECK(D.SetGarrison({Mil, Mil, Arc}, Perun) == Error::None && D.Forecast(0).holdPct >= 90);
	D.Advance(T0 + 24 * Hour);
	KD_CHECK(D.Raids().size() == 1 && D.Raids()[0].stars == 0 && D.Raids()[0].titanId == Perun && D.Raids()[0].squads == 3);
	KD_CHECK(D.Raids()[0].vp == D.VictoryVp(D.Raids()[0].level) * 50 / 100 && D.Amount(Res::Vp) == 1000 + D.Raids()[0].vp);
	KD_CHECK(D.CityShieldUntil() == 0 && D.Amount(Res::Gold) == 12000);

	// A raid on an unguarded land takes what it holds and shields it; the city is shielded, so the land is the target.
	Kingdom L(Content());
	KD_CHECK(L.Load(WithRaids(WithLand, "{\"next\":" + std::to_string(T0 + 24 * Hour) + ",\"visit\":" + std::to_string(T0) +
		",\"counter\":0,\"cityShield\":" + std::to_string(T0 + 100 * 24 * Hour) + ",\"read\":" + std::to_string(T0) + ",\"garrison\":[],\"titan\":0,\"log\":[]}"), Error));
	L.SetBattleContent(&Battle());
	L.Advance(T0 + 24 * Hour);
	KD_CHECK(L.Raids().size() == 1 && L.Raids()[0].landId == 800 && L.Raids()[0].stars == 3);
	KD_CHECK(L.Raids()[0].lost[static_cast<size_t>(ResIndex(Res::Gold))] == L.LandStoreCap(*L.FindLand(800)) && L.UncollectedLand(*L.FindLand(800)) == 0);
	KD_CHECK(L.FindLand(800)->shieldUntil == T0 + 36 * Hour && L.Amount(Res::Gold) == 12000);

	// Raids keep coming every 6-10 hours, and stop two days after the player's last visit.
	Kingdom E(Content());
	KD_CHECK(E.Load(WithLand, Error));
	E.SetBattleContent(&Battle());
	KD_CHECK(E.SetGarrison({Mil, Arc}, Talos) == Error::None && E.AssignTitan(800, Perun) == Error::None);
	const ResArray Before = [&E]() { ResArray A0{}; for (int I = 0; I < kResCount; ++I) A0[static_cast<size_t>(I)] = E.Amount(static_cast<Res>(I)); return A0; }();
	E.Advance(T0 + 72 * Hour); // before the season ends (T0 + 87 h) and resets VP
	KD_CHECK(E.Raids().size() >= 2 && E.Raids().size() <= 4 && E.NextRaidAt() == 0);
	int64_t LostGold = 0;
	int64_t VpDelta = 0;
	for (size_t I = 0; I < E.Raids().size(); ++I)
	{
		const RaidReport& Rep = E.Raids()[I];
		KD_CHECK(Rep.at <= T0 + 48 * Hour && (I == 0 || Rep.at >= E.Raids()[I - 1].at + 6 * Hour));
		KD_CHECK(Rep.level >= 2 && Rep.level <= 4 && Rep.raidPower > 0 && Rep.defensePower > 0);
		LostGold += Rep.landId ? 0 : Rep.lost[static_cast<size_t>(ResIndex(Res::Gold))];
		VpDelta += Rep.vp - Rep.lost[static_cast<size_t>(ResIndex(Res::Vp))];
	}
	KD_CHECK(E.Amount(Res::Gold) == Before[static_cast<size_t>(ResIndex(Res::Gold))] - LostGold);
	KD_CHECK(E.Amount(Res::Vp) == Before[static_cast<size_t>(ResIndex(Res::Vp))] + VpDelta);
	const size_t Within = E.Raids().size();
	E.Advance(T0 + 10 * 24 * Hour);
	KD_CHECK(E.Raids().size() == Within);
	// The same time in small steps, or through a save, ends in the same kingdom.
	Kingdom S(Content());
	KD_CHECK(S.Load(WithLand, Error));
	S.SetBattleContent(&Battle());
	KD_CHECK(S.SetGarrison({Mil, Arc}, Talos) == Error::None && S.AssignTitan(800, Perun) == Error::None);
	for (int64_t T = T0; T <= T0 + 5 * 24 * Hour; T += 1234)
	{
		S.Advance(T);
	}
	Kingdom S2(Content());
	KD_CHECK(S2.Load(S.Save(), Error) && S2.Hash() == S.Hash() && S2.Save() == S.Save());
	S2.SetBattleContent(&Battle());
	S2.Advance(T0 + 10 * 24 * Hour);
	KD_CHECK(S2.Hash() == E.Hash() && S2.Raids().size() == E.Raids().size());
	// A visit brings them back, from the visit on.
	E.Visit();
	KD_CHECK(E.LastVisit() == E.Now() && E.NextRaidAt() >= E.Now() + 6 * Hour && E.NextRaidAt() <= E.Now() + 10 * Hour);
	const size_t Count = E.Raids().size();
	E.Advance(E.Now() + 11 * Hour);
	KD_CHECK(E.Raids().size() == std::min<size_t>(Count + 1, static_cast<size_t>(R.log)) && E.Raids().back().at > E.LastVisit());
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
		{"LandUpgrades", TestLandUpgrades},
		{"TitanMonumentsAndXpArenas", TestTitanMonumentsAndXpArenas},
		{"Campaigns", TestCampaigns},
		{"DailyQuests", TestDailyQuests},
		{"TitanSkills", TestTitanSkills},
		{"Raids", TestRaids},
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
