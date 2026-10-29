// Web/dot_web.cpp
// C API over the deterministic battle simulation for the browser demo (Emscripten).
// The simulation sources are exactly the ones the Unreal DoTSim module compiles; this file
// only adds a flat interface: content loading from memory, stepping, commands and
// per-frame snapshots (JSON for low-frequency data, float buffers for soldiers and titans).
// Doubles appear here only for presentation output and for converting player input,
// exactly like the Unreal game module does.
#include "DoTSim/BattleSimulation.h"
#include "DoTSim/Hash.h"
#include "DoTSim/Replay.h"
#include "DoTKingdom/Kingdom.h"

#include <emscripten/emscripten.h>

#include <cmath>
#include <cstdio>
#include <map>
#include <memory>
#include <cstdlib>
#include <string>
#include <vector>

namespace
{
class MemoryContentSource : public dot::IContentSource
{
public:
	std::map<std::string, std::string> Files;

	bool ReadText(const std::string& RelativePath, std::string& OutText) override
	{
		const auto It = Files.find(RelativePath);
		if (It == Files.end())
		{
			return false;
		}
		OutText = It->second;
		return true;
	}
};

struct WebState
{
	MemoryContentSource Source;
	dot::SimContent Content;
	bool bContentLoaded = false;
	dot::BattleSetup Setup;
	std::unique_ptr<dot::BattleSimulation> Sim;
	std::vector<dot::BattleCommand> Pending;
	std::vector<dot::BattleCommand> Recorded;
	std::vector<dot::BattleCommand> PlaybackSource;
	std::unique_ptr<dot::CommandPlayback> Playback;
	bool bReplaying = false;
	std::vector<dot::SimEvent> Events;
	std::vector<dot::SimEvent> StepEvents;
	int SoldierDeaths = 0;
	std::vector<float> SoldierBuffer;
	std::vector<float> TitanBuffer;
	std::string Error;
	std::string Text;
	// Kingdom economy (Source/DoTKingdom).
	dot::kingdom::KingdomContent KingdomContent;
	std::unique_ptr<dot::kingdom::Kingdom> Kingdom;
};

WebState& G()
{
	static WebState State;
	return State;
}

// --- Small JSON text builder (presentation output only) ---
class Out
{
public:
	explicit Out(std::string& InText) : S(InText) { S.clear(); }
	Out& Raw(const char* T) { S += T; return *this; }
	Out& Int(long long V)
	{
		char B[32];
		std::snprintf(B, sizeof(B), "%lld", V);
		S += B;
		return *this;
	}
	Out& Num(double V, int Decimals = 2)
	{
		if (!std::isfinite(V))
		{
			V = 0.0;
		}
		char B[48];
		std::snprintf(B, sizeof(B), "%.*f", Decimals, V);
		// Trim trailing zeros to keep the payload small.
		char* End = B + std::char_traits<char>::length(B);
		if (Decimals > 0)
		{
			while (End > B && End[-1] == '0')
			{
				--End;
			}
			if (End > B && End[-1] == '.')
			{
				--End;
			}
		}
		*End = 0;
		if (B[0] == '-' && B[1] == '0' && B[2] == 0)
		{
			S += "0";
		}
		else
		{
			S += B;
		}
		return *this;
	}
	Out& Fx(dot::Fix64 V, int Decimals = 2) { return Num(V.ToDouble(), Decimals); }
	Out& Str(const std::string& V)
	{
		S += '"';
		for (const char C : V)
		{
			if (C == '"' || C == '\\')
			{
				S += '\\';
				S += C;
			}
			else if (static_cast<unsigned char>(C) < 0x20)
			{
				S += ' ';
			}
			else
			{
				S += C;
			}
		}
		S += '"';
		return *this;
	}
	Out& Key(const char* K)
	{
		S += '"';
		S += K;
		S += "\":";
		return *this;
	}
	Out& Comma() { S += ','; return *this; }
	Out& Vec(dot::FixVec2 V) { S += '['; Fx(V.x); S += ','; Fx(V.z); S += ']'; return *this; }

private:
	std::string& S;
};

double Yaw(dot::FixVec2 Dir)
{
	return std::atan2(Dir.x.ToDouble(), Dir.z.ToDouble());
}

dot::FixVec2 ToFix(double X, double Z)
{
	return dot::FixVec2(dot::Fix64::FromDouble(X), dot::Fix64::FromDouble(Z));
}

bool StartFromSetup(const dot::BattleSetup& Setup, bool bReplay)
{
	WebState& S = G();
	auto NewSim = std::make_unique<dot::BattleSimulation>(S.Content);
	std::string Error;
	if (!NewSim->Init(Setup, Error))
	{
		S.Error = "Battle init error: " + Error;
		return false;
	}
	S.Sim = std::move(NewSim);
	S.Pending.clear();
	S.Events.clear();
	S.SoldierDeaths = 0;
	S.bReplaying = bReplay;
	if (bReplay)
	{
		S.Playback = std::make_unique<dot::CommandPlayback>(S.PlaybackSource);
	}
	else
	{
		S.Recorded.clear();
		S.Playback.reset();
	}
	return true;
}

const char* Finish(std::string& Text)
{
	return Text.c_str();
}
} // namespace

