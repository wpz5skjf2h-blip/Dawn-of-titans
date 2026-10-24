// SimTools/harness.cpp
// AI-vs-AI balance harness (master prompt 4.20): runs many battles with random armies and
// prints win/pick rates per troop type and titan as CSV.
//
//   dot_harness [--battles N] [--seed S] [--ai PROFILE] [--csv FILE]
#include "ToolCommon.h"

#include "DoTSim/BattleSimulation.h"

#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>
#include <vector>

namespace
{
struct Stats
{
	int picks = 0;
	int wins = 0;
};
} // namespace

int main(int Argc, char** Argv)
{
	int Battles = 200;
	uint64_t Seed = 1;
	std::string Ai = "veteran";
	std::string CsvPath;
	for (int I = 1; I + 1 < Argc; I += 2)
	{
		const std::string A = Argv[I];
		if (A == "--battles") Battles = std::atoi(Argv[I + 1]);
		else if (A == "--seed") Seed = static_cast<uint64_t>(std::atoll(Argv[I + 1]));
		else if (A == "--ai") Ai = Argv[I + 1];
		else if (A == "--csv") CsvPath = Argv[I + 1];
	}

	dot::SimContent Content;
	std::string Error;
	if (!dottools::LoadContentDir(dottools::DefaultContentDir(), Content, Error))
	{
		std::fprintf(stderr, "Content error: %s\n", Error.c_str());
		return 1;
	}
	dot::BattleSetup Base;
	if (!dottools::LoadScenario(dottools::DefaultContentDir(), "scenarios/m0_sandbox.json", Base, Error))
	{
		std::fprintf(stderr, "Scenario error: %s\n", Error.c_str());
		return 1;
	}

	dot::Rng Picker(Seed);
	std::map<std::string, Stats> TroopStats;
	std::map<std::string, Stats> TitanStats;
	int AttackerWins = 0;
	int64_t TotalTicks = 0;
	int Timeouts = 0;
	// Raised-only troops (skeletons) are never part of an army.
	std::vector<std::string> Trainable;
	for (const dot::TroopDef& T : Content.troops)
	{
		if (T.id != "troop.skeleton")
		{
			Trainable.push_back(T.id);
		}
	}
	for (int B = 0; B < Battles; ++B)
	{
		dot::BattleSetup S = Base;
		S.seed = Seed * 1000003ull + static_cast<uint64_t>(B);
		S.skipDeploy = true;
		for (int Side = 0; Side < 2; ++Side)
		{
			dot::SideSetup& SS = S.sides[Side];
			SS.aiProfile = Ai;
			SS.titan.titanId = Content.titans[static_cast<size_t>(Picker.RangeInt(0, static_cast<int>(Content.titans.size()) - 1))].id;
			// Equal-power titan levels so the harness measures troop balance.
			SS.titan.level = SS.titan.titanId == "titan.thor" ? 10 : 36;
			SS.titan.stars = SS.titan.titanId == "titan.thor" ? 4 : 3;
			for (dot::SquadSetup& Q : SS.squads)
			{
				Q.troopId = Trainable[static_cast<size_t>(Picker.RangeInt(0, static_cast<int>(Trainable.size()) - 1))];
				Q.tier = 2;
				Q.count = 0;
				Q.stanceSet = false;
			}
		}
		dot::BattleSimulation Sim(Content);
		if (!Sim.Init(S, Error))
		{
			std::fprintf(stderr, "Init error: %s\n", Error.c_str());
			return 1;
		}
		while (!Sim.IsFinished())
		{
			Sim.Step();
		}
		// Winner = side that destroyed the larger share of the enemy's power.
		const auto& Att = Sim.GetSide(dot::Side::Attacker);
		const auto& Def = Sim.GetSide(dot::Side::Defender);
		const int64_t AttScore = Def.destroyedPower * 1000 / (Def.startPower > 0 ? Def.startPower : 1);
		const int64_t DefScore = Att.destroyedPower * 1000 / (Att.startPower > 0 ? Att.startPower : 1);
		const int Winner = AttScore > DefScore ? 0 : 1;
		AttackerWins += Winner == 0 ? 1 : 0;
		TotalTicks += Sim.Result().durationTicks;
		Timeouts += Sim.Result().durationTicks >= S.timerSec * dot::kTicksPerSecond ? 1 : 0;
		for (int Side = 0; Side < 2; ++Side)
		{
			Stats& TS = TitanStats[S.sides[Side].titan.titanId];
			++TS.picks;
			TS.wins += Winner == Side ? 1 : 0;
			for (const dot::SquadSetup& Q : S.sides[Side].squads)
			{
				Stats& St = TroopStats[Q.troopId];
				++St.picks;
				St.wins += Winner == Side ? 1 : 0;
			}
		}
	}

	std::string Csv = "kind,id,picks,wins,win_rate\n";
	for (const auto& KV : TroopStats)
	{
		char Line[256];
		std::snprintf(Line, sizeof(Line), "troop,%s,%d,%d,%.3f\n", KV.first.c_str(), KV.second.picks, KV.second.wins,
			KV.second.picks ? static_cast<double>(KV.second.wins) / KV.second.picks : 0.0);
		Csv += Line;
	}
	for (const auto& KV : TitanStats)
	{
		char Line[256];
		std::snprintf(Line, sizeof(Line), "titan,%s,%d,%d,%.3f\n", KV.first.c_str(), KV.second.picks, KV.second.wins,
			KV.second.picks ? static_cast<double>(KV.second.wins) / KV.second.picks : 0.0);
		Csv += Line;
	}
	std::printf("%s", Csv.c_str());
	std::printf("# battles=%d attacker_win_rate=%.3f avg_duration_s=%.1f timeouts=%d ai=%s\n", Battles, static_cast<double>(AttackerWins) / Battles,
		static_cast<double>(TotalTicks) / Battles / dot::kTicksPerSecond, Timeouts, Ai.c_str());
	if (!CsvPath.empty())
	{
		dottools::WriteFileText(CsvPath, Csv);
	}
	return 0;
}
