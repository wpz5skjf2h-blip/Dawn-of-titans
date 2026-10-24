// Source/DoTSim/Private/Replay.cpp
#include "DoTSim/Replay.h"

#include "DoTSim/Hash.h"

#include <cstdlib>

namespace dot
{
namespace
{
bool ReadVec(const JsonValue& J, const char* DesignKey, const char* RawKey, FixVec2& Out)
{
	if (const JsonValue* Raw = J.Find(RawKey))
	{
		if (Raw->Size() >= 2)
		{
			Out = FixVec2(Fix64::FromRaw((*Raw)[0].AsInt64()), Fix64::FromRaw((*Raw)[1].AsInt64()));
			return true;
		}
	}
	if (const JsonValue* V = J.Find(DesignKey))
	{
		if (V->Size() >= 2)
		{
			Out = FixVec2((*V)[0].AsFix(), (*V)[1].AsFix());
			return true;
		}
	}
	return false;
}

FixVec2 ReadFacing(const JsonValue& J, FixVec2 Default)
{
	if (const JsonValue* Raw = J.Find("facingRaw"))
	{
		if (Raw->Size() >= 2)
		{
			return FixVec2(Fix64::FromRaw((*Raw)[0].AsInt64()), Fix64::FromRaw((*Raw)[1].AsInt64()));
		}
	}
	if (const JsonValue* Deg = J.Find("facingDeg"))
	{
		return fx::DirFromDegrees(Deg->AsFix());
	}
	return Default;
}

void WriteVecRaw(JsonWriter& W, const char* Key, FixVec2 V)
{
	W.Key(Key);
	W.BeginArray();
	W.Int(V.x.raw);
	W.Int(V.z.raw);
	W.EndArray();
}

bool ParseSide(const JsonValue& J, SideSetup& Out, bool bAttacker, std::string& Error)
{
	Out = SideSetup();
	Out.name = J.GetString("name");
	Out.aiProfile = J.GetString("ai");
	Out.autoClash = J.GetBool("autoClash", false);
	Out.spellLevel = J.GetInt("spellLevel", 1);
	const FixVec2 DefaultFacing(Fix64::Zero(), bAttacker ? Fix64::One() : -Fix64::One());
	if (const JsonValue* T = J.Find("titan"))
	{
		if (!T->IsNull())
		{
			Out.titan.present = true;
			Out.titan.titanId = T->GetString("id");
			Out.titan.level = T->GetInt("level", 1);
			Out.titan.stars = T->GetInt("stars", 1);
			if (const JsonValue* AL = T->Find("abilityLevels"))
			{
				for (size_t I = 0; I < AL->Size() && I < 2; ++I)
				{
					Out.titan.abilityLevels[I] = (*AL)[I].AsInt(1);
				}
			}
			if (const JsonValue* Bonus = T->Find("bonus"))
			{
				Out.titan.hpPct = Bonus->GetInt("hpPct", 0);
				Out.titan.atkPct = Bonus->GetInt("atkPct", 0);
				Out.titan.armorPct = Bonus->GetInt("armorPct", 0);
			}
			if (!ReadVec(*T, "pos", "posRaw", Out.titan.position))
			{
				Error = "titan needs pos";
				return false;
			}
			Out.titan.facing = ReadFacing(*T, DefaultFacing);
		}
	}
	if (const JsonValue* Spells = J.Find("spells"))
	{
		for (size_t I = 0; I < Spells->Size(); ++I)
		{
			Out.spells.push_back((*Spells)[I].AsString());
		}
	}
	if (const JsonValue* Charges = J.Find("spellCharges"))
	{
		for (size_t I = 0; I < Charges->Size(); ++I)
		{
			Out.spellCharges.push_back((*Charges)[I].AsInt(0));
		}
	}
	if (const JsonValue* Levels = J.Find("spellLevels"))
	{
		for (size_t I = 0; I < Levels->Size(); ++I)
		{
			Out.spellLevels.push_back((*Levels)[I].AsInt(1));
		}
	}
	if (const JsonValue* Squads = J.Find("squads"))
	{
		for (size_t I = 0; I < Squads->Size(); ++I)
		{
			const JsonValue& Q = (*Squads)[I];
			SquadSetup S;
			S.troopId = Q.GetString("troop");
			S.tier = Q.GetInt("tier", 1);
			S.count = Q.GetInt("count", 0);
			if (!ReadVec(Q, "pos", "posRaw", S.position))
			{
				Error = "squad '" + S.troopId + "' needs pos";
				return false;
			}
			S.facing = ReadFacing(Q, DefaultFacing);
			ParseFormation(Q.GetString("formation", "Line"), S.formation);
			if (Q.Has("stance"))
			{
				S.stanceSet = ParseStance(Q.GetString("stance"), S.stance);
			}
			Out.squads.push_back(S);
		}
	}
	return true;
}

void WriteSide(JsonWriter& W, const SideSetup& S)
{
	W.BeginObject();
	W.Key("name");
	W.String(S.name);
	W.Key("ai");
	W.String(S.aiProfile);
	W.Key("autoClash");
	W.Bool(S.autoClash);
	W.Key("spellLevel");
	W.Int(S.spellLevel);
	W.Key("titan");
	if (S.titan.present)
	{
		W.BeginObject();
		W.Key("id");
		W.String(S.titan.titanId);
		W.Key("level");
		W.Int(S.titan.level);
		W.Key("stars");
		W.Int(S.titan.stars);
		W.Key("abilityLevels");
		W.BeginArray();
		W.Int(S.titan.abilityLevels[0]);
		W.Int(S.titan.abilityLevels[1]);
		W.EndArray();
		if (S.titan.hpPct || S.titan.atkPct || S.titan.armorPct)
		{
			W.Key("bonus");
			W.BeginObject();
			W.Key("hpPct");
			W.Int(S.titan.hpPct);
			W.Key("atkPct");
			W.Int(S.titan.atkPct);
			W.Key("armorPct");
			W.Int(S.titan.armorPct);
			W.EndObject();
		}
		WriteVecRaw(W, "posRaw", S.titan.position);
		WriteVecRaw(W, "facingRaw", S.titan.facing);
		W.EndObject();
	}
	else
	{
		W.Null();
	}
	W.Key("spells");
	W.BeginArray();
	for (const std::string& Sp : S.spells)
	{
		W.String(Sp);
	}
	W.EndArray();
	if (!S.spellCharges.empty())
	{
		W.Key("spellCharges");
		W.BeginArray();
		for (const int C : S.spellCharges)
		{
			W.Int(C);
		}
		W.EndArray();
	}
	if (!S.spellLevels.empty())
	{
		W.Key("spellLevels");
		W.BeginArray();
		for (const int L : S.spellLevels)
		{
			W.Int(L);
		}
		W.EndArray();
	}
	W.Key("squads");
	W.BeginArray();
	for (const SquadSetup& Q : S.squads)
	{
		W.BeginObject();
		W.Key("troop");
		W.String(Q.troopId);
		W.Key("tier");
		W.Int(Q.tier);
		W.Key("count");
		W.Int(Q.count);
		WriteVecRaw(W, "posRaw", Q.position);
		WriteVecRaw(W, "facingRaw", Q.facing);
		W.Key("formation");
		W.String(ToString(Q.formation));
		if (Q.stanceSet)
		{
			W.Key("stance");
			W.String(ToString(Q.stance));
		}
		W.EndObject();
	}
	W.EndArray();
	W.EndObject();
}
} // namespace

bool ParseBattleSetup(const JsonValue& J, BattleSetup& Out, std::string& Error)
{
	Out = BattleSetup();
	Out.formatVersion = J.GetInt("format", 1);
	Out.name = J.GetString("name");
	Out.contentHash = J.GetString("contentHash");
	Out.seed = static_cast<uint64_t>(J.GetInt64("seed", 1));
	Out.mapId = J.GetString("map");
	Out.timerSec = J.GetInt("timerSec", 180);
	Out.skipDeploy = J.GetBool("skipDeploy", false);
	const JsonValue* Sides = J.Find("sides");
	if (!Sides || !Sides->Find("attacker") || !Sides->Find("defender"))
	{
		Error = "setup needs sides.attacker and sides.defender";
		return false;
	}
	return ParseSide(*Sides->Find("attacker"), Out.sides[0], true, Error) && ParseSide(*Sides->Find("defender"), Out.sides[1], false, Error);
}

void WriteBattleSetup(JsonWriter& W, const BattleSetup& Setup)
{
	W.BeginObject();
	W.Key("format");
	W.Int(Setup.formatVersion);
	W.Key("name");
	W.String(Setup.name);
	W.Key("contentHash");
	W.String(Setup.contentHash);
	W.Key("seed");
	W.Int(static_cast<int64_t>(Setup.seed));
	W.Key("map");
	W.String(Setup.mapId);
	W.Key("timerSec");
	W.Int(Setup.timerSec);
	W.Key("skipDeploy");
	W.Bool(Setup.skipDeploy);
	W.Key("sides");
	W.BeginObject();
	W.Key("attacker");
	WriteSide(W, Setup.sides[0]);
	W.Key("defender");
	WriteSide(W, Setup.sides[1]);
	W.EndObject();
	W.EndObject();
}

void WriteCommand(JsonWriter& W, const BattleCommand& Cmd)
{
	W.BeginObject();
	W.Key("t");
	W.Int(Cmd.tick);
	W.Key("s");
	W.Int(static_cast<int>(Cmd.side));
	W.Key("type");
	W.String(ToString(Cmd.type));
	W.Key("u");
	W.Int(Cmd.unitId);
	WriteVecRaw(W, "p", Cmd.point);
	W.Key("tg");
	W.Int(Cmd.targetId);
	W.Key("pa");
	W.Int(Cmd.param);
	if (!Cmd.waypoints.empty())
	{
		W.Key("w");
		W.BeginArray();
		for (const FixVec2& P : Cmd.waypoints)
		{
			W.BeginArray();
			W.Int(P.x.raw);
			W.Int(P.z.raw);
			W.EndArray();
		}
		W.EndArray();
	}
	W.EndObject();
}

bool ParseCommand(const JsonValue& J, BattleCommand& Out)
{
	Out = BattleCommand();
	Out.tick = J.GetInt("t", 0);
	Out.side = J.GetInt("s", 0) == 0 ? Side::Attacker : Side::Defender;
	if (!ParseCommandType(J.GetString("type"), Out.type))
	{
		return false;
	}
	Out.unitId = J.GetInt("u", 0);
	if (const JsonValue* P = J.Find("p"))
	{
		if (P->Size() >= 2)
		{
			Out.point = FixVec2(Fix64::FromRaw((*P)[0].AsInt64()), Fix64::FromRaw((*P)[1].AsInt64()));
		}
	}
	Out.targetId = J.GetInt("tg", 0);
	Out.param = J.GetInt("pa", 0);
	if (const JsonValue* Wp = J.Find("w"))
	{
		for (size_t I = 0; I < Wp->Size(); ++I)
		{
			const JsonValue& P = (*Wp)[I];
			if (P.Size() >= 2)
			{
				Out.waypoints.emplace_back(Fix64::FromRaw(P[0].AsInt64()), Fix64::FromRaw(P[1].AsInt64()));
			}
		}
	}
	return true;
}

std::string SerializeReplay(const ReplayData& Replay)
{
	JsonWriter W;
	W.BeginObject();
	W.Key("replayFormat");
	W.Int(Replay.formatVersion);
	W.Key("contentHash");
	W.String(Replay.contentHash);
	W.Key("setup");
	WriteBattleSetup(W, Replay.setup);
	W.Key("commands");
	W.BeginArray();
	for (const BattleCommand& C : Replay.commands)
	{
		WriteCommand(W, C);
	}
	W.EndArray();
	W.Key("hashes");
	W.BeginArray();
	for (const auto& H : Replay.hashes)
	{
		W.BeginArray();
		W.Int(H.first);
		W.String(HashToHex(H.second));
		W.EndArray();
	}
	W.EndArray();
	if (Replay.hasResult)
	{
		W.Key("result");
		W.BeginObject();
		W.Key("stars");
		W.Int(Replay.result.stars);
		W.Key("victory");
		W.Bool(Replay.result.outcome == BattleOutcome::Victory);
		W.Key("durationTicks");
		W.Int(Replay.result.durationTicks);
		W.Key("destructionPct");
		W.Int(Replay.result.destructionPct);
		W.Key("battleScore");
		W.Int(Replay.result.battleScore);
		W.Key("finalHash");
		W.String(HashToHex(Replay.result.finalHash));
		W.EndObject();
	}
	W.EndObject();
	return W.Result();
}

bool ParseReplay(const std::string& Text, ReplayData& Out, std::string& Error)
{
	Out = ReplayData();
	JsonValue Root;
	if (!ParseJson(Text, Root, Error))
	{
		return false;
	}
	Out.formatVersion = Root.GetInt("replayFormat", 1);
	Out.contentHash = Root.GetString("contentHash");
	const JsonValue* Setup = Root.Find("setup");
	if (!Setup || !ParseBattleSetup(*Setup, Out.setup, Error))
	{
		if (Error.empty())
		{
			Error = "replay has no setup";
		}
		return false;
	}
	if (const JsonValue* Commands = Root.Find("commands"))
	{
		for (size_t I = 0; I < Commands->Size(); ++I)
		{
			BattleCommand C;
			if (!ParseCommand((*Commands)[I], C))
			{
				Error = "bad command #" + std::to_string(I);
				return false;
			}
			Out.commands.push_back(C);
		}
	}
	if (const JsonValue* Hashes = Root.Find("hashes"))
	{
		for (size_t I = 0; I < Hashes->Size(); ++I)
		{
			const JsonValue& H = (*Hashes)[I];
			if (H.Size() >= 2)
			{
				Out.hashes.emplace_back(H[0].AsInt(), static_cast<uint64_t>(std::strtoull(H[1].AsString().c_str(), nullptr, 16)));
			}
		}
	}
	if (const JsonValue* R = Root.Find("result"))
	{
		Out.hasResult = true;
		Out.result.stars = R->GetInt("stars");
		Out.result.outcome = R->GetBool("victory") ? BattleOutcome::Victory : BattleOutcome::Defeat;
		Out.result.durationTicks = R->GetInt("durationTicks");
		Out.result.destructionPct = R->GetInt("destructionPct");
		Out.result.battleScore = R->GetInt64("battleScore");
		Out.result.finalHash = static_cast<uint64_t>(std::strtoull(R->GetString("finalHash").c_str(), nullptr, 16));
	}
	return true;
}

ReplayData MakeReplay(const BattleSimulation& Sim, const std::vector<BattleCommand>& Commands)
{
	ReplayData R;
	R.contentHash = Sim.Content().hash;
	R.setup = Sim.Setup();
	R.setup.contentHash = Sim.Content().hash;
	R.commands = Commands;
	R.hashes = Sim.HashLog();
	R.hasResult = Sim.IsFinished();
	R.result = Sim.Result();
	return R;
}

void CommandPlayback::CommandsForTick(int32_t Tick, std::vector<BattleCommand>& Out)
{
	Out.clear();
	while (Next < Commands.size() && Commands[Next].tick < Tick)
	{
		++Next; // skip commands from ticks already simulated
	}
	while (Next < Commands.size() && Commands[Next].tick == Tick)
	{
		Out.push_back(Commands[Next++]);
	}
}

bool VerifyReplay(const SimContent& Content, const ReplayData& Replay, BattleResult& OutResult, std::string& Error)
{
	if (!Replay.contentHash.empty() && Replay.contentHash != Content.hash)
	{
		Error = "content hash mismatch: replay " + Replay.contentHash + ", loaded " + Content.hash;
		return false;
	}
	BattleSimulation Sim(Content);
	if (!Sim.Init(Replay.setup, Error))
	{
		return false;
	}
	CommandPlayback Playback(Replay.commands);
	std::vector<BattleCommand> Batch;
	const int32_t MaxTicks = (Replay.setup.timerSec + 1800) * kTicksPerSecond;
	while (!Sim.IsFinished() && Sim.CurrentTick() < MaxTicks)
	{
		Playback.CommandsForTick(Sim.CurrentTick(), Batch);
		Sim.Step(Batch);
		if (Replay.commands.empty() && Sim.Phase() == BattlePhase::Deploy)
		{
			Error = "replay never starts the battle";
			return false;
		}
	}
	const auto& Log = Sim.HashLog();
	size_t K = 0;
	for (const auto& Expected : Replay.hashes)
	{
		while (K < Log.size() && Log[K].first < Expected.first)
		{
			++K;
		}
		if (K >= Log.size() || Log[K].first != Expected.first || Log[K].second != Expected.second)
		{
			Error = "state hash mismatch at tick " + std::to_string(Expected.first);
			return false;
		}
	}
	OutResult = Sim.Result();
	if (Replay.hasResult && Replay.result.finalHash != 0 && Replay.result.finalHash != OutResult.finalHash)
	{
		Error = "final hash mismatch";
		return false;
	}
	return true;
}
} // namespace dot