extern "C" {

EMSCRIPTEN_KEEPALIVE void dot_add_file(const char* Path, const char* Text)
{
	G().Source.Files[Path] = Text;
}

EMSCRIPTEN_KEEPALIVE int dot_load_content()
{
	WebState& S = G();
	std::string Error;
	S.bContentLoaded = dot::LoadContent(S.Source, S.Content, Error);
	if (!S.bContentLoaded)
	{
		S.Error = "Content error: " + Error;
	}
	return S.bContentLoaded ? 1 : 0;
}

EMSCRIPTEN_KEEPALIVE const char* dot_error()
{
	return G().Error.c_str();
}

/** Starts a battle. The attacker is the local player unless PlayerAi names an AI profile. */
EMSCRIPTEN_KEEPALIVE int dot_start(const char* ScenarioPath, const char* EnemyAi, int SkipDeploy, const char* PlayerAi)
{
	WebState& S = G();
	if (!S.bContentLoaded)
	{
		S.Error = "Content is not loaded";
		return 0;
	}
	std::string Text;
	if (!S.Source.ReadText(ScenarioPath, Text))
	{
		S.Error = std::string("Cannot read scenario ") + ScenarioPath;
		return 0;
	}
	dot::JsonValue Json;
	std::string Error;
	dot::BattleSetup Setup;
	if (!dot::ParseJson(Text, Json, Error) || !dot::ParseBattleSetup(Json, Setup, Error))
	{
		S.Error = "Scenario error: " + Error;
		return 0;
	}
	Setup.contentHash = S.Content.hash;
	if (SkipDeploy >= 0)
	{
		Setup.skipDeploy = SkipDeploy != 0;
	}
	if (EnemyAi && EnemyAi[0])
	{
		Setup.sides[1].aiProfile = EnemyAi;
	}
	// "@scenario" keeps the scenario's own attacker controller (AI-vs-AI showcase scenarios).
	if (!PlayerAi || std::string(PlayerAi) != "@scenario")
	{
		Setup.sides[0].aiProfile = PlayerAi ? PlayerAi : "";
	}
	S.Setup = Setup;
	return StartFromSetup(Setup, false) ? 1 : 0;
}

/** Restarts the current battle and feeds it the commands recorded so far (replay). */
EMSCRIPTEN_KEEPALIVE int dot_replay()
{
	WebState& S = G();
	if (!S.Sim)
	{
		return 0;
	}
	if (!S.bReplaying)
	{
		S.PlaybackSource = S.Recorded;
	}
	return StartFromSetup(S.Setup, true) ? 1 : 0;
}

EMSCRIPTEN_KEEPALIVE int dot_is_replay()
{
	return G().bReplaying ? 1 : 0;
}

EMSCRIPTEN_KEEPALIVE void dot_cmd(int Type, int UnitId, double X, double Z, int TargetId, int Param)
{
	WebState& S = G();
	if (!S.Sim || S.bReplaying)
	{
		return;
	}
	dot::BattleCommand Cmd;
	Cmd.side = dot::Side::Attacker;
	Cmd.type = static_cast<dot::CommandType>(Type);
	Cmd.unitId = UnitId;
	Cmd.point = ToFix(X, Z);
	Cmd.targetId = TargetId;
	Cmd.param = Param;
	S.Pending.push_back(Cmd);
}

/** Path command; Points holds Count (x, z) pairs. */
EMSCRIPTEN_KEEPALIVE void dot_cmd_path(int UnitId, double X, double Z, int TargetId, const double* Points, int Count)
{
	WebState& S = G();
	if (!S.Sim || S.bReplaying)
	{
		return;
	}
	dot::BattleCommand Cmd;
	Cmd.side = dot::Side::Attacker;
	Cmd.type = dot::CommandType::Path;
	Cmd.unitId = UnitId;
	Cmd.point = ToFix(X, Z);
	Cmd.targetId = TargetId;
	for (int I = 0; I < Count; ++I)
	{
		Cmd.waypoints.push_back(ToFix(Points[I * 2], Points[I * 2 + 1]));
	}
	S.Pending.push_back(Cmd);
}

/** Advances the simulation by one 30 Hz tick. Returns 0 once the battle is finished. */
EMSCRIPTEN_KEEPALIVE int dot_step()
{
	WebState& S = G();
	if (!S.Sim || S.Sim->IsFinished())
	{
		return 0;
	}
	dot::BattleSimulation& Sim = *S.Sim;
	std::vector<dot::BattleCommand> Batch;
	if (S.bReplaying && S.Playback)
	{
		S.Playback->CommandsForTick(Sim.CurrentTick(), Batch);
	}
	else
	{
		Batch.swap(S.Pending);
		for (dot::BattleCommand& Cmd : Batch)
		{
			Cmd.tick = Sim.CurrentTick();
			S.Recorded.push_back(Cmd);
		}
	}
	Sim.Step(Batch);
	S.StepEvents.clear();
	Sim.DrainEvents(S.StepEvents);
	for (const dot::SimEvent& E : S.StepEvents)
	{
		if (E.type == dot::SimEventType::SoldierDied)
		{
			++S.SoldierDeaths;
		}
		else if (E.type != dot::SimEventType::TitanAttack || S.Events.size() < 512)
		{
			S.Events.push_back(E);
		}
	}
	if (S.Events.size() > 1024)
	{
		S.Events.erase(S.Events.begin(), S.Events.begin() + 512);
	}
	return Sim.IsFinished() ? 0 : 1;
}

EMSCRIPTEN_KEEPALIVE int dot_tick()
{
	return G().Sim ? G().Sim->CurrentTick() : 0;
}

/** Soldier snapshot: 8 floats per soldier (x, z, y, yaw, state, deathTick, lastAttackTick, squad). */
EMSCRIPTEN_KEEPALIVE float* dot_soldiers()
{
	WebState& S = G();
	S.SoldierBuffer.clear();
	if (!S.Sim)
	{
		return S.SoldierBuffer.data();
	}
	const dot::NavGrid& Grid = S.Sim->Grid();
	for (const dot::Soldier& So : S.Sim->Soldiers())
	{
		S.SoldierBuffer.push_back(static_cast<float>(So.pos.x.ToDouble()));
		S.SoldierBuffer.push_back(static_cast<float>(So.pos.z.ToDouble()));
		S.SoldierBuffer.push_back(static_cast<float>(Grid.HeightAt(So.pos).ToDouble() + So.height.ToDouble()));
		S.SoldierBuffer.push_back(static_cast<float>(Yaw(So.facing)));
		S.SoldierBuffer.push_back(static_cast<float>(static_cast<int>(So.state)));
		S.SoldierBuffer.push_back(static_cast<float>(So.deathTick));
		S.SoldierBuffer.push_back(static_cast<float>(So.lastAttackTick));
		S.SoldierBuffer.push_back(static_cast<float>(So.squad));
	}
	return S.SoldierBuffer.data();
}

EMSCRIPTEN_KEEPALIVE int dot_soldier_count()
{
	return G().Sim ? static_cast<int>(G().Sim->Soldiers().size()) : 0;
}

/** Titan snapshot: 4 floats per titan (x, z, ground height, yaw). */
EMSCRIPTEN_KEEPALIVE float* dot_titans()
{
	WebState& S = G();
	S.TitanBuffer.clear();
	if (!S.Sim)
	{
		return S.TitanBuffer.data();
	}
	for (const dot::Titan& T : S.Sim->Titans())
	{
		S.TitanBuffer.push_back(static_cast<float>(T.pos.x.ToDouble()));
		S.TitanBuffer.push_back(static_cast<float>(T.pos.z.ToDouble()));
		S.TitanBuffer.push_back(static_cast<float>(S.Sim->Grid().HeightAt(T.pos).ToDouble()));
		S.TitanBuffer.push_back(static_cast<float>(Yaw(T.facing)));
	}
	return S.TitanBuffer.data();
}

EMSCRIPTEN_KEEPALIVE int dot_titan_count()
{
	return G().Sim ? static_cast<int>(G().Sim->Titans().size()) : 0;
}

EMSCRIPTEN_KEEPALIVE double dot_height(double X, double Z)
{
	return G().Sim ? G().Sim->Grid().HeightAt(ToFix(X, Z)).ToDouble() : 0.0;
}

/** Unit under a ground point, like the Unreal controller's PickUnit. Returns a unit id or -1. */
EMSCRIPTEN_KEEPALIVE int dot_pick(double X, double Z, int bEnemy, double SquadRadius)
{
	WebState& S = G();
	if (!S.Sim)
	{
		return -1;
	}
	const dot::BattleSimulation& Sim = *S.Sim;
	const dot::Side Side = bEnemy ? dot::Side::Defender : dot::Side::Attacker;
	const dot::FixVec2 P = ToFix(X, Z);
	const int Titan = Sim.FindTitanAt(Side, P, dot::fx::Int(3));
	if (Titan >= 0)
	{
		return Sim.Titans()[static_cast<size_t>(Titan)].id;
	}
	const int Squad = Sim.FindSquadAt(Side, P, dot::Fix64::FromDouble(SquadRadius));
	if (Squad >= 0 && !bEnemy && Sim.Squads()[static_cast<size_t>(Squad)].summoned)
	{
		return -1; // raised skeletons cannot be given orders
	}
	if (Squad >= 0 && (!bEnemy || Sim.IsSquadVisibleTo(dot::Side::Attacker, Squad)))
	{
		return Sim.Squads()[static_cast<size_t>(Squad)].id;
	}
	return -1;
}

/** Everything that does not change during a battle: map, terrain grid, troops, abilities, units. */
EMSCRIPTEN_KEEPALIVE const char* dot_static()
{
	WebState& S = G();
	Out O(S.Text);
	if (!S.Sim)
	{
		O.Raw("null");
		return Finish(S.Text);
	}
	const dot::BattleSimulation& Sim = *S.Sim;
	const dot::SimContent& C = S.Content;
	const dot::MapDef& M = Sim.Map();
	const dot::NavGrid& Grid = Sim.Grid();
	O.Raw("{").Key("name").Str(Sim.Setup().name).Comma().Key("seed").Int(static_cast<long long>(Sim.Setup().seed)).Comma();
	O.Key("timer").Int(Sim.Setup().timerSec).Comma().Key("contentHash").Str(C.hash).Comma().Key("version").Str(C.version).Comma();
	O.Key("map").Raw("{").Key("name").Str(M.name).Comma().Key("w").Fx(M.width).Comma().Key("d").Fx(M.depth).Comma().Key("feat").Raw("[");
	for (size_t I = 0; I < M.features.size(); ++I)
	{
		const dot::MapFeature& F = M.features[I];
		if (I)
		{
			O.Comma();
		}
		O.Raw("{").Key("t").Int(static_cast<int>(F.type)).Comma().Key("c").Int(F.circle ? 1 : 0).Comma();
		O.Key("x").Fx(F.center.x).Comma().Key("z").Fx(F.center.z).Comma().Key("r").Fx(F.radius).Comma();
		O.Key("x0").Fx(F.min.x).Comma().Key("z0").Fx(F.min.z).Comma().Key("x1").Fx(F.max.x).Comma().Key("z1").Fx(F.max.z).Comma();
		O.Key("h").Fx(F.height).Raw("}");
	}
	O.Raw("],").Key("dep").Raw("[");
	for (int I = 0; I < 2; ++I)
	{
		if (I)
		{
			O.Comma();
		}
		O.Raw("[").Fx(M.deployMin[I].x).Comma().Fx(M.deployMin[I].z).Comma().Fx(M.deployMax[I].x).Comma().Fx(M.deployMax[I].z).Raw("]");
	}
	O.Raw("]}").Comma();
	O.Key("grid").Raw("{").Key("nx").Int(Grid.Width()).Comma().Key("nz").Int(Grid.Depth()).Comma().Key("cell").Fx(Grid.CellSize()).Comma().Key("h").Raw("[");
	for (size_t I = 0; I < Grid.RawHeights().size(); ++I)
	{
		if (I)
		{
			O.Comma();
		}
		O.Fx(Grid.RawHeights()[I], 2);
	}
	O.Raw("],").Key("f").Raw("[");
	for (size_t I = 0; I < Grid.RawFlags().size(); ++I)
	{
		if (I)
		{
			O.Comma();
		}
		O.Int(Grid.RawFlags()[I]);
	}
	O.Raw("]}").Comma();
	O.Key("troops").Raw("[");
	for (size_t I = 0; I < C.troops.size(); ++I)
	{
		const dot::TroopDef& T = C.troops[I];
		if (I)
		{
			O.Comma();
		}
		O.Raw("{").Key("id").Str(T.id).Comma().Key("name").Str(T.name).Comma().Key("short").Str(T.shortName).Comma();
		O.Key("cls").Int(static_cast<int>(T.cls)).Comma().Key("ranged").Int(T.ranged ? 1 : 0).Comma().Key("cav").Int(T.IsCavalry() ? 1 : 0).Comma();
		O.Key("size").Int(T.squadSize).Comma().Key("forms").Int(static_cast<long long>(T.formationMask)).Comma().Key("range").Fx(T.range).Comma().Key("tiers").Raw("[");
		for (size_t K = 0; K < T.tiers.size(); ++K)
		{
			if (K)
			{
				O.Comma();
			}
			O.Str(T.tiers[K].name);
		}
		O.Raw("]}");
	}
	O.Raw("],").Key("abil").Raw("[");
	for (size_t I = 0; I < C.abilities.size(); ++I)
	{
		const dot::AbilityDef& A = C.abilities[I];
		if (I)
		{
			O.Comma();
		}
		O.Raw("{").Key("id").Str(A.id).Comma().Key("name").Str(A.name).Comma().Key("spell").Int(A.isSpell ? 1 : 0).Comma();
		O.Key("rage").Int(A.rageCost).Comma().Key("valor").Int(A.valorCost).Comma().Key("cd").Int(A.cooldownTicks).Comma();
		O.Key("shape").Int(static_cast<int>(A.shape)).Comma().Key("del").Int(static_cast<int>(A.delivery)).Comma();
		O.Key("range").Fx(A.range).Comma().Key("radius").Fx(A.radius).Comma().Key("width").Fx(A.width).Comma();
		O.Key("speed").Fx(A.projectileSpeed).Comma().Key("cast").Int(A.castTicks).Raw("}");
	}
	O.Raw("],").Key("squads").Raw("[");
	for (size_t I = 0; I < Sim.Squads().size(); ++I)
	{
		const dot::Squad& Sq = Sim.Squads()[I];
		if (I)
		{
			O.Comma();
		}
		O.Raw("{").Key("id").Int(Sq.id).Comma().Key("side").Int(static_cast<int>(Sq.side)).Comma().Key("troop").Int(Sq.troop).Comma();
		O.Key("tier").Int(Sq.tier).Comma().Key("start").Int(Sq.startCount).Comma().Key("range").Fx(Sq.range).Comma();
		O.Key("sum").Int(Sq.summoned ? 1 : 0).Raw("}");
	}
	O.Raw("],").Key("titans").Raw("[");
	for (size_t I = 0; I < Sim.Titans().size(); ++I)
	{
		const dot::Titan& T = Sim.Titans()[I];
		const dot::TitanDef& D = Sim.TitanDefOf(T);
		if (I)
		{
			O.Comma();
		}
		O.Raw("{").Key("id").Int(T.id).Comma().Key("side").Int(static_cast<int>(T.side)).Comma().Key("def").Str(D.id).Comma();
		O.Key("name").Str(D.name).Comma().Key("title").Str(D.title).Comma().Key("cls").Int(static_cast<int>(D.cls)).Comma();
		O.Key("lvl").Int(T.level).Comma().Key("stars").Int(T.stars).Comma().Key("mhp").Fx(T.maxHp, 0).Comma();
		O.Key("h").Fx(T.height).Comma().Key("r").Fx(T.radius).Comma().Key("step").Fx(T.stepLength).Comma();
		O.Key("ab").Raw("[").Int(D.abilities[0]).Comma().Int(D.abilities[1]).Raw("]").Comma().Key("power").Int(T.power).Raw("}");
	}
	O.Raw("],").Key("sides").Raw("[");
	for (int I = 0; I < 2; ++I)
	{
		const dot::SideState& Side = Sim.GetSide(static_cast<dot::Side>(I));
		if (I)
		{
			O.Comma();
		}
		O.Raw("{").Key("name").Str(Sim.Setup().sides[I].name).Comma().Key("ai").Str(Sim.Setup().sides[I].aiProfile).Comma();
		O.Key("power").Int(Side.startPower).Comma().Key("spells").Raw("[");
		for (size_t K = 0; K < Side.spells.size(); ++K)
		{
			if (K)
			{
				O.Comma();
			}
			O.Int(Side.spells[K]);
		}
		O.Raw("]}");
	}
	O.Raw("],").Key("rules").Raw("{").Key("valorMax").Fx(C.rules.valorMax).Comma().Key("rageMax").Int(100).Comma();
	O.Key("perfectMs").Int(C.rules.clashPerfectMs).Comma().Key("goodMs").Int(C.rules.clashGoodMs).Raw("}}");
	return Finish(S.Text);
}

/** Changing state for HUD and effects; also drains the events gathered since the last call. */
EMSCRIPTEN_KEEPALIVE const char* dot_frame()
{
	WebState& S = G();
	Out O(S.Text);
	if (!S.Sim)
	{
		O.Raw("null");
		return Finish(S.Text);
	}
	const dot::BattleSimulation& Sim = *S.Sim;
	O.Raw("{").Key("tick").Int(Sim.CurrentTick()).Comma().Key("phase").Int(static_cast<int>(Sim.Phase())).Comma();
	O.Key("timer").Int(Sim.TimerTicksRemaining()).Comma().Key("countdown").Int(Sim.CountdownTicksRemaining()).Comma();
	O.Key("replay").Int(S.bReplaying ? 1 : 0).Comma().Key("deaths").Int(S.SoldierDeaths).Comma();
	O.Key("obj").Raw("[").Int(Sim.ObjectiveMet(0)).Comma().Int(Sim.ObjectiveMet(1)).Comma().Int(Sim.ObjectiveMet(2)).Raw("]").Comma();
	O.Key("sides").Raw("[");
	for (int I = 0; I < 2; ++I)
	{
		const dot::Side Sd = static_cast<dot::Side>(I);
		const dot::SideState& Side = Sim.GetSide(Sd);
		if (I)
		{
			O.Comma();
		}
		O.Raw("{").Key("valor").Fx(Side.valor).Comma().Key("power").Int(Sim.SidePowerRemaining(Sd)).Comma();
		O.Key("titanDown").Int(Side.titanDefeated ? 1 : 0).Comma().Key("ready").Raw("[");
		for (size_t K = 0; K < Side.spellReadyTick.size(); ++K)
		{
			if (K)
			{
				O.Comma();
			}
			O.Int(Side.spellReadyTick[K]);
		}
		O.Raw("],").Key("ch").Raw("[");
		for (size_t K = 0; K < Side.spellCharges.size(); ++K)
		{
			O.Raw(K ? "," : "").Int(Side.spellCharges[K]);
		}
		O.Raw("]}");
	}
	O.Raw("],").Key("sq").Raw("[");
	for (size_t I = 0; I < Sim.Squads().size(); ++I)
	{
		const dot::Squad& Sq = Sim.Squads()[I];
		if (I)
		{
			O.Comma();
		}
		const bool bVisible = Sq.side == dot::Side::Attacker || Sim.IsSquadVisibleTo(dot::Side::Attacker, static_cast<int>(I));
		O.Raw("{").Key("a").Int(Sq.aliveCount).Comma().Key("m").Fx(Sq.morale, 0).Comma().Key("ms").Int(static_cast<int>(Sq.moraleState)).Comma();
		O.Key("f").Int(static_cast<int>(Sq.formation)).Comma().Key("st").Int(static_cast<int>(Sq.stance)).Comma();
		O.Key("au").Int(Sq.autoControl ? 1 : 0).Comma().Key("o").Int(static_cast<int>(Sq.order)).Comma().Key("ot").Int(Sq.orderTarget).Comma();
		O.Key("c").Vec(Sq.centroid).Comma().Key("l").Vec(Sq.leader).Comma().Key("fc").Vec(Sq.facing).Comma().Key("r").Fx(Sq.radius).Comma();
		O.Key("mel").Int(Sq.InMelee() ? 1 : 0).Comma().Key("vis").Int(bVisible ? 1 : 0).Comma().Key("act").Int(Sq.Active() ? 1 : 0).Comma();
		O.Key("chg").Int(Sq.chargeUntil > Sim.CurrentTick() ? 1 : 0);
		if (Sq.side == dot::Side::Attacker && Sq.order != dot::OrderType::None)
		{
			O.Comma().Key("op").Vec(Sq.orderPoint).Comma().Key("p").Raw("[");
			for (size_t K = Sq.pathIndex; K < Sq.path.size(); ++K)
			{
				if (K > Sq.pathIndex)
				{
					O.Comma();
				}
				O.Vec(Sq.path[K]);
			}
			O.Raw("]");
		}
		O.Raw("}");
	}
	O.Raw("],").Key("ti").Raw("[");
	for (size_t I = 0; I < Sim.Titans().size(); ++I)
	{
		const dot::Titan& T = Sim.Titans()[I];
		if (I)
		{
			O.Comma();
		}
		O.Raw("{").Key("hp").Fx(T.hp, 0).Comma().Key("rage").Fx(T.rage, 1).Comma().Key("s").Int(static_cast<int>(T.state)).Comma();
		O.Key("dead").Int(T.dead ? 1 : 0).Comma().Key("fall").Num(Yaw(T.fallDir), 3).Comma();
		O.Key("ls").Int(T.leapStart).Comma().Key("le").Int(T.leapEnd).Comma().Key("la").Int(T.leapAbility).Comma().Key("stg").Int(T.staggerUntil).Comma();
		O.Key("ar").Raw("[").Int(T.abilityReadyTick[0]).Comma().Int(T.abilityReadyTick[1]).Raw("]").Comma();
		O.Key("ch").Int(Sim.TitanChains(T)).Comma().Key("pn").Int(Sim.TitanPins(T)).Comma().Key("bd").Int(T.boundUntil).Comma();
		O.Key("au").Int(T.autoControl ? 1 : 0).Comma().Key("o").Int(static_cast<int>(T.order)).Comma().Key("cast").Int(T.castingAbility);
		if (T.side == dot::Side::Attacker && T.order != dot::OrderType::None)
		{
			O.Comma().Key("op").Vec(T.orderPoint).Comma().Key("p").Raw("[");
			for (size_t K = T.pathIndex; K < T.path.size(); ++K)
			{
				if (K > T.pathIndex)
				{
					O.Comma();
				}
				O.Vec(T.path[K]);
			}
			O.Raw("]");
		}
		O.Raw("}");
	}
	O.Raw("],").Key("pr").Raw("[");
	bool bFirst = true;
	for (const dot::Projectile& P : Sim.Projectiles())
	{
		if (!P.active)
		{
			continue;
		}
		if (!bFirst)
		{
			O.Comma();
		}
		bFirst = false;
		O.Raw("[").Int(static_cast<int>(P.kind)).Comma().Fx(P.from.x).Comma().Fx(P.from.z).Comma().Fx(P.to.x).Comma().Fx(P.to.z).Comma();
		O.Fx(P.fromHeight).Comma().Fx(Sim.Grid().HeightAt(P.to)).Comma().Int(P.fireTick).Comma().Int(P.impactTick).Comma().Int(static_cast<int>(P.side)).Raw("]");
	}
	O.Raw("],").Key("im").Raw("[");
	bFirst = true;
	for (const dot::PendingImpact& P : Sim.PendingImpacts())
	{
		if (!P.active)
		{
			continue;
		}
		if (!bFirst)
		{
			O.Comma();
		}
		bFirst = false;
		O.Raw("[").Int(P.ability).Comma().Fx(P.origin.x).Comma().Fx(P.origin.z).Comma().Fx(P.point.x).Comma().Fx(P.point.z).Comma();
		O.Int(P.castTick).Comma().Int(P.impactTick).Comma().Int(static_cast<int>(P.side)).Raw("]");
	}
	O.Raw("],").Key("zo").Raw("[");
	bFirst = true;
	for (const dot::Zone& Z : Sim.Zones())
	{
		if (!Z.active)
		{
			continue;
		}
		if (!bFirst)
		{
			O.Comma();
		}
		bFirst = false;
		O.Raw("[").Int(Z.ability).Comma().Fx(Z.center.x).Comma().Fx(Z.center.z).Comma().Fx(Z.radius).Comma().Int(static_cast<int>(Z.side)).Raw("]");
	}
	O.Raw("],").Key("fp").Raw("[");
	bFirst = true;
	for (const dot::FirePatch& F : Sim.FirePatches())
	{
		if (!bFirst)
		{
			O.Comma();
		}
		bFirst = false;
		O.Raw("[").Fx(F.center.x).Comma().Fx(F.center.z).Comma().Fx(F.radius).Comma().Int(F.endTick).Raw("]");
	}
	// Crewmen still serving each living siege engine: [soldier index, crew].
	O.Raw("],").Key("cw").Raw("[");
	bFirst = true;
	for (size_t I = 0; I < Sim.Soldiers().size(); ++I)
	{
		const int Crew = Sim.CrewAlive(Sim.Soldiers()[I]);
		if (Crew > 0)
		{
			O.Raw(bFirst ? "[" : ",[").Int(static_cast<int>(I)).Comma().Int(Crew).Raw("]");
			bFirst = false;
		}
	}
	const dot::ClashState& Cl = Sim.Clash();
	O.Raw("],").Key("cl").Raw("{").Key("on").Int(Cl.active ? 1 : 0).Comma().Key("ex").Int(Cl.exchange).Comma();
	O.Key("ws").Int(Cl.windupStart).Comma().Key("mk").Int(Cl.markerTick).Comma().Key("rs").Int(Cl.resolveTick).Comma();
	O.Key("in").Int(Cl.inputReceived[0] ? 1 : 0).Comma().Key("lg").Raw("[").Int(Cl.lastGrade[0]).Comma().Int(Cl.lastGrade[1]).Raw("]").Comma();
	O.Key("t").Raw("[").Int(Cl.titan[0]).Comma().Int(Cl.titan[1]).Raw("]}").Comma();
	O.Key("ev").Raw("[");
	for (size_t I = 0; I < S.Events.size(); ++I)
	{
		const dot::SimEvent& E = S.Events[I];
		if (I)
		{
			O.Comma();
		}
		O.Raw("[").Int(static_cast<int>(E.type)).Comma().Int(E.tick).Comma().Int(static_cast<int>(E.side)).Comma().Int(E.a).Comma().Int(E.b).Comma();
		O.Fx(E.position.x).Comma().Fx(E.position.z).Comma().Int(E.value).Raw("]");
	}
	S.Events.clear();
	O.Raw("]");
	if (Sim.IsFinished())
	{
		const dot::BattleResult& R = Sim.Result();
		O.Comma().Key("res").Raw("{").Key("win").Int(R.outcome == dot::BattleOutcome::Victory ? 1 : 0).Comma().Key("stars").Int(R.stars).Comma();
		O.Key("dur").Int(R.durationTicks).Comma().Key("destr").Int(R.destructionPct).Comma();
		O.Key("as").Int(R.attackerSurvivors).Comma().Key("al").Int(R.attackerLost).Comma();
		O.Key("ds").Int(R.defenderSurvivors).Comma().Key("dl").Int(R.defenderLost).Comma();
		O.Key("score").Int(R.battleScore).Comma().Key("hash").Str(dot::HashToHex(R.finalHash)).Comma();
		O.Key("cmds").Int(static_cast<long long>(S.bReplaying ? S.PlaybackSource.size() : S.Recorded.size())).Raw("}");
	}
	O.Raw("}");
	return Finish(S.Text);
}

/** Re-simulates the finished battle from its setup and command log, as the game server would. */
EMSCRIPTEN_KEEPALIVE const char* dot_verify()
{
	WebState& S = G();
	Out O(S.Text);
	if (!S.Sim || !S.Sim->IsFinished())
	{
		O.Raw("{\"ok\":0,\"error\":\"battle not finished\"}");
		return Finish(S.Text);
	}
	const std::vector<dot::BattleCommand>& Commands = S.bReplaying ? S.PlaybackSource : S.Recorded;
	const dot::ReplayData Replay = dot::MakeReplay(*S.Sim, Commands);
	// Round-trip through the replay text format to prove the file alone is enough.
	const std::string Text = dot::SerializeReplay(Replay);
	dot::ReplayData Parsed;
	std::string Error;
	dot::BattleResult Result;
	const bool bOk = dot::ParseReplay(Text, Parsed, Error) && dot::VerifyReplay(S.Content, Parsed, Result, Error);
	O.Raw("{").Key("ok").Int(bOk ? 1 : 0).Comma().Key("hash").Str(dot::HashToHex(Result.finalHash)).Comma();
	O.Key("hashes").Int(static_cast<long long>(Parsed.hashes.size())).Comma().Key("commands").Int(static_cast<long long>(Parsed.commands.size())).Comma();
	O.Key("bytes").Int(static_cast<long long>(Text.size())).Comma().Key("error").Str(Error).Raw("}");
	return Finish(S.Text);
}

EMSCRIPTEN_KEEPALIVE const char* dot_hash()
{
	WebState& S = G();
	S.Text = S.Sim ? dot::HashToHex(S.Sim->ComputeHash()) : std::string();
	return S.Text.c_str();
}
}

