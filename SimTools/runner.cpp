// SimTools/runner.cpp
// Headless battle runner: simulate a scenario, print the result, write or verify replays,
// and print determinism reports (one hash per seed) for cross-platform CI comparison.
//
//   dot_runner --scenario scenarios/m0_sandbox.json --attacker-ai veteran
//   dot_runner --scenario scenarios/m0_sandbox.json --replay-out sandbox.replay.json
//   dot_runner --verify sandbox.replay.json
//   dot_runner --scenario scenarios/m0_sandbox.json --determinism 100
#include "ToolCommon.h"

#include "DoTSim/BattleSimulation.h"
#include "DoTSim/Hash.h"
#include "DoTSim/Replay.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>

namespace
{
struct Options
{
	std::string contentDir = dottools::DefaultContentDir();
	std::string scenario = "scenarios/m0_sandbox.json";
	std::string attackerAi;
	std::string defenderAi;
	std::string replayOut;
	std::string verify;
	long long seed = -1;
	int determinism = 0;
	bool quiet = false;
};

void PrintUsage()
{
	std::printf(
		"dot_runner [--content DIR] [--scenario FILE] [--attacker-ai ID] [--defender-ai ID] [--seed N]\n"
		"           [--replay-out FILE] [--verify FILE] [--determinism N] [--quiet]\n");
}

bool ParseArgs(int Argc, char** Argv, Options& O)
{
	for (int I = 1; I < Argc; ++I)
	{
		const std::string A = Argv[I];
		auto Next = [&](std::string& Out)
		{
			if (I + 1 >= Argc)
			{
				return false;
			}
			Out = Argv[++I];
			return true;
		};
		std::string V;
		if (A == "--content" && Next(V)) O.contentDir = V;
		else if (A == "--scenario" && Next(V)) O.scenario = V;
		else if (A == "--attacker-ai" && Next(V)) O.attackerAi = V;
		else if (A == "--defender-ai" && Next(V)) O.defenderAi = V;
		else if (A == "--replay-out" && Next(V)) O.replayOut = V;
		else if (A == "--verify" && Next(V)) O.verify = V;
		else if (A == "--seed" && Next(V)) O.seed = std::atoll(V.c_str());
		else if (A == "--determinism" && Next(V)) O.determinism = std::atoi(V.c_str());
		else if (A == "--quiet") O.quiet = true;
		else
		{
			PrintUsage();
			return false;
		}
	}
	return true;
}

const char* EventName(dot::SimEventType T)
{
	static const char* const Names[] = {"SoldierDied", "TitanAttack", "TitanSmash", "TitanStomp", "TitanGrab", "TitanThrowImpact", "TitanFootstep",
		"TitanChargeStart", "TitanStaggered", "TitanDied", "AbilityCast", "AbilityImpact", "SpellCast", "SpellImpact", "ClashStarted", "ClashExchange",
		"ClashEnded", "SquadRouted", "SquadRallied", "SquadLeftField", "SquadDestroyed", "ChargeImpact", "StarEarned", "BattleStarted", "BattleEnded", "DeadRaised",
		"FirebombImpact", "ChainThrown", "TitanBound", "BoltImpact"};
	const int I = static_cast<int>(T);
	return I >= 0 && I < static_cast<int>(dot::SimEventType::Count) ? Names[I] : "?";
}

/** Runs a battle to completion with no player commands. Returns ticks per second of simulation. */
double RunToEnd(dot::BattleSimulation& Sim, std::map<std::string, int>* EventCounts)
{
	const auto Start = std::chrono::steady_clock::now();
	std::vector<dot::SimEvent> Events;
	int32_t Ticks = 0;
	while (!Sim.IsFinished() && Sim.CurrentTick() < 60 * 60 * dot::kTicksPerSecond)
	{
		Sim.Step();
		++Ticks;
		if (EventCounts)
		{
			Events.clear();
			Sim.DrainEvents(Events);
			for (const dot::SimEvent& E : Events)
			{
				++(*EventCounts)[EventName(E.type)];
			}
		}
	}
	const double Seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - Start).count();
	return Seconds > 0 ? Ticks / Seconds : 0.0;
}

void PrintResult(const dot::BattleSimulation& Sim)
{
	const dot::BattleResult& R = Sim.Result();
	std::printf("Outcome:        %s, %d star(s) [%s %s %s]\n", R.outcome == dot::BattleOutcome::Victory ? "ATTACKER VICTORY" : "ATTACKER DEFEAT", R.stars,
		R.objectives[0] ? "50%" : "---", R.objectives[1] ? "TITAN" : "-----", R.objectives[2] ? "ALL" : "---");
	std::printf("Duration:       %.1f s\n", R.durationTicks / static_cast<double>(dot::kTicksPerSecond));
	std::printf("Destruction:    %d%% of defender power\n", R.destructionPct);
	std::printf("Soldiers:       attacker %d alive / %d dead, defender %d alive / %d dead\n", R.attackerSurvivors, R.attackerLost, R.defenderSurvivors,
		R.defenderLost);
	for (const dot::Titan& T : Sim.Titans())
	{
		std::printf("Titan %-8s    %s HP %.0f / %.0f\n", Sim.TitanDefOf(T).name.c_str(), T.dead ? "DEFEATED," : "alive,   ", T.hp.ToDouble(), T.maxHp.ToDouble());
	}
	std::printf("Battle score:   %lld\n", static_cast<long long>(R.battleScore));
	std::printf("Final hash:     %s\n", dot::HashToHex(R.finalHash).c_str());
}
} // namespace

