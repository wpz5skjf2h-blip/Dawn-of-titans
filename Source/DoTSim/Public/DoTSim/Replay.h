// Source/DoTSim/Public/DoTSim/Replay.h
// Scenario/setup parsing, command and replay serialization, replay verification (4.16).
#pragma once

#include "DoTSim/BattleSimulation.h"
#include "DoTSim/Json.h"

#include <string>
#include <vector>

namespace dot
{
/** Parses a scenario/setup object. Accepts designer units (pos, facingDeg) and exact raw values (posRaw, facingRaw). */
DOTSIM_API bool ParseBattleSetup(const JsonValue& J, BattleSetup& Out, std::string& Error);
/** Writes a setup with exact raw fixed-point values so it round-trips bit-for-bit. */
DOTSIM_API void WriteBattleSetup(JsonWriter& W, const BattleSetup& Setup);
DOTSIM_API void WriteCommand(JsonWriter& W, const BattleCommand& Cmd);
DOTSIM_API bool ParseCommand(const JsonValue& J, BattleCommand& Out);

struct ReplayData
{
	int formatVersion = 1;
	std::string contentHash;
	BattleSetup setup;
	std::vector<BattleCommand> commands;
	std::vector<std::pair<int32_t, uint64_t>> hashes;
	BattleResult result;
	bool hasResult = false;
};

DOTSIM_API std::string SerializeReplay(const ReplayData& Replay);
DOTSIM_API bool ParseReplay(const std::string& Text, ReplayData& Out, std::string& Error);

/** Builds replay data from a finished (or running) simulation plus the commands that were fed to it. */
DOTSIM_API ReplayData MakeReplay(const BattleSimulation& Sim, const std::vector<BattleCommand>& Commands);

/**
 * Re-simulates a replay from its setup and command log and checks every recorded state hash
 * (server verification). Returns false with Error on the first mismatch.
 */
DOTSIM_API bool VerifyReplay(const SimContent& Content, const ReplayData& Replay, BattleResult& OutResult, std::string& Error);

/** Feeds recorded commands to a simulation tick by tick (playback / verification helper). */
class DOTSIM_API CommandPlayback
{
public:
	explicit CommandPlayback(const std::vector<BattleCommand>& InCommands) : Commands(InCommands) {}
	/** Returns the commands recorded for the given tick. */
	void CommandsForTick(int32_t Tick, std::vector<BattleCommand>& Out);
	bool Exhausted() const { return Next >= Commands.size(); }

private:
	const std::vector<BattleCommand>& Commands;
	size_t Next = 0;
};
} // namespace dot
