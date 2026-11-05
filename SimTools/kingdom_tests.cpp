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
#include <limits>
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
	KD_CHECK(C.titans.size() == 13);
	KD_CHECK(C.titans[static_cast<size_t>(C.FindTitan("titan.thor"))].MaxLevel() == 50);
	// The original's Infiltrator, Paladin and Shaman classes.
	KD_CHECK(C.FindTitan("titan.loki") >= 0 && C.titans[static_cast<size_t>(C.FindTitan("titan.loki"))].cls == "Infiltrator" &&
		C.titans[static_cast<size_t>(C.FindTitan("titan.loki"))].stars == 2);
	KD_CHECK(C.FindTitan("titan.tyr") >= 0 && C.titans[static_cast<size_t>(C.FindTitan("titan.tyr"))].cls == "Paladin" &&
		C.titans[static_cast<size_t>(C.FindTitan("titan.tyr"))].stars == 3);
	KD_CHECK(C.FindTitan("titan.veles") >= 0 && C.titans[static_cast<size_t>(C.FindTitan("titan.veles"))].cls == "Shaman" &&
		C.titans[static_cast<size_t>(C.FindTitan("titan.veles"))].stars == 4);
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

void TestQueueRefunds()
{
	const KingdomContent& Ct = Content();
	Kingdom K(Ct);
	K.NewGame(T0, 5);
	const int Mil = Troop("troop.militia");
	// Each queued unit keeps the price paid for it, so research in between does not change what a cancel returns.
	KD_CHECK(K.Train(Mil) == Error::None && K.Train(Mil) == Error::None);
	const std::string Base = K.Save();
	KD_CHECK(Base.find("\"queuePaid\":[135,135]") != std::string::npos);
	std::string Error;
	Kingdom A(Ct);
	KD_CHECK(A.Load(Replace(Base, "\"queuePaid\":[135,135]", "\"queuePaid\":[135,100]"), Error));
	int64_t Food = A.Amount(Res::Food);
	KD_CHECK(A.CancelTrain(1) == Error::None && A.Amount(Res::Food) == Food + 100 && A.TrainQueue().size() == 1);
	// A save without the prices (older format) takes today's.
	Kingdom B(Ct);
	KD_CHECK(B.Load(Replace(Base, ",\"queuePaid\":[135,135]", ""), Error) && B.Save() == Base && B.Hash() == K.Hash());
	// The refund goes into the granaries only as far as they hold it.
	const int64_t Cap = K.Capacity(Res::Food);
	Food = K.Amount(Res::Food);
	Kingdom F(Ct);
	KD_CHECK(F.Load(Replace(Base, "\"food\":" + std::to_string(Food), "\"food\":" + std::to_string(Cap - 50)), Error));
	KD_CHECK(F.Amount(Res::Food) == Cap - 50 && F.CancelTrain(0) == Error::None && F.Amount(Res::Food) == Cap);
	// The unit that came out of the queue takes its price along.
	K.Advance(T0 + 60);
	KD_CHECK(K.TrainQueue().size() == 1 && K.Save().find("\"queuePaid\":[135]") != std::string::npos);
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
	// A store the titan filled above the plain cap keeps what it holds after the titan leaves, and grows no more.
	L.Advance(L.Now() + 48 * Hour);
	const int64_t Full = L.UncollectedLand(*L.FindLand(R.landId));
	KD_CHECK(Full == L.LandStoreCap(*L.FindLand(R.landId)));
	KD_CHECK(L.RecallTitan(R.landId) == Error::None);
	L.Advance(L.Now() + Hour);
	KD_CHECK(Full > L.LandStoreCap(*L.FindLand(R.landId)) && L.UncollectedLand(*L.FindLand(R.landId)) == Full);
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
	KD_CHECK(Sk.slots == 4 && Sk.openPct == 35 && Sk.upgradePct == 25 && Sk.kinds.size() == 7 && Sk.classWeights.size() == 8);
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

void TestRivals()
{
	const KingdomContent& Ct = Content();
	const RivalRules& RR = Ct.rivals;
	KD_CHECK(RR.castleOffsets.size() == 5 && RR.lords.size() == 8 && RR.castle == 3 && RR.lootPct == 150 && RR.vpPct == 150);
	KD_CHECK(RR.titanLevels.size() == 9 && RR.regrowSec == 8 * Hour && RR.shieldSec == 8 * Hour);
	// A new kingdom: five different lords (Castles 1 to 3), two lands each, all of their own band; the neutral world as before.
	Kingdom K(Ct);
	K.NewGame(T0, 21);
	KD_CHECK(K.Rivals().size() == 5 && K.RivalLands().size() == 10 && static_cast<int>(K.World().size()) == Ct.worldSize);
	std::vector<int> Lords;
	for (const Rival& R : K.Rivals())
	{
		KD_CHECK(std::find(Lords.begin(), Lords.end(), R.lord) == Lords.end() && R.regrow.empty());
		Lords.push_back(R.lord);
	}
	KD_CHECK(K.RivalCastle(0) == 1 && K.RivalCastle(1) == 1 && K.RivalCastle(3) == 2 && K.RivalCastle(4) == 3);
	for (const WorldLand& W : K.RivalLands())
	{
		KD_CHECK(W.id >= Kingdom::kRivalLandBase && W.owner >= 0 && W.owner < 5 && W.level >= 1 && W.level <= 3 && W.shieldUntil == 0);
		KD_CHECK(K.FindWorldLand(W.id) == &W && Ct.landTypes[static_cast<size_t>(W.type)].castle <= K.RivalCastle(W.owner));
		// The lord's titan (one star at these Castles) guards every land, at the table's level for the land's.
		KD_CHECK(Ct.titans[static_cast<size_t>(K.RivalTitan(W.owner))].stars == 1 && K.RivalTitanLevel(W) == RR.titanLevels[static_cast<size_t>(W.level - 1)]);
	}
	// Their lands open at Castle 3.
	KD_CHECK(K.StartAttack(K.RivalLands()[0].id, false) == Error::CastleTooLow);
	// The same seed gives the same lords and lands.
	Kingdom K2(Ct);
	K2.NewGame(T0, 21);
	KD_CHECK(K2.Hash() == K.Hash());

	// An old save at Castle 6 (no lords in it): lords at Castles 5-8 hold 2 or 3 lands.
	std::string Base = Replace(K.Save(), "\"def\":\"castle\",\"level\":1", "\"def\":\"castle\",\"level\":6");
	Base = Replace(Base, "\"rivals\":", "\"oldRivals\":");
	std::string Error;
	Kingdom A(Ct);
	KD_CHECK(A.Load(Base, Error));
	int Expected = 0;
	for (int R = 0; R < 5; ++R)
	{
		Expected += A.RivalLandTarget(R);
	}
	KD_CHECK(Expected == 14 && static_cast<int>(A.RivalLands().size()) == Expected);
	KD_CHECK(Ct.titans[static_cast<size_t>(A.RivalTitan(0))].stars == 1 && Ct.titans[static_cast<size_t>(A.RivalTitan(4))].stars == 2);
	for (const WorldLand& W : A.RivalLands())
	{
		const int Lo = (A.RivalCastle(W.owner) + 1) / 3;
		KD_CHECK(W.level >= Lo && W.level <= Lo + 2);
		// The table's level for the land's, under the titan's own cap.
		KD_CHECK(A.RivalTitanLevel(W) == std::min(static_cast<int>(RR.titanLevels[static_cast<size_t>(W.level - 1)]),
			Ct.titans[static_cast<size_t>(A.RivalTitan(W.owner))].MaxLevel()));
	}

	// A raid: half as much again of loot and VP; the land stays the lord's under a shield.
	const WorldLand T = A.RivalLands()[0];
	const LandTypeDef& Ty = Ct.landTypes[static_cast<size_t>(T.type)];
	const std::vector<int64_t>& Table = Ty.raidLoot.empty() ? Ct.raidLoot : Ty.raidLoot;
	KD_CHECK(A.RaidLoot(T) == Table[static_cast<size_t>(T.level - 1)] * 150 / 100 && A.WorldVp(T) == A.VictoryVp(T.level) * 150 / 100);
	Reward R;
	KD_CHECK(A.StartAttack(T.id, false) == Error::None);
	KD_CHECK(A.FinishAttack(T.id, false, 3, &R) == Error::None);
	KD_CHECK(R.res[static_cast<size_t>(ResIndex(Res::Vp))] == A.WorldVp(T) && R.res[static_cast<size_t>(ResIndex(Ty.resource))] > 0);
	const WorldLand* Raided = A.FindWorldLand(T.id);
	KD_CHECK(Raided && Raided->shieldUntil == A.Now() + RR.shieldSec && static_cast<int>(A.RivalLands().size()) == Expected);
	KD_CHECK(A.StartAttack(T.id, true) == Error::NotReady);
	// A lost battle changes nothing.
	const WorldLand U = A.RivalLands()[1];
	KD_CHECK(A.StartAttack(U.id, false) == Error::None && A.FinishAttack(U.id, false, 0, &R) == Error::None);
	KD_CHECK(A.FindWorldLand(U.id)->shieldUntil == 0 && R.res[static_cast<size_t>(ResIndex(Res::Vp))] == 0);
	// After the shield it can be taken: the player gets it at its level, and the lord wins another one back later.
	A.Advance(A.Now() + RR.shieldSec);
	KD_CHECK(A.StartAttack(T.id, true) == Error::None);
	KD_CHECK(A.FinishAttack(T.id, true, 2, &R) == Error::None && R.landId != 0);
	const Land* Taken = A.FindLand(R.landId);
	KD_CHECK(Taken && Taken->type == T.type && Taken->level == T.level && !A.FindWorldLand(T.id));
	KD_CHECK(static_cast<int>(A.RivalLands().size()) == Expected - 1 && A.Rivals()[static_cast<size_t>(T.owner)].regrow.size() == 1);
	const int64_t Back = A.Rivals()[static_cast<size_t>(T.owner)].regrow[0];
	KD_CHECK(Back == A.Now() + RR.regrowSec);
	// Saved and loaded in between, both copies go on the same way.
	Kingdom B(Ct);
	KD_CHECK(B.Load(A.Save(), Error) && B.Hash() == A.Hash() && B.Save() == A.Save());
	A.Advance(Back - 1);
	KD_CHECK(static_cast<int>(A.RivalLands().size()) == Expected - 1);
	A.Advance(Back);
	KD_CHECK(static_cast<int>(A.RivalLands().size()) == Expected && A.Rivals()[static_cast<size_t>(T.owner)].regrow.empty());
	KD_CHECK(A.RivalLands().back().owner == T.owner && A.RivalLands().back().id > T.id);
	B.Advance(Back + Hour);
	A.Advance(Back + Hour);
	KD_CHECK(B.Hash() == A.Hash());
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
std::string WithGoal(const std::string& Save, const std::string& Goal)
{
	// The goal is the save's last key.
	const size_t P = Save.find("\"goal\":{");
	return P == std::string::npos ? Save : Save.substr(0, P) + "\"goal\":" + Goal + "}";
}

int GoalIx(const char* Id)
{
	for (size_t I = 0; I < Content().goals.size(); ++I)
	{
		if (Content().goals[I].id == Id)
		{
			return static_cast<int>(I);
		}
	}
	return -1;
}

void TestGoals()
{
	const KingdomContent& Ct = Content();
	const std::vector<GoalDef>& Line = Ct.goals;
	const size_t GoldI = static_cast<size_t>(ResIndex(Res::Gold));
	const size_t GemsI = static_cast<size_t>(ResIndex(Res::Gems));
	KD_CHECK(Line.size() >= 60 && Line.front().id == "first_food");
	KD_CHECK(!Line.empty() && Line.back().check == GoalCheck::Castle && Line.back().n == 9);

	// Nothing deadlocks: whatever a level needs can be built at the Castle that level itself asks for.
	for (const BuildingDef& B : Ct.buildings)
	{
		for (const LevelDef& L : B.levels)
		{
			for (const Requirement& Q : L.needs)
			{
				const BuildingDef& N = Ct.buildings[static_cast<size_t>(Q.building)];
				const bool bOk = Q.level >= 1 && Q.level <= N.MaxLevel() && N.levels[static_cast<size_t>(Q.level - 1)].castle <= L.castle &&
					N.unlockCastle <= L.castle && N.MaxCount(L.castle) >= Q.count;
				if (!bOk)
				{
					std::printf("  (%s level %d needs %s level %d, not open at Castle %d)\n", B.id.c_str(), L.level, N.id.c_str(), Q.level, L.castle);
				}
				KD_CHECK(bOk);
			}
		}
	}

	// Walk the line as a player would and check that each goal can be done when it comes up: what it builds is open
	// at the Castle reached so far, and what the building needs was asked for by an earlier goal (or stands at the start).
	std::vector<std::vector<int>> Levels(Ct.buildings.size());
	for (const StartBuilding& S : Ct.startBuildings)
	{
		Levels[static_cast<size_t>(S.building)].push_back(S.level);
	}
	const int CastleDef = Def("castle");
	int Castle = 1;
	const auto Top = [&](int D) {
		int V = 0;
		for (int L : Levels[static_cast<size_t>(D)])
		{
			V = std::max(V, L);
		}
		return V;
	};
	const auto LevelOpen = [&](int D, int L, const std::string& Id) {
		const BuildingDef& B = Ct.buildings[static_cast<size_t>(D)];
		bool bOk = L >= 1 && L <= B.MaxLevel() && B.unlockCastle <= Castle && B.levels[static_cast<size_t>(L - 1)].castle <= Castle &&
			B.levels[static_cast<size_t>(L - 1)].expansion == 0;
		for (const Requirement& Q : bOk ? B.levels[static_cast<size_t>(L - 1)].needs : std::vector<Requirement>())
		{
			int Have = 0;
			for (int X : Levels[static_cast<size_t>(Q.building)])
			{
				Have += X >= Q.level ? 1 : 0;
			}
			bOk = bOk && Have >= Q.count;
		}
		if (!bOk)
		{
			std::printf("  (goal %s: %s level %d is not open yet at Castle %d)\n", Id.c_str(), B.id.c_str(), L, Castle);
		}
		return bOk;
	};
	const auto Stat = [&](const char* Id, int64_t LevelDef::*Field) {
		const int D = Def(Id);
		const int L = Top(D);
		return L >= 1 ? Ct.buildings[static_cast<size_t>(D)].levels[static_cast<size_t>(L - 1)].*Field : 0;
	};
	int64_t Gems = 0;
	int64_t Titans = static_cast<int64_t>(Ct.startTitans.size());
	std::vector<bool> Used(static_cast<size_t>(GoalCheck::Count), false);
	for (const GoalDef& G : Line)
	{
		Used[static_cast<size_t>(G.check)] = true;
		Gems += G.reward[GemsI];
		int64_t Sum = 0;
		for (int64_t R : G.reward)
		{
			Sum += R;
		}
		KD_CHECK(Sum > 0);
		bool bOk = true;
		switch (G.check)
		{
		case GoalCheck::Castle:
			bOk = G.n == Castle + 1 && LevelOpen(CastleDef, static_cast<int>(G.n), G.id);
			Castle = static_cast<int>(G.n);
			Levels[static_cast<size_t>(CastleDef)] = {Castle};
			break;
		case GoalCheck::Build:
		{
			std::vector<int>& Have = Levels[static_cast<size_t>(G.building)];
			bOk = LevelOpen(G.building, 1, G.id) && Ct.buildings[static_cast<size_t>(G.building)].MaxCount(Castle) >= G.n &&
				static_cast<int64_t>(Have.size()) < G.n;
			while (static_cast<int64_t>(Have.size()) < G.n)
			{
				Have.push_back(1);
			}
			break;
		}
		case GoalCheck::Level:
		{
			const int From = Top(G.building);
			bOk = From >= 1 && From < G.n;
			for (int L = From + 1; L <= G.n; ++L)
			{
				bOk = LevelOpen(G.building, L, G.id) && bOk;
			}
			std::vector<int>& Have = Levels[static_cast<size_t>(G.building)];
			*std::max_element(Have.begin(), Have.end()) = static_cast<int>(G.n);
			break;
		}
		case GoalCheck::TroopLevel:
			bOk = Ct.troops[static_cast<size_t>(G.troop)].unlockCastle <= Castle && Stat("armoury", &LevelDef::maxTroopLevel) >= G.n;
			break;
		case GoalCheck::Lands:
		case GoalCheck::Guard:
			bOk = Stat("castle", &LevelDef::landCapacity) >= G.n;
			break;
		case GoalCheck::Garrison:
			bOk = Stat("garrison", &LevelDef::defenseSlots) + 1 >= G.n;
			break;
		case GoalCheck::Titans:
			bOk = Stat("hall_of_titans", &LevelDef::titans) >= G.n;
			break;
		case GoalCheck::Relics:
			bOk = Top(Def("relic_temple")) >= 1;
			break;
		case GoalCheck::Spells:
			bOk = Top(Def("spell_forge")) >= 1;
			break;
		case GoalCheck::SpellLevel:
			bOk = Stat("arcane_tower", &LevelDef::maxSpellLevel) >= G.n;
			break;
		case GoalCheck::Missions:
			bOk = Top(Def("adventurers_guild")) >= 1;
			break;
		case GoalCheck::Power:
		{
			// Not done by what the line asked for so far (its buildings, the titans at level 1), and within reach at
			// this Castle (every building there at its top level, as many of the weakest titan at its top level).
			const auto Each = [&](int D, int L) {
				const int64_t V = static_cast<int64_t>(ISqrt64(static_cast<uint64_t>(Ct.power.building * Ct.power.building) * L * L * L));
				return D == CastleDef ? V * Ct.power.castleMul : V;
			};
			int64_t Low = 0;
			int64_t High = 0;
			for (size_t D = 0; D < Ct.buildings.size(); ++D)
			{
				const BuildingDef& B = Ct.buildings[D];
				for (int L : Levels[D])
				{
					Low += Each(static_cast<int>(D), L);
				}
				int Open = 0;
				for (int L = 1; L <= B.MaxLevel() && B.unlockCastle <= Castle; ++L)
				{
					Open = B.levels[static_cast<size_t>(L - 1)].castle <= Castle ? L : Open;
				}
				High += Open ? Each(static_cast<int>(D), Open) * B.MaxCount(Castle) : 0;
			}
			int64_t Weakest = std::numeric_limits<int64_t>::max();
			int64_t WeakestTop = std::numeric_limits<int64_t>::max();
			for (const TitanType& T : Ct.titans)
			{
				TitanSetup S;
				S.present = true;
				S.titanId = T.id;
				S.stars = T.stars;
				S.level = 1;
				Weakest = std::min(Weakest, TitanPower(Battle(), S) / 100);
				S.level = 10 + 10 * T.stars;
				WeakestTop = std::min(WeakestTop, TitanPower(Battle(), S) / 100);
			}
			const int64_t Count = std::min<int64_t>(Titans, Ct.power.titans);
			Low += Count * Weakest;
			High += Count * WeakestTop;
			bOk = Low < G.n && G.n <= High;
			if (!bOk)
			{
				std::printf("  (goal %s: Power %lld, at least %lld and at most %lld at Castle %d)\n", G.id.c_str(), static_cast<long long>(G.n),
					static_cast<long long>(Low), static_cast<long long>(High), Castle);
			}
			break;
		}
		default:
			break;
		}
		if (G.check == GoalCheck::Titans)
		{
			Titans = std::max<int64_t>(Titans, G.n);
		}
		if (!bOk)
		{
			std::printf("  (goal %s cannot be done when it comes up)\n", G.id.c_str());
		}
		KD_CHECK(bOk);
	}
	// About 250 gems over the line (master prompt 8.6: a free player's 40 a day comes mostly from elsewhere).
	KD_CHECK(Gems >= 200 && Gems <= 300);
	for (size_t I = 0; I < Used.size(); ++I)
	{
		KD_CHECK(Used[I]);
	}

	// A new kingdom starts at the first goal: collect food.
	Kingdom K(Ct);
	K.NewGame(T0, 11);
	KD_CHECK(K.GoalIndex() == 0 && K.GoalValue() == 0 && !K.GoalDone());
	KD_CHECK(K.ClaimGoal(nullptr) == Error::NotReady);
	const uint64_t Fresh = K.Hash();
	K.Advance(T0 + Hour);
	KD_CHECK(K.Collect(FindId(K, "gold_mine")) == Error::None && K.GoalValue() == 0); // gold is not food
	KD_CHECK(K.Collect(FindId(K, "farm")) == Error::None && K.GoalValue() == 100 && K.GoalDone());
	const int64_t Gold = K.Amount(Res::Gold);
	Reward R;
	KD_CHECK(K.ClaimGoal(&R) == Error::None && R.res[GoldI] == 300 && K.Amount(Res::Gold) == Gold + 300);
	KD_CHECK(K.GoalIndex() == 1 && K.GoalValue() == 0 && !K.GoalDone() && K.ClaimGoal(nullptr) == Error::NotReady);
	K.Advance(T0 + 2 * Hour);
	KD_CHECK(K.Collect(FindId(K, "gold_mine")) == Error::None && K.GoalDone());
	const std::string Save = K.Save();
	KD_CHECK(Save.find("\"goal\":{\"at\":1,\"id\":\"first_gold\",\"events\":100}}") != std::string::npos);
	std::string Error;
	Kingdom L(Ct);
	KD_CHECK(L.Load(Save, Error) && L.Hash() == K.Hash() && L.Save() == Save && L.GoalDone());
	// A save from before the line starts at its first goal, and a kingdom that has not started it hashes as before.
	Kingdom N(Ct);
	N.NewGame(T0, 11);
	const std::string NewSave = N.Save();
	KD_CHECK(N.Hash() == Fresh);
	Kingdom O(Ct);
	KD_CHECK(O.Load(NewSave.substr(0, NewSave.find(",\"goal\":{")) + "}", Error) && O.GoalIndex() == 0 && O.Hash() == Fresh);
	KD_CHECK(O.Load(WithGoal(Save, "{}"), Error) && O.GoalIndex() == 0 && O.GoalValue() == 0);
	// The goal's id wins over its index (the line may change); an unknown id keeps the index, clamped.
	KD_CHECK(O.Load(WithGoal(Save, "{\"at\":1,\"id\":\"castle_3\",\"events\":0}"), Error) && O.GoalIndex() == GoalIx("castle_3"));
	KD_CHECK(O.Load(WithGoal(Save, "{\"at\":999,\"id\":\"gone\",\"events\":-5}"), Error) && O.GoalIndex() == static_cast<int>(Line.size()));
	KD_CHECK(!O.GoalDone() && O.GoalValue() == 0 && O.ClaimGoal(nullptr) == Error::Nothing);
	KD_CHECK(O.Save().find("\"goal\":{\"at\":" + std::to_string(Line.size()) + ",\"id\":\"\",\"events\":0}") != std::string::npos);

	// A state goal counts what is there now: Castle 2, then a second gold mine.
	const int Castle2 = GoalIx("castle_2");
	const int Mine2 = GoalIx("mine_2");
	KD_CHECK(Castle2 >= 0 && Mine2 == Castle2 + 1);
	Kingdom C(Ct);
	KD_CHECK(C.Load(WithGoal(Save, "{\"id\":\"castle_2\"}"), Error) && C.GoalIndex() == Castle2 && !C.GoalDone());
	KD_CHECK(C.Load(Replace(WithGoal(Save, "{\"id\":\"castle_2\"}"), "\"def\":\"castle\",\"level\":1", "\"def\":\"castle\",\"level\":2"), Error));
	const int64_t GemsBefore = C.Amount(Res::Gems);
	KD_CHECK(C.GoalDone() && C.ClaimGoal(&R) == Error::None && R.res[GemsI] == 10 && C.Amount(Res::Gems) == GemsBefore + 10);
	KD_CHECK(C.GoalIndex() == Mine2 && C.GoalValue() == 1 && !C.GoalDone());
	KD_CHECK(C.Load(WithBuilding(C.Save(), "gold_mine", 0, 30), Error) && C.GoalValue() == 1); // still being built
	KD_CHECK(C.Load(WithBuilding(C.Save(), "gold_mine", 1, 34), Error) && C.GoalValue() == 2 && C.GoalDone());
	// An event goal counts only while it is the current goal, and a cancelled squad does not count.
	const int Squad = GoalIx("first_squad");
	KD_CHECK(Squad == 2);
	Kingdom S(Ct);
	KD_CHECK(S.Load(WithGoal(Save, "{\"id\":\"first_squad\"}"), Error) && S.GoalValue() == 0);
	KD_CHECK(S.Train(Troop("troop.militia")) == Error::None && S.GoalDone());
	KD_CHECK(S.CancelTrain(static_cast<int>(S.TrainQueue().size()) - 1) == Error::None && S.GoalValue() == 0);
}

int AchIx(const char* Id)
{
	for (size_t I = 0; I < Content().achievements.size(); ++I)
	{
		if (Content().achievements[I].id == Id)
		{
			return static_cast<int>(I);
		}
	}
	return -1;
}

std::string WithAch(const std::string& Save, const std::string& Ach)
{
	const size_t P = Save.find("\"achievements\":{");
	const size_t E = Save.find(",\"goal\":{");
	return P == std::string::npos || E == std::string::npos ? Save : Save.substr(0, P) + "\"achievements\":" + Ach + Save.substr(E);
}

void TestAchievements()
{
	const KingdomContent& Ct = Content();
	const size_t GemsI = static_cast<size_t>(ResIndex(Res::Gems));
	// Bronze, silver and gold for gems (master prompt 13.5); about 900 gems in all, over months of play.
	KD_CHECK(Ct.achievements.size() >= 20);
	int64_t Gems = 0;
	std::vector<bool> Events(static_cast<size_t>(QuestEvent::Count), false);
	for (const AchievementDef& A : Ct.achievements)
	{
		KD_CHECK(A.tiers.size() == 3 && A.tiers[0].second > 0 && A.tiers[0].second < A.tiers[1].second && A.tiers[1].second < A.tiers[2].second);
		for (const auto& T : A.tiers)
		{
			Gems += T.second;
		}
		if (A.check == GoalCheck::Event)
		{
			Events[static_cast<size_t>(A.event)] = true;
		}
	}
	KD_CHECK(Gems >= 600 && Gems <= 1200);
	KD_CHECK(Events[static_cast<size_t>(QuestEvent::Win)] && Events[static_cast<size_t>(QuestEvent::CollectGold)] && Events[static_cast<size_t>(QuestEvent::Train)]);
	KD_CHECK(std::string(QuestEventName(QuestEvent::CollectGold)) == "collectGold" && std::string(QuestEventName(QuestEvent::Count)).empty());

	// Counted over the whole game, whatever the goal or the daily quests are.
	Kingdom K(Ct);
	K.NewGame(T0, 13);
	const uint64_t Fresh = K.Hash();
	const int Gold = AchIx("ach_gold");
	const int Train = AchIx("ach_train");
	const int Castle = AchIx("ach_castle");
	KD_CHECK(Gold >= 0 && Train >= 0 && Castle >= 0);
	KD_CHECK(K.AchievementValue(Gold) == 0 && K.AchievementTier(Gold) == 0 && !K.AchievementReady(Gold));
	KD_CHECK(K.AchievementValue(Castle) == 1 && !K.AchievementReady(Castle));
	KD_CHECK(K.ClaimAchievement(Gold, nullptr) == Error::NotReady && K.ClaimAchievement(-1, nullptr) == Error::BadId &&
		K.ClaimAchievement(static_cast<int>(Ct.achievements.size()), nullptr) == Error::BadId);
	K.Advance(T0 + Hour);
	KD_CHECK(K.Collect(FindId(K, "gold_mine")) == Error::None && K.Lifetime(QuestEvent::CollectGold) == 100 && K.AchievementValue(Gold) == 100);
	KD_CHECK(K.Hash() != Fresh);
	const int Mil = Troop("troop.militia");
	KD_CHECK(K.Train(Mil) == Error::None && K.Train(Mil) == Error::None && K.Lifetime(QuestEvent::Train) == 2);
	KD_CHECK(K.CancelTrain(1) == Error::None && K.AchievementValue(Train) == 1); // a cancelled squad does not count

	// The first tier pays once; the next one needs more.
	std::string Error;
	const std::string Save = K.Save();
	KD_CHECK(Save.find("\"achievements\":{\"counts\":{\"collectGold\":100,\"train\":1},\"claimed\":{}}") != std::string::npos);
	Kingdom A(Ct);
	KD_CHECK(A.Load(WithAch(Save, "{\"counts\":{\"collectGold\":99950,\"train\":1},\"claimed\":{}}"), Error) && !A.AchievementReady(Gold));
	A.Advance(T0 + 2 * Hour);
	KD_CHECK(A.Collect(FindId(A, "gold_mine")) == Error::None && A.AchievementValue(Gold) == 100050 && A.AchievementReady(Gold));
	const int64_t GemsBefore = A.Amount(Res::Gems);
	Reward R;
	KD_CHECK(A.ClaimAchievement(Gold, &R) == Error::None && R.res[GemsI] == Ct.achievements[static_cast<size_t>(Gold)].tiers[0].second);
	KD_CHECK(A.Amount(Res::Gems) == GemsBefore + R.res[GemsI] && A.AchievementTier(Gold) == 1 && !A.AchievementReady(Gold));
	KD_CHECK(A.ClaimAchievement(Gold, nullptr) == Error::NotReady);
	const std::string Paid = A.Save();
	KD_CHECK(Paid.find("\"claimed\":{\"ach_gold\":1}") != std::string::npos);
	Kingdom B(Ct);
	KD_CHECK(B.Load(Paid, Error) && B.Hash() == A.Hash() && B.Save() == Paid && B.AchievementTier(Gold) == 1);
	// Every tier paid: Nothing. Unknown ids and too many tiers are dropped.
	KD_CHECK(B.Load(WithAch(Paid, "{\"counts\":{\"collectGold\":-5,\"bogus\":3},\"claimed\":{\"ach_gold\":9,\"gone\":1}}"), Error));
	KD_CHECK(B.Lifetime(QuestEvent::CollectGold) == 0 && B.AchievementTier(Gold) == 3 && !B.AchievementReady(Gold) && B.ClaimAchievement(Gold, nullptr) == Error::Nothing);
	// A fact of the kingdom: Castle 5 reaches bronze.
	KD_CHECK(B.Load(Replace(Paid, "\"def\":\"castle\",\"level\":1", "\"def\":\"castle\",\"level\":5"), Error) && B.AchievementValue(Castle) == 5 && B.AchievementReady(Castle));
	KD_CHECK(B.ClaimAchievement(Castle, &R) == Error::None && R.res[GemsI] == 5 && B.AchievementTier(Castle) == 1 && !B.AchievementReady(Castle));
	// A save from before achievements counts from zero and keeps its hash.
	Kingdom N(Ct);
	N.NewGame(T0, 13);
	const std::string NewSave = N.Save();
	Kingdom O(Ct);
	const size_t P = NewSave.find(",\"achievements\":{");
	KD_CHECK(P != std::string::npos && O.Load(NewSave.substr(0, P) + NewSave.substr(NewSave.find(",\"goal\":{")), Error) && O.Hash() == Fresh);
	KD_CHECK(O.Lifetime(QuestEvent::Win) == 0 && O.AchievementTier(Gold) == 0);
}

/** The save with its weekly quests replaced. */
std::string WithWeekly(const std::string& Save, const std::string& Weekly)
{
	const size_t P = Save.find("\"weekly\":{");
	const size_t Q = Save.find(",\"calendar\":{");
	return P == std::string::npos || Q == std::string::npos ? Save : Save.substr(0, P) + "\"weekly\":" + Weekly + Save.substr(Q);
}

void TestWeeklyQuests()
{
	const KingdomContent& Ct = Content();
	const QuestRules& W = Ct.weekly;
	const QuestRules& D = Ct.quests;
	const size_t GemsI = static_cast<size_t>(ResIndex(Res::Gems));
	const size_t StonesI = static_cast<size_t>(ResIndex(Res::Stones));
	// Master prompt 13.5: five a week and a weekly chest. Ours: one swap, 55 gems a week (about 8 a day; with the daily
	// quests' 20 and the calendar's 3 under the free player's 40, 8.6).
	KD_CHECK(W.count == 5 && W.rerolls == 1 && W.pool.size() == 11 && W.chest[GemsI] == 30 && W.chestRelic.size() == 2);
	int64_t Most = 0;
	for (const QuestDef& Q : W.pool)
	{
		Most = std::max(Most, Q.reward[GemsI]);
		KD_CHECK(!Q.target.empty() && Q.target.front().first <= Q.castle);
	}
	KD_CHECK(W.count * Most + W.chest[GemsI] >= 40 && W.count * Most + W.chest[GemsI] <= 70);
	// Every weekly quest has a daily twin, and at every Castle its target is three to five days of the twin's.
	Kingdom Probe(Ct);
	for (const QuestDef& Q : W.pool)
	{
		const auto Twin = std::find_if(D.pool.begin(), D.pool.end(), [&](const QuestDef& X) { return X.event == Q.event; });
		KD_CHECK(Twin != D.pool.end() && Twin->castle == Q.castle && Twin->building == Q.building);
		for (int Castle = 1; Twin != D.pool.end() && Castle <= 25; ++Castle)
		{
			const auto At = [Castle](const QuestDef& X) {
				int64_t T = X.target.front().second;
				for (const std::pair<int, int64_t>& S : X.target)
				{
					T = Castle >= S.first ? S.second : T;
				}
				return T;
			};
			if (Castle >= Q.castle)
			{
				KD_CHECK(At(Q) >= 3 * At(*Twin) && At(Q) <= 5 * At(*Twin));
			}
		}
	}

	// A new kingdom gets five different weekly quests open at Castle 1, until the next Monday.
	Kingdom K(Ct);
	K.NewGame(T0, 7);
	const int64_t Day = 86400;
	KD_CHECK(K.WeeklyQuests().size() == 5 && K.WeeklyRerollsLeft() == 1 && !K.WeeklyChestClaimed());
	KD_CHECK(K.WeekEnd() > T0 && K.WeekEnd() <= T0 + 7 * Day && (K.WeekEnd() / Day + 3) % 7 == 0 && K.WeekEnd() % Day == 0);
	for (size_t I = 0; I < K.WeeklyQuests().size(); ++I)
	{
		const DailyQuest& Q = K.WeeklyQuests()[I];
		KD_CHECK(W.pool[static_cast<size_t>(Q.def)].castle == 1 && Q.progress == 0 && !Q.claimed);
		for (size_t J = 0; J < I; ++J)
		{
			KD_CHECK(K.WeeklyQuests()[J].def != Q.def);
		}
	}
	// The same week on every client; the daily ones change, the weekly ones stay until Monday.
	Kingdom K2(Ct);
	K2.NewGame(T0, 7);
	KD_CHECK(K2.Hash() == K.Hash());
	const std::vector<DailyQuest> Week0 = K.WeeklyQuests();
	K.Advance(K.WeekEnd() - 60);
	KD_CHECK(K.WeeklyQuests().size() == 5 && K.WeeklyQuests()[0].def == Week0[0].def && K.WeeklyQuests()[4].def == Week0[4].def);

	// Progress from the same events as the daily quests; rewards go on top of a full portal.
	std::string Error;
	const std::string List = "{\"week\":" + std::to_string((T0 / Day + 3) / 7) + ",\"rerolls\":0,\"chest\":false,\"list\":["
		"{\"id\":\"w_train\",\"target\":12,\"progress\":11,\"claimed\":false},"
		"{\"id\":\"w_gold\",\"target\":2000,\"progress\":2000,\"claimed\":false},"
		"{\"id\":\"w_win\",\"target\":8,\"progress\":8,\"claimed\":false},"
		"{\"id\":\"w_stones\",\"target\":36,\"progress\":40,\"claimed\":false},"
		"{\"id\":\"w_upgrade\",\"target\":6,\"progress\":0,\"claimed\":false}]}";
	Kingdom P(Ct);
	P.NewGame(T0, 7);
	KD_CHECK(P.Load(WithWeekly(P.Save(), List), Error) && P.WeeklyQuests().size() == 5 && P.WeeklyQuests()[4].def >= 0);
	KD_CHECK(P.ClaimWeekly(0, nullptr) == Error::NotReady && P.ClaimWeekly(5, nullptr) == Error::BadId);
	KD_CHECK(P.Train(Troop("troop.militia")) == Error::None && P.WeeklyQuests()[0].progress == 12);
	KD_CHECK(P.Amount(Res::Stones) >= P.Capacity(Res::Stones));
	const int64_t Gems = P.Amount(Res::Gems);
	const int64_t Stones = P.Amount(Res::Stones);
	Reward R;
	KD_CHECK(P.ClaimWeekly(0, &R) == Error::None && R.res[GemsI] == 5 && R.res[StonesI] == 6 && P.Amount(Res::Gems) == Gems + 5);
	KD_CHECK(P.Amount(Res::Stones) == Stones + 6 && P.Amount(Res::Stones) > P.Capacity(Res::Stones) && P.ClaimWeekly(0, nullptr) == Error::Nothing);
	KD_CHECK(P.Train(Troop("troop.militia")) == Error::None && P.WeeklyQuests()[0].progress == 12); // a claimed quest counts no more
	// One swap a week, only for an unfinished quest.
	KD_CHECK(P.RerollWeekly(1) == Error::Nothing && P.RerollWeekly(0) == Error::Nothing);
	const int Before = P.WeeklyQuests()[4].def;
	KD_CHECK(P.RerollWeekly(4) == Error::None && P.WeeklyQuests()[4].def != Before && P.WeeklyRerollsLeft() == 0 && P.RerollWeekly(4) == Error::LimitReached);
	// The chest after every reward; its relic is two stars at Castle 1.
	KD_CHECK(P.ClaimWeekly(1, nullptr) == Error::None && P.ClaimWeekly(2, nullptr) == Error::None && P.ClaimWeekly(3, nullptr) == Error::None);
	KD_CHECK(P.ClaimWeeklyChest(nullptr) == Error::NotReady);
	const std::string Done = "{\"week\":" + std::to_string((T0 / Day + 3) / 7) + ",\"rerolls\":1,\"chest\":false,\"list\":["
		"{\"id\":\"w_train\",\"target\":12,\"progress\":12,\"claimed\":true},{\"id\":\"w_win\",\"target\":8,\"progress\":8,\"claimed\":true}]}";
	Kingdom Ch(Ct);
	Ch.NewGame(T0, 7);
	KD_CHECK(Ch.Load(WithWeekly(Ch.Save(), Done), Error) && Ch.WeeklyQuests().size() == 2);
	const size_t Relics = Ch.Relics().size();
	const int64_t Gems2 = Ch.Amount(Res::Gems);
	KD_CHECK(Ch.ClaimWeeklyChest(&R) == Error::None && R.res[GemsI] == 30 && Ch.Amount(Res::Gems) == Gems2 + 30 && R.relics.size() == 1);
	KD_CHECK(Ch.Relics().size() == Relics + 1 && Ch.FindRelic(R.relics[0])->rarity == 2 && Ch.ClaimWeeklyChest(nullptr) == Error::Nothing);

	// Saving keeps the week; a new week brings new quests and a closed chest.
	Kingdom L(Ct);
	KD_CHECK(L.Load(Ch.Save(), Error) && L.Hash() == Ch.Hash() && L.Save() == Ch.Save() && L.WeeklyChestClaimed());
	L.Advance(L.WeekEnd() + 60);
	KD_CHECK(L.WeeklyQuests().size() == 5 && !L.WeeklyChestClaimed() && L.WeeklyRerollsLeft() == 1 && L.WeekEnd() == Ch.WeekEnd() + 7 * Day);
	// A save from before weekly quests rolls this week's on the next Advance, the same as a new kingdom's.
	const std::string Full = K2.Save();
	const size_t P0 = Full.find(",\"weekly\":{");
	Kingdom Pre(Ct);
	KD_CHECK(P0 != std::string::npos && Pre.Load(Full.substr(0, P0) + Full.substr(Full.find(",\"calendar\":{")), Error) && Pre.WeeklyQuests().empty());
	Pre.Advance(T0 + 60);
	KD_CHECK(Pre.WeeklyQuests().size() == 5 && Pre.WeeklyQuests()[0].def == Week0[0].def && Pre.Hash() != 0);
}

void TestTitanTrials()
{
	const KingdomContent& Ct = Content();
	const TrialRules& T = Ct.trials;
	const size_t GemsI = static_cast<size_t>(ResIndex(Res::Gems));
	const size_t StonesI = static_cast<size_t>(ResIndex(Res::Stones));
	const int64_t Day = 86400;
	// Master prompt 10.4: Monday to Wednesday, brackets of 100, milestones and ranks. Ours: Castle 4, three battles a day,
	// 30 gems for the milestones and 5 to 60 by place.
	KD_CHECK(T.castle == 4 && T.days == 3 && T.attempts == 3 && T.bracket == 100 && T.list.size() == 6);
	KD_CHECK(T.milestones.size() == 3 && T.ranks.size() == 6 && T.ranks.front().first == 1 && T.ranks.back().first == 100);
	int64_t MilestoneGems = 0;
	for (const auto& M : T.milestones)
	{
		MilestoneGems += M.second.res[GemsI];
	}
	KD_CHECK(MilestoneGems == 30 && T.ranks.front().second.res[GemsI] == 60 && T.ranks.front().second.relic == 3 && T.milestones[2].second.relic == 2);
	KD_CHECK(T.list[0].id == "olympus" && T.list[0].titans.size() == 6 && T.list[3].noTitan && T.list[3].levelOffset == 1 && T.list[4].maxStars == 2);
	KD_CHECK(T.list[5].troops.size() == 8 && T.list[1].enemy.size() == 4 && T.list[1].enemyTitan == Ct.FindTitan("titan.atlas") && T.list[3].enemyTitan == -1);

	// T0 is a Thursday of week 2910 (olympus): a new kingdom is below Castle 4.
	Kingdom K(Ct);
	K.NewGame(T0, 71);
	KD_CHECK(K.TrialCount() == 6 && K.TrialIndex() == 0 && K.CheckTrial().error == Error::Locked && K.CheckTrial().castle == 4);
	KD_CHECK(K.StartTrial({}, 0, {}) == Error::Locked && K.TrialBest() == 0 && K.ClaimTrialRank(nullptr) == Error::Nothing);
	const int64_t Monday = (2911 * 7 - 3) * Day;
	KD_CHECK(K.TrialOpensAt() == Monday - 7 * Day && K.TrialClosesAt() == Monday - 4 * Day);
	// At Castle 4 it is closed until Monday.
	std::string Error;
	Kingdom L(Ct);
	KD_CHECK(L.Load(Replace(K.Save(), "\"def\":\"castle\",\"level\":1", "\"def\":\"castle\",\"level\":4"), Error) && L.CastleLevel() == 4);
	KD_CHECK(L.CheckTrial().error == Error::NotReady && L.CheckTrial().timeSec == Monday - T0 && L.StartTrial({}, 0, {}) == Error::NotReady);
	L.Advance(Monday + Hour);
	// Week 2911: the aesir's trial. Perun (the first titan) may not go; without a titan the battle starts, free of stones.
	const int32_t Perun = L.Titans()[0].id;
	const int Militia = Troop("troop.militia");
	const int Knights = Troop("troop.knights");
	KD_CHECK(L.TrialIndex() == 1 && L.CheckTrial().error == Error::None && L.TrialAttemptsLeft() == 3 && L.TrialLevel() == 4 && L.TrialClosesAt() == Monday + 3 * Day);
	KD_CHECK(!L.TrialAllowsTitan(L.Titans()[0].def) && L.TrialAllowsTitan(Ct.FindTitan("titan.thor")) && L.TrialAllowsTroop(Knights));
	KD_CHECK(L.StartTrial({Militia}, Perun, {}) == Error::NotAllowed && L.TrialAttemptsLeft() == 3 && L.PendingAttack() == 0);
	const int64_t Stones = L.Amount(Res::Stones);
	const int64_t Army = L.ArmyCount(Militia);
	KD_CHECK(Army >= 1 && L.StartTrial({Militia}, 0, {}) == Error::None && L.PendingTrial() && L.PendingSite() == -1 && L.PendingMission() == -1);
	KD_CHECK(L.Amount(Res::Stones) == Stones && L.TrialAttemptsLeft() == 2 && L.PendingSquads().size() == 1);
	KD_CHECK(L.FinishSiteAttack(0, 3, {100}, {}, nullptr) == Error::NoAttack && L.FinishMission(0, 0, 3, {100}, {}, nullptr) == Error::NoAttack);
	// A save in the middle of the battle loads to the same state.
	Kingdom M(Ct);
	KD_CHECK(M.Load(L.Save(), Error) && M.Hash() == L.Hash() && M.Save() == L.Save() && M.PendingTrial());
	// Nothing is lost; the score counts within what the stars allow; a win counts for quests.
	const int64_t Wins = L.Lifetime(QuestEvent::Win);
	Reward R;
	KD_CHECK(L.FinishTrial(2, 3100, &R) == Error::None && !L.PendingTrial() && R.squadsBack == 1 && R.squadsLost == 0 && R.titanXp == 0);
	KD_CHECK(L.ArmyCount(Militia) == Army && L.TrialBest() == 3100 && L.Lifetime(QuestEvent::Win) == Wins + 1 && L.FinishTrial(3, 1, nullptr) == Error::NoAttack);
	KD_CHECK(L.StartTrial({}, 0, {}) == Error::None && L.FinishTrial(1, 9000, nullptr) == Error::None && L.TrialBest() == 3500);
	KD_CHECK(L.StartTrial({}, 0, {}) == Error::None && L.FinishTrial(0, 200, nullptr) == Error::None && L.TrialBest() == 3500);
	KD_CHECK(L.CheckTrial().error == Error::LimitReached && L.CheckTrial().timeSec == Day - Hour && L.StartTrial({}, 0, {}) == Error::LimitReached);
	// Milestones by the best score, paid on top of a full portal.
	KD_CHECK(L.ClaimTrialMilestone(3, nullptr) == Error::BadId && L.ClaimTrialMilestone(2, nullptr) == Error::NotReady);
	const int64_t Gems = L.Amount(Res::Gems);
	L.RefillStones();
	const int64_t Full = L.Amount(Res::Stones);
	KD_CHECK(L.ClaimTrialMilestone(0, &R) == Error::None && R.res[GemsI] == 5 && R.res[StonesI] == 6 && L.Amount(Res::Stones) == Full + 6);
	KD_CHECK(L.ClaimTrialMilestone(1, &R) == Error::None && L.Amount(Res::Gems) == Gems + 15 && L.ClaimTrialMilestone(0, nullptr) == Error::Nothing);
	KD_CHECK(L.TrialMilestoneClaimed(0) && L.TrialMilestoneClaimed(1) && !L.TrialMilestoneClaimed(2));
	// The next day brings three more battles.
	L.Advance(Monday + Day + Hour);
	KD_CHECK(L.TrialAttemptsLeft() == 3 && L.CheckTrial().error == Error::None && L.TrialBest() == 3500);
	KD_CHECK(L.StartTrial({}, 0, {}) == Error::None && L.FinishTrial(3, 4300, nullptr) == Error::None);
	const size_t Relics = L.Relics().size();
	KD_CHECK(L.ClaimTrialMilestone(2, &R) == Error::None && R.res[GemsI] == 15 && R.relics.size() == 1 && L.Relics().size() == Relics + 1);
	KD_CHECK(L.FindRelic(R.relics[0])->rarity == 2);
	// The computer lords post their best during the open days: about a tenth never fight, none above 4800.
	KD_CHECK(L.ClaimTrialRank(nullptr) == Error::NotReady && L.TrialLordScore(99) == 0 && L.TrialLordScore(-1) == 0);
	L.Advance(Monday + 3 * Day + Hour);
	int Zero = 0;
	int Above = 0;
	for (int I = 0; I < 99; ++I)
	{
		const int64_t S = L.TrialLordScore(I);
		Zero += S == 0 ? 1 : 0;
		Above += S > L.TrialBest() ? 1 : 0;
		KD_CHECK(S == 0 || (S >= 1200 && S <= 4800));
	}
	KD_CHECK(Zero >= 3 && Zero <= 20 && L.TrialRank() == Above + 1 && L.CheckTrial().error == Error::NotReady);
	KD_CHECK(L.CheckTrial().timeSec == 4 * Day - Hour);
	// The same lords on every client.
	Kingdom L2(Ct);
	KD_CHECK(L2.Load(L.Save(), Error) && L2.Hash() == L.Hash() && L2.TrialRank() == L.TrialRank() && L2.TrialLordScore(7) == L.TrialLordScore(7));
	// The rank reward once the trial is closed.
	const int Pick = L.TrialRankReward(L.TrialRank());
	KD_CHECK(Pick >= 0 && L.TrialRankReward(1) == 0 && L.TrialRankReward(2) == 1 && L.TrialRankReward(100) == 5 && L.TrialRankReward(101) == -1);
	const int64_t Gems2 = L.Amount(Res::Gems);
	KD_CHECK(L.ClaimTrialRank(&R) == Error::None && L.Amount(Res::Gems) == Gems2 + T.ranks[static_cast<size_t>(Pick)].second.res[GemsI]);
	KD_CHECK(L.TrialRankClaimed() && L.ClaimTrialRank(nullptr) == Error::Nothing);
	// Saving keeps the week; the next Monday brings the next trial with nothing claimed.
	Kingdom S(Ct);
	KD_CHECK(S.Load(L.Save(), Error) && S.Hash() == L.Hash() && S.Save() == L.Save() && S.TrialRankClaimed() && S.TrialBest() == 4300);
	S.Advance(Monday + 7 * Day + Hour);
	KD_CHECK(S.TrialIndex() == 2 && S.TrialBest() == 0 && !S.TrialRankClaimed() && !S.TrialMilestoneClaimed(0) && S.TrialAttemptsLeft() == 3);
	// Week 2912: Perun and Veles. Week 2913: no titan at all, one level up. 2914: up to two stars. 2915: no cavalry.
	KD_CHECK(S.TrialAllowsTitan(Ct.FindTitan("titan.perun")) && S.TrialAllowsTitan(Ct.FindTitan("titan.veles")) && !S.TrialAllowsTitan(Ct.FindTitan("titan.talos")));
	S.Advance(Monday + 14 * Day + Hour);
	KD_CHECK(S.TrialIndex() == 3 && !S.TrialAllowsTitan(Ct.FindTitan("titan.perun")) && S.TrialLevel() == 5 && S.StartTrial({}, Perun, {}) == Error::NotAllowed);
	S.Advance(Monday + 21 * Day + Hour);
	KD_CHECK(S.TrialIndex() == 4 && S.TrialAllowsTitan(Ct.FindTitan("titan.perun")) && !S.TrialAllowsTitan(Ct.FindTitan("titan.thor")));
	S.Advance(Monday + 28 * Day + Hour);
	KD_CHECK(S.TrialIndex() == 5 && S.TrialAllowsTroop(Militia) && !S.TrialAllowsTroop(Knights) && !S.TrialAllowsTroop(Troop("troop.ballista")));
	KD_CHECK(S.TrialAllowsTitan(Ct.FindTitan("titan.thor")) && S.StartTrial({Knights}, 0, {}) == Error::NotAllowed);
	// Without a battle there is no rank reward.
	S.Advance(Monday + 31 * Day + Hour);
	KD_CHECK(S.TrialBest() == 0 && S.ClaimTrialRank(nullptr) == Error::Nothing);
	// A save from before trials starts this week's on the next Advance.
	const std::string Now = S.Save();
	const size_t P0 = Now.find(",\"trials\":{");
	Kingdom Pre(Ct);
	KD_CHECK(P0 != std::string::npos && Pre.Load(Now.substr(0, P0) + Now.substr(Now.find(",\"calendar\":{")), Error) && Pre.TrialBest() == 0);
	Pre.Advance(S.Now() + 60);
	KD_CHECK(Pre.TrialIndex() == S.TrialIndex() && Pre.Hash() != 0);
}

std::string WithWarpath(const std::string& Save, const std::string& Wp)
{
	const size_t P = Save.find("\"warpath\":{");
	const size_t Q = Save.find(",\"assault\":{");
	return P == std::string::npos || Q == std::string::npos ? Save : Save.substr(0, P) + "\"warpath\":" + Wp + Save.substr(Q);
}

void TestWarpath()
{
	const KingdomContent& Ct = Content();
	const WarpathRules& W = Ct.warpath;
	const size_t GoldI = static_cast<size_t>(ResIndex(Res::Gold));
	const size_t GemsI = static_cast<size_t>(ResIndex(Res::Gems));
	const size_t StonesI = static_cast<size_t>(ResIndex(Res::Stones));
	const size_t ShardsI = static_cast<size_t>(ResIndex(Res::Shards));
	const int64_t Day = 86400;
	// Master prompt 13.4: four-week passes of 50 tiers of 1 000 XP, 150 gems a tier, a bonus chest per 2 000 XP past the
	// last tier. Ours pays 200 gems on the free track, 120 of them by tier 30, and a relic every ten tiers.
	KD_CHECK(W.weeks == 4 && W.tierXp == 1000 && W.tiers.size() == 50 && W.skipGems == 150 && W.bonusXp == 2000);
	KD_CHECK(W.dailyXp == 100 && W.weeklyXp == 500 && W.battleXp == 10 && W.battleCap == 300 && W.trialXp == 300);
	int64_t TierGems = 0;
	int64_t Gems30 = 0;
	int RelicTiers = 0;
	for (size_t I = 0; I < W.tiers.size(); ++I)
	{
		TierGems += W.tiers[I].res[GemsI];
		Gems30 += I < 30 ? W.tiers[I].res[GemsI] : 0;
		RelicTiers += W.tiers[I].relic > 0 ? 1 : 0;
	}
	KD_CHECK(TierGems == 200 && Gems30 == 120 && RelicTiers == 5 && W.tiers[0].res[GoldI] == 1000 && W.tiers[49].relic == 4);
	KD_CHECK(W.bonus.res[GemsI] == 5 && W.bonus.res[StonesI] == 6 && W.bonus.relic == 0);

	// T0, a Thursday of week 2910, is in pass 727 (weeks 2908 to 2911), which ends on the Monday of week 2912.
	Kingdom K(Ct);
	K.NewGame(T0, 73);
	const int64_t End = (2912 * 7 - 3) * Day;
	KD_CHECK(K.WarpathSeason() == 727 && K.WarpathEnd() == End && K.WarpathXp() == 0 && K.WarpathTier() == 0 && K.WarpathBonusReady() == 0);
	KD_CHECK(K.ClaimWarpath(0, nullptr) == Error::NotReady && K.ClaimWarpath(50, nullptr) == Error::BadId && K.ClaimWarpath(-1, nullptr) == Error::BadId);
	KD_CHECK(K.ClaimWarpathBonus(nullptr) == Error::NotReady && K.WarpathPaidSeason() == -1 && !K.WarpathClaimed(0) && !K.WarpathClaimed(64));
	// A battle gives 10 XP, a lost one too.
	KD_CHECK(K.StartAttack(K.World()[0].id, false) == Error::None && K.FinishAttack(K.World()[0].id, false, 3) == Error::None && K.WarpathXp() == 10);
	KD_CHECK(K.StartAttack(K.World()[0].id, false) == Error::None && K.FinishAttack(K.World()[0].id, false, 0) == Error::None);
	KD_CHECK(K.WarpathXp() == 20 && K.WarpathBattleXpToday() == 20);
	// Up to 300 a day from battles; the next day counts again.
	std::string Error;
	Kingdom L(Ct);
	KD_CHECK(L.Load(Replace(K.Save(), "\"dayXp\":20", "\"dayXp\":295"), Error) && L.WarpathBattleXpToday() == 295 && L.WarpathXp() == 20);
	KD_CHECK(L.StartAttack(L.World()[0].id, false) == Error::None && L.FinishAttack(L.World()[0].id, false, 0) == Error::None && L.WarpathXp() == 25);
	L.RefillStones();
	KD_CHECK(L.StartAttack(L.World()[0].id, false) == Error::None && L.FinishAttack(L.World()[0].id, false, 0) == Error::None && L.WarpathXp() == 25);
	KD_CHECK(L.WarpathBattleXpToday() == 300);
	L.Advance(T0 + Day);
	L.RefillStones();
	KD_CHECK(L.WarpathBattleXpToday() == 0 && L.StartAttack(L.World()[0].id, false) == Error::None && L.FinishAttack(L.World()[0].id, false, 0) == Error::None);
	KD_CHECK(L.WarpathXp() == 35 && L.WarpathBattleXpToday() == 10);
	// A daily quest claimed gives 100 XP, a weekly one 500. A save from before the Warpath starts the pass on the next
	// Advance.
	const std::string Daily = "{\"day\":" + std::to_string(T0 / Day) + ",\"rerolls\":0,\"chest\":false,\"counter\":0,\"list\":["
		"{\"id\":\"q_train\",\"target\":3,\"progress\":3,\"claimed\":false},"
		"{\"id\":\"q_gold\",\"target\":500,\"progress\":500,\"claimed\":false}]}";
	Kingdom Q(Ct);
	KD_CHECK(Q.Load(WithQuests(K.Save(), Daily), Error) && Q.WarpathSeason() == -1 && Q.ClaimWarpath(0, nullptr) == Error::BadId);
	Q.Advance(T0 + 60);
	KD_CHECK(Q.WarpathSeason() == 727 && Q.WarpathXp() == 0 && Q.ClaimQuest(0, nullptr) == Error::None && Q.WarpathXp() == 100);
	KD_CHECK(Q.ClaimQuest(1, nullptr) == Error::None && Q.WarpathXp() == 200 && Q.ClaimQuest(1, nullptr) == Error::Nothing && Q.WarpathXp() == 200);
	const std::string Weekly = "{\"week\":" + std::to_string((T0 / Day + 3) / 7) + ",\"rerolls\":0,\"chest\":false,\"list\":["
		"{\"id\":\"w_gold\",\"target\":2000,\"progress\":2000,\"claimed\":false}]}";
	KD_CHECK(Q.Load(WithWeekly(K.Save(), Weekly), Error) && Q.WarpathSeason() == -1);
	Q.Advance(T0 + 60);
	KD_CHECK(Q.WarpathSeason() == 727 && Q.ClaimWeekly(0, nullptr) == Error::None && Q.WarpathXp() == 500);

	// At Castle 4 on Monday: a trial battle gives battle XP, a trial milestone 300.
	Kingdom T(Ct);
	KD_CHECK(T.Load(Replace(K.Save(), "\"def\":\"castle\",\"level\":1", "\"def\":\"castle\",\"level\":4"), Error) && T.CastleLevel() == 4);
	const int64_t Monday = (2911 * 7 - 3) * Day;
	T.Advance(Monday + Hour);
	KD_CHECK(T.WarpathSeason() == 727 && T.WarpathXp() == 20 && T.WarpathBattleXpToday() == 0);
	KD_CHECK(T.StartTrial({}, 0, {}) == Error::None && T.FinishTrial(2, 3100, nullptr) == Error::None && T.WarpathXp() == 30);
	KD_CHECK(T.ClaimTrialMilestone(0, nullptr) == Error::None && T.WarpathXp() == 330 && T.WarpathTier() == 0);
	// Gems buy XP up to the next tier; a tier pays once, gold and food by the Castle's level.
	const int64_t Gems = T.Amount(Res::Gems);
	KD_CHECK(T.BuyWarpathTier() == Error::None && T.WarpathXp() == 1000 && T.WarpathTier() == 1 && T.Amount(Res::Gems) == Gems - 150);
	Reward R;
	KD_CHECK(T.ClaimWarpath(1, nullptr) == Error::NotReady && T.ClaimWarpath(0, &R) == Error::None && R.res[GoldI] == 4000 && R.relics.empty());
	KD_CHECK(T.WarpathClaimed(0) && T.ClaimWarpath(0, nullptr) == Error::Nothing);
	KD_CHECK(T.BuyWarpathTier() == Error::None && T.WarpathTier() == 2 && T.ClaimWarpath(1, &R) == Error::None && R.res[GemsI] == 5 && R.res[StonesI] == 6);
	KD_CHECK(T.Amount(Res::Gems) == Gems - 300 + 5);
	Kingdom U(Ct);
	KD_CHECK(U.Load(T.Save(), Error) && U.Hash() == T.Hash() && U.Save() == T.Save() && U.WarpathClaimed(1) && !U.WarpathClaimed(2));
	KD_CHECK(U.Load(Replace(T.Save(), "\"gems\":" + std::to_string(T.Amount(Res::Gems)), "\"gems\":149"), Error) && U.Amount(Res::Gems) == 149);
	KD_CHECK(U.BuyWarpathTier() == Error::NotEnough && U.WarpathTier() == 2 && U.Amount(Res::Gems) == 149);

	// Past the last tier, bonus chests; tier 50 pays a 4-star relic.
	Kingdom B(Ct);
	KD_CHECK(B.Load(WithWarpath(T.Save(), "{\"no\":727,\"xp\":54100,\"mask\":1,\"bonus\":0,\"day\":-1,\"dayXp\":0}"), Error));
	KD_CHECK(B.WarpathTier() == 50 && B.WarpathBonusReady() == 2 && B.BuyWarpathTier() == Error::LimitReached && B.WarpathClaimed(0) && !B.WarpathClaimed(1));
	KD_CHECK(B.ClaimWarpathBonus(&R) == Error::None && R.res[GemsI] == 5 && R.res[StonesI] == 6 && B.WarpathBonusReady() == 1);
	KD_CHECK(B.ClaimWarpath(49, &R) == Error::None && R.relics.size() == 1 && B.FindRelic(R.relics[0])->rarity == 4 && R.res[GemsI] == 20);
	// The end of the pass pays what was reached and not claimed: 180 gems of tiers, a chest and four relics.
	const int64_t Gems2 = B.Amount(Res::Gems);
	const size_t Relics = B.Relics().size();
	B.Advance(End + Hour);
	KD_CHECK(B.WarpathSeason() == 728 && B.WarpathXp() == 0 && B.WarpathTier() == 0 && !B.WarpathClaimed(0) && B.WarpathBonusReady() == 0);
	KD_CHECK(B.WarpathPaidSeason() == 727 && B.WarpathPaid()[GemsI] == 185 && B.WarpathPaid()[ShardsI] == 10 && B.WarpathPaidRelics() == 4);
	KD_CHECK(B.Amount(Res::Gems) >= Gems2 + 185 && B.Relics().size() == Relics + 4 && B.WarpathEnd() == End + 28 * Day);
	B.Advance(End + 2 * Hour);
	KD_CHECK(B.WarpathPaidSeason() == 727 && B.WarpathPaid()[GemsI] == 185 && B.WarpathSeason() == 728);
	Kingdom B2(Ct);
	KD_CHECK(B2.Load(B.Save(), Error) && B2.Hash() == B.Hash() && B2.Save() == B.Save() && B2.WarpathPaid()[GemsI] == 185 && B2.WarpathPaidRelics() == 4);
	// A full inventory: tier relics wait for room (InventoryFull); at the end only the relic is lost.
	std::string Many = RelicJson(1000, "atk", 1, 20, "[1]");
	for (int I = 1; I < Ct.maxRelics; ++I)
	{
		Many += "," + RelicJson(1000 + I, "atk", 1, 20, "[1]");
	}
	Kingdom N(Ct);
	N.NewGame(T0, 74);
	const std::string Full = Replace(WithWarpath(N.Save(), "{\"no\":727,\"xp\":10000,\"mask\":0,\"bonus\":0,\"day\":-1,\"dayXp\":0}"), "\"relics\":[],\"relicAt\"", "\"relics\":[" + Many + "],\"relicAt\"");
	Kingdom F(Ct);
	KD_CHECK(F.Load(Full, Error) && static_cast<int>(F.Relics().size()) == Ct.maxRelics && F.WarpathTier() == 10);
	KD_CHECK(F.ClaimWarpath(9, nullptr) == Error::InventoryFull && !F.WarpathClaimed(9) && F.ClaimWarpath(8, &R) == Error::None);
	F.Advance(End + Hour);
	KD_CHECK(F.WarpathSeason() == 728 && F.WarpathPaid()[GemsI] == 40 && F.WarpathPaid()[StonesI] == 21 && F.WarpathPaidRelics() == 0);
	KD_CHECK(F.WarpathPaid()[GoldI] == 2000 && static_cast<int>(F.Relics().size()) == Ct.maxRelics);
}

void TestCalendars()
{
	const KingdomContent& Ct = Content();
	const CalendarRules& Cal = Ct.calendar;
	const size_t GoldI = static_cast<size_t>(ResIndex(Res::Gold));
	const size_t GemsI = static_cast<size_t>(ResIndex(Res::Gems));
	const int64_t Day = 86400;
	// Master prompt 13.6: seven days for a newcomer (day 3: one of three Epic titans, day 7: a Rare one), 28-day cycles
	// with up to three catch-ups.
	KD_CHECK(Cal.newcomer.size() == 7 && Cal.monthly.size() == 28 && Cal.catchUps == 3 && Cal.catchUpGems > 0);
	KD_CHECK(Cal.newcomer.size() == 7 && Cal.newcomer[2].choice.size() == 3 && Cal.newcomer[6].titan >= 0);
	for (const int T : Cal.newcomer.size() == 7 ? Cal.newcomer[2].choice : std::vector<int>())
	{
		KD_CHECK(Ct.titans[static_cast<size_t>(T)].stars == 3);
	}
	KD_CHECK(Cal.newcomer.size() == 7 && Ct.titans[static_cast<size_t>(Cal.newcomer[6].titan)].stars == 2);
	int64_t MonthGems = 0;
	for (const CalendarDay& D : Cal.monthly)
	{
		MonthGems += D.res[GemsI];
	}
	KD_CHECK(MonthGems >= 60 && MonthGems <= 140); // a few gems a day of the free player's 40 (8.6)

	Kingdom K(Ct);
	K.NewGame(T0, 17);
	const uint64_t Fresh = K.Hash();
	KD_CHECK(K.LoginDays() == 0 && !K.NewcomerReady(0) && K.ClaimNewcomer(0, 0, nullptr) == Error::NotReady);
	KD_CHECK(K.MonthStartDay() == -1 && K.ClaimMonthly(nullptr) == Error::NotReady && K.MonthMissed() == 0 && K.CatchUpMonthly() == Error::Nothing);
	// The first visit of a day is a login; a second one the same day is not.
	K.Visit();
	K.Visit();
	KD_CHECK(K.LoginDays() == 1 && K.MonthStartDay() == T0 / Day && K.MonthLogins() == 1 && K.Hash() != Fresh);
	KD_CHECK(K.NewcomerReady(0) && !K.NewcomerReady(1) && K.ClaimNewcomer(1, 0, nullptr) == Error::NotReady && K.ClaimNewcomer(7, 0, nullptr) == Error::BadId);
	const int64_t Gems = K.Amount(Res::Gems);
	const int64_t Gold = K.Amount(Res::Gold);
	Reward R;
	KD_CHECK(K.ClaimNewcomer(0, 0, &R) == Error::None && R.res[GemsI] == Cal.newcomer[0].res[GemsI] && K.Amount(Res::Gems) == Gems + R.res[GemsI]);
	// A gift goes on top of full vaults; the vault then collects nothing until gold is spent.
	KD_CHECK(Gold + Cal.newcomer[0].res[GoldI] > K.Capacity(Res::Gold) && R.res[GoldI] == Cal.newcomer[0].res[GoldI] && K.Amount(Res::Gold) == Gold + R.res[GoldI]);
	KD_CHECK(K.NewcomerClaimed(0) && !K.NewcomerReady(0) && K.ClaimNewcomer(0, 0, nullptr) == Error::Nothing);
	KD_CHECK(K.ClaimMonthly(&R) == Error::None && K.MonthClaimed() == 1 && K.ClaimMonthly(nullptr) == Error::NotReady);

	// The next day counts; a skipped day is missed and can be caught up for gems.
	K.Advance(T0 + Day);
	KD_CHECK(K.LoginDays() == 1); // time alone is not a login
	K.Visit();
	K.Advance(T0 + 3 * Day);
	K.Visit();
	KD_CHECK(K.LoginDays() == 3 && K.MonthLogins() == 3 && K.MonthMissed() == 1);
	const int64_t GemsBefore = K.Amount(Res::Gems);
	KD_CHECK(K.CatchUpMonthly() == Error::None && K.Amount(Res::Gems) == GemsBefore - Cal.catchUpGems && K.MonthLogins() == 4 && K.MonthMissed() == 0);
	KD_CHECK(K.CatchUpMonthly() == Error::Nothing && K.MonthCatchUps() == 1);
	// Day 3: one of three titans.
	const size_t Titans = K.Titans().size();
	KD_CHECK(K.ClaimNewcomer(2, 3, nullptr) == Error::BadId && K.ClaimNewcomer(2, -1, nullptr) == Error::BadId);
	KD_CHECK(K.ClaimNewcomer(2, 1, &R) == Error::None && K.Titans().size() == Titans + 1 && R.newTitan == K.Titans().back().id);
	KD_CHECK(K.Titans().back().def == Cal.newcomer[2].choice[1] && K.Titans().back().level == 1 && !K.Titans().back().skills.empty());
	for (int I = 0; I < 3; ++I)
	{
		KD_CHECK(K.ClaimMonthly(nullptr) == Error::None);
	}
	KD_CHECK(K.MonthClaimed() == 4 && K.ClaimMonthly(nullptr) == Error::NotReady);

	// Saving keeps it all; a save from before the calendars starts at the next visit with the same hash.
	std::string Error;
	const std::string Save = K.Save();
	KD_CHECK(Save.find("\"calendar\":{\"logins\":3,\"day\":" + std::to_string((T0 + 3 * Day) / Day) + ",\"newcomer\":5,") != std::string::npos);
	Kingdom L(Ct);
	KD_CHECK(L.Load(Save, Error) && L.Hash() == K.Hash() && L.Save() == Save && L.MonthCatchUps() == 1);
	KD_CHECK(L.Load(Replace(Save, "\"catchUps\":1}", "\"catchUps\":3}"), Error) && L.MonthCatchUps() == 3);
	L.Advance(T0 + 5 * Day);
	L.Visit();
	KD_CHECK(L.MonthMissed() == 1 && L.CatchUpMonthly() == Error::LimitReached);
	Kingdom N(Ct);
	N.NewGame(T0, 17);
	const std::string NewSave = N.Save();
	const size_t P = NewSave.find(",\"calendar\":{");
	Kingdom O(Ct);
	KD_CHECK(P != std::string::npos && O.Load(NewSave.substr(0, P) + NewSave.substr(NewSave.find(",\"achievements\":{")), Error) && O.Hash() == Fresh && O.LoginDays() == 0);

	// A new cycle drops what was not claimed.
	K.Advance(T0 + 28 * Day);
	KD_CHECK(K.ClaimMonthly(nullptr) == Error::NotReady && K.MonthMissed() == 0);
	K.Visit();
	KD_CHECK(K.MonthStartDay() == T0 / Day + 28 && K.MonthLogins() == 1 && K.MonthClaimed() == 0 && K.MonthCatchUps() == 0);
	KD_CHECK(K.ClaimMonthly(nullptr) == Error::None && K.LoginDays() == 4);
	// Day 7 comes on the seventh login, however many days that takes.
	for (int D = 29; D < 32; ++D)
	{
		K.Advance(T0 + D * Day);
		K.Visit();
	}
	KD_CHECK(K.LoginDays() == 7 && K.NewcomerReady(6) && K.NewcomerReady(1) && K.ClaimNewcomer(6, 0, &R) == Error::None);
	KD_CHECK(K.Titans().back().def == Cal.newcomer[6].titan && R.res[GemsI] == Cal.newcomer[6].res[GemsI]);
}

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

/** The save with its "assault" object replaced. */
std::string WithAssault(const std::string& Save, const std::string& As)
{
	const size_t P = Save.find("\"assault\":{");
	const size_t Q = Save.find(",\"calendar\":{");
	return P == std::string::npos || Q == std::string::npos ? Save : Save.substr(0, P) + "\"assault\":" + As + Save.substr(Q);
}

/** Kingdom Power (master prompt 7.7): buildings by level^1.5 (the Castle five times), the five strongest titans' battle
 *  Power and every research level. */
void TestKingdomPower()
{
	const KingdomContent& Ct = Content();
	KD_CHECK(Ct.power.building == 10 && Ct.power.castleMul == 5 && Ct.power.titans == 5 && Ct.power.research == 25);
	Kingdom K(Ct);
	K.NewGame(T0, 7);
	// Without the battle content: the Castle at level 1 is 10 × 5, the other start buildings 10 each, no research.
	KingdomPowerInfo P = K.KingdomPower();
	const int64_t StartBuildings = 50 + 10 * static_cast<int64_t>(Ct.startBuildings.size() - 1);
	KD_CHECK(P.buildings == StartBuildings && P.titans == 0 && P.top.empty() && P.research == 0 && P.total == StartBuildings);
	KD_CHECK(K.TitanPowerOf(K.Titans()[0].id) == 0);
	const uint64_t Hash = K.Hash();
	K.SetBattleContent(&Battle());
	KD_CHECK(K.Hash() == Hash); // derived, not saved
	// The start titans with their battle Power (the simulation's / 100), strongest first.
	const auto Own = [&](const Kingdom& X, int32_t Id) {
		const kingdom::TitanState* T = X.FindTitan(Id);
		const TitanType& Type = Ct.titans[static_cast<size_t>(T->def)];
		TitanSetup S;
		S.present = true;
		S.titanId = Type.id;
		S.stars = Type.stars;
		S.level = T->level;
		S.ascended = T->ascended;
		S.atkPct = static_cast<int>(X.TitanBonus(Id, RelicStat::Atk) / 10); // relics and skills, as in battle
		S.hpPct = static_cast<int>(X.TitanBonus(Id, RelicStat::Hp) / 10);
		S.armorPct = static_cast<int>(X.TitanBonus(Id, RelicStat::Armor) / 10);
		S.critPct = static_cast<int>(X.TitanBonus(Id, RelicStat::Crit) / 10);
		S.piercePct = static_cast<int>(X.TitanBonus(Id, RelicStat::Pierce) / 10);
		return TitanPower(Battle(), S) / 100;
	};
	P = K.KingdomPower();
	KD_CHECK(P.top.size() == K.Titans().size() && P.top.size() == Ct.startTitans.size() && P.top.size() >= 2);
	int64_t Sum = 0;
	for (size_t I = 0; I < P.top.size(); ++I)
	{
		KD_CHECK(P.top[I].second == Own(K, P.top[I].first) && P.top[I].second == K.TitanPowerOf(P.top[I].first) && P.top[I].second > 1000);
		KD_CHECK(I == 0 || P.top[I - 1].second >= P.top[I].second);
		Sum += P.top[I].second;
	}
	KD_CHECK(P.titans == Sum && P.total == StartBuildings + Sum && P.research == 0);
	KD_CHECK(K.TitanPowerOf(-5) == 0);

	// Levels count as L^1.5 rounded down (2 gives 28, 4 gives 80, 9 gives 270), the Castle five times.
	std::string Error;
	std::string S = Replace(K.Save(), "\"def\":\"castle\",\"level\":1", "\"def\":\"castle\",\"level\":4");
	S = WithBuilding(WithBuilding(S, "gold_mine", 2, 30), "farm", 9, 33);
	Kingdom B(Ct);
	KD_CHECK(B.Load(S, Error));
	B.SetBattleContent(&Battle());
	KD_CHECK(B.KingdomPower().buildings == StartBuildings - 50 + 400 + 28 + 270 && B.KingdomPower().titans == P.titans);

	// Research: 25 for every level of a troop (Armoury) and a spell (Arcane Tower) above the first.
	S = Replace(S, "\"levels\":{\"troop.militia\":1", "\"levels\":{\"troop.militia\":3");
	S = Replace(S, "\"levels\":{\"" + Ct.spells[0].id + "\":1", "\"levels\":{\"" + Ct.spells[0].id + "\":4");
	Kingdom R(Ct);
	KD_CHECK(R.Load(S, Error) && R.TroopLevel(0) == 3 && R.SpellLevel(0) == 4);
	R.SetBattleContent(&Battle());
	KD_CHECK(R.KingdomPower().research == 125 && R.KingdomPower().total == B.KingdomPower().total + 125);

	// Only the five strongest titans count; equal ones by id. A higher level counts more.
	S = Replace(WithTitans(K.Save(), {{701, "titan.talos"}, {702, "titan.talos"}, {703, "titan.thor"}, {704, "titan.artemis"}, {705, "titan.cyclops"}, {706, "titan.surtr"}},
					",\"skills\":[[\"vigor\",1]]"),
		"\"id\":703,\"def\":\"titan.thor\",\"level\":1", "\"id\":703,\"def\":\"titan.thor\",\"level\":30");
	Kingdom M(Ct);
	KD_CHECK(M.Load(S, Error) && M.Titans().size() == 8);
	M.SetBattleContent(&Battle());
	const KingdomPowerInfo Many = M.KingdomPower();
	KD_CHECK(Many.top.size() == 5 && Many.top[0].first == 703 && Many.top[0].second == Own(M, 703));
	Sum = 0;
	for (size_t I = 0; I < Many.top.size(); ++I)
	{
		KD_CHECK(I == 0 || Many.top[I - 1].second >= Many.top[I].second);
		Sum += Many.top[I].second;
	}
	KD_CHECK(Many.titans == Sum);
	for (const kingdom::TitanState& T : M.Titans())
	{
		const bool bIn = std::any_of(Many.top.begin(), Many.top.end(), [&](const std::pair<int32_t, int64_t>& X) { return X.first == T.id; });
		KD_CHECK(bIn || M.TitanPowerOf(T.id) <= Many.top.back().second);
	}
	// The two level-1 Talos are equal: if only one of them counts, it is the one with the lower id.
	const bool b701 = std::any_of(Many.top.begin(), Many.top.end(), [](const std::pair<int32_t, int64_t>& X) { return X.first == 701; });
	const bool b702 = std::any_of(Many.top.begin(), Many.top.end(), [](const std::pair<int32_t, int64_t>& X) { return X.first == 702; });
	KD_CHECK(M.TitanPowerOf(701) == M.TitanPowerOf(702) && (b701 || !b702));
	// Skills and relics add to a titan's Power as they do in battle.
	KD_CHECK(M.TitanBonus(701, RelicStat::Hp) > 0 && Own(M, 701) == M.TitanPowerOf(701));

	// The achievement and the goal check read it (with the battle content when there is one).
	const int Ach = AchIx("ach_power");
	KD_CHECK(Ach >= 0 && Ct.achievements[static_cast<size_t>(Ach)].check == GoalCheck::Power);
	KD_CHECK(M.AchievementValue(Ach) == Many.total && K.AchievementValue(Ach) == P.total);
	Kingdom NoBattle(Ct);
	KD_CHECK(NoBattle.Load(S, Error) && NoBattle.AchievementValue(Ach) == Many.buildings + Many.research);
}

/** Scouting before a battle (master prompt 9.3): the odds of the raids for any battle. */
void TestScouting()
{
	const KingdomContent& Ct = Content();
	Kingdom K(Ct);
	K.NewGame(T0, 3);
	const RaidRules& R = Ct.raids;
	// Titans weigh titanPct, the defender's AI profile scales its side, a player (no profile) and unknown ones are 100%.
	KD_CHECK(R.titanPct > 100 && K.OddsPower(1000, 0, "") == 1000 && K.OddsPower(0, 1000, "") == 10 * R.titanPct);
	KD_CHECK(K.OddsPower(1000, 0, "no such ai") == 1000);
	for (const auto& A : R.aiPct)
	{
		KD_CHECK(K.OddsPower(1000, 500, A.first) == (1000 + 5 * R.titanPct) * A.second / 100);
	}
	// Equal armies: a little under even (c is a bit above 1); a fifth weaker: about one in four; never certain.
	const int64_t Even = K.BattleWinPermille(50000, 400000, 50000, 400000, "");
	KD_CHECK(Even > 400 && Even < 520);
	const int64_t Weaker = K.BattleWinPermille(40000, 320000, 50000, 400000, "");
	KD_CHECK(Weaker > 150 && Weaker < 330);
	const int64_t Stronger = K.BattleWinPermille(60000, 480000, 50000, 400000, "");
	KD_CHECK(Stronger > 650 && Stronger < 900);
	KD_CHECK(K.BattleWinPermille(1, 0, 1000000, 1000000, "") >= 0 && K.BattleWinPermille(1000000, 1000000, 1, 0, "") <= 1000);
	KD_CHECK(K.BattleWinPermille(0, 0, 100, 0, "") == 0 && K.BattleWinPermille(100, 0, 0, 0, "") == 1000);
	// More squads, a stronger titan or a weaker enemy AI raise the chance.
	KD_CHECK(K.BattleWinPermille(60000, 400000, 50000, 400000, "") > Even && K.BattleWinPermille(50000, 450000, 50000, 400000, "") > Even);
	KD_CHECK(K.BattleWinPermille(50000, 400000, 50000, 400000, "recruit") > Even && K.BattleWinPermille(50000, 400000, 50000, 400000, "nightmare") < Even);
	// A titan's Power weighs more than the same Power in squads.
	KD_CHECK(K.BattleWinPermille(50000, 100000, 50000, 50000, "") > K.BattleWinPermille(100000, 50000, 50000, 50000, ""));
}

/** The save with these squads in the camp (troop id, squads); the other troops have none. */
std::string WithUnits(const std::string& Save, const std::vector<std::pair<const char*, int>>& Units)
{
	const size_t A = Save.find("\"units\":{");
	const size_t B = A == std::string::npos ? A : Save.find('}', A);
	if (B == std::string::npos)
	{
		std::printf("  (no army units in the save)\n");
		return Save;
	}
	std::string Obj = "\"units\":{";
	for (size_t I = 0; I < Units.size(); ++I)
	{
		Obj += (I ? ",\"" : "\"") + std::string(Units[I].first) + "\":" + std::to_string(Units[I].second);
	}
	return Save.substr(0, A) + Obj + Save.substr(B);
}

void TestMarshal()
{
	const KingdomContent& Ct = Content();
	const size_t N = Ct.troops.size();
	const int Mil = Troop("troop.militia");
	const int Arc = Troop("troop.archers");
	const int Spr = Troop("troop.spearmen");
	const int Bal = Troop("troop.ballista");
	const int Cav = static_cast<int>(DefenderClass::Cavalry);
	const int Tit = static_cast<int>(DefenderClass::Titan);
	const auto Squads = [](const char* Troop, int Count) {
		SideSetup S;
		for (int I = 0; I < Count; ++I)
		{
			SquadSetup Q;
			Q.troopId = Troop;
			Q.tier = 2;
			S.squads.push_back(Q);
		}
		return S;
	};
	const auto Per = [N](std::vector<std::pair<int, int>> List) {
		std::vector<int> V(N, 0);
		for (const auto& X : List)
		{
			V[static_cast<size_t>(X.first)] = X.second;
		}
		return V;
	};
	const auto Sum = [](const std::vector<int>& V) { int S = 0; for (const int X : V) S += X; return S; };
	Kingdom K(Ct);
	K.NewGame(T0, 9);
	std::string Save = Replace(K.Save(), "\"def\":\"castle\",\"level\":1", "\"def\":\"castle\",\"level\":4");
	Save = WithBuilding(Save, "army_camp", 3, 30);
	Save = WithUnits(Save, {{"troop.militia", 3}, {"troop.archers", 3}, {"troop.spearmen", 3}, {"troop.swordsmen", 2}});
	std::string Error;
	KD_CHECK(K.Load(Save, Error));
	const SideSetup Riders = Squads("troop.outriders", 4);
	const std::vector<int> Default = Per({{Mil, 2}, {Arc, 2}});
	// Without the battle content the Marshal is silent.
	MarshalAdvice A = K.Marshal(Riders, Default, {});
	KD_CHECK(A.tips.empty() && A.plan.size() == N && Sum(A.plan) == 0 && A.planScore == 0 && A.takenScore == 0);
	K.SetBattleContent(&Battle());
	const int Slots = K.BattleSlots();
	KD_CHECK(Slots >= 4 && K.ArmyCount(Spr) == 3 && K.CastleLevel() == 4);
	// Four squads of outriders against militia and archers: bring the spearmen (2.0 against cavalry, counters.json), and
	// the archers are their easy prey (1.75). The plan takes every spearman, stays in the camp and the slots and beats the
	// squads taken on the counter-weighted Power.
	A = K.Marshal(Riders, Default, {});
	KD_CHECK(A.tips.size() == 2);
	KD_CHECK(A.tips[0].kind == MarshalTipKind::Bring && A.tips[0].troop == Spr && A.tips[0].enemyClass == Cav && A.tips[0].pct == 200);
	KD_CHECK(A.tips[1].kind == MarshalTipKind::Exposed && A.tips[1].troop == Arc && A.tips[1].enemyClass == Cav && A.tips[1].pct == 175);
	KD_CHECK(Sum(A.plan) == Slots && A.plan[static_cast<size_t>(Spr)] == 3 && A.plan[static_cast<size_t>(Arc)] == 0);
	for (size_t T = 0; T < N; ++T)
	{
		KD_CHECK(A.plan[T] >= 0 && A.plan[T] <= K.ArmyCount(static_cast<int>(T)));
	}
	KD_CHECK(A.takenScore > 0 && A.planScore > A.takenScore * 3 / 2);
	// The plan taken: no tips left, and the same score as the plan's.
	const MarshalAdvice Again = K.Marshal(Riders, A.plan, {});
	KD_CHECK(Again.tips.empty() && Again.plan == A.plan && Again.takenScore == A.planScore);
	// The same call gives the same answer, and it changes nothing in the kingdom.
	const uint64_t Hash = K.Hash();
	const MarshalAdvice Twice = K.Marshal(Riders, Default, {});
	KD_CHECK(Twice.plan == A.plan && Twice.planScore == A.planScore && Twice.tips.size() == A.tips.size() && K.Hash() == Hash);
	// A trial that bars the spearmen: neither tip nor plan takes them, and nothing else hits cavalry hard.
	std::vector<bool> NoSpears(N, true);
	NoSpears[static_cast<size_t>(Spr)] = false;
	A = K.Marshal(Riders, Default, NoSpears);
	KD_CHECK(A.plan[static_cast<size_t>(Spr)] == 0 && A.tips.size() == 1 && A.tips[0].kind == MarshalTipKind::Exposed);
	// No spearmen in the camp: train them (Castle 3 opens them); below Castle 3 nothing to say but the archers' risk.
	Kingdom NoSp(Ct);
	KD_CHECK(NoSp.Load(WithUnits(Save, {{"troop.militia", 3}, {"troop.archers", 3}}), Error));
	NoSp.SetBattleContent(&Battle());
	A = NoSp.Marshal(Riders, Default, {});
	KD_CHECK(A.tips.size() == 2 && A.tips[0].kind == MarshalTipKind::Train && A.tips[0].troop == Spr && A.tips[1].kind == MarshalTipKind::Exposed);
	KD_CHECK(A.plan[static_cast<size_t>(Spr)] == 0 && Sum(A.plan) == std::min(Slots, 6));
	Kingdom Low(Ct);
	KD_CHECK(Low.Load(Replace(WithUnits(Save, {{"troop.militia", 3}, {"troop.archers", 3}}), "\"def\":\"castle\",\"level\":4", "\"def\":\"castle\",\"level\":2"), Error));
	Low.SetBattleContent(&Battle());
	A = Low.Marshal(Riders, Default, {});
	KD_CHECK(A.tips.size() == 1 && A.tips[0].kind == MarshalTipKind::Exposed);
	// A titan behind the swordsmen: ballista crews from the camp hit it 2.0 (none can be trained here: no Jade council).
	SideSetup Titan = Squads("troop.swordsmen", 2);
	Titan.titan.present = true;
	Titan.titan.titanId = "titan.talos";
	Titan.titan.level = 20;
	Titan.titan.stars = 2;
	A = K.Marshal(Titan, Default, {});
	for (const MarshalTip& T : A.tips)
	{
		KD_CHECK(T.enemyClass != Tit);
	}
	Kingdom Bl(Ct);
	KD_CHECK(Bl.Load(WithUnits(Save, {{"troop.militia", 3}, {"troop.archers", 3}, {"troop.ballista", 1}}), Error));
	Bl.SetBattleContent(&Battle());
	A = Bl.Marshal(Titan, Default, {});
	KD_CHECK(!A.tips.empty() && A.tips[0].kind == MarshalTipKind::Bring && A.tips[0].troop == Bal && A.tips[0].enemyClass == Tit && A.tips[0].pct == 200);
	KD_CHECK(A.tips.size() <= 3 && A.plan[static_cast<size_t>(Bal)] == 1);
	// Mixed enemies (the land battles' armies, a titan on some): the plan always scores at least the army taken, and once
	// it is taken the Marshal has nothing to bring and nobody to warn about (the tips never argue with the plan).
	const std::vector<std::vector<const char*>> Mixes = {{"troop.militia", "troop.spearmen", "troop.archers", "troop.outriders"},
		{"troop.militia", "troop.spearmen", "troop.swordsmen", "troop.archers", "troop.outriders"}, {"troop.knights", "troop.knights", "troop.archers"},
		{"troop.goliaths", "troop.pikemen", "troop.priests", "troop.grenadiers", "troop.panthers", "troop.knights"}, {"troop.archers", "troop.archers", "troop.grenadiers"}};
	for (size_t I = 0; I < Mixes.size(); ++I)
	{
		SideSetup E;
		for (const char* Id : Mixes[I])
		{
			SquadSetup Q;
			Q.troopId = Id;
			Q.tier = 2;
			E.squads.push_back(Q);
		}
		E.titan = Titan.titan;
		E.titan.present = I % 2 == 1;
		const MarshalAdvice D = K.Marshal(E, Default, {});
		KD_CHECK(D.tips.size() <= 3 && Sum(D.plan) <= Slots && D.planScore >= D.takenScore);
		const MarshalAdvice P = K.Marshal(E, D.plan, {});
		KD_CHECK(P.plan == D.plan && P.takenScore == D.planScore);
		for (const MarshalTip& T : P.tips)
		{
			KD_CHECK(T.kind == MarshalTipKind::Train);
		}
	}
}

void TestDailyTrials()
{
	const KingdomContent& Ct = Content();
	const DailyTrialRules& D = Ct.dailyTrials;
	const size_t GoldI = static_cast<size_t>(ResIndex(Res::Gold));
	const size_t FoodI = static_cast<size_t>(ResIndex(Res::Food));
	const size_t ShardsI = static_cast<size_t>(ResIndex(Res::Shards));
	const size_t StonesI = static_cast<size_t>(ResIndex(Res::Stones));
	const int64_t Day = 86400;
	// Master prompt 10.3: a trial a weekday, all on Sunday, 3 attempts a day each, 10 tiers. Ours: from Castle 3, tiers at
	// rising Castle levels, three lineages, no gems.
	KD_CHECK(D.castle == 3 && D.attempts == 3 && D.lineageBonusPct == 50 && D.tierCastle.size() == 10 && D.lineages.size() == 3 && D.list.size() == 6);
	KD_CHECK(D.tierCastle.front() == 3 && D.tierCastle.back() == 20 && D.lineages[2] == std::vector<int>({Ct.FindTitan("titan.perun"), Ct.FindTitan("titan.veles")}));
	const DailyPay Pays[] = {DailyPay::Gold, DailyPay::TitanXp, DailyPay::Relic, DailyPay::Shards, DailyPay::Food, DailyPay::Study};
	for (size_t I = 0; I < D.list.size(); ++I)
	{
		KD_CHECK(D.list[I].day == static_cast<int>(I) && D.list[I].pay == Pays[I] && D.list[I].amounts.size() == 10);
		for (size_t T = 1; T < 10; ++T)
		{
			KD_CHECK(D.list[I].amounts[T] >= D.list[I].amounts[T - 1] && D.tierCastle[T] > D.tierCastle[T - 1]);
		}
	}
	KD_CHECK(D.list[0].amounts[0] == 4000 && D.list[1].amounts[0] == 300 && D.list[2].amounts.back() == 4 && D.list[5].amounts[0] == 15);

	// T0 is a Thursday (the Lineage Trial's day) of week 2910 (the lineage of Olympus). A new kingdom is below Castle 3.
	const int64_t Today = T0 / Day;
	KD_CHECK((Today + 3) % 7 == 3 && T0 % Day > 0);
	Kingdom K(Ct);
	K.NewGame(T0, 101);
	KD_CHECK(K.DailyCount() == 6 && K.DailyTiers() == 10 && K.DailyWeekday() == 3 && K.DailyLineage() == 0 && K.DailyOpen(3) && !K.DailyOpen(0));
	KD_CHECK(K.CheckDaily(3, 1).error == Error::Locked && K.CheckDaily(3, 1).castle == 3 && K.DailyTopTier(3) == 0 && K.StartDaily(3, 1, {}, 0, {}) == Error::Locked);
	KD_CHECK(K.CheckDaily(6, 1).error == Error::BadId && K.CheckDaily(3, 0).error == Error::BadId && K.CheckDaily(3, 11).error == Error::BadId);
	const uint64_t Untouched = K.Hash();
	std::string Error;
	Kingdom L(Ct);
	KD_CHECK(L.Load(Replace(K.Save(), "\"def\":\"castle\",\"level\":1", "\"def\":\"castle\",\"level\":3"), Error) && L.CastleLevel() == 3);
	// At Castle 3 the first tier is open, the second waits for Castle 4; Monday's trial opens on Sunday.
	KD_CHECK(L.DailyTopTier(3) == 1 && L.CheckDaily(3, 1).error == Error::None && L.CheckDaily(3, 2).error == Error::Locked && L.CheckDaily(3, 2).castle == 4);
	KD_CHECK(L.CheckDaily(0, 1).error == Error::NotReady && L.CheckDaily(0, 1).timeSec == (Today + 3) * Day - T0 && L.StartDaily(0, 1, {}, 0, {}) == Error::NotReady);
	KD_CHECK(L.DailyAttemptsLeft(3) == 3 && L.DailyStars(3, 1) == 0 && L.SweepDaily(3, 1, 0, nullptr) == Error::NotReady);
	// A battle is free of stones and counts when it starts; it reloads as it was.
	const int32_t Perun = L.Titans()[0].id;
	KD_CHECK(L.Titans()[0].def == Ct.FindTitan("titan.perun"));
	const int Militia = Troop("troop.militia");
	const int64_t Army = L.ArmyCount(Militia);
	const int64_t Stones = L.Amount(Res::Stones);
	const int64_t Count = L.Lifetime(QuestEvent::Count);
	const int64_t Wins = L.Lifetime(QuestEvent::Win);
	KD_CHECK(Army >= 1 && L.StartDaily(3, 1, {Militia}, Perun, {}) == Error::None && L.PendingDaily() && L.PendingSite() == -1 && L.PendingMission() == -1);
	KD_CHECK(L.DailyBattleTrial() == 3 && L.DailyBattleTier() == 1 && L.Amount(Res::Stones) == Stones && L.DailyAttemptsLeft(3) == 2 && L.Lifetime(QuestEvent::Count) == Count);
	KD_CHECK(L.SweepDaily(3, 1, 0, nullptr) == Error::Busy && L.FinishTrial(1, 100, nullptr) == Error::NoAttack && L.FinishBoss(1, 10, nullptr) == Error::NoAttack);
	Kingdom M(Ct);
	KD_CHECK(M.Load(L.Save(), Error) && M.Hash() == L.Hash() && M.Save() == L.Save() && M.PendingDaily() && M.DailyBattleTrial() == 3);
	// A 3-star win pays the tier's shard (Perun is not of Olympus); nothing is lost and the titan gets no battle XP.
	const int64_t Shards = L.Amount(Res::Shards);
	Reward R;
	KD_CHECK(L.FinishDaily(3, &R) == Error::None && !L.PendingDaily() && L.DailyBattleTrial() == -1 && R.squadsBack == 1 && R.squadsLost == 0 && R.titanXp == 0);
	KD_CHECK(R.res[ShardsI] == 1 && L.Amount(Res::Shards) == Shards + 1 && L.ArmyCount(Militia) == Army && L.DailyStars(3, 1) == 3 && L.Lifetime(QuestEvent::Win) == Wins + 1);
	KD_CHECK(L.FinishDaily(3, nullptr) == Error::NoAttack && L.DailyTopTier(3) == 1);
	// A tier won on 3 stars sweeps for an attempt; a lost battle pays nothing and keeps the stars.
	KD_CHECK(L.SweepDaily(3, 1, 0, &R) == Error::None && R.res[ShardsI] == 1 && R.squadsBack == 0 && L.DailyAttemptsLeft(3) == 1 && L.Lifetime(QuestEvent::Win) == Wins + 1);
	KD_CHECK(L.SweepDaily(3, 1, 999999, nullptr) == Error::BadId && L.DailyAttemptsLeft(3) == 1);
	KD_CHECK(L.StartDaily(3, 1, {}, 0, {}) == Error::None && L.FinishDaily(0, &R) == Error::None && R.res[ShardsI] == 0 && L.DailyStars(3, 1) == 3);
	KD_CHECK(L.CheckDaily(3, 1).error == Error::LimitReached && L.CheckDaily(3, 1).timeSec == (Today + 1) * Day - T0 && L.SweepDaily(3, 1, 0, nullptr) == Error::LimitReached);
	KD_CHECK(M.Load(L.Save(), Error) && M.Hash() == L.Hash() && M.Save() == L.Save() && M.DailyStars(3, 1) == 3 && M.DailyAttemptsLeft(3) == 0);
	KD_CHECK(L.Hash() != Untouched);

	// Sunday: every trial is open with three attempts.
	L.Advance((Today + 3) * Day + Hour);
	KD_CHECK(L.DailyWeekday() == 6);
	for (int I = 0; I < 6; ++I)
	{
		KD_CHECK(L.DailyOpen(I) && L.DailyAttemptsLeft(I) == 3 && L.CheckDaily(I, 1).error == Error::None);
	}
	KD_CHECK(L.CheckDaily(0, 2).error == Error::Locked);
	// Gold Vault: gold on top of full vaults. Banner Grounds: food.
	const int64_t Gold = L.Amount(Res::Gold);
	KD_CHECK(L.StartDaily(0, 1, {Militia}, 0, {}) == Error::None && L.FinishDaily(1, &R) == Error::None && R.res[GoldI] == 4000 && L.Amount(Res::Gold) == Gold + 4000);
	KD_CHECK(L.DailyStars(0, 1) == 1 && L.SweepDaily(0, 1, 0, nullptr) == Error::NotReady);
	KD_CHECK(L.StartDaily(4, 1, {}, 0, {}) == Error::None && L.FinishDaily(2, &R) == Error::None && R.res[FoodI] == 4000 && R.res[StonesI] == 0);
	// Essence Springs: only with a titan, whose XP grows by the tier's amount; a sweep gives it to the titan picked.
	const int64_t Xp = L.Titans()[0].xp;
	const int Level = L.Titans()[0].level;
	KD_CHECK(L.StartDaily(1, 1, {Militia}, 0, {}) == Error::NotAllowed && L.DailyAttemptsLeft(1) == 3 && !L.PendingDaily());
	KD_CHECK(L.StartDaily(1, 1, {}, Perun, {}) == Error::None && L.FinishDaily(3, &R) == Error::None && R.titanId == Perun && R.titanXp == 300);
	KD_CHECK(L.Titans()[0].level > Level || L.Titans()[0].xp == Xp + 300);
	KD_CHECK(L.SweepDaily(1, 1, 0, nullptr) == Error::NotAllowed && L.SweepDaily(1, 1, Perun, &R) == Error::None && R.titanXp == 300 && L.DailyAttemptsLeft(1) == 1);
	// Forge Depths: a relic of the tier's rarity.
	const size_t Relics = L.Relics().size();
	KD_CHECK(L.StartDaily(2, 1, {}, 0, {}) == Error::None && L.FinishDaily(3, &R) == Error::None && R.relics.size() == 1 && L.Relics().size() == Relics + 1);
	KD_CHECK(L.FindRelic(R.relics[0]) && L.FindRelic(R.relics[0])->rarity == 1);
	// Hall of Tomes: minutes off the research running; none running, none taken.
	KD_CHECK(L.StartDaily(5, 1, {}, 0, {}) == Error::None && L.FinishDaily(3, &R) == Error::None && R.studySec == 0);
	Kingdom S(Ct);
	const int64_t Until = L.Now() + 2 * Hour;
	KD_CHECK(S.Load(Replace(L.Save(), "\"research\":\"\",\"researchFrom\":0,\"researchUntil\":0",
		"\"research\":\"troop.militia\",\"researchFrom\":" + std::to_string(L.Now()) + ",\"researchUntil\":" + std::to_string(Until)), Error));
	KD_CHECK(S.ResearchTroop() == Militia && S.ResearchFinish() == Until);
	KD_CHECK(S.SweepDaily(5, 1, 0, &R) == Error::None && R.studySec == 15 * 60 && S.ResearchFinish() == Until - 15 * 60 && S.ResearchTroop() == Militia);
	const int MilitiaLevel = S.TroopLevel(Militia);
	S.Advance(Until - 16 * 60);
	KD_CHECK(S.SweepDaily(5, 1, 0, &R) == Error::None && R.studySec == 15 * 60 && S.ResearchTroop() < 0 && S.TroopLevel(Militia) == MilitiaLevel + 1);
	// The Lineage Trial on Sunday of a Nav week (2912): Perun brings half as many shards again (at least one).
	Kingdom N(Ct);
	KD_CHECK(N.Load(L.Save(), Error));
	N.Advance((Today + 14) * Day + Hour);
	KD_CHECK(N.DailyLineage() == 2 && N.DailyWeekday() == 3 && N.DailyAttemptsLeft(3) == 3);
	KD_CHECK(N.SweepDaily(3, 1, Perun, &R) == Error::None && R.res[ShardsI] == 2 && N.SweepDaily(3, 1, 0, &R) == Error::None && R.res[ShardsI] == 1);
	// Monday: the Gold Vault alone; the Lineage Trial waits for Thursday.
	L.Advance((Today + 4) * Day + Hour);
	KD_CHECK(L.DailyWeekday() == 0 && L.DailyOpen(0) && !L.DailyOpen(3) && L.DailyAttemptsLeft(0) == 3 && L.CheckDaily(3, 1).timeSec == 3 * Day - Hour);
	// A save without daily trials starts with none won.
	Kingdom O(Ct);
	KD_CHECK(O.Load(Replace(L.Save(), "\"daily\":{", "\"dailyOld\":{"), Error) && O.DailyStars(3, 1) == 0 && O.DailyAttemptsLeft(0) == 3);
}

/** A kingdom with VP to join any alliance, at Castle 5. */
Kingdom BossKingdom(uint64_t Seed)
{
	Kingdom K = WithVp(Seed, 8000);
	Kingdom L(Content());
	std::string Error;
	KD_CHECK(L.Load(Replace(K.Save(), "\"def\":\"castle\",\"level\":1", "\"def\":\"castle\",\"level\":5"), Error) && L.CastleLevel() == 5);
	return L;
}

void TestRaidBoss()
{
	const KingdomContent& Ct = Content();
	const RaidBossRules& B = Ct.raidBoss;
	const size_t GoldI = static_cast<size_t>(ResIndex(Res::Gold));
	const size_t FoodI = static_cast<size_t>(ResIndex(Res::Food));
	const size_t GemsI = static_cast<size_t>(ResIndex(Res::Gems));
	const size_t StonesI = static_cast<size_t>(ResIndex(Res::Stones));
	const int64_t Day = 86400;
	// Master prompt 12.10: Sunday and Monday, three battles a day of two minutes, tiers 1-10, four bosses, phases at
	// 75/50/25%, damage milestones and an alliance chest by tier.
	KD_CHECK(B.castle == 5 && B.days == 2 && B.attempts == 3 && B.battleSec == 120 && B.hpPct == 300 && B.poolPct == 100);
	KD_CHECK(B.bosses.size() == 4 && B.tiers.size() == 10 && B.leagueTier.size() == Ct.alliances.leagues.size() && B.leagueTier.back() == 10);
	KD_CHECK(B.phases == std::vector<int>({750, 500, 250}) && B.milestones.size() == 3 && B.milestones.back().first == 300);
	KD_CHECK(B.bosses[0].id == "behemoth" && B.bosses[0].titan == Ct.FindTitan("titan.atlas") && B.bosses[3].titan == Ct.FindTitan("titan.surtr"));
	// Escorts and husks are battle troops; tiers grow, and so do their chests (few gems: 12 at most).
	const SimContent& Sim = Battle();
	const auto IsTroop = [&Sim](const std::string& Id) {
		return std::any_of(Sim.troops.begin(), Sim.troops.end(), [&Id](const TroopDef& T) { return T.id == Id; });
	};
	KD_CHECK(IsTroop(B.husk));
	for (const RaidBossDef& D : B.bosses)
	{
		KD_CHECK(IsTroop(D.escort));
	}
	for (size_t I = 1; I < B.tiers.size(); ++I)
	{
		KD_CHECK(B.tiers[I].level > B.tiers[I - 1].level && B.tiers[I].stars >= B.tiers[I - 1].stars && B.tiers[I].escort >= B.tiers[I - 1].escort);
		KD_CHECK(B.tiers[I].chest.res[GemsI] > B.tiers[I - 1].chest.res[GemsI] && B.tiers[I].chest.res[GemsI] <= 12);
	}
	for (const auto& M : B.milestones)
	{
		KD_CHECK(M.second.res[GemsI] == 0);
	}

	// T0 is a Thursday; the next event opens on Sunday 00:00 UTC. Outside an alliance there is no boss.
	const int64_t Sunday = ((T0 / Day - 3) / 7 * 7 + 3 + 7) * Day;
	KD_CHECK((Sunday / Day + 3) % 7 == 6 && Sunday > T0 && Sunday - T0 < 7 * Day);
	Kingdom N(Ct);
	N.NewGame(T0, 81);
	KD_CHECK(N.CheckBoss().error == Error::NoAlliance && N.BossTier() == 0 && N.BossPool() == 0 && N.StartBoss({}, 0, {}) == Error::NoAlliance);
	KD_CHECK(N.ChooseBossTier(1) == Error::NoAlliance && N.ClaimBossChest(nullptr) == Error::NotReady);
	// Below Castle 5 the alliance fights without the player.
	Kingdom Low = WithVp(81, 8000);
	KD_CHECK(Low.JoinAlliance(0) == Error::None && Low.CheckBoss().error == Error::Locked && Low.CheckBoss().castle == 5);

	// An alliance of the lowest league fights tier 1; until Sunday the boss is closed.
	Kingdom K = BossKingdom(82);
	KD_CHECK(K.JoinAlliance(0) == Error::None && K.ChooseBossTier(3) == Error::NotAllowed);
	const int Members = static_cast<int>(K.Members(0).size()) - 1;
	KD_CHECK(K.BossTier() == 1 && K.BossHomeTier() == 1 && K.BossNextTier() == 1 && K.BossPool() == (Members + 1) * 1000);
	KD_CHECK(K.CheckBoss().error == Error::NotReady && K.CheckBoss().timeSec == Sunday - T0 && K.BossOpensAt() == Sunday && K.BossClosesAt() == Sunday + 2 * Day);
	KD_CHECK(!K.BossOpen() && K.StartBoss({}, 0, {}) == Error::NotReady);
	K.Advance(Sunday + Hour);
	const int Index = K.BossIndex();
	KD_CHECK(K.BossOpen() && K.CheckBoss().error == Error::None && K.BossAttemptsLeft() == 3 && Index >= 0 && Index < 4);
	KD_CHECK(static_cast<int>(K.BossMembers().size()) == Members && K.BossDamage() < K.BossPool() / 10);
	// A battle is free of stones, nothing is lost and the titan gets no XP.
	const int Militia = Troop("troop.militia");
	const int64_t Army = K.ArmyCount(Militia);
	const int64_t Stones = K.Amount(Res::Stones);
	const int64_t Count = K.Lifetime(QuestEvent::Count);
	KD_CHECK(Army >= 1 && K.StartBoss({Militia}, 0, {}) == Error::None && K.PendingBoss() && K.PendingSite() == -1 && K.PendingMission() == -1);
	KD_CHECK(K.Amount(Res::Stones) == Stones && K.BossAttemptsLeft() == 2 && K.Lifetime(QuestEvent::Count) == Count);
	KD_CHECK(K.FinishTrial(1, 100, nullptr) == Error::NoAttack && K.FinishSiteAttack(0, 1, {100}, {}, nullptr) == Error::NoAttack);
	std::string Error;
	Kingdom M(Ct);
	KD_CHECK(M.Load(K.Save(), Error) && M.Hash() == K.Hash() && M.Save() == K.Save() && M.PendingBoss());
	// A battle that took 32% of the boss's health pays every milestone (gold and food times the Castle, stones on top
	// of a full portal).
	const int64_t Gems = K.Amount(Res::Gems);
	Reward R;
	KD_CHECK(K.FinishBoss(0, 320, &R) == Error::None && !K.PendingBoss() && R.squadsBack == 1 && R.squadsLost == 0 && R.titanXp == 0);
	KD_CHECK(R.res[GoldI] == 200 * 5 && R.res[FoodI] == 200 * 5 && R.res[StonesI] == 3 && R.res[GemsI] == 0 && K.Amount(Res::Stones) == Stones + 3);
	KD_CHECK(K.Amount(Res::Gems) == Gems && K.ArmyCount(Militia) == Army);
	KD_CHECK(K.BossMyDamage() == 320 && K.BossMyBattles() == 1 && K.BossMyBest() == 320 && K.FinishBoss(0, 10, nullptr) == Error::NoAttack);
	// A weak battle pays less; the damage of one battle is at most the boss's whole health.
	KD_CHECK(K.StartBoss({}, 0, {}) == Error::None && K.FinishBoss(0, 60, &R) == Error::None && R.res[GoldI] == 1000 && R.res[FoodI] == 0 && R.res[StonesI] == 0);
	KD_CHECK(K.StartBoss({}, 0, {}) == Error::None && K.FinishBoss(0, 5000, nullptr) == Error::None && K.BossMyDamage() == 1380 && K.BossMyBest() == 1000);
	KD_CHECK(K.CheckBoss().error == Error::LimitReached && K.CheckBoss().timeSec == Day - Hour && K.StartBoss({}, 0, {}) == Error::LimitReached);
	// The pool holds the members' battles and the player's.
	int64_t Sum = K.BossMyDamage();
	for (const BossMemberHits& H : K.BossMembers())
	{
		Sum += H.dmg;
	}
	KD_CHECK(Sum == K.BossDamage() && !K.BossDefeated());
	// Every client sees the same members; Monday brings three more battles.
	Kingdom K2(Ct);
	KD_CHECK(K2.Load(K.Save(), Error) && K2.Hash() == K.Hash() && K2.BossDamage() == K.BossDamage());
	K.Advance(Sunday + Day + Hour);
	KD_CHECK(K.BossAttemptsLeft() == 3 && K.BossMyDamage() == 1380 && K.BossDamage() > K2.BossDamage());

	// By the end every member fought two to six battles. Without the player some bosses fall, with a strong player most.
	K.Advance(Sunday + 2 * Day + Hour);
	KD_CHECK(!K.BossOpen() && K.CheckBoss().error == Error::NotReady && K.CheckBoss().timeSec == 7 * Day - 2 * Day - Hour);
	for (const BossMemberHits& H : K.BossMembers())
	{
		KD_CHECK((H.battles >= 2 && H.battles <= 6) || K.BossDefeated());
	}
	int Alone = 0;
	int Helped = 0;
	int Alliances = 0;
	Kingdom Base = BossKingdom(91);
	for (int I = 0; I < Base.AllianceCount(); ++I)
	{
		Kingdom A(Ct);
		KD_CHECK(A.Load(Base.Save(), Error));
		if (A.JoinAlliance(I) != Error::None)
		{
			continue;
		}
		// Each league fights its own tier.
		KD_CHECK(A.BossTier() == B.leagueTier[static_cast<size_t>(std::min(I / 8, 5))]);
		Kingdom H(Ct);
		KD_CHECK(H.Load(A.Save(), Error));
		A.Advance(Sunday + 2 * Day + Hour);
		Alone += A.BossDefeated() ? 1 : 0;
		// Five battles of 30% each.
		for (int D = 0; D < 2; ++D)
		{
			H.Advance(Sunday + D * Day + 20 * Hour);
			for (int X = 0; X < 3 - D; ++X)
			{
				if (H.StartBoss({}, 0, {}) == Error::None)
				{
					KD_CHECK(H.FinishBoss(0, 300, nullptr) == Error::None);
				}
			}
		}
		H.Advance(Sunday + 2 * Day + Hour);
		Helped += H.BossDefeated() ? 1 : 0;
		KD_CHECK(H.BossDamage() >= A.BossDamage() && (!H.BossDefeated() || H.BossDefeatedAt() <= A.BossDefeatedAt() || !A.BossDefeated()));
		++Alliances;
	}
	KD_CHECK(Alliances >= 40 && Alone * 100 >= 10 * Alliances && Alone * 100 <= 40 * Alliances && Helped * 100 >= 55 * Alliances && Helped > Alone);

	// A boss that falls: everyone who fought opens the chest until the next event.
	Kingdom F(Ct);
	int Who = -1;
	for (int I = 0; I < Base.AllianceCount() && Who < 0; ++I)
	{
		KD_CHECK(F.Load(Base.Save(), Error));
		if (F.JoinAlliance(I) != Error::None)
		{
			continue;
		}
		F.Advance(Sunday + Day + 20 * Hour);
		for (int X = 0; X < 3; ++X)
		{
			if (F.StartBoss({}, 0, {}) == Error::None)
			{
				KD_CHECK(F.FinishBoss(0, 400, nullptr) == Error::None);
			}
		}
		Who = F.BossDefeated() ? I : -1;
	}
	KD_CHECK(Who >= 0 && F.BossDamage() == F.BossPool() && F.BossDefeatedAt() > Sunday && F.BossDefeatedAt() <= F.Now());
	KD_CHECK(F.CheckBoss().error == Error::Nothing && F.CheckBoss().timeSec == Sunday + 7 * Day - F.Now() && F.StartBoss({}, 0, {}) == Error::Nothing);
	const RaidBossTier& Tier = B.tiers[static_cast<size_t>(F.BossTier() - 1)];
	const int64_t FGems = F.Amount(Res::Gems);
	KD_CHECK(F.ClaimBossChest(&R) == Error::None && R.res[GemsI] == Tier.chest.res[GemsI] && F.Amount(Res::Gems) == FGems + Tier.chest.res[GemsI]);
	KD_CHECK(F.BossChestClaimed() && F.ClaimBossChest(nullptr) == Error::Nothing);
	// A member who did not fight gets none.
	Kingdom Idle(Ct);
	KD_CHECK(Idle.Load(Base.Save(), Error) && Idle.JoinAlliance(Who) == Error::None);
	Idle.Advance(F.Now());
	KD_CHECK(Idle.BossDamage() < F.BossDamage() && Idle.ClaimBossChest(nullptr) == Error::NotReady);
	Idle.Advance(Sunday + 6 * Day);
	KD_CHECK(!Idle.BossDefeated() || Idle.ClaimBossChest(nullptr) == Error::NotAllowed);
	// Saving keeps the event; the next Sunday brings the next boss with nothing fought.
	Kingdom S(Ct);
	KD_CHECK(S.Load(F.Save(), Error) && S.Hash() == F.Hash() && S.Save() == F.Save() && S.BossChestClaimed() && S.BossDefeated());
	S.Advance(Sunday + 7 * Day + Hour);
	KD_CHECK(S.BossIndex() == (Index + 1) % 4 && !S.BossChestClaimed() && S.BossMyDamage() == 0 && S.BossMyBattles() == 0 && !S.BossDefeated());
	KD_CHECK(S.CheckBoss().error == Error::None && S.BossAttemptsLeft() == 3);

	// Leaving the alliance: the battles stay with it, today's battles stay spent.
	Kingdom V = BossKingdom(83);
	KD_CHECK(V.JoinAlliance(0) == Error::None);
	V.Advance(Sunday + Hour);
	KD_CHECK(V.StartBoss({}, 0, {}) == Error::None && V.FinishBoss(0, 200, nullptr) == Error::None && V.BossMyDamage() == 200);
	KD_CHECK(V.LeaveAlliance() == Error::None && V.CheckBoss().error == Error::NoAlliance && V.BossMyDamage() == 0);
	KD_CHECK(V.JoinAlliance(1) == Error::None && V.BossMyDamage() == 0 && V.BossAttemptsLeft() == 2);
	KD_CHECK(V.StartBoss({}, 0, {}) == Error::None && V.FinishBoss(0, 100, nullptr) == Error::None && V.BossMyDamage() == 100 && V.BossMyBattles() == 1);

	// An own alliance: the leader picks the tier of the coming events; against a higher tier the members do less.
	Kingdom O = BossKingdom(84);
	KD_CHECK(O.CreateAlliance("Дом Рассвета", "ДОМ") == Error::None && O.BossTier() == 1 && O.BossHomeTier() == 1);
	KD_CHECK(O.ChooseBossTier(11) == Error::BadId && O.ChooseBossTier(-1) == Error::BadId && O.ChooseBossTier(3) == Error::None && O.BossNextTier() == 3);
	O.Advance(Sunday + Hour);
	KD_CHECK(O.BossTier() == 3 && O.BossHomeTier() == 1 && O.Members(O.MyAlliance()).size() == O.BossMembers().size() + 1);
	// Changing the pick during the event leaves this one alone.
	KD_CHECK(O.ChooseBossTier(1) == Error::None && O.BossTier() == 3 && O.BossNextTier() == 1);
	Kingdom O2(Ct);
	KD_CHECK(O2.Load(O.Save(), Error) && O2.Save() == O.Save() && O2.Hash() == O.Hash() && O2.BossTier() == 3 && O2.BossNextTier() == 1);
	const auto PerBattle = [](const Kingdom& X) {
		int64_t Dmg = 0;
		int64_t Battles = 0;
		for (const BossMemberHits& H : X.BossMembers())
		{
			Dmg += H.dmg;
			Battles += H.battles;
		}
		return Battles > 0 ? Dmg / Battles : 0;
	};
	O.Advance(Sunday + 2 * Day);
	const int64_t High = PerBattle(O);
	O.Advance(Sunday + 7 * Day + Hour);
	KD_CHECK(O.BossTier() == 1 && O.ChooseBossTier(0) == Error::None && O.BossNextTier() == 1);
	O.Advance(Sunday + 9 * Day);
	// At their own tier the recruits hit 250 on average; at two tiers above it 60% of that.
	const int64_t Own = PerBattle(O);
	KD_CHECK(Own >= 220 && Own <= 280 && High * 100 >= Own * 50 && High * 100 <= Own * 70);

	// A save from before the boss starts this event's state on the next Advance.
	const std::string Now = S.Save();
	const size_t P0 = Now.find(",\"boss\":{");
	Kingdom Pre(Ct);
	KD_CHECK(P0 != std::string::npos && Pre.Load(Now.substr(0, P0) + Now.substr(Now.find(",\"calendar\":{")), Error) && Pre.BossMyBattles() == 0);
	Pre.Advance(S.Now() + 60);
	KD_CHECK(Pre.BossIndex() == S.BossIndex() && Pre.BossTier() == S.BossTier() && Pre.BossAttemptsLeft() == 3);
}

void TestAssault()
{
	const KingdomContent& Ct = Content();
	const AssaultRules& A = Ct.assault;
	const size_t GoldI = static_cast<size_t>(ResIndex(Res::Gold));
	const size_t FoodI = static_cast<size_t>(ResIndex(Res::Food));
	const size_t GemsI = static_cast<size_t>(ResIndex(Res::Gems));
	const int64_t Day = 86400;
	// Master prompt 10.5: a monthly event of 5 days, 10 fortified sieges, 1 of 3 boons after a stage, 3 retries a run.
	// Ours: from Castle 5, Friday to Tuesday in the second week of every four, 70 gems for all stages.
	KD_CHECK(A.castle == 5 && A.weeks == 4 && A.week == 1 && A.startDay == 4 && A.days == 5 && A.retries == 3 && A.boonChoices == 3);
	KD_CHECK(A.stages.size() == 10 && A.boons.size() == 5 && A.garrison.size() == 3 && A.garrison[1] == Troop("troop.ballista"));
	int64_t StageGems = 0;
	int Titans = 0;
	for (const AssaultStage& S : A.stages)
	{
		StageGems += S.reward.res[GemsI];
		Titans += S.titan ? 1 : 0;
	}
	KD_CHECK(StageGems == 70 && Titans == 2 && A.stages[4].titan && A.stages[9].titan && A.stages[9].reward.relic == 3 && A.stages[4].reward.relic == 2);
	KD_CHECK(A.stages[0].level == -2 && A.stages[9].level == 2 && A.stages[9].extra == 2 && A.stages[9].fortPct == 25 && A.stages[0].reward.res[GoldI] == 1000);
	KD_CHECK(A.boons[2].id == "medic" && A.boons[2].revivePct == 30 && A.boons[0].troopAtkPct == 15 && A.boons[1].troopHpPct == 15);
	KD_CHECK(A.boons[3].titanPct == 20 && A.boons[4].titanCritPct == 10);

	// T0 (day 20370, a Thursday of week 2910) is after event 727 (Friday 20364 to Tuesday 20368); event 728 opens on
	// Friday 20392, in week 2913. A new kingdom is below Castle 5.
	Kingdom K(Ct);
	K.NewGame(T0, 75);
	const int64_t Open = 20392 * Day;
	KD_CHECK(K.AssaultEvent() == 727 && !K.AssaultOpen() && K.AssaultOpensAt() == Open && K.AssaultClosesAt() == Open + 5 * Day);
	KD_CHECK(K.CheckAssault().error == Error::Locked && K.CheckAssault().castle == 5 && K.StartAssault({}, 0) == Error::Locked);
	KD_CHECK(!K.AssaultStarted() && !K.AssaultRunning() && K.AssaultRetriesLeft() == 3 && K.StartAssaultStage({}) == Error::NoAttack && K.EndAssault() == Error::NoAttack);
	const int Militia = Troop("troop.militia");
	std::string Error;
	Kingdom L(Ct);
	std::string Save = Replace(K.Save(), "\"def\":\"castle\",\"level\":1", "\"def\":\"castle\",\"level\":5");
	Save = Replace(Save, "\"units\":{\"troop.militia\":" + std::to_string(K.ArmyCount(Militia)), "\"units\":{\"troop.militia\":9");
	KD_CHECK(L.Load(Save, Error) && L.CastleLevel() == 5 && L.ArmyCount(Militia) == 9);
	KD_CHECK(L.CheckAssault().error == Error::NotReady && L.CheckAssault().timeSec == Open - T0 && L.StartAssault({Militia}, 0) == Error::NotReady);
	L.Advance(Open + Hour);
	KD_CHECK(L.AssaultEvent() == 728 && L.AssaultOpen() && L.AssaultOpensAt() == Open && L.CheckAssault().error == Error::None);
	// The army: squads of the camp (no more than it has, no more than the slots) and a titan that is not resting.
	const int Slots = L.BattleSlots();
	const int32_t Perun = L.Titans()[0].id;
	KD_CHECK(Slots >= 2 && Slots < 9 && L.StartAssault({}, 0) == Error::NoTroops && L.StartAssault(std::vector<int>(static_cast<size_t>(Slots) + 1, Militia), 0) == Error::TooManySquads);
	KD_CHECK(L.StartAssault({-1}, 0) == Error::BadId && L.StartAssault({Militia}, 99999) == Error::BadId && !L.AssaultStarted());
	Kingdom Few(Ct);
	KD_CHECK(Few.Load(Replace(L.Save(), "\"units\":{\"troop.militia\":9", "\"units\":{\"troop.militia\":1"), Error) && Few.StartAssault({Militia, Militia}, 0) == Error::NoTroops);
	KD_CHECK(L.StartAssault({Militia, Militia}, Perun) == Error::None && L.AssaultRunning() && L.AssaultStagesWon() == 0 && L.AssaultBase() == 5);
	KD_CHECK(L.ArmyCount(Militia) == 9 && L.AssaultArmy().size() == 2 && L.AssaultArmy()[0].pct == 100 && L.AssaultArmy()[1].level == L.TroopLevel(Militia));
	KD_CHECK(L.AssaultTitan() == Perun && L.AssaultLevel(0) == 3 && L.AssaultLevel(4) == 5 && L.AssaultLevel(9) == 7 && L.StartAssault({Militia}, 0) == Error::Nothing);
	KD_CHECK(L.FinishAssaultStage(3, {100, 100}, nullptr) == Error::NoAttack && L.ChooseAssaultBoon(0) == Error::Nothing);
	// A stage battle: free of stones, the camp's squads stay home; no other battle or ending the run meanwhile.
	const int64_t Stones = L.Amount(Res::Stones);
	const int64_t Gold = L.Amount(Res::Gold);
	const int64_t Xp = L.WarpathXp();
	const int64_t Wins = L.Lifetime(QuestEvent::Win);
	KD_CHECK(L.StartAssaultStage({}) == Error::None && L.PendingAssault() && L.PendingSite() == -1 && L.PendingMission() == -1 && !L.PendingTrial());
	KD_CHECK(L.PendingAttack() == Kingdom::AssaultPending && L.Amount(Res::Stones) == Stones && L.PendingSquads().empty());
	KD_CHECK(L.StartAssaultStage({}) == Error::Busy && L.EndAssault() == Error::Busy && L.FinishTrial(3, 100, nullptr) == Error::NoAttack);
	Kingdom M(Ct);
	KD_CHECK(M.Load(L.Save(), Error) && M.Hash() == L.Hash() && M.Save() == L.Save() && M.PendingAssault());
	// A win: losses carry over, the stage pays (gold x Castle level), three different boons are offered.
	Reward R;
	KD_CHECK(L.FinishAssaultStage(3, {50, 100}, &R) == Error::None && !L.PendingAssault() && L.AssaultStagesWon() == 1);
	KD_CHECK(L.AssaultArmy()[0].pct == 50 && L.AssaultArmy()[1].pct == 100 && R.squadsBack == 2 && R.squadsLost == 0 && R.titanXp == 0);
	KD_CHECK(R.res[GoldI] == 5000 && L.Amount(Res::Gold) == Gold + 5000 && L.Lifetime(QuestEvent::Win) == Wins + 1 && L.WarpathXp() == Xp + 10);
	const std::vector<int> Offer = L.AssaultOffer();
	KD_CHECK(Offer.size() == 3 && Offer[0] != Offer[1] && Offer[1] != Offer[2] && Offer[0] != Offer[2] && Offer[0] >= 0 && Offer[2] < 5);
	KD_CHECK(L.StartAssaultStage({}) == Error::NotReady && L.ChooseAssaultBoon(3) == Error::BadId && L.ChooseAssaultBoon(-1) == Error::BadId);
	// The same seed, event and stage offer the same boons.
	KD_CHECK(M.FinishAssaultStage(3, {50, 100}, nullptr) == Error::None && M.AssaultOffer() == Offer && M.Hash() == L.Hash());
	// The field medic brings back 30 % of each squad's losses (a whole squad too).
	Kingdom N(Ct);
	const std::string Medic = "{\"no\":728,\"run\":true,\"over\":false,\"stage\":1,\"tries\":0,\"base\":5,\"titan\":0,\"squads\":[[0,3,50],[0,3,0],[0,3,100]],\"boons\":[],\"offer\":[2,0,1]}";
	KD_CHECK(N.Load(WithAssault(L.Save(), Medic), Error) && N.AssaultArmy().size() == 3 && N.AssaultOffer().size() == 3 && N.AssaultTitan() == 0);
	KD_CHECK(N.ChooseAssaultBoon(0) == Error::None && N.AssaultArmy()[0].pct == 65 && N.AssaultArmy()[1].pct == 30 && N.AssaultArmy()[2].pct == 100);
	KD_CHECK(N.AssaultBoons() == std::vector<int>{2} && N.AssaultOffer().empty() && N.ChooseAssaultBoon(0) == Error::Nothing);
	// The fallen squad came back: the next battle takes all three.
	KD_CHECK(N.StartAssaultStage({}) == Error::None && N.FinishAssaultStage(2, {100, 0, 40}, nullptr) == Error::None);
	KD_CHECK(N.AssaultArmy()[0].pct == 65 && N.AssaultArmy()[1].pct == 0 && N.AssaultArmy()[2].pct == 40 && N.AssaultStagesWon() == 2);
	// Boons add up for the battle.
	const int Pick = L.AssaultOffer()[0];
	KD_CHECK(L.ChooseAssaultBoon(0) == Error::None && L.AssaultBoons() == std::vector<int>{Pick} && L.AssaultOffer().empty());
	const AssaultBonus Sum = L.AssaultBonuses();
	KD_CHECK(Sum.troopAtk == A.boons[static_cast<size_t>(Pick)].troopAtkPct && Sum.troopHp == A.boons[static_cast<size_t>(Pick)].troopHpPct);
	KD_CHECK(Sum.titan == A.boons[static_cast<size_t>(Pick)].titanPct && Sum.titanCrit == A.boons[static_cast<size_t>(Pick)].titanCritPct);
	// A loss (or a retreat) spends a retry and leaves the army as it was; the fourth loss ends the run.
	const std::vector<AssaultSquad> Before = L.AssaultArmy();
	for (int Try = 0; Try < 3; ++Try)
	{
		KD_CHECK(L.StartAssaultStage({}) == Error::None && L.FinishAssaultStage(0, {0, 0}, &R) == Error::None && L.AssaultRunning());
		KD_CHECK(L.AssaultRetriesLeft() == 2 - Try && L.AssaultStagesWon() == 1 && L.AssaultArmy()[0].pct == Before[0].pct && L.AssaultOffer().empty());
	}
	KD_CHECK(R.res[GoldI] == 0 && L.StartAssaultStage({}) == Error::None && L.FinishAssaultStage(0, {}, nullptr) == Error::None);
	KD_CHECK(!L.AssaultRunning() && L.AssaultOver() && L.AssaultStagesWon() == 1 && L.CheckAssault().error == Error::Nothing);
	KD_CHECK(L.CheckAssault().timeSec == Open + 28 * Day - L.Now() && L.StartAssault({Militia}, 0) == Error::Nothing && L.EndAssault() == Error::NoAttack);
	KD_CHECK(L.StartAssaultStage({}) == Error::NoAttack && L.ArmyCount(Militia) == 9);

	// A whole run: ten stages won pay 70 gems, two relics (★★ and ★★★) and gold and food by the Castle's level.
	Kingdom F(Ct);
	KD_CHECK(F.Load(Save, Error));
	F.Advance(Open + 2 * Hour);
	const int64_t Gems = F.Amount(Res::Gems);
	const int64_t Food = F.Amount(Res::Food);
	const size_t Relics = F.Relics().size();
	KD_CHECK(F.StartAssault({Militia}, 0) == Error::None);
	int64_t PaidGold = 0;
	int64_t PaidFood = 0;
	for (int S = 0; S < 10; ++S)
	{
		KD_CHECK(F.StartAssaultStage({}) == Error::None && F.FinishAssaultStage(3, {100}, &R) == Error::None && F.AssaultStagesWon() == S + 1);
		PaidGold += R.res[GoldI];
		PaidFood += R.res[FoodI];
		KD_CHECK(F.AssaultOffer().size() == (S < 9 ? 3u : 0u));
		if (S < 9)
		{
			KD_CHECK(F.ChooseAssaultBoon(S % 3) == Error::None);
		}
	}
	KD_CHECK(F.AssaultOver() && F.AssaultBoons().size() == 9 && F.Amount(Res::Gems) == Gems + 70 && F.Relics().size() == Relics + 2);
	KD_CHECK(PaidGold == 15000 && PaidFood == 15000 && F.Amount(Res::Food) >= Food + 15000 - 1000 && F.FindRelic(R.relics[0])->rarity == 3);
	KD_CHECK(F.StartAssaultStage({}) == Error::NoAttack && F.CheckAssault().error == Error::Nothing);

	// The army wiped out in a won stage (no titan) ends the run.
	Kingdom W(Ct);
	KD_CHECK(W.Load(Save, Error));
	W.Advance(Open + 3 * Hour);
	KD_CHECK(W.StartAssault({Militia}, 0) == Error::None && W.StartAssaultStage({}) == Error::None && W.FinishAssaultStage(1, {0}, &R) == Error::None);
	KD_CHECK(W.AssaultOver() && W.AssaultStagesWon() == 1 && W.AssaultOffer().empty() && R.squadsLost == 1);
	// With the titan the run goes on without squads; ending it by hand closes it.
	Kingdom T(Ct);
	KD_CHECK(T.Load(Save, Error));
	T.Advance(Open + 3 * Hour);
	KD_CHECK(T.StartAssault({Militia}, Perun) == Error::None && T.StartAssaultStage({}) == Error::None && T.FinishAssaultStage(1, {0}, nullptr) == Error::None);
	KD_CHECK(T.AssaultRunning() && T.AssaultOffer().size() == 3 && T.EndAssault() == Error::None && T.AssaultOver() && T.AssaultOffer().empty());

	// The event closes on Tuesday night: no stage can start, the state stays until the next event opens.
	Kingdom C(Ct);
	KD_CHECK(C.Load(Save, Error));
	C.Advance(Open + Hour);
	KD_CHECK(C.StartAssault({Militia}, 0) == Error::None);
	C.Advance(Open + 5 * Day);
	KD_CHECK(!C.AssaultOpen() && C.AssaultOpensAt() == Open + 28 * Day && C.CheckAssault().error == Error::NotReady && C.StartAssaultStage({}) == Error::NotReady);
	KD_CHECK(C.AssaultEvent() == 728 && C.AssaultRunning() && C.AssaultArmy().size() == 1);
	// A stage battle still on when the next event opens keeps its run until it ends; the next roll starts afresh.
	Kingdom P(Ct);
	KD_CHECK(P.Load(Save, Error));
	P.Advance(Open + 5 * Day - Hour);
	KD_CHECK(P.StartAssault({Militia}, 0) == Error::None && P.StartAssaultStage({}) == Error::None);
	P.Advance(Open + 28 * Day + Hour);
	KD_CHECK(P.AssaultEvent() == 728 && P.PendingAssault() && P.FinishAssaultStage(3, {100}, nullptr) == Error::None && P.AssaultStagesWon() == 1);
	P.Advance(Open + 28 * Day + 2 * Hour);
	KD_CHECK(P.AssaultEvent() == 729 && !P.AssaultStarted() && P.AssaultStagesWon() == 0 && P.AssaultArmy().empty() && P.CheckAssault().error == Error::None);
	C.Advance(Open + 28 * Day);
	KD_CHECK(C.AssaultEvent() == 729 && !C.AssaultStarted() && C.CheckAssault().error == Error::None);

	// Loading: a pending stage without a run is dropped, broken squads and boons are left out.
	Kingdom B(Ct);
	KD_CHECK(B.Load(WithAssault(M.Save(), "{\"no\":728,\"run\":false,\"stage\":0,\"squads\":[],\"boons\":[],\"offer\":[]}"), Error) && !B.PendingAssault());
	const std::string Broken = "{\"no\":728,\"run\":true,\"over\":false,\"stage\":40,\"tries\":9,\"base\":5,\"titan\":0,\"squads\":[[99,1,50],[0,0,180],[0,3]],\"boons\":[7,1,-1],\"offer\":[4,4,4,4,4]}";
	KD_CHECK(B.Load(WithAssault(L.Save(), Broken), Error) && B.AssaultStagesWon() == 10 && B.AssaultRetriesLeft() == 0 && B.AssaultArmy().size() == 1);
	KD_CHECK(B.AssaultArmy()[0].level == 1 && B.AssaultArmy()[0].pct == 100 && B.AssaultBoons() == std::vector<int>{1} && B.AssaultOffer().size() == 3);
	// A save from before the Assault loads with no event until the next Advance, which rolls this one.
	const size_t P0 = L.Save().find(",\"assault\":{");
	const size_t P1 = L.Save().find(",\"calendar\":{");
	Kingdom O(Ct);
	KD_CHECK(P0 != std::string::npos && O.Load(L.Save().substr(0, P0) + L.Save().substr(P1), Error) && O.AssaultEvent() == -1 && !O.AssaultStarted());
	O.Advance(O.Now() + 1);
	KD_CHECK(O.AssaultEvent() == 728 && !O.AssaultStarted() && O.CheckAssault().error == Error::None);
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
		{"Rivals", TestRivals}, {"QueueRefunds", TestQueueRefunds}, {"Goals", TestGoals}, {"Achievements", TestAchievements}, {"Calendars", TestCalendars}, {"WeeklyQuests", TestWeeklyQuests}, {"TitanTrials", TestTitanTrials}, {"Warpath", TestWarpath}, {"Assault", TestAssault}, {"KingdomPower", TestKingdomPower}, {"Scouting", TestScouting}, {"Marshal", TestMarshal}, {"RaidBoss", TestRaidBoss}, {"DailyTrials", TestDailyTrials},
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
