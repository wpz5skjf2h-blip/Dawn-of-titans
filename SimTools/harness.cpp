// SimTools/harness.cpp
// AI-vs-AI balance harness (master prompt 4.20): runs many battles with random armies and
// prints win/pick rates per troop type and titan as CSV.
//
//   dot_harness [--battles N] [--seed S] [--ai PROFILE] [--csv FILE]
//
// Titan round robin: every pair of titans meets N times from each side, both armies the same random squads,
// so only the titans differ. "--levels equal" puts every titan at level 30 with 2 stars (titan design);
// "--levels max" at its kingdom cap, stars = rarity, level 10 + 10 per star (R1 20 ... R4 50).
//
// "--only ID" keeps the pairs with that titan (tuning one titan without the whole round robin).
//
//   dot_harness --titans N [--levels equal|max] [--same-rarity 1] [--only ID] [--seed S] [--ai PROFILE] [--csv FILE]
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

/** Runs a battle to the end; the winner is the side that destroyed the larger share of the enemy's power. */
int RunToWinner(const dot::SimContent& Content, const dot::BattleSetup& S, int64_t* OutTicks)
{
	dot::BattleSimulation Sim(Content);
	std::string Error;
	if (!Sim.Init(S, Error))
	{
		std::fprintf(stderr, "Init error: %s\n", Error.c_str());
		return -1;
	}
	while (!Sim.IsFinished())
	{
		Sim.Step();
	}
	const auto& Att = Sim.GetSide(dot::Side::Attacker);
	const auto& Def = Sim.GetSide(dot::Side::Defender);
	const int64_t AttScore = Def.destroyedPower * 1000 / (Def.startPower > 0 ? Def.startPower : 1);
	const int64_t DefScore = Att.destroyedPower * 1000 / (Att.startPower > 0 ? Att.startPower : 1);
	if (OutTicks)
	{
		*OutTicks = Sim.Result().durationTicks;
	}
	return AttScore > DefScore ? 0 : 1;
}