// --- Kingdom ----------------------------------------------------------------------------------
// Times are Unix seconds passed in by the page; the economy itself never reads a clock.

namespace
{
dot::kingdom::Kingdom* K()
{
	return G().Kingdom.get();
}

int64_t ToTime(double Seconds)
{
	return static_cast<int64_t>(Seconds);
}

void WriteCheck(Out& O, const dot::kingdom::Check& Ck)
{
	O.Raw("{").Key("err").Int(static_cast<int>(Ck.error)).Comma().Key("cost").Raw("[");
	for (size_t I = 0; I < Ck.cost.size(); ++I)
	{
		O.Raw(I ? "," : "").Int(Ck.cost[I]);
	}
	O.Raw("]").Comma();
	O.Key("t").Int(Ck.timeSec).Comma().Key("c").Int(Ck.castle).Comma().Key("e").Int(Ck.expansion).Comma();
	O.Key("g").Int(Ck.gemsForMissing).Comma().Key("m").Raw("[").Int(Ck.missing.building).Comma().Int(Ck.missing.level).Comma();
	O.Int(Ck.missing.count).Raw("]}");
}

/** "0,0,2" -> {0, 0, 2}. */
std::vector<int> ParseInts(const char* Text)
{
	std::vector<int> Out;
	if (!Text)
	{
		return Out;
	}
	const char* P = Text;
	while (*P)
	{
		char* End = nullptr;
		const long V = std::strtol(P, &End, 10);
		if (End == P)
		{
			++P;
			continue;
		}
		Out.push_back(static_cast<int>(V));
		P = End;
	}
	return Out;
}

const char* CollectResult(dot::kingdom::Error E, int64_t Amount)
{
	Out O(G().Text);
	O.Raw("{").Key("err").Int(static_cast<int>(E)).Comma().Key("amount").Int(Amount).Raw("}");
	return G().Text.c_str();
}
} // namespace