int main(int Argc, char** Argv)
{
	Options O;
	if (!ParseArgs(Argc, Argv, O))
	{
		return 2;
	}
	dot::SimContent Content;
	std::string Error;
	if (!dottools::LoadContentDir(O.contentDir, Content, Error))
	{
		std::fprintf(stderr, "Content error: %s\n", Error.c_str());
		return 1;
	}

	if (!O.verify.empty())
	{
		std::string Text;
		dot::ReplayData Replay;
		if (!dottools::ReadFileText(O.verify, Text) || !dot::ParseReplay(Text, Replay, Error))
		{
			std::fprintf(stderr, "Replay error: %s\n", Error.empty() ? "cannot read file" : Error.c_str());
			return 1;
		}
		dot::BattleResult Result;
		if (!dot::VerifyReplay(Content, Replay, Result, Error))
		{
			std::printf("VERIFY FAILED: %s\n", Error.c_str());
			return 3;
		}
		std::printf("VERIFY OK: %d star(s), final hash %s, %zu commands, %zu hashes checked\n", Result.stars, dot::HashToHex(Result.finalHash).c_str(),
			Replay.commands.size(), Replay.hashes.size());
		return 0;
	}

	dot::BattleSetup Setup;
	if (!dottools::LoadScenario(O.contentDir, O.scenario, Setup, Error))
	{
		std::fprintf(stderr, "Scenario error: %s\n", Error.c_str());
		return 1;
	}
	// Headless: both sides need a controller, and nobody presses "Fight".
	Setup.sides[0].aiProfile = !O.attackerAi.empty() ? O.attackerAi : (Setup.sides[0].aiProfile.empty() ? "veteran" : Setup.sides[0].aiProfile);
	if (!O.defenderAi.empty())
	{
		Setup.sides[1].aiProfile = O.defenderAi;
	}
	if (Setup.sides[1].aiProfile.empty())
	{
		Setup.sides[1].aiProfile = "veteran";
	}
	Setup.skipDeploy = true;
	Setup.contentHash = Content.hash;
	if (O.seed >= 0)
	{
		Setup.seed = static_cast<uint64_t>(O.seed);
	}

	if (O.determinism > 0)
	{
		// One line per seed: "<seed> <final hash> <stars> <duration ticks>". CI diffs this across platforms.
		for (int I = 0; I < O.determinism; ++I)
		{
			dot::BattleSetup S = Setup;
			S.seed = Setup.seed + static_cast<uint64_t>(I);
			dot::BattleSimulation Sim(Content);
			if (!Sim.Init(S, Error))
			{
				std::fprintf(stderr, "Init error: %s\n", Error.c_str());
				return 1;
			}
			RunToEnd(Sim, nullptr);
			std::printf("%llu %s %d %d\n", static_cast<unsigned long long>(S.seed), dot::HashToHex(Sim.Result().finalHash).c_str(), Sim.Result().stars,
				Sim.Result().durationTicks);
		}
		return 0;
	}

	dot::BattleSimulation Sim(Content);
	if (!Sim.Init(Setup, Error))
	{
		std::fprintf(stderr, "Init error: %s\n", Error.c_str());
		return 1;
	}
	std::map<std::string, int> Counts;
	const double TicksPerSecond = RunToEnd(Sim, &Counts);
	if (!O.quiet)
	{
		std::printf("Scenario:       %s\n", Setup.name.c_str());
		std::printf("Content:        %s (hash %s)\n", Content.version.c_str(), Content.hash.c_str());
		std::printf("AI:             attacker=%s defender=%s seed=%llu\n", Setup.sides[0].aiProfile.c_str(), Setup.sides[1].aiProfile.c_str(),
			static_cast<unsigned long long>(Setup.seed));
		PrintResult(Sim);
		std::printf("Sim speed:      %.0f ticks/s (%.0fx real time)\n", TicksPerSecond, TicksPerSecond / dot::kTicksPerSecond);
		std::printf("Events:        ");
		for (const auto& KV : Counts)
		{
			if (KV.first != "SoldierDied" && KV.first != "TitanFootstep" && KV.first != "TitanAttack")
			{
				std::printf(" %s=%d", KV.first.c_str(), KV.second);
			}
		}
		std::printf("\n");
	}
	else
	{
		std::printf("%s stars=%d hash=%s\n", Sim.Result().outcome == dot::BattleOutcome::Victory ? "VICTORY" : "DEFEAT", Sim.Result().stars,
			dot::HashToHex(Sim.Result().finalHash).c_str());
	}
	if (!O.replayOut.empty())
	{
		const dot::ReplayData Replay = dot::MakeReplay(Sim, std::vector<dot::BattleCommand>());
		if (!dottools::WriteFileText(O.replayOut, dot::SerializeReplay(Replay)))
		{
			std::fprintf(stderr, "Cannot write %s\n", O.replayOut.c_str());
			return 1;
		}
		std::printf("Replay written: %s\n", O.replayOut.c_str());
	}
	return 0;
}