int TitanRoundRobin(const dot::SimContent& Content, const dot::BattleSetup& Base, const std::vector<std::string>& Trainable, int PerPair,
	bool bMaxLevels, bool bSameRarity, const std::string& Only, uint64_t Seed, const std::string& Ai, const std::string& CsvPath)
{
	const size_t N = Content.titans.size();
	std::vector<Stats> All(N);
	std::vector<Stats> Same(N);
	std::vector<std::vector<Stats>> Pair(N, std::vector<Stats>(N));
	dot::Rng Picker(Seed);
	int Battles = 0;
	for (size_t A = 0; A < N; ++A)
	{
		for (size_t B = A + 1; B < N; ++B)
		{
			const bool bSame = Content.titans[A].rarity == Content.titans[B].rarity;
			if ((bSameRarity && !bSame) || (!Only.empty() && Content.titans[A].id != Only && Content.titans[B].id != Only))
			{
				continue;
			}
			for (int R = 0; R < PerPair; ++R)
			{
				std::vector<std::string> Army;
				for (size_t Q = 0; Q < Base.sides[0].squads.size(); ++Q)
				{
					Army.push_back(Trainable[static_cast<size_t>(Picker.RangeInt(0, static_cast<int>(Trainable.size()) - 1))]);
				}
				for (int Swap = 0; Swap < 2; ++Swap)
				{
					dot::BattleSetup S = Base;
					S.seed = Seed * 1000003ull + static_cast<uint64_t>(Battles++);
					S.skipDeploy = true;
					const size_t Id[2] = {Swap ? B : A, Swap ? A : B};
					for (int Side = 0; Side < 2; ++Side)
					{
						dot::SideSetup& SS = S.sides[Side];
						const dot::TitanDef& TD = Content.titans[Id[Side]];
						SS.aiProfile = Ai;
						SS.titan.titanId = TD.id;
						SS.titan.stars = bMaxLevels ? static_cast<int>(TD.rarity) + 1 : 2;
						SS.titan.level = bMaxLevels ? 10 + 10 * SS.titan.stars : 30;
						for (size_t Q = 0; Q < SS.squads.size(); ++Q)
						{
							SS.squads[Q].troopId = Army[Q % Army.size()];
							SS.squads[Q].tier = 2;
							SS.squads[Q].count = 0;
							SS.squads[Q].stanceSet = false;
						}
					}
					const int Winner = RunToWinner(Content, S, nullptr);
					if (Winner < 0)
					{
						return 1;
					}
					for (int Side = 0; Side < 2; ++Side)
					{
						const size_t Me = Id[Side];
						const bool bWon = Winner == Side;
						++All[Me].picks;
						All[Me].wins += bWon ? 1 : 0;
						++Pair[Me][Id[1 - Side]].picks;
						Pair[Me][Id[1 - Side]].wins += bWon ? 1 : 0;
						if (bSame)
						{
							++Same[Me].picks;
							Same[Me].wins += bWon ? 1 : 0;
						}
					}
				}
			}
		}
	}
	const auto Rate = [](const Stats& St) { return St.picks ? static_cast<double>(St.wins) / St.picks : -1.0; };
	std::string Csv = "kind,id,rarity,games,win_rate,same_rarity_games,same_rarity_win_rate\n";
	for (size_t I = 0; I < N; ++I)
	{
		char Line[256];
		std::snprintf(Line, sizeof(Line), "titan,%s,R%d,%d,%.3f,%d,%.3f\n", Content.titans[I].id.c_str(), static_cast<int>(Content.titans[I].rarity) + 1,
			All[I].picks, Rate(All[I]), Same[I].picks, Rate(Same[I]));
		Csv += Line;
	}
	// Head-to-head: the row titan's win rate against the column titan.
	Csv += "pair";
	for (size_t J = 0; J < N; ++J)
	{
		Csv += "," + Content.titans[J].id;
	}
	Csv += "\n";
	for (size_t I = 0; I < N; ++I)
	{
		Csv += Content.titans[I].id;
		for (size_t J = 0; J < N; ++J)
		{
			char Cell[32];
			std::snprintf(Cell, sizeof(Cell), ",%.2f", Rate(Pair[I][J]));
			Csv += Pair[I][J].picks ? Cell : ",";
		}
		Csv += "\n";
	}
	std::printf("%s", Csv.c_str());
	std::printf("# titan round robin: battles=%d per_pair=%d levels=%s same_rarity=%d ai=%s\n", Battles, PerPair, bMaxLevels ? "max" : "equal",
		bSameRarity ? 1 : 0, Ai.c_str());
	if (!CsvPath.empty())
	{
		dottools::WriteFileText(CsvPath, Csv);
	}
	return 0;
}
} // namespace

int main(int Argc, char** Argv)
{
	int Battles = 200;
	uint64_t Seed = 1;
	std::string Ai = "veteran";
	std::string CsvPath;
	int TitanPairs = 0;
	bool bMaxLevels = false;
	bool bSameRarity = false;
	std::string Only;
	for (int I = 1; I + 1 < Argc; I += 2)
	{
		const std::string A = Argv[I];
		if (A == "--battles") Battles = std::atoi(Argv[I + 1]);
		else if (A == "--seed") Seed = static_cast<uint64_t>(std::atoll(Argv[I + 1]));
		else if (A == "--ai") Ai = Argv[I + 1];
		else if (A == "--csv") CsvPath = Argv[I + 1];
		else if (A == "--titans") TitanPairs = std::atoi(Argv[I + 1]);
		else if (A == "--levels") bMaxLevels = std::string(Argv[I + 1]) == "max";
		else if (A == "--same-rarity") bSameRarity = std::atoi(Argv[I + 1]) != 0;
		else if (A == "--only") Only = Argv[I + 1];
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
	if (TitanPairs > 0)
	{
		return TitanRoundRobin(Content, Base, Trainable, TitanPairs, bMaxLevels, bSameRarity, Only, Seed, Ai, CsvPath);
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
		int64_t Ticks = 0;
		const int Winner = RunToWinner(Content, S, &Ticks);
		if (Winner < 0)
		{
			return 1;
		}
		AttackerWins += Winner == 0 ? 1 : 0;
		TotalTicks += Ticks;
		Timeouts += Ticks >= S.timerSec * dot::kTicksPerSecond ? 1 : 0;
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
