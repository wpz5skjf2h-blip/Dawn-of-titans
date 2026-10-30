// SimTools/tests.cpp
// Unit and integration tests for the deterministic simulation (master prompt 4.16, 18.3, 25.1).
#include "ToolCommon.h"

#include "DoTSim/BattleSimulation.h"
#include "DoTSim/Hash.h"
#include "DoTSim/Json.h"
#include "DoTSim/NavGrid.h"
#include "DoTSim/Random.h"
#include "DoTSim/Replay.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace
{
int GFailures = 0;
int GChecks = 0;

#define DOT_CHECK(Cond)                                                                \
	do                                                                                 \
	{                                                                                  \
		++GChecks;                                                                     \
		if (!(Cond))                                                                   \
		{                                                                              \
			++GFailures;                                                               \
			std::printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #Cond);             \
		}                                                                              \
	} while (0)

bool Near(double A, double B, double Eps) { return std::fabs(A - B) <= Eps; }

using namespace dot;

const SimContent& Content()
{
	static SimContent C;
	static bool bLoaded = false;
	if (!bLoaded)
	{
		std::string Error;
		if (!dottools::LoadContentDir(dottools::DefaultContentDir(), C, Error))
		{
			std::printf("Content load failed: %s\n", Error.c_str());
		}
		bLoaded = true;
	}
	return C;
}

BattleSetup Scenario(const char* Path)
{
	BattleSetup S;
	std::string Error;
	if (!dottools::LoadScenario(dottools::DefaultContentDir(), Path, S, Error))
	{
		std::printf("Scenario load failed: %s\n", Error.c_str());
	}
	S.contentHash = Content().hash;
	return S;
}

void RunToEnd(BattleSimulation& Sim, int MaxTicks = 60 * 60 * kTicksPerSecond)
{
	while (!Sim.IsFinished() && Sim.CurrentTick() < MaxTicks)
	{
		Sim.Step();
	}
}

// --- Tests -----------------------------------------------------------------------

void TestFix64()
{
	DOT_CHECK(Near((fx::Int(3) * fx::Milli(1500)).ToDouble(), 4.5, 1e-9));
	DOT_CHECK(Near((fx::Int(-3) * fx::Milli(1500)).ToDouble(), -4.5, 1e-9));
	DOT_CHECK(Near((fx::Int(10) / fx::Int(4)).ToDouble(), 2.5, 1e-9));
	DOT_CHECK(Near((fx::Int(-10) / fx::Int(4)).ToDouble(), -2.5, 1e-9));
	DOT_CHECK(Near((fx::Int(100000) / fx::Milli(1300)).ToDouble(), 100000.0 / 1.3, 1e-4));
	DOT_CHECK(Near(fx::Sqrt(fx::Int(2)).ToDouble(), std::sqrt(2.0), 1e-4));
	DOT_CHECK(Near(fx::Sqrt(fx::Int(67600)).ToDouble(), 260.0, 1e-3));
	DOT_CHECK(Near(fx::Sqrt(fx::Milli(250)).ToDouble(), 0.5, 1e-6));
	for (int Deg = -720; Deg <= 720; Deg += 15)
	{
		const double R = Deg * 3.14159265358979323846 / 180.0;
		DOT_CHECK(Near(fx::Sin(fx::DegToRad(fx::Int(Deg))).ToDouble(), std::sin(R), 2e-5));
		DOT_CHECK(Near(fx::Cos(fx::DegToRad(fx::Int(Deg))).ToDouble(), std::cos(R), 2e-5));
	}
	const FixVec2 Facing(Fix64::Zero(), Fix64::One());
	DOT_CHECK(Near(Facing.Right().x.ToDouble(), 1.0, 1e-9));
	const FixVec2 D = fx::DirFromDegrees(fx::Int(90));
	DOT_CHECK(Near(D.x.ToDouble(), 1.0, 1e-4) && Near(D.z.ToDouble(), 0.0, 1e-4));
	DOT_CHECK(Near(FixVec2::FromInts(3, 4).Length().ToDouble(), 5.0, 1e-6));
}

void TestRandomAndHash()
{
	Rng A(42);
	Rng B(42);
	Rng C(43);
	bool bSame = true;
	bool bDiffers = false;
	for (int I = 0; I < 1000; ++I)
	{
		const uint32_t VA = A.NextU32();
		bSame = bSame && VA == B.NextU32();
		bDiffers = bDiffers || VA != C.NextU32();
	}
	DOT_CHECK(bSame);
	DOT_CHECK(bDiffers);
	Rng R(7);
	for (int I = 0; I < 1000; ++I)
	{
		const Fix64 V = R.Next01();
		DOT_CHECK(V >= Fix64::Zero() && V < Fix64::One());
	}
	// xxHash64 reference values.
	DOT_CHECK(XXHash64("", 0, 0) == 0xEF46DB3751D8E999ull);
	DOT_CHECK(XXHash64("abc", 3, 0) == 0x44BC2CF5AD770999ull);
}

void TestJson()
{
	JsonValue J;
	std::string Error;
	DOT_CHECK(ParseJson("{ // comment\n \"a\": [1, 2.5, -3], \"b\": {\"c\": \"x\\ny\"}, \"big\": 9007199254740993, \"t\": true }", J, Error));
	DOT_CHECK(J.Find("a") && J.Find("a")->Size() == 3);
	DOT_CHECK(Near(J.Find("a")->items[1].AsDouble(), 2.5, 1e-12));
	DOT_CHECK(J.Find("b")->GetString("c") == "x\ny");
	DOT_CHECK(J.GetInt64("big") == 9007199254740993LL);
	DOT_CHECK(J.GetBool("t"));
	DOT_CHECK(!ParseJson("{\"a\": }", J, Error));
	JsonWriter W;
	W.BeginObject();
	W.Key("k");
	W.BeginArray();
	W.Int(1);
	W.String("q\"");
	W.EndArray();
	W.Key("n");
	W.Null();
	W.EndObject();
	DOT_CHECK(W.Result() == "{\"k\":[1,\"q\\\"\"],\"n\":null}");
	JsonValue Back;
	DOT_CHECK(ParseJson(W.Result(), Back, Error));
}

void TestContent()
{
	const SimContent& C = Content();
	DOT_CHECK(C.troops.size() == 14);
	DOT_CHECK(C.FindTroop("troop.skeleton") == 6);
	const TroopDef& Ballista = C.troops[static_cast<size_t>(C.FindTroop("troop.ballista"))];
	DOT_CHECK(Ballista.siege && Ballista.ranged && Ballista.cls == DefenderClass::Siege && Ballista.move == MoveClass::Siege);
	DOT_CHECK(Ballista.squadSize == 3 && Ballista.crew == 3 && Ballista.pierceTargets == 3 && Ballista.pinMaxStacks == 3);
	DOT_CHECK(Ballista.setupTicks == 2 * kTicksPerSecond && Ballista.poiseFactor > C.rules.poiseFactorTroops);
	DOT_CHECK(C.troops[static_cast<size_t>(C.FindTroop("troop.pikemen"))].frost);
	DOT_CHECK(C.troops[static_cast<size_t>(C.FindTroop("troop.goliaths"))].cleave);
	DOT_CHECK(C.troops[static_cast<size_t>(C.FindTroop("troop.goliaths"))].maxAttackers == 8);
	DOT_CHECK(C.troops[static_cast<size_t>(C.FindTroop("troop.panthers"))].poison);
	DOT_CHECK(C.troops[static_cast<size_t>(C.FindTroop("troop.panthers"))].IsCavalry());
	DOT_CHECK(C.troops[static_cast<size_t>(C.FindTroop("troop.grenadiers"))].firebomb);
	DOT_CHECK(C.troops[static_cast<size_t>(C.FindTroop("troop.chainbreakers"))].chains);
	DOT_CHECK(C.troops[static_cast<size_t>(C.FindTroop("troop.priests"))].support);
	DOT_CHECK(C.titans.size() == 12);
	DOT_CHECK(C.FindAbility("spell.meteor_strike") >= 0);
	const TroopDef& Spear = C.troops[static_cast<size_t>(C.FindTroop("troop.spearmen"))];
	DOT_CHECK(Near(C.counters.Get(Spear.counterRow, DefenderClass::Cavalry).ToDouble(), 2.0, 1e-9));
	DOT_CHECK(Spear.brace);
	DOT_CHECK(Spear.attackIntervalTicks == 39);
	DOT_CHECK(C.rules.stompCooldownTicks == 300);
	DOT_CHECK(C.rules.clashWindupTicks == 30);
	DOT_CHECK(!C.hash.empty());
}

void TestNavGrid()
{
	const SimContent& C = Content();
	NavGrid Nav;
	Nav.Build(C.maps[0]);
	DOT_CHECK(Nav.Width() == 120 && Nav.Depth() == 80);
	DOT_CHECK(!Nav.WalkableAt(MoveClass::Infantry, FixVec2::FromInts(20, 79))); // river
	DOT_CHECK(Nav.WalkableAt(MoveClass::Infantry, FixVec2::FromInts(50, 79))); // ford
	DOT_CHECK(Nav.WalkableAt(MoveClass::Infantry, FixVec2::FromInts(120, 79))); // bridge
	DOT_CHECK(Nav.WalkableAt(MoveClass::Titan, FixVec2::FromInts(20, 79))); // titans wade
	DOT_CHECK(Nav.HeightAt(FixVec2::FromInts(70, 52)) > fx::Int(5));
	std::vector<FixVec2> Path;
	DOT_CHECK(Nav.FindPath(MoveClass::Infantry, FixVec2::FromInts(20, 40), FixVec2::FromInts(20, 120), Path));
	DOT_CHECK(!Path.empty());
	FixVec2 From = FixVec2::FromInts(20, 40);
	bool bAllWalkable = true;
	for (const FixVec2& P : Path)
	{
		bAllWalkable = bAllWalkable && Nav.LineWalkable(MoveClass::Infantry, From, P);
		From = P;
	}
	DOT_CHECK(bAllWalkable);
	DOT_CHECK(fx::Dist(Path.back(), FixVec2::FromInts(20, 120)) < fx::Int(3));
}

void TestDeterminism()
{
	BattleSetup S = Scenario("scenarios/m0_sandbox.json");
	S.sides[0].aiProfile = "veteran";
	S.skipDeploy = true;
	BattleSimulation A(Content());
	BattleSimulation B(Content());
	std::string Error;
	DOT_CHECK(A.Init(S, Error));
	DOT_CHECK(B.Init(S, Error));
	RunToEnd(A);
	RunToEnd(B);
	DOT_CHECK(A.IsFinished() && B.IsFinished());
	DOT_CHECK(A.Result().finalHash == B.Result().finalHash);
	DOT_CHECK(A.HashLog() == B.HashLog());
	DOT_CHECK(A.Result().durationTicks <= S.timerSec * kTicksPerSecond);
	S.seed += 1;
	BattleSimulation Other(Content());
	DOT_CHECK(Other.Init(S, Error));
	RunToEnd(Other);
	DOT_CHECK(Other.Result().finalHash != A.Result().finalHash);
}

void TestInvariants()
{
	BattleSetup S = Scenario("scenarios/m0_sandbox.json");
	S.sides[0].aiProfile = "warlord";
	S.sides[1].aiProfile = "warlord";
	S.skipDeploy = true;
	S.seed = 99;
	BattleSimulation Sim(Content());
	std::string Error;
	DOT_CHECK(Sim.Init(S, Error));
	bool bInBounds = true;
	bool bHpValid = true;
	bool bCountsValid = true;
	const Fix64 W = Sim.Map().width;
	const Fix64 D = Sim.Map().depth;
	while (!Sim.IsFinished())
	{
		Sim.Step();
		for (const Soldier& Sol : Sim.Soldiers())
		{
			if (!Sol.Alive())
			{
				continue;
			}
			bInBounds = bInBounds && Sol.pos.x >= Fix64::Zero() && Sol.pos.z >= Fix64::Zero() && Sol.pos.x <= W && Sol.pos.z <= D;
			bHpValid = bHpValid && Sol.hp > Fix64::Zero() && Sol.hp <= Sol.maxHp;
		}
		for (const Squad& Sq : Sim.Squads())
		{
			int Alive = 0;
			for (const int32_t M : Sq.members)
			{
				Alive += Sim.Soldiers()[static_cast<size_t>(M)].Alive() ? 1 : 0;
			}
			bCountsValid = bCountsValid && Alive == Sq.aliveCount;
		}
		for (const Titan& T : Sim.Titans())
		{
			bHpValid = bHpValid && T.hp <= T.maxHp && T.rage <= fx::Int(100) && T.rage >= Fix64::Zero();
		}
	}
	DOT_CHECK(bInBounds);
	DOT_CHECK(bHpValid);
	DOT_CHECK(bCountsValid);
}

void TestCounters()
{
	// Spearmen holding a line should beat light cavalry most of the time.
	int SpearWins = 0;
	for (uint64_t Seed = 1; Seed <= 10; ++Seed)
	{
		BattleSetup S = Scenario("scenarios/m0_cavalry_test.json");
		S.seed = Seed;
		BattleSimulation Sim(Content());
		std::string Error;
		DOT_CHECK(Sim.Init(S, Error));
		RunToEnd(Sim);
		SpearWins += Sim.Result().outcome == BattleOutcome::Victory ? 1 : 0;
	}
	DOT_CHECK(SpearWins >= 7);
	std::printf("  spearmen vs outriders: %d/10 wins\n", SpearWins);

	// Outriders should beat archers.
	int CavWins = 0;
	for (uint64_t Seed = 1; Seed <= 10; ++Seed)
	{
		BattleSetup S = Scenario("scenarios/m0_cavalry_test.json");
		S.seed = Seed;
		S.sides[0].squads[0].troopId = "troop.outriders";
		S.sides[0].squads[0].stanceSet = false;
		S.sides[1].squads[0].troopId = "troop.archers";
		S.sides[1].squads[0].stanceSet = false;
		BattleSimulation Sim(Content());
		std::string Error;
		DOT_CHECK(Sim.Init(S, Error));
		RunToEnd(Sim);
		CavWins += Sim.Result().outcome == BattleOutcome::Victory ? 1 : 0;
	}
	DOT_CHECK(CavWins >= 7);
	std::printf("  outriders vs archers: %d/10 wins\n", CavWins);
}

void TestCommandsAndReplay()
{
	BattleSetup S = Scenario("scenarios/m0_sandbox.json"); // attacker is player-controlled, defender AI
	BattleSimulation Sim(Content());
	std::string Error;
	DOT_CHECK(Sim.Init(S, Error));
	DOT_CHECK(Sim.Phase() == BattlePhase::Deploy);

	std::vector<BattleCommand> Log;
	auto Make = [&](CommandType Type, int32_t Unit, FixVec2 Point, int32_t Target, int32_t Param)
	{
		BattleCommand C;
		C.side = Side::Attacker;
		C.type = Type;
		C.unitId = Unit;
		C.point = Point;
		C.targetId = Target;
		C.param = Param;
		return C;
	};
	const int32_t EnemyTitan = kTitanIdBase + 1;
	std::vector<BattleCommand> Batch;
	while (!Sim.IsFinished() && Sim.CurrentTick() < 40 * 60 * kTicksPerSecond)
	{
		Batch.clear();
		const int32_t T = Sim.CurrentTick();
		if (T == 2) Batch.push_back(Make(CommandType::DeployMove, 6, FixVec2::FromInts(30, 30), 0, 0));
		if (T == 3) Batch.push_back(Make(CommandType::StartBattle, 0, FixVec2(), 0, 0));
		if (T == 60)
		{
			BattleCommand P = Make(CommandType::Path, 6, FixVec2(), 0, 0);
			P.waypoints = {FixVec2::FromInts(50, 60), FixVec2::FromInts(52, 90), FixVec2::FromInts(80, 130)};
			Batch.push_back(P);
		}
		if (T == 61) Batch.push_back(Make(CommandType::Attack, 0, FixVec2(), EnemyTitan, 0));
		if (T == 62) Batch.push_back(Make(CommandType::Attack, kTitanIdBase, FixVec2(), EnemyTitan, 0));
		if (T == 300) Batch.push_back(Make(CommandType::CastSpell, 0, FixVec2::FromInts(120, 110), 0, 1));
		if (T == 420) Batch.push_back(Make(CommandType::SetFormation, 2, FixVec2(), 0, static_cast<int32_t>(Formation::Square)));
		if (T == 600) Batch.push_back(Make(CommandType::SetAuto, 0, FixVec2(), 0, 1));
		if (T == 601) Batch.push_back(Make(CommandType::SetAuto, kTitanIdBase, FixVec2(), 0, 1));
		// Clash input: tap exactly on the marker (Perfect).
		const ClashState& Clash = Sim.Clash();
		if (Clash.active && T == Clash.markerTick)
		{
			Batch.push_back(Make(CommandType::ClashInput, 0, FixVec2(), 0, 0));
		}
		for (BattleCommand& C : Batch)
		{
			C.tick = T;
			Log.push_back(C);
		}
		Sim.Step(Batch);
	}
	DOT_CHECK(Sim.IsFinished());

	const ReplayData Replay = MakeReplay(Sim, Log);
	const std::string Text = SerializeReplay(Replay);
	ReplayData Parsed;
	DOT_CHECK(ParseReplay(Text, Parsed, Error));
	DOT_CHECK(Parsed.commands.size() == Log.size());
	BattleResult Verified;
	const bool bOk = VerifyReplay(Content(), Parsed, Verified, Error);
	if (!bOk)
	{
		std::printf("  verify error: %s\n", Error.c_str());
	}
	DOT_CHECK(bOk);
	DOT_CHECK(Verified.finalHash == Sim.Result().finalHash);
	std::printf("  replay: %zu commands, %zu bytes, %d stars\n", Log.size(), Text.size(), Verified.stars);

	// A tampered command must be detected.
	ReplayData Tampered = Parsed;
	for (BattleCommand& C : Tampered.commands)
	{
		if (C.type == CommandType::CastSpell)
		{
			C.point = FixVec2::FromInts(60, 60);
		}
	}
	BattleResult Ignored;
	DOT_CHECK(!VerifyReplay(Content(), Tampered, Ignored, Error));
}

void TestClashGrades()
{
	BattleSetup S = Scenario("scenarios/m0_titan_duel.json");
	BattleSimulation Sim(Content());
	std::string Error;
	DOT_CHECK(Sim.Init(S, Error));
	std::vector<SimEvent> Events;
	int PerfectExchanges = 0;
	int Exchanges = 0;
	std::vector<BattleCommand> Batch;
	while (!Sim.IsFinished())
	{
		Batch.clear();
		if (Sim.CurrentTick() == 50)
		{
			BattleCommand C;
			C.type = CommandType::Attack;
			C.unitId = kTitanIdBase;
			C.targetId = kTitanIdBase + 1;
			Batch.push_back(C);
		}
		const ClashState& Clash = Sim.Clash();
		if (Clash.active && Sim.CurrentTick() == Clash.markerTick)
		{
			BattleCommand C;
			C.type = CommandType::ClashInput;
			C.param = 30; // 30 ms late: still Perfect
			Batch.push_back(C);
		}
		Sim.Step(Batch);
		Events.clear();
		Sim.DrainEvents(Events);
		for (const SimEvent& E : Events)
		{
			if (E.type == SimEventType::ClashExchange)
			{
				++Exchanges;
				PerfectExchanges += E.a == 3 ? 1 : 0;
			}
		}
	}
	DOT_CHECK(Exchanges > 0);
	DOT_CHECK(PerfectExchanges == Exchanges);
	std::printf("  clash: %d exchanges, %d perfect; result %d stars\n", Exchanges, PerfectExchanges, Sim.Result().stars);
}

void TestSurrender()
{
	BattleSetup S = Scenario("scenarios/m0_sandbox.json");
	S.skipDeploy = true;
	BattleSimulation Sim(Content());
	std::string Error;
	DOT_CHECK(Sim.Init(S, Error));
	for (int I = 0; I < 100; ++I)
	{
		Sim.Step();
	}
	BattleCommand C;
	C.type = CommandType::Surrender;
	Sim.Step({C});
	DOT_CHECK(Sim.IsFinished());
	DOT_CHECK(Sim.Result().outcome == BattleOutcome::Defeat);
	DOT_CHECK(Sim.Result().surrendered);
}

/** Without a defending titan the titan star comes with the first star, not for free at the start. */
void TestNoTitanStars()
{
	BattleSetup S = Scenario("scenarios/m0_sandbox.json");
	S.skipDeploy = true;
	S.sides[1].titan.present = false;
	BattleSimulation Sim(Content());
	std::string Error;
	DOT_CHECK(Sim.Init(S, Error));
	for (int I = 0; I < 60; ++I)
	{
		Sim.Step();
	}
	DOT_CHECK(!Sim.ObjectiveMet(0));
	DOT_CHECK(!Sim.ObjectiveMet(1));
	BattleCommand C;
	C.type = CommandType::Surrender;
	Sim.Step({C});
	DOT_CHECK(Sim.Result().stars == 0);
	DOT_CHECK(Sim.Result().outcome == BattleOutcome::Defeat);
}

/** Spell stock (charges), relic bonuses on titans and the Terror spell's morale hit. */
void TestSpellChargesRelicsMorale()
{
	BattleSetup S = Scenario("scenarios/m0_sandbox.json");
	S.skipDeploy = true;
	S.sides[0].spells = {"spell.terror"};
	S.sides[0].spellCharges = {1};
	S.sides[0].spellLevels = {3};
	BattleSimulation Sim(Content());
	std::string Error;
	DOT_CHECK(Sim.Init(S, Error));
	while (Sim.Phase() != BattlePhase::Battle && Sim.CurrentTick() < 20 * kTicksPerSecond)
	{
		Sim.Step();
	}
	for (int I = 0; I < 30 * kTicksPerSecond && Sim.GetSide(Side::Attacker).valor < fx::Int(4); ++I)
	{
		Sim.Step();
	}
	// Cast on the first enemy squad: its morale drops by 35 x level multiplier.
	const Squad* Target = nullptr;
	for (const Squad& Q : Sim.Squads())
	{
		if (Q.side == Side::Defender && Q.Active())
		{
			Target = &Q;
			break;
		}
	}
	DOT_CHECK(Target != nullptr);
	const int32_t TargetId = Target->id;
	const Fix64 Before = Target->morale;
	BattleCommand C;
	C.type = CommandType::CastSpell;
	C.side = Side::Attacker;
	C.param = 0;
	C.point = Target->centroid;
	Sim.Step({C});
	const Squad& After = Sim.Squads()[static_cast<size_t>(TargetId - 1)];
	DOT_CHECK(After.morale < Before - fx::Int(25));
	DOT_CHECK(Sim.GetSide(Side::Attacker).spellCharges[0] == 0);
	DOT_CHECK(Sim.GetSide(Side::Attacker).spellLevels[0] == 3);
	// With no charge left the spell cannot be cast again.
	for (int I = 0; I < 40 * kTicksPerSecond; ++I)
	{
		Sim.Step();
	}
	const Fix64 Valor = Sim.GetSide(Side::Attacker).valor;
	Sim.Step({C});
	DOT_CHECK(Sim.GetSide(Side::Attacker).valor >= Valor);

	// Relics: +20% health and +10% damage on the titan.
	BattleSetup Plain = Scenario("scenarios/m0_sandbox.json");
	BattleSetup Boosted = Plain;
	Boosted.sides[0].titan.hpPct = 20;
	Boosted.sides[0].titan.atkPct = 10;
	BattleSimulation A(Content());
	BattleSimulation B(Content());
	DOT_CHECK(A.Init(Plain, Error) && B.Init(Boosted, Error));
	const double Hp = A.Titans()[0].maxHp.ToDouble();
	DOT_CHECK(Near(B.Titans()[0].maxHp.ToDouble(), Hp * 1.2, Hp * 0.001));
	DOT_CHECK(Near(B.Titans()[0].atk.ToDouble(), A.Titans()[0].atk.ToDouble() * 1.1, A.Titans()[0].atk.ToDouble() * 0.001));
	// Complex relics: +6 points of critical chance, 15% armor piercing, and +8% health / +5% damage for the whole army.
	BattleSetup Rich = Plain;
	Rich.sides[0].titan.critPct = 6;
	Rich.sides[0].titan.piercePct = 15;
	Rich.sides[0].troopHpPct = 8;
	Rich.sides[0].troopAtkPct = 5;
	BattleSimulation Rc(Content());
	DOT_CHECK(Rc.Init(Rich, Error));
	DOT_CHECK(Rc.Titans()[0].critChance == A.Titans()[0].critChance + fx::Pct(6) && Rc.Titans()[0].pierce == fx::Pct(15) && A.Titans()[0].pierce == Fix64::Zero());
	DOT_CHECK(Near(Rc.Squads()[0].hp.ToDouble(), A.Squads()[0].hp.ToDouble() * 1.08, 0.01) && Near(Rc.Squads()[0].atk.ToDouble(), A.Squads()[0].atk.ToDouble() * 1.05, 0.01));
	DOT_CHECK(Rc.Squads().back().hp == A.Squads().back().hp); // the enemy army is untouched
	JsonWriter WR;
	WriteBattleSetup(WR, Rich);
	JsonValue JR;
	BattleSetup RichBack;
	DOT_CHECK(ParseJson(WR.Result(), JR, Error) && ParseBattleSetup(JR, RichBack, Error));
	DOT_CHECK(RichBack.sides[0].titan.critPct == 6 && RichBack.sides[0].titan.piercePct == 15 && RichBack.sides[0].troopHpPct == 8 && RichBack.sides[0].troopAtkPct == 5);
	// The same battle twice ends the same way (relic bonuses keep the simulation deterministic).
	Rich.sides[0].aiProfile = "veteran";
	Rich.sides[1].aiProfile = "veteran";
	BattleSimulation R1(Content());
	BattleSimulation R2(Content());
	DOT_CHECK(R1.Init(Rich, Error) && R2.Init(Rich, Error));
	for (int I = 0; I < 60 * kTicksPerSecond && R1.Phase() != BattlePhase::Finished; ++I)
	{
		R1.Step();
		R2.Step();
	}
	DOT_CHECK(R1.ComputeHash() == R2.ComputeHash());

	// Ascension (kingdom): an ascended titan may stand ten levels above the cap of 10 + 10 per star.
	BattleSetup High = Plain;
	High.sides[1].titan.level = 45; // Talos with three stars: cap 40
	BattleSetup Ascended = High;
	Ascended.sides[1].titan.ascended = true;
	BattleSimulation H(Content());
	BattleSimulation G(Content());
	DOT_CHECK(H.Init(High, Error) && G.Init(Ascended, Error));
	DOT_CHECK(H.Titans()[1].level == 40 && G.Titans()[1].level == 45 && G.Titans()[1].maxHp > H.Titans()[1].maxHp);
	// The flag travels with the setup in replays; plain titans write nothing new.
	JsonWriter W;
	WriteBattleSetup(W, Ascended);
	JsonValue J;
	BattleSetup Back;
	DOT_CHECK(ParseJson(W.Result(), J, Error) && ParseBattleSetup(J, Back, Error) && Back.sides[1].titan.ascended && !Back.sides[0].titan.ascended);
	JsonWriter WP;
	WriteBattleSetup(WP, Plain);
	DOT_CHECK(WP.Result().find("ascended") == std::string::npos);
}

/** The battlefields of forest, lake, mountain and ruins lands: they load and AI battles on them finish the same way twice. */
void TestLandMaps()
{
	for (const char* Id : {"map.forest_01", "map.lake_01", "map.mountain_01", "map.ruins_01"})
	{
		DOT_CHECK(Content().FindMap(Id) >= 0);
		BattleSetup S = Scenario("scenarios/m0_sandbox.json");
		S.mapId = Id;
		S.sides[0].aiProfile = "veteran";
		S.skipDeploy = true;
		BattleSimulation A(Content());
		BattleSimulation B(Content());
		std::string Error;
		DOT_CHECK(A.Init(S, Error) && B.Init(S, Error));
		RunToEnd(A);
		RunToEnd(B);
		DOT_CHECK(A.IsFinished() && A.Result().finalHash == B.Result().finalHash);
		std::printf("  %s: %s, %d star(s), %d s\n", Id, A.Result().outcome == BattleOutcome::Victory ? "attacker wins" : "defender holds", A.Result().stars,
			A.Result().durationTicks / kTicksPerSecond);
	}
	// The lake's ford crosses deep water: walkable for squads, unlike the lake around it.
	const MapDef& Lake = Content().maps[static_cast<size_t>(Content().FindMap("map.lake_01"))];
	NavGrid Nav;
	Nav.Build(Lake);
	DOT_CHECK(Nav.WalkableAt(MoveClass::Infantry, FixVec2(fx::Int(120), fx::Int(80))));
	DOT_CHECK(!Nav.WalkableAt(MoveClass::Infantry, FixVec2(fx::Int(100), fx::Int(80))));
}
/** Index of the fresh body with the most fresh bodies within Radius (-1 if none). */
int DensestCorpse(const BattleSimulation& Sim, Fix64 Radius, int32_t MaxAge)
{
	const std::vector<Soldier>& All = Sim.Soldiers();
	int Best = -1;
	int BestCount = 0;
	for (size_t I = 0; I < All.size(); ++I)
	{
		const Soldier& A = All[I];
		if (A.state != SoldierState::Dead || A.raised || Sim.CurrentTick() - A.deathTick > MaxAge)
		{
			continue;
		}
		int Count = 0;
		for (const Soldier& B : All)
		{
			Count += (B.state == SoldierState::Dead && !B.raised && Sim.CurrentTick() - B.deathTick <= MaxAge && fx::Dist(A.pos, B.pos) <= Radius) ? 1 : 0;
		}
		if (Count > BestCount)
		{
			BestCount = Count;
			Best = static_cast<int>(I);
		}
	}
	return Best;
}

void TestRaiseDead()
{
	BattleSetup S = Scenario("scenarios/m0_sandbox.json");
	S.skipDeploy = true;
	S.sides[0].spells = {"spell.raise_dead"};
	S.sides[0].spellCharges = {2};
	S.sides[0].spellLevels = {5};
	BattleSimulation Sim(Content());
	std::string Error;
	DOT_CHECK(Sim.Init(S, Error));
	const size_t ArmySquads = Sim.Squads().size();
	BattleCommand Auto;
	Auto.type = CommandType::SetAuto;
	Auto.side = Side::Attacker;
	Auto.unitId = 0;
	Auto.param = 1;
	while (Sim.Phase() != BattlePhase::Battle && Sim.CurrentTick() < 20 * kTicksPerSecond)
	{
		Sim.Step();
	}
	Sim.Step({Auto});
	const int32_t Age = 17 * kTicksPerSecond;
	int Corpse = -1;
	for (int I = 0; I < 120 * kTicksPerSecond && !Sim.IsFinished(); ++I)
	{
		Sim.Step();
		if (I % 15 == 0 && Sim.GetSide(Side::Attacker).valor >= fx::Int(5))
		{
			Corpse = DensestCorpse(Sim, fx::Int(9), Age);
			int Near9 = 0;
			if (Corpse >= 0)
			{
				for (const Soldier& B : Sim.Soldiers())
				{
					Near9 += (B.state == SoldierState::Dead && !B.raised && fx::Dist(B.pos, Sim.Soldiers()[static_cast<size_t>(Corpse)].pos) <= fx::Int(9)) ? 1 : 0;
				}
			}
			if (Near9 >= 8)
			{
				break;
			}
			Corpse = -1;
		}
	}
	DOT_CHECK(Corpse >= 0);
	if (Corpse < 0)
	{
		return;
	}
	const int64_t LostBefore = Sim.GetSide(Side::Attacker).destroyedPower;
	BattleCommand C;
	C.type = CommandType::CastSpell;
	C.side = Side::Attacker;
	C.param = 0;
	C.point = Sim.Soldiers()[static_cast<size_t>(Corpse)].pos;
	Sim.Step({C});
	DOT_CHECK(Sim.Squads().size() == ArmySquads + 1);
	if (Sim.Squads().size() != ArmySquads + 1)
	{
		return;
	}
	const Squad& Risen = Sim.Squads().back();
	DOT_CHECK(Risen.summoned && Risen.autoControl && Risen.side == Side::Attacker);
	DOT_CHECK(Risen.troop == Content().FindTroop("troop.skeleton"));
	DOT_CHECK(Risen.tier == 2); // spell level 5
	DOT_CHECK(Risen.aliveCount >= 8 && Risen.aliveCount <= 16); // 12 + 1 per level above the first
	DOT_CHECK(Sim.GetSide(Side::Attacker).spellCharges[0] == 1);
	int Raised = 0;
	for (const Soldier& B : Sim.Soldiers())
	{
		Raised += (B.state == SoldierState::Dead && B.raised) ? 1 : 0;
	}
	DOT_CHECK(Raised >= Risen.startCount);
	// The same bodies cannot rise twice: a second cast on the spot raises nothing new there.
	const int32_t RisenId = Risen.id;
	const int32_t Expire = Risen.expireTick;
	DOT_CHECK(Expire == Sim.CurrentTick() - 1 + 25 * kTicksPerSecond);
	// Skeletons are fearless and their deaths are not the army's losses.
	while (Sim.CurrentTick() <= Expire + 1 && !Sim.IsFinished())
	{
		Sim.Step();
		const Squad& Q = Sim.Squads()[static_cast<size_t>(RisenId - 1)];
		if (Q.Active())
		{
			DOT_CHECK(Q.morale == fx::Int(100));
		}
	}
	if (!Sim.IsFinished())
	{
		const Squad& Q = Sim.Squads()[static_cast<size_t>(RisenId - 1)];
		DOT_CHECK(!Q.Active() && Q.aliveCount == 0);
	}
	(void)LostBefore;
	RunToEnd(Sim);
	const BattleResult& R = Sim.Result();
	int Army = 0;
	for (size_t I = 0; I < ArmySquads; ++I)
	{
		Army += Sim.Squads()[I].side == Side::Attacker ? Sim.Squads()[I].startCount : 0;
	}
	DOT_CHECK(R.attackerSurvivors + R.attackerLost == Army);

	// AI commanders cast it too, and the battle stays deterministic with squads added mid-fight.
	BattleSetup Ai = Scenario("scenarios/m0_sandbox.json");
	Ai.skipDeploy = true;
	Ai.sides[0].aiProfile = "warlord";
	Ai.sides[1].aiProfile = "warlord";
	Ai.sides[0].spells = {"spell.raise_dead"};
	Ai.sides[1].spells = {"spell.raise_dead"};
	BattleSimulation A(Content());
	BattleSimulation B(Content());
	DOT_CHECK(A.Init(Ai, Error) && B.Init(Ai, Error));
	int RaisedEvents = 0;
	std::vector<SimEvent> Events;
	while (!A.IsFinished() && A.CurrentTick() < 10 * 60 * kTicksPerSecond)
	{
		A.Step();
		B.Step();
		Events.clear();
		A.DrainEvents(Events);
		for (const SimEvent& E : Events)
		{
			RaisedEvents += E.type == SimEventType::DeadRaised ? 1 : 0;
		}
	}
	RunToEnd(B);
	DOT_CHECK(RaisedEvents >= 1);
	DOT_CHECK(A.Result().finalHash == B.Result().finalHash);
	DOT_CHECK(A.HashLog() == B.HashLog());
}

SquadSetup MakeSquad(const char* Troop, int X, int Z, bool bFacingNorth, Stance St)
{
	SquadSetup Q;
	Q.troopId = Troop;
	Q.position = FixVec2(fx::Int(X), fx::Int(Z));
	Q.facing = FixVec2(Fix64::Zero(), bFacingNorth ? Fix64::One() : -Fix64::One());
	Q.stance = St;
	Q.stanceSet = true;
	return Q;
}

int CountEvents(const std::vector<SimEvent>& Ev, SimEventType Type)
{
	return static_cast<int>(std::count_if(Ev.begin(), Ev.end(), [Type](const SimEvent& E) { return E.type == Type; }));
}

void TestNewTroops()
{
	std::string Error;
	// Grenadiers: firebombs burst in a splash and leave burning ground.
	{
		BattleSetup S = Scenario("scenarios/m0_sandbox.json");
		S.skipDeploy = true;
		S.sides[0].titan.present = false;
		S.sides[1].titan.present = false;
		S.sides[1].aiProfile.clear();
		S.sides[0].squads = {MakeSquad("troop.grenadiers", 120, 60, true, Stance::Hold)};
		S.sides[1].squads = {MakeSquad("troop.militia", 120, 78, false, Stance::Hold)};
		BattleSimulation Sim(Content());
		DOT_CHECK(Sim.Init(S, Error));
		std::vector<SimEvent> Ev;
		bool bFire = false;
		for (int I = 0; I < 15 * kTicksPerSecond && !Sim.IsFinished(); ++I)
		{
			Sim.Step();
			Sim.DrainEvents(Ev);
			bFire = bFire || !Sim.FirePatches().empty();
		}
		DOT_CHECK(CountEvents(Ev, SimEventType::FirebombImpact) >= 12);
		DOT_CHECK(bFire);
		DOT_CHECK(Sim.Squads()[1].aliveCount < Sim.Squads()[1].startCount - 4);
	}
	// Chainbreakers: two squads bind a titan with five chains; the chains then break and the titan is immune for a while.
	{
		BattleSetup S = Scenario("scenarios/m0_sandbox.json");
		S.skipDeploy = true;
		S.sides[0].titan.present = false;
		S.sides[1].aiProfile.clear();
		S.sides[1].titan.position = FixVec2(fx::Int(120), fx::Int(100));
		S.sides[0].squads = {MakeSquad("troop.chainbreakers", 112, 90, true, Stance::Hold), MakeSquad("troop.chainbreakers", 128, 90, true, Stance::Hold)};
		S.sides[1].squads = {MakeSquad("troop.militia", 120, 140, false, Stance::Hold)};
		BattleSimulation Sim(Content());
		DOT_CHECK(Sim.Init(S, Error));
		std::vector<SimEvent> Ev;
		int MaxChains = 0;
		int32_t BoundTick = -1;
		for (int I = 0; I < 12 * kTicksPerSecond && !Sim.IsFinished(); ++I)
		{
			Sim.Step();
			Sim.DrainEvents(Ev);
			MaxChains = std::max(MaxChains, Sim.TitanChains(Sim.Titans()[0]));
			if (BoundTick < 0 && CountEvents(Ev, SimEventType::TitanBound) > 0)
			{
				BoundTick = Sim.CurrentTick();
				const Titan& T = Sim.Titans()[0];
				DOT_CHECK(T.state == TitanState::Staggered);
				DOT_CHECK(T.boundUntil > Sim.CurrentTick());
				DOT_CHECK(Sim.TitanChains(T) == 0);
			}
		}
		DOT_CHECK(MaxChains == 4);
		DOT_CHECK(BoundTick > 0);
		DOT_CHECK(CountEvents(Ev, SimEventType::TitanBound) == 1); // immune for 12 s after the bind
		DOT_CHECK(Sim.Titans()[0].chainImmuneUntil > Sim.CurrentTick());
	}
	// War Priests: never attack; their sanctuary armors and heals allies nearby.
	{
		BattleSetup S = Scenario("scenarios/m0_sandbox.json");
		S.skipDeploy = true;
		S.sides[0].titan.present = false;
		S.sides[1].titan.present = false;
		S.sides[0].squads = {MakeSquad("troop.militia", 120, 70, true, Stance::Aggressive), MakeSquad("troop.priests", 120, 62, true, Stance::Aggressive)};
		S.sides[1].squads = {MakeSquad("troop.militia", 120, 90, false, Stance::Aggressive)};
		BattleSimulation Sim(Content());
		DOT_CHECK(Sim.Init(S, Error));
		bool bWarded = false;
		for (int I = 0; I < 30 * kTicksPerSecond && !Sim.IsFinished(); ++I)
		{
			Sim.Step();
			bWarded = bWarded || Sim.SquadStat(Sim.Squads()[0], StatType::ArmorAdd) >= fx::Pct(10);
		}
		DOT_CHECK(bWarded);
		for (const int32_t M : Sim.Squads()[1].members)
		{
			DOT_CHECK(Sim.Soldiers()[static_cast<size_t>(M)].lastAttackTick < 0);
		}
		DOT_CHECK(Sim.Squads()[1].aliveCount > 0);
	}
}

/** Wins of side A in an AI-vs-AI fight over several seeds; each side lists its squads, the second one 12 m behind the first. */
int DuelWins(std::vector<const char*> A, std::vector<const char*> B, int Seeds)
{
	int Wins = 0;
	for (uint64_t Seed = 1; Seed <= static_cast<uint64_t>(Seeds); ++Seed)
	{
		BattleSetup S = Scenario("scenarios/m0_cavalry_test.json");
		S.seed = Seed;
		for (int Side = 0; Side < 2; ++Side)
		{
			const SquadSetup Base = S.sides[Side].squads[0];
			S.sides[Side].squads.clear();
			const std::vector<const char*>& List = Side == 0 ? A : B;
			for (size_t K = 0; K < List.size(); ++K)
			{
				SquadSetup Q = Base;
				Q.troopId = List[K];
				Q.stanceSet = false;
				Q.formation = Formation::Line;
				Q.position = Base.position + FixVec2(Fix64::Zero(), fx::Int(static_cast<int>(K) * (Side == 0 ? -12 : 12)));
				S.sides[Side].squads.push_back(Q);
			}
		}
		BattleSimulation Sim(Content());
		std::string Error;
		DOT_CHECK(Sim.Init(S, Error));
		RunToEnd(Sim);
		Wins += Sim.Result().outcome == BattleOutcome::Victory ? 1 : 0;
	}
	return Wins;
}

void TestCouncilTroops()
{
	std::string Error;
	const SimContent& C = Content();
	// Pikemen (ice): every hit chills the enemy squad, slowing its steps and blows; chills refresh, they do not stack.
	{
		BattleSetup S = Scenario("scenarios/m0_sandbox.json");
		S.skipDeploy = true;
		S.sides[0].titan.present = false;
		S.sides[1].titan.present = false;
		S.sides[0].squads = {MakeSquad("troop.pikemen", 120, 70, true, Stance::Aggressive)};
		S.sides[1].squads = {MakeSquad("troop.militia", 120, 86, false, Stance::Aggressive)};
		BattleSimulation Sim(Content());
		DOT_CHECK(Sim.Init(S, Error));
		Fix64 Slowest;
		for (int I = 0; I < 20 * kTicksPerSecond && !Sim.IsFinished(); ++I)
		{
			Sim.Step();
			Slowest = fx::Min(Slowest, Sim.SquadStat(Sim.Squads()[1], StatType::MoveSpeedPct));
		}
		DOT_CHECK(Slowest == -fx::Pct(20));
		DOT_CHECK(Sim.SquadStat(Sim.Squads()[0], StatType::MoveSpeedPct) == Fix64::Zero());
	}
	// Goliaths (earth): three giants; a club sweep hurts and topples the soldiers around its target, and up to
	// eight enemies can surround one giant.
	{
		BattleSetup S = Scenario("scenarios/m0_sandbox.json");
		S.skipDeploy = true;
		S.sides[0].titan.present = false;
		S.sides[1].titan.present = false;
		S.sides[0].squads = {MakeSquad("troop.goliaths", 120, 70, true, Stance::Hold)};
		S.sides[1].squads = {MakeSquad("troop.militia", 120, 84, false, Stance::Aggressive)};
		BattleSimulation Sim(Content());
		DOT_CHECK(Sim.Init(S, Error));
		DOT_CHECK(Sim.Squads()[0].startCount == 3);
		int MostDown = 0;
		int MostOnOneGiant = 0;
		for (int I = 0; I < 25 * kTicksPerSecond && !Sim.IsFinished(); ++I)
		{
			Sim.Step();
			int Down = 0;
			std::vector<int> OnGiant(Sim.Soldiers().size(), 0);
			for (const int32_t M : Sim.Squads()[1].members)
			{
				const Soldier& Sol = Sim.Soldiers()[static_cast<size_t>(M)];
				Down += Sol.Alive() && Sol.state == SoldierState::KnockedDown ? 1 : 0;
				if (Sol.Alive() && Sol.targetSoldier >= 0)
				{
					MostOnOneGiant = std::max(MostOnOneGiant, ++OnGiant[static_cast<size_t>(Sol.targetSoldier)]);
				}
			}
			MostDown = std::max(MostDown, Down);
		}
		DOT_CHECK(MostDown >= 3);
		DOT_CHECK(MostOnOneGiant > C.rules.maxAttackersPerSoldier);
		DOT_CHECK(MostOnOneGiant <= 8);
		DOT_CHECK(Sim.Squads()[1].aliveCount < Sim.Squads()[1].startCount - 8);
	}
	// Panthers (poison): bites leave venom that keeps hurting; War Priests' sanctuary washes it out.
	{
		BattleSetup S = Scenario("scenarios/m0_sandbox.json");
		S.skipDeploy = true;
		S.sides[0].titan.present = false;
		S.sides[1].titan.present = false;
		S.sides[0].squads = {MakeSquad("troop.panthers", 120, 60, true, Stance::Aggressive)};
		S.sides[1].squads = {MakeSquad("troop.archers", 120, 90, false, Stance::Hold)};
		BattleSimulation Sim(Content());
		DOT_CHECK(Sim.Init(S, Error));
		bool bPoisoned = false;
		for (int I = 0; I < 20 * kTicksPerSecond && !Sim.IsFinished(); ++I)
		{
			Sim.Step();
			for (const int32_t M : Sim.Squads()[1].members)
			{
				const Soldier& Sol = Sim.Soldiers()[static_cast<size_t>(M)];
				bPoisoned = bPoisoned || (Sol.Alive() && Sol.poisonTicks > 0 && Sol.poisonDps > Fix64::Zero());
			}
		}
		DOT_CHECK(bPoisoned);
	}
	// The original's counter chart: pikes beat panthers, panthers beat archers and grenadiers, goliaths beat militia
	// and fall to shooters (militia backed by archers win where two militia squads lose).
	const int PikesVsPanthers = DuelWins({"troop.pikemen"}, {"troop.panthers"}, 6);
	const int PanthersVsArchers = DuelWins({"troop.panthers"}, {"troop.archers"}, 6);
	const int PanthersVsGrenadiers = DuelWins({"troop.panthers"}, {"troop.grenadiers"}, 6);
	const int GoliathsVsMilitia = DuelWins({"troop.goliaths"}, {"troop.militia"}, 6);
	const int MilitiaPairVsGoliaths = DuelWins({"troop.militia", "troop.militia"}, {"troop.goliaths"}, 6);
	const int MilitiaArchersVsGoliaths = DuelWins({"troop.militia", "troop.archers"}, {"troop.goliaths"}, 6);
	std::printf("  pikemen vs panthers %d/6, panthers vs archers %d/6, panthers vs grenadiers %d/6, goliaths vs militia %d/6,\n"
		"  goliaths vs militia x2: militia %d/6, vs militia + archers: those %d/6\n",
		PikesVsPanthers, PanthersVsArchers, PanthersVsGrenadiers, GoliathsVsMilitia, MilitiaPairVsGoliaths, MilitiaArchersVsGoliaths);
	DOT_CHECK(PikesVsPanthers >= 5);
	DOT_CHECK(PanthersVsArchers >= 5);
	DOT_CHECK(PanthersVsGrenadiers >= 5);
	DOT_CHECK(GoliathsVsMilitia >= 5);
	DOT_CHECK(MilitiaPairVsGoliaths <= 1);
	DOT_CHECK(MilitiaArchersVsGoliaths >= 5);
}

void TestBallista()
{
	std::string Error;
	const SimContent& C = Content();
	const TroopDef& TD = C.troops[static_cast<size_t>(C.FindTroop("troop.ballista"))];
	// A bolt goes through up to three soldiers standing in its line; an engine fires only after standing still for its setup time;
	// a damaged engine has fewer crewmen and reloads slower.
	{
		BattleSetup S = Scenario("scenarios/m0_sandbox.json");
		S.skipDeploy = true;
		S.sides[0].titan.present = false;
		S.sides[1].titan.present = false;
		S.sides[1].aiProfile.clear();
		S.sides[0].aiProfile.clear();
		S.sides[0].squads = {MakeSquad("troop.ballista", 120, 40, true, Stance::Aggressive)};
		SquadSetup Column = MakeSquad("troop.militia", 120, 94, false, Stance::Hold);
		Column.formation = Formation::Column;
		S.sides[1].squads = {Column};
		BattleSimulation Sim(Content());
		DOT_CHECK(Sim.Init(S, Error));
		std::vector<SimEvent> Ev;
		std::vector<int32_t> LastMoved(Sim.Soldiers().size(), -1000);
		int MostPierced = 0;
		int Bolts = 0;
		bool bSetupKept = true;
		bool bReloadKept = true;
		for (int I = 0; I < 40 * kTicksPerSecond && !Sim.IsFinished(); ++I)
		{
			Sim.Step();
			Sim.DrainEvents(Ev);
			for (const int32_t M : Sim.Squads()[0].members)
			{
				const Soldier& E = Sim.Soldiers()[static_cast<size_t>(M)];
				if (E.state == SoldierState::Moving)
				{
					LastMoved[static_cast<size_t>(M)] = Sim.CurrentTick();
				}
				if (E.Alive() && E.lastAttackTick == Sim.CurrentTick())
				{
					bSetupKept = bSetupKept && Sim.CurrentTick() - LastMoved[static_cast<size_t>(M)] >= TD.setupTicks;
					bReloadKept = bReloadKept && E.attackCooldown >= Sim.Squads()[0].interval * TD.crew / Sim.CrewAlive(E);
				}
			}
		}
		for (const SimEvent& E : Ev)
		{
			if (E.type == SimEventType::BoltImpact)
			{
				++Bolts;
				MostPierced = std::max(MostPierced, E.b);
			}
		}
		std::printf("  ballista: %d bolts, at most %d soldiers in one line\n", Bolts, MostPierced);
		DOT_CHECK(Bolts >= 6);
		DOT_CHECK(MostPierced >= 2 && MostPierced <= TD.pierceTargets);
		DOT_CHECK(bSetupKept);
		DOT_CHECK(bReloadKept);
		// The engines walked into range first, so the column is hurt.
		DOT_CHECK(Sim.Squads()[1].aliveCount < Sim.Squads()[1].startCount);
	}
	// Bolts pin a titan (up to three pins, 10% slower each) and shake its poise far more than troops do; a titan's
	// sweeps and stomps never send an engine flying.
	{
		BattleSetup S = Scenario("scenarios/m0_sandbox.json");
		S.skipDeploy = true;
		S.sides[0].titan.present = false;
		S.sides[1].titan.position = FixVec2(fx::Int(120), fx::Int(110));
		S.sides[0].squads = {MakeSquad("troop.ballista", 104, 50, true, Stance::Hold), MakeSquad("troop.ballista", 136, 50, true, Stance::Hold)};
		S.sides[1].squads = {MakeSquad("troop.militia", 40, 150, false, Stance::Hold)};
		BattleSimulation Sim(Content());
		DOT_CHECK(Sim.Init(S, Error));
		int MostPins = 0;
		Fix64 Slowest;
		bool bAirborne = false;
		Fix64 Poise;
		for (int I = 0; I < 40 * kTicksPerSecond && !Sim.IsFinished(); ++I)
		{
			Sim.Step();
			const Titan& T = Sim.Titans()[0];
			MostPins = std::max(MostPins, Sim.TitanPins(T));
			Slowest = fx::Min(Slowest, Sim.TitanStat(T, StatType::MoveSpeedPct));
			Poise = fx::Max(Poise, T.poiseDamage);
			for (const Squad& Sq : Sim.Squads())
			{
				for (const int32_t M : Sq.members)
				{
					bAirborne = bAirborne || (Sq.side == Side::Attacker && Sim.Soldiers()[static_cast<size_t>(M)].state == SoldierState::Airborne);
				}
			}
		}
		std::printf("  ballista vs titan: %d pins at most, speed %.0f%%, poise damage up to %.0f\n", MostPins, Slowest.ToDouble() * 100, Poise.ToDouble());
		DOT_CHECK(MostPins == TD.pinMaxStacks);
		DOT_CHECK(Slowest <= -TD.pinMoveSlow * TD.pinMaxStacks);
		DOT_CHECK(!bAirborne);
	}
	// Cavalry is the answer to engines; a lone ballista crew loses to infantry that closes in, too.
	const int OutridersVsBallista = DuelWins({"troop.outriders"}, {"troop.ballista"}, 6);
	const int MilitiaVsBallista = DuelWins({"troop.militia"}, {"troop.ballista"}, 6);
	std::printf("  outriders vs ballista %d/6, militia vs ballista %d/6\n", OutridersVsBallista, MilitiaVsBallista);
	DOT_CHECK(OutridersVsBallista >= 5);
	DOT_CHECK(MilitiaVsBallista >= 4);
}

void TestTitanAbilities()
{
	// Every kingdom titan has two abilities of its own, and an AI-driven titan uses them in battle.
	const SimContent& C = Content();
	const char* const Roster[] = {"titan.minotaur", "titan.cyclops", "titan.perun", "titan.atlas", "titan.ymir", "titan.surtr", "titan.hecate", "titan.artemis",
		"titan.loki", "titan.tyr"};
	std::vector<int> Seen;
	for (const char* Id : Roster)
	{
		const int Def = C.FindTitan(Id);
		DOT_CHECK(Def >= 0);
		if (Def < 0)
		{
			continue;
		}
		const TitanDef& TD = C.titans[static_cast<size_t>(Def)];
		const std::string Prefix = std::string("ability.") + (Id + 6) + ".";
		for (int Slot = 0; Slot < 2; ++Slot)
		{
			DOT_CHECK(TD.abilities[Slot] >= 0);
			if (TD.abilities[Slot] < 0)
			{
				continue;
			}
			DOT_CHECK(C.abilities[static_cast<size_t>(TD.abilities[Slot])].id.compare(0, Prefix.size(), Prefix) == 0);
			DOT_CHECK(std::find(Seen.begin(), Seen.end(), TD.abilities[Slot]) == Seen.end());
			Seen.push_back(TD.abilities[Slot]);
		}

		BattleSetup S = Scenario("scenarios/m0_sandbox.json");
		S.skipDeploy = true;
		S.sides[0].aiProfile = "warlord";
		S.sides[0].titan.titanId = Id;
		S.sides[0].titan.level = 20;
		S.sides[0].titan.stars = 2;
		BattleSimulation Sim(C);
		std::string Error;
		DOT_CHECK(Sim.Init(S, Error));
		int Casts[2] = {0, 0};
		std::vector<SimEvent> Events;
		while (!Sim.IsFinished() && Sim.CurrentTick() < 10 * 60 * kTicksPerSecond)
		{
			Sim.Step();
			Events.clear();
			Sim.DrainEvents(Events);
			for (const SimEvent& E : Events)
			{
				if (E.type == SimEventType::AbilityCast && E.side == Side::Attacker && E.b >= 0 && E.b < 2)
				{
					++Casts[E.b];
				}
			}
		}
		std::printf("  %s: %d + %d casts\n", Id, Casts[0], Casts[1]);
		DOT_CHECK(Casts[0] + Casts[1] >= 1);
	}
}
} // namespace