extern "C" {

EMSCRIPTEN_KEEPALIVE int dk_init()
{
	WebState& S = G();
	std::string Error;
	if (!dot::kingdom::LoadKingdomContent(S.Source, S.KingdomContent, Error))
	{
		S.Error = "Kingdom content error: " + Error;
		return 0;
	}
	S.Kingdom = std::make_unique<dot::kingdom::Kingdom>(S.KingdomContent);
	return 1;
}

EMSCRIPTEN_KEEPALIVE void dk_new(double Now, double Seed)
{
	if (K())
	{
		K()->NewGame(ToTime(Now), static_cast<uint64_t>(Seed));
	}
}

EMSCRIPTEN_KEEPALIVE int dk_load(const char* Text)
{
	std::string Error;
	if (!K() || !K()->Load(Text, Error))
	{
		G().Error = "Save error: " + Error;
		return 0;
	}
	return 1;
}

EMSCRIPTEN_KEEPALIVE const char* dk_save()
{
	G().Text = K() ? K()->Save() : std::string();
	return G().Text.c_str();
}

EMSCRIPTEN_KEEPALIVE void dk_advance(double Now)
{
	if (K())
	{
		K()->Advance(ToTime(Now));
	}
}

/** Everything the kingdom screen shows, computed at the kingdom's current time. */
EMSCRIPTEN_KEEPALIVE const char* dk_state()
{
	using namespace dot::kingdom;
	WebState& S = G();
	Out O(S.Text);
	Kingdom* Km = K();
	if (!Km)
	{
		O.Raw("null");
		return Finish(S.Text);
	}
	const KingdomContent& C = Km->Content();
	O.Raw("{").Key("now").Int(Km->Now()).Comma().Key("castle").Int(Km->CastleLevel()).Comma();
	O.Key("builders").Int(Km->Builders()).Comma().Key("busy").Int(Km->BusyBuilders()).Comma();
	O.Key("expansions").Int(Km->Expansions()).Comma().Key("landCap").Int(Km->LandCapacity()).Comma();
	O.Key("extraSlots").Int(Km->ExtraLandSlots()).Comma().Key("stonesNext").Int(Km->StonesNextAt()).Comma();
	O.Key("hash").Str(dot::HashToHex(Km->Hash())).Comma();
	O.Key("res").Raw("[").Int(Km->Amount(Res::Gold)).Comma().Int(Km->Amount(Res::Food)).Comma().Int(Km->Amount(Res::Gems)).Comma();
	O.Int(Km->Amount(Res::Stones)).Comma().Int(Km->Amount(Res::Shards)).Comma().Int(Km->Amount(Res::Vp)).Raw("]").Comma();
	// Season of victory points: its bounds, the league the VP reach now, how the last one ended.
	const SeasonResult& Last = Km->LastSeason();
	O.Key("season").Raw("{").Key("no").Int(Km->Season()).Comma().Key("start").Int(Km->SeasonStart(Km->Season())).Comma();
	O.Key("end").Int(Km->SeasonStart(Km->Season() + 1)).Comma().Key("league").Int(Km->League(Km->Amount(Res::Vp))).Comma();
	O.Key("last").Raw("{").Key("no").Int(Last.season).Comma().Key("vp").Int(Last.vp).Comma().Key("league").Int(Last.league).Comma();
	O.Key("gems").Int(Last.gems).Comma().Key("stones").Int(Last.stones).Raw("}}").Comma();
	O.Key("cap").Raw("[").Int(Km->Capacity(Res::Gold)).Comma().Int(Km->Capacity(Res::Food)).Comma().Int(0).Comma();
	O.Int(Km->Capacity(Res::Stones)).Raw("]").Comma();
	O.Key("prot").Raw("[").Int(Km->Protected(Res::Gold)).Comma().Int(Km->Protected(Res::Food)).Raw("]").Comma();
	O.Key("rate").Raw("[").Int(Km->ProductionPerHour(Res::Gold)).Comma().Int(Km->ProductionPerHour(Res::Food)).Raw("]").Comma();
	O.Key("exp");
	WriteCheck(O, Km->CheckExpansion());
	O.Comma().Key("b").Raw("[");
	bool bFirst = true;
	for (const Building& B : Km->Buildings())
	{
		if (!bFirst)
		{
			O.Comma();
		}
		bFirst = false;
		const BuildingDef& D = C.buildings[static_cast<size_t>(B.def)];
		const int64_t StoreCap = (B.level >= 1 && D.role == Role::Producer) ? D.levels[static_cast<size_t>(B.level - 1)].store : 0;
		O.Raw("{").Key("id").Int(B.id).Comma().Key("def").Int(B.def).Comma().Key("lv").Int(B.level).Comma();
		O.Key("x").Int(B.x).Comma().Key("y").Int(B.y).Comma().Key("up").Int(B.upgrading ? 1 : 0).Comma();
		O.Key("s").Int(B.startAt).Comma().Key("f").Int(B.finishAt).Comma();
		O.Key("st").Int(Km->Uncollected(B)).Comma().Key("sc").Int(StoreCap).Comma().Key("skip").Int(Km->SkipCost(B.id)).Comma();
		O.Key("nx");
		WriteCheck(O, Km->CheckUpgrade(B.id));
		O.Raw("}");
	}
	O.Raw("],").Key("shop").Raw("[");
	for (size_t I = 0; I < C.buildings.size(); ++I)
	{
		if (I)
		{
			O.Comma();
		}
		const Check Ck = Km->CheckBuild(static_cast<int>(I));
		O.Raw("{").Key("cnt").Int(Km->CountOf(static_cast<int>(I))).Comma().Key("max").Int(Km->MaxCountOf(static_cast<int>(I))).Comma();
		O.Key("ck");
		WriteCheck(O, Ck);
		O.Raw("}");
	}
	O.Raw("],").Key("lands").Raw("[");
	bFirst = true;
	for (const Land& L : Km->Lands())
	{
		if (!bFirst)
		{
			O.Comma();
		}
		bFirst = false;
		O.Raw("{").Key("id").Int(L.id).Comma().Key("type").Int(L.type).Comma().Key("lv").Int(L.level).Comma().Key("titan").Int(L.titanId).Comma();
		O.Key("st").Int(Km->UncollectedLand(L)).Comma().Key("cap").Int(Km->LandStoreCap(L)).Comma().Key("rate").Int(Km->LandRate(L)).Comma();
		O.Key("fill").Int(Km->LandFillHours(L)).Raw("}");
	}
	O.Raw("],").Key("world").Raw("[");
	bFirst = true;
	for (const WorldLand& W : Km->World())
	{
		if (!bFirst)
		{
			O.Comma();
		}
		bFirst = false;
		O.Raw("{").Key("id").Int(W.id).Comma().Key("type").Int(W.type).Comma().Key("lv").Int(W.level).Comma();
		O.Key("loot").Int(Km->RaidLoot(W)).Comma().Key("vp").Int(Km->VictoryVp(W.level)).Raw("}");
	}
	O.Raw("],").Key("pending").Int(Km->PendingAttack()).Comma().Key("pendingCapture").Int(Km->PendingCapture() ? 1 : 0).Comma();
	// Army: troop levels and counts, camp, queue, Armoury research, Medical School, spells allowed in battle.
	O.Key("army").Raw("{").Key("lv").Raw("[");
	for (size_t I = 0; I < C.troops.size(); ++I)
	{
		O.Raw(I ? "," : "").Int(Km->TroopLevel(static_cast<int>(I)));
	}
	O.Raw("],").Key("n").Raw("[");
	for (size_t I = 0; I < C.troops.size(); ++I)
	{
		O.Raw(I ? "," : "").Int(Km->ArmyCount(static_cast<int>(I)));
	}
	O.Raw("],").Key("lvCap").Raw("[");
	for (size_t I = 0; I < C.troops.size(); ++I)
	{
		O.Raw(I ? "," : "").Int(Km->TroopLevelCap(static_cast<int>(I)));
	}
	O.Raw("],").Key("total").Int(Km->ArmyTotal()).Comma().Key("cap").Int(Km->ArmyCapacity()).Comma().Key("slots").Int(Km->BattleSlots()).Comma();
	O.Key("qcap").Int(Km->QueueCapacity()).Comma().Key("queue").Raw("[");
	for (size_t I = 0; I < Km->TrainQueue().size(); ++I)
	{
		O.Raw(I ? "," : "").Int(Km->TrainQueue()[I]);
	}
	O.Raw("],").Key("qs").Int(Km->QueueHeadStart()).Comma().Key("qf").Int(Km->QueueHeadFinish()).Comma().Key("qb").Int(Km->QueueBlocked() ? 1 : 0).Comma();
	O.Key("tskip").Int(Km->TrainSkipCost()).Comma().Key("maxLv").Int(Km->MaxTroopLevel()).Comma().Key("surv").Int(Km->SurvivalPct()).Comma();
	O.Key("train").Raw("[");
	for (size_t I = 0; I < C.troops.size(); ++I)
	{
		O.Raw(I ? "," : "");
		WriteCheck(O, Km->CheckTrain(static_cast<int>(I)));
	}
	O.Raw("],").Key("upg").Raw("[");
	for (size_t I = 0; I < C.troops.size(); ++I)
	{
		O.Raw(I ? "," : "");
		WriteCheck(O, Km->CheckTroopUpgrade(static_cast<int>(I)));
	}
	O.Raw("],").Key("rs").Raw("{").Key("t").Int(Km->ResearchTroop()).Comma().Key("s").Int(Km->ResearchStart()).Comma();
	O.Key("f").Int(Km->ResearchFinish()).Comma().Key("g").Int(Km->ResearchSkipCost()).Raw("}").Comma().Key("away").Raw("[");
	for (size_t I = 0; I < Km->PendingSquads().size(); ++I)
	{
		O.Raw(I ? "," : "").Int(Km->PendingSquads()[I]);
	}
	O.Raw("],").Key("awayTitan").Int(Km->PendingTitan()).Raw("},");
	// Titans: roster, Hall of Titans capacity, Relic Temple altars, fusion gate.
	O.Key("titans").Raw("[");
	bFirst = true;
	for (const TitanState& T : Km->Titans())
	{
		O.Raw(bFirst ? "" : ",");
		bFirst = false;
		O.Raw("{").Key("id").Int(T.id).Comma().Key("def").Int(T.def).Comma().Key("lv").Int(T.level).Comma().Key("xp").Int(T.xp).Comma();
		O.Key("next").Int(Km->XpToNext(T.level)).Comma().Key("max").Int(Km->TitanMaxLevel(T)).Comma().Key("asc").Int(T.ascended ? 1 : 0).Comma();
		O.Key("ack");
		WriteCheck(O, Km->CheckAscend(T.id));
		O.Comma();
		O.Key("rec").Int(T.recoverUntil).Comma().Key("skip").Int(Km->TitanSkipCost(T.id)).Comma().Key("slots").Int(Km->RelicSlots(T)).Comma();
		O.Key("relics").Raw("[");
		for (size_t K = 0; K < T.relics.size(); ++K)
		{
			O.Raw(K ? "," : "").Int(T.relics[K]);
		}
		// Relic bonuses in tenths of a percent: damage, health, armor, crit, piercing, army damage, army health, XP.
		O.Raw("],").Key("bonus").Raw("[");
		for (int B = 0; B <= static_cast<int>(dot::kingdom::RelicStat::Xp); ++B)
		{
			O.Raw(B ? "," : "").Int(Km->TitanRelicBonus(T.id, static_cast<dot::kingdom::RelicStat>(B)));
		}
		O.Raw("]}");
	}
	O.Raw("],").Key("tcap").Int(Km->TitanCapacity()).Comma().Key("relicAt").Int(Km->RelicFreeAt()).Comma().Key("altars").Raw("[");
	for (size_t I = 0; I < C.altars.size(); ++I)
	{
		O.Raw(I ? "," : "");
		WriteCheck(O, Km->CheckAltar(static_cast<int>(I)));
	}
	O.Raw("],").Key("fuseCastle").Int(C.fusionCastle).Comma();
	// Spells: stock, levels, Spell Forge queue, Arcane Tower research.
	O.Key("spells").Raw("{").Key("stock").Raw("[");
	for (size_t I = 0; I < C.spells.size(); ++I)
	{
		O.Raw(I ? "," : "").Int(Km->SpellStock(static_cast<int>(I)));
	}
	O.Raw("],").Key("lv").Raw("[");
	for (size_t I = 0; I < C.spells.size(); ++I)
	{
		O.Raw(I ? "," : "").Int(Km->SpellLevel(static_cast<int>(I)));
	}
	O.Raw("],").Key("total").Int(Km->SpellTotal()).Comma().Key("cap").Int(Km->SpellCapacity()).Comma().Key("qcap").Int(Km->CraftQueueCapacity()).Comma();
	O.Key("queue").Raw("[");
	for (size_t I = 0; I < Km->CraftQueue().size(); ++I)
	{
		O.Raw(I ? "," : "").Int(Km->CraftQueue()[I]);
	}
	O.Raw("],").Key("qs").Int(Km->CraftHeadStart()).Comma().Key("qf").Int(Km->CraftHeadFinish()).Comma().Key("qb").Int(Km->CraftBlocked() ? 1 : 0).Comma();
	O.Key("skip").Int(Km->CraftSkipCost()).Comma().Key("slots").Int(Km->SpellResearchSlots()).Comma().Key("maxLv").Int(Km->MaxSpellLevel()).Comma();
	O.Key("kinds").Int(C.battleSpellKinds).Comma().Key("craft").Raw("[");
	for (size_t I = 0; I < C.spells.size(); ++I)
	{
		O.Raw(I ? "," : "");
		WriteCheck(O, Km->CheckCraft(static_cast<int>(I)));
	}
	O.Raw("],").Key("upg").Raw("[");
	for (size_t I = 0; I < C.spells.size(); ++I)
	{
		O.Raw(I ? "," : "");
		WriteCheck(O, Km->CheckSpellUpgrade(static_cast<int>(I)));
	}
	O.Raw("],").Key("labs").Raw("[");
	for (size_t I = 0; I < Km->SpellResearch().size(); ++I)
	{
		const SpellResearchSlot& Lab = Km->SpellResearch()[I];
		O.Raw(I ? "," : "").Raw("{").Key("s").Int(Lab.spell).Comma().Key("from").Int(Lab.from).Comma().Key("until").Int(Lab.until).Comma();
		O.Key("g").Int(Km->SpellResearchSkipCost(static_cast<int>(I))).Raw("}");
	}
	O.Raw("],").Key("away").Raw("[");
	for (size_t I = 0; I < Km->PendingSpells().size(); ++I)
	{
		O.Raw(I ? "," : "").Int(Km->PendingSpells()[I]);
	}
	O.Raw("]},");
	// Relic items: kind, rarity, durability left (battles, or seconds for economy relics), the titan or building
	// it is on, effect levels (0 = locked) and their values in tenths of a percent.
	O.Key("relics").Raw("[");
	bFirst = true;
	for (const RelicItem& R : Km->Relics())
	{
		O.Raw(bFirst ? "" : ",");
		bFirst = false;
		O.Raw("{").Key("id").Int(R.id).Comma().Key("kind").Int(R.kind).Comma().Key("r").Int(R.rarity).Comma().Key("dur").Int(Km->RelicLeft(R)).Comma();
		O.Key("max").Int(Km->RelicMax(R)).Comma().Key("on").Int(Km->RelicTitan(R.id)).Comma().Key("b").Int(R.building).Comma().Key("lv").Raw("[");
		for (size_t S = 0; S < R.levels.size(); ++S)
		{
			O.Raw(S ? "," : "").Int(R.levels[S]);
		}
		O.Raw("],").Key("v").Raw("[");
		for (size_t S = 0; S < R.levels.size(); ++S)
		{
			O.Raw(S ? "," : "").Int(Km->RelicEffectValue(R, static_cast<int>(S)));
		}
		O.Raw("]}");
	}
	O.Raw("],").Key("repairCastle").Int(C.relicRepairCastle).Comma().Key("repairFood").Raw("[");
	for (size_t I = 0; I < C.relicRepairFood.size(); ++I)
	{
		O.Raw(I ? "," : "").Int(C.relicRepairFood[I]);
	}
	O.Raw("]}");
	return Finish(S.Text);
}

EMSCRIPTEN_KEEPALIVE int dk_build(int Def, int X, int Y, int UseGems)
{
	return K() ? static_cast<int>(K()->Build(Def, X, Y, UseGems != 0)) : -1;
}

EMSCRIPTEN_KEEPALIVE int dk_upgrade(int Id, int UseGems)
{
	return K() ? static_cast<int>(K()->Upgrade(Id, UseGems != 0)) : -1;
}

EMSCRIPTEN_KEEPALIVE int dk_speedup(int Id)
{
	return K() ? static_cast<int>(K()->Speedup(Id)) : -1;
}

EMSCRIPTEN_KEEPALIVE const char* dk_collect(int Id)
{
	int64_t Amount = 0;
	const dot::kingdom::Error E = K() ? K()->Collect(Id, &Amount) : dot::kingdom::Error::BadId;
	return CollectResult(E, Amount);
}

EMSCRIPTEN_KEEPALIVE const char* dk_collect_all()
{
	dot::kingdom::ResArray Got{};
	const dot::kingdom::Error E = K() ? K()->CollectAll(&Got) : dot::kingdom::Error::BadId;
	Out O(G().Text);
	O.Raw("{").Key("err").Int(static_cast<int>(E)).Comma().Key("gold").Int(Got[0]).Comma().Key("food").Int(Got[1]).Raw("}");
	return G().Text.c_str();
}

EMSCRIPTEN_KEEPALIVE int dk_move(int Id, int X, int Y)
{
	return K() ? static_cast<int>(K()->Move(Id, X, Y)) : -1;
}

EMSCRIPTEN_KEEPALIVE int dk_can_place(int Def, int X, int Y, int IgnoreId)
{
	return K() && K()->CanPlace(Def, X, Y, IgnoreId) ? 1 : 0;
}

EMSCRIPTEN_KEEPALIVE int dk_buy_expansion(int UseGems)
{
	return K() ? static_cast<int>(K()->BuyExpansion(UseGems != 0)) : -1;
}

EMSCRIPTEN_KEEPALIVE int dk_buy_slot()
{
	return K() ? static_cast<int>(K()->BuyLandSlot()) : -1;
}

EMSCRIPTEN_KEEPALIVE int dk_refill_stones()
{
	return K() ? static_cast<int>(K()->RefillStones()) : -1;
}

EMSCRIPTEN_KEEPALIVE const char* dk_collect_land(int Id)
{
	int64_t Amount = 0;
	const dot::kingdom::Error E = K() ? K()->CollectLand(Id, &Amount) : dot::kingdom::Error::BadId;
	return CollectResult(E, Amount);
}

EMSCRIPTEN_KEEPALIVE int dk_abandon_land(int Id)
{
	return K() ? static_cast<int>(K()->AbandonLand(Id)) : -1;
}

/** Squads are troop indices separated by commas (one per squad); TitanId 0 means no titan. */
EMSCRIPTEN_KEEPALIVE int dk_attack_start(int WorldId, int Capture, const char* Squads, int TitanId, const char* Spells)
{
	return K() ? static_cast<int>(K()->StartAttack(WorldId, Capture != 0, ParseInts(Squads), TitanId, ParseInts(Spells))) : -1;
}

/** Survivors: each squad's surviving share in percent, in the order the squads were sent. */
EMSCRIPTEN_KEEPALIVE const char* dk_attack_finish(int WorldId, int Capture, int Stars, const char* Survivors, const char* SpellsUsed)
{
	dot::kingdom::Reward R;
	const dot::kingdom::Error E = K() ? K()->FinishAttack(WorldId, Capture != 0, Stars, ParseInts(Survivors), ParseInts(SpellsUsed), &R) : dot::kingdom::Error::BadId;
	Out O(G().Text);
	O.Raw("{").Key("err").Int(static_cast<int>(E)).Comma().Key("gold").Int(R.res[0]).Comma().Key("food").Int(R.res[1]).Comma();
	O.Key("shards").Int(R.res[static_cast<size_t>(dot::kingdom::ResIndex(dot::kingdom::Res::Shards))]).Comma();
	O.Key("vp").Int(R.res[static_cast<size_t>(dot::kingdom::ResIndex(dot::kingdom::Res::Vp))]).Comma();
	O.Key("land").Int(R.landId).Comma().Key("lost").Int(R.squadsLost).Comma().Key("back").Int(R.squadsBack).Comma();
	O.Key("titan").Int(R.titanId).Comma().Key("xp").Int(R.titanXp).Comma().Key("levels").Int(R.titanLevels).Comma();
	O.Key("broken").Int(R.relicsBroken).Comma().Key("relicsLost").Int(R.relicsLost).Comma().Key("relics").Raw("[");
	for (size_t I = 0; I < R.relics.size(); ++I)
	{
		O.Raw(I ? "," : "").Int(R.relics[I]);
	}
	O.Raw("]}");
	return G().Text.c_str();
}

EMSCRIPTEN_KEEPALIVE int dk_train(int Troop)
{
	return K() ? static_cast<int>(K()->Train(Troop)) : -1;
}

EMSCRIPTEN_KEEPALIVE int dk_cancel_train(int Index)
{
	return K() ? static_cast<int>(K()->CancelTrain(Index)) : -1;
}

EMSCRIPTEN_KEEPALIVE int dk_skip_train()
{
	return K() ? static_cast<int>(K()->SkipTraining()) : -1;
}

EMSCRIPTEN_KEEPALIVE int dk_dismiss(int Troop, int Count)
{
	return K() ? static_cast<int>(K()->DismissTroops(Troop, Count)) : -1;
}

EMSCRIPTEN_KEEPALIVE int dk_upgrade_troop(int Troop, int UseGems)
{
	return K() ? static_cast<int>(K()->UpgradeTroop(Troop, UseGems != 0)) : -1;
}

EMSCRIPTEN_KEEPALIVE int dk_skip_research()
{
	return K() ? static_cast<int>(K()->SkipResearch()) : -1;
}

EMSCRIPTEN_KEEPALIVE const char* dk_open_altar(int Altar)
{
	int32_t Id = 0;
	int32_t Relic = 0;
	const dot::kingdom::Error E = K() ? K()->OpenAltar(Altar, &Id, &Relic) : dot::kingdom::Error::BadId;
	Out O(G().Text);
	O.Raw("{").Key("err").Int(static_cast<int>(E)).Comma().Key("titan").Int(Id).Comma().Key("relic").Int(Relic).Raw("}");
	return G().Text.c_str();
}

EMSCRIPTEN_KEEPALIVE int dk_craft(int Spell)
{
	return K() ? static_cast<int>(K()->CraftSpell(Spell)) : -1;
}

EMSCRIPTEN_KEEPALIVE int dk_cancel_craft(int Index)
{
	return K() ? static_cast<int>(K()->CancelCraft(Index)) : -1;
}

EMSCRIPTEN_KEEPALIVE int dk_skip_craft()
{
	return K() ? static_cast<int>(K()->SkipCraft()) : -1;
}

EMSCRIPTEN_KEEPALIVE int dk_upgrade_spell(int Spell, int UseGems)
{
	return K() ? static_cast<int>(K()->UpgradeSpell(Spell, UseGems != 0)) : -1;
}

EMSCRIPTEN_KEEPALIVE int dk_skip_spell(int Index)
{
	return K() ? static_cast<int>(K()->SkipSpellResearch(Index)) : -1;
}

EMSCRIPTEN_KEEPALIVE int dk_equip(int TitanId, int RelicId)
{
	return K() ? static_cast<int>(K()->EquipRelic(TitanId, RelicId)) : -1;
}

EMSCRIPTEN_KEEPALIVE int dk_attach(int BuildingId, int RelicId)
{
	return K() ? static_cast<int>(K()->AttachRelic(BuildingId, RelicId)) : -1;
}

EMSCRIPTEN_KEEPALIVE int dk_unequip(int RelicId)
{
	return K() ? static_cast<int>(K()->UnequipRelic(RelicId)) : -1;
}

EMSCRIPTEN_KEEPALIVE int dk_discard(int RelicId)
{
	return K() ? static_cast<int>(K()->DiscardRelic(RelicId)) : -1;
}

EMSCRIPTEN_KEEPALIVE int dk_repair_chance(int Target, const char* Sacrifices)
{
	const std::vector<int> Ids = ParseInts(Sacrifices);
	return K() ? K()->RepairChancePct(Target, std::vector<int32_t>(Ids.begin(), Ids.end())) : 0;
}

/** 1 when the target's next locked effect may open with these sacrifices. */
EMSCRIPTEN_KEEPALIVE int dk_repair_unlock(int Target, const char* Sacrifices)
{
	const std::vector<int> Ids = ParseInts(Sacrifices);
	return K() && K()->CanUnlockWith(Target, std::vector<int32_t>(Ids.begin(), Ids.end())) ? 1 : 0;
}

EMSCRIPTEN_KEEPALIVE const char* dk_repair(int Target, const char* Sacrifices)
{
	const std::vector<int> Ids = ParseInts(Sacrifices);
	dot::kingdom::RepairResult R;
	const dot::kingdom::Error E = K() ? K()->RepairRelic(Target, std::vector<int32_t>(Ids.begin(), Ids.end()), &R) : dot::kingdom::Error::BadId;
	Out O(G().Text);
	O.Raw("{").Key("err").Int(static_cast<int>(E)).Comma().Key("ok").Int(R.repaired ? 1 : 0).Comma().Key("slot").Int(R.slot).Comma();
	O.Key("unlocked").Int(R.unlocked ? 1 : 0).Raw("}");
	return G().Text.c_str();
}

EMSCRIPTEN_KEEPALIVE int dk_assign(int LandId, int TitanId)
{
	return K() ? static_cast<int>(K()->AssignTitan(LandId, TitanId)) : -1;
}

EMSCRIPTEN_KEEPALIVE int dk_recall(int LandId)
{
	return K() ? static_cast<int>(K()->RecallTitan(LandId)) : -1;
}

EMSCRIPTEN_KEEPALIVE int dk_fuse(int Target, const char* Sacrifices)
{
	if (!K())
	{
		return -1;
	}
	const std::vector<int> Ids = ParseInts(Sacrifices);
	return static_cast<int>(K()->FuseTitans(Target, std::vector<int32_t>(Ids.begin(), Ids.end())));
}

EMSCRIPTEN_KEEPALIVE const char* dk_fuse_check(int Target, const char* Sacrifices)
{
	Out O(G().Text);
	if (!K())
	{
		O.Raw("null");
		return G().Text.c_str();
	}
	const std::vector<int> Ids = ParseInts(Sacrifices);
	WriteCheck(O, K()->CheckFusion(Target, std::vector<int32_t>(Ids.begin(), Ids.end())));
	return G().Text.c_str();
}

EMSCRIPTEN_KEEPALIVE int dk_skip_titan(int Id)
{
	return K() ? static_cast<int>(K()->SkipRecovery(Id)) : -1;
}

EMSCRIPTEN_KEEPALIVE int dk_ascend(int Id, int UseGems)
{
	return K() ? static_cast<int>(K()->Ascend(Id, UseGems != 0)) : -1;
}
}