int main()
{
	struct NamedTest
	{
		const char* name;
		void (*fn)();
	};
	const NamedTest Tests[] = {{"Fix64", TestFix64}, {"RandomAndHash", TestRandomAndHash}, {"Json", TestJson}, {"Content", TestContent},
		{"NavGrid", TestNavGrid}, {"Determinism", TestDeterminism}, {"Invariants", TestInvariants}, {"Counters", TestCounters},
		{"CommandsAndReplay", TestCommandsAndReplay}, {"ClashGrades", TestClashGrades}, {"Surrender", TestSurrender}, {"NoTitanStars", TestNoTitanStars}, {"SpellChargesRelicsMorale", TestSpellChargesRelicsMorale},
		{"RaiseDead", TestRaiseDead}, {"TitanAbilities", TestTitanAbilities},
		{"NewTroops", TestNewTroops}, {"CouncilTroops", TestCouncilTroops}, {"Ballista", TestBallista}, {"LandMaps", TestLandMaps}};
	for (const NamedTest& T : Tests)
	{
		const int Before = GFailures;
		std::printf("[ RUN  ] %s\n", T.name);
		T.fn();
		std::printf("[ %s ] %s\n", GFailures == Before ? " OK " : "FAIL", T.name);
	}
	std::printf("\n%d checks, %d failures\n", GChecks, GFailures);
	return GFailures == 0 ? 0 : 1;
}
