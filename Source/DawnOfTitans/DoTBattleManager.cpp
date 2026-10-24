// Source/DawnOfTitans/DoTBattleManager.cpp
#include "DoTBattleManager.h"

#include "DawnOfTitans.h"
#include "DoTCoords.h"

#include "HAL/PlatformTime.h"
#include "Misc/CommandLine.h"
#include "Misc/DateTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"

#include "DoTSim/BattleSimulation.h"
#include "DoTSim/Content.h"
#include "DoTSim/Replay.h"

#include <memory>
#include <string>

namespace
{
/** Reads content files through Unreal's file system (works with loose files and .pak files). */
class FUEContentSource : public dot::IContentSource
{
public:
	explicit FUEContentSource(const FString& InRoot) : Root(InRoot) {}

	virtual bool ReadText(const std::string& RelativePath, std::string& OutText) override
	{
		TArray<uint8> Bytes;
		const FString Path = Root / FString(UTF8_TO_TCHAR(RelativePath.c_str()));
		if (!FFileHelper::LoadFileToArray(Bytes, *Path))
		{
			return false;
		}
		OutText.assign(reinterpret_cast<const char*>(Bytes.GetData()), static_cast<size_t>(Bytes.Num()));
		return true;
	}

private:
	FString Root;
};

FString ToFString(const std::string& S)
{
	return FString(UTF8_TO_TCHAR(S.c_str()));
}

std::string ToStd(const FString& S)
{
	return std::string(TCHAR_TO_UTF8(*S));
}

const TCHAR* const GAiProfiles[] = {TEXT("recruit"), TEXT("veteran"), TEXT("warlord"), TEXT("nightmare")};
} // namespace

struct FDoTBattleState
{
	dot::SimContent Content;
	dot::BattleSetup BaseSetup;
	std::unique_ptr<dot::BattleSimulation> Sim;
	std::vector<dot::BattleCommand> Pending;
	std::vector<dot::BattleCommand> Recorded;
	std::vector<dot::BattleCommand> PlaybackSource;
	std::unique_ptr<dot::CommandPlayback> Playback;
	std::vector<dot::FixVec2> PrevSoldierPos;
	std::vector<dot::Fix64> PrevSoldierHeight;
	std::vector<dot::FixVec2> PrevTitanPos;
	std::vector<dot::FixVec2> PrevTitanFacing;
	std::vector<dot::SimEvent> FrameEvents;
	std::vector<dot::SimEvent> StepEvents;
	bool bContentLoaded = false;
};

ADoTBattleManager::ADoTBattleManager()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PrePhysics;
	State = MakeShared<FDoTBattleState>();
}

bool ADoTBattleManager::Initialize()
{
	LastError.Reset();
	FString CommandLineScenario;
	if (FParse::Value(FCommandLine::Get(), TEXT("scenario="), CommandLineScenario))
	{
		ScenarioPath = CommandLineScenario;
	}
	FString CommandLineAi;
	if (FParse::Value(FCommandLine::Get(), TEXT("enemyai="), CommandLineAi))
	{
		EnemyAiProfile = CommandLineAi;
	}

	FUEContentSource Source(FPaths::ProjectContentDir() / TEXT("Data"));
	std::string Error;
	if (!dot::LoadContent(Source, State->Content, Error))
	{
		LastError = FString::Printf(TEXT("Content error: %s"), *ToFString(Error));
		UE_LOG(LogDoT, Error, TEXT("%s"), *LastError);
		return false;
	}
	State->bContentLoaded = true;

	std::string ScenarioText;
	if (!Source.ReadText(ToStd(ScenarioPath), ScenarioText))
	{
		LastError = FString::Printf(TEXT("Cannot read scenario %s"), *ScenarioPath);
		UE_LOG(LogDoT, Error, TEXT("%s"), *LastError);
		return false;
	}
	dot::JsonValue Json;
	if (!dot::ParseJson(ScenarioText, Json, Error) || !dot::ParseBattleSetup(Json, State->BaseSetup, Error))
	{
		LastError = FString::Printf(TEXT("Scenario error: %s"), *ToFString(Error));
		UE_LOG(LogDoT, Error, TEXT("%s"), *LastError);
		return false;
	}
	UE_LOG(LogDoT, Log, TEXT("Content %s (hash %s), scenario '%s'"), *ToFString(State->Content.version), *ToFString(State->Content.hash),
		*ToFString(State->BaseSetup.name));
	return RestartBattle(State->BaseSetup.skipDeploy);
}

bool ADoTBattleManager::RestartBattle(bool bSkipDeploy)
{
	if (!State->bContentLoaded)
	{
		return false;
	}
	dot::BattleSetup Setup = State->BaseSetup;
	Setup.contentHash = State->Content.hash;
	Setup.skipDeploy = bSkipDeploy;
	if (!EnemyAiProfile.IsEmpty())
	{
		Setup.sides[1].aiProfile = ToStd(EnemyAiProfile);
	}
	Setup.sides[0].aiProfile.clear(); // the local player commands the attacker

	auto NewSim = std::make_unique<dot::BattleSimulation>(State->Content);
	std::string Error;
	if (!NewSim->Init(Setup, Error))
	{
		LastError = FString::Printf(TEXT("Battle init error: %s"), *ToFString(Error));
		UE_LOG(LogDoT, Error, TEXT("%s"), *LastError);
		return false;
	}
	State->Sim = std::move(NewSim);
	State->Pending.clear();
	State->Recorded.clear();
	State->Playback.reset();
	State->FrameEvents.clear();
	bReplaying = false;
	bAllAuto = false;
	Accumulator = 0.0;
	SnapshotPrevious();
	++ResetCounter;
	return true;
}

void ADoTBattleManager::StartReplayPlayback()
{
	if (!State->Sim)
	{
		return;
	}
	const dot::BattleSetup Setup = State->Sim->Setup();
	State->PlaybackSource = State->Recorded;
	auto NewSim = std::make_unique<dot::BattleSimulation>(State->Content);
	std::string Error;
	if (!NewSim->Init(Setup, Error))
	{
		LastError = FString::Printf(TEXT("Replay init error: %s"), *ToFString(Error));
		return;
	}
	State->Sim = std::move(NewSim);
	State->Playback = std::make_unique<dot::CommandPlayback>(State->PlaybackSource);
	State->Pending.clear();
	State->FrameEvents.clear();
	bReplaying = true;
	Accumulator = 0.0;
	SnapshotPrevious();
	++ResetCounter;
}

bool ADoTBattleManager::SaveReplay(FString& OutPath) const
{
	if (!State->Sim)
	{
		return false;
	}
	const std::vector<dot::BattleCommand>& Commands = bReplaying ? State->PlaybackSource : State->Recorded;
	const dot::ReplayData Replay = dot::MakeReplay(*State->Sim, Commands);
	const std::string Text = dot::SerializeReplay(Replay);
	OutPath = FPaths::ProjectSavedDir() / TEXT("Replays") / FString::Printf(TEXT("battle_%s.replay.json"), *FDateTime::Now().ToString());
	return FFileHelper::SaveStringToFile(ToFString(Text), *OutPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
}

void ADoTBattleManager::QueueCommand(const dot::BattleCommand& Command)
{
	if (!bReplaying && State->Sim)
	{
		State->Pending.push_back(Command);
	}
}

void ADoTBattleManager::CycleEnemyAi()
{
	const int32 Count = static_cast<int32>(UE_ARRAY_COUNT(GAiProfiles));
	int32 Index = 0;
	for (int32 I = 0; I < Count; ++I)
	{
		if (EnemyAiProfile.Equals(GAiProfiles[I], ESearchCase::IgnoreCase))
		{
			Index = (I + 1) % Count;
		}
	}
	EnemyAiProfile = GAiProfiles[Index];
	RestartBattle(false);
}

void ADoTBattleManager::CycleSpeed()
{
	Speed = Speed < 1.5f ? 2.f : (Speed < 3.f ? 4.f : 1.f);
}

void ADoTBattleManager::SetAllAuto(bool bAuto)
{
	const dot::BattleSimulation* Sim = GetSim();
	if (!Sim)
	{
		return;
	}
	bAllAuto = bAuto;
	dot::BattleCommand Squads;
	Squads.side = dot::Side::Attacker;
	Squads.type = dot::CommandType::SetAuto;
	Squads.unitId = 0;
	Squads.param = bAuto ? 1 : 0;
	QueueCommand(Squads);
	for (const dot::Titan& T : Sim->Titans())
	{
		if (T.side == dot::Side::Attacker)
		{
			dot::BattleCommand TitanCmd = Squads;
			TitanCmd.unitId = T.id;
			QueueCommand(TitanCmd);
		}
	}
}

const dot::BattleSimulation* ADoTBattleManager::GetSim() const
{
	return State ? State->Sim.get() : nullptr;
}

const dot::SimContent* ADoTBattleManager::GetContent() const
{
	return (State && State->bContentLoaded) ? &State->Content : nullptr;
}

const std::vector<dot::SimEvent>& ADoTBattleManager::GetFrameEvents() const
{
	return State->FrameEvents;
}

void ADoTBattleManager::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	State->FrameEvents.clear();
	if (!State->Sim || bPaused || State->Sim->IsFinished())
	{
		return;
	}
	const double Dt = 1.0 / dot::kTicksPerSecond;
	Accumulator = FMath::Min(Accumulator + static_cast<double>(DeltaSeconds) * Speed, 0.5);
	int32 Steps = 0;
	while (Accumulator >= Dt && Steps < 16)
	{
		StepOnce();
		Accumulator -= Dt;
		++Steps;
	}
}

void ADoTBattleManager::SnapshotPrevious()
{
	const dot::BattleSimulation* Sim = GetSim();
	if (!Sim)
	{
		return;
	}
	const auto& Soldiers = Sim->Soldiers();
	State->PrevSoldierPos.resize(Soldiers.size());
	State->PrevSoldierHeight.resize(Soldiers.size());
	for (size_t I = 0; I < Soldiers.size(); ++I)
	{
		State->PrevSoldierPos[I] = Soldiers[I].pos;
		State->PrevSoldierHeight[I] = Soldiers[I].height;
	}
	const auto& Titans = Sim->Titans();
	State->PrevTitanPos.resize(Titans.size());
	State->PrevTitanFacing.resize(Titans.size());
	for (size_t I = 0; I < Titans.size(); ++I)
	{
		State->PrevTitanPos[I] = Titans[I].pos;
		State->PrevTitanFacing[I] = Titans[I].facing;
	}
}

void ADoTBattleManager::StepOnce()
{
	dot::BattleSimulation& Sim = *State->Sim;
	SnapshotPrevious();
	std::vector<dot::BattleCommand> Batch;
	if (bReplaying && State->Playback)
	{
		State->Playback->CommandsForTick(Sim.CurrentTick(), Batch);
	}
	else
	{
		Batch.swap(State->Pending);
		for (dot::BattleCommand& Cmd : Batch)
		{
			Cmd.tick = Sim.CurrentTick();
			State->Recorded.push_back(Cmd);
		}
	}
	const double Start = FPlatformTime::Seconds();
	Sim.Step(Batch);
	const float Ms = static_cast<float>((FPlatformTime::Seconds() - Start) * 1000.0);
	SimMsPerTick = SimMsPerTick * 0.95f + Ms * 0.05f;
	State->StepEvents.clear();
	Sim.DrainEvents(State->StepEvents);
	State->FrameEvents.insert(State->FrameEvents.end(), State->StepEvents.begin(), State->StepEvents.end());
}

float ADoTBattleManager::GetAlpha() const
{
	const dot::BattleSimulation* Sim = GetSim();
	if (!Sim || Sim->IsFinished())
	{
		return 1.f;
	}
	return FMath::Clamp(static_cast<float>(Accumulator * dot::kTicksPerSecond), 0.f, 1.f);
}

double ADoTBattleManager::GetRenderTick() const
{
	const dot::BattleSimulation* Sim = GetSim();
	return Sim ? static_cast<double>(Sim->CurrentTick() - 1) + GetAlpha() : 0.0;
}

int32 ADoTBattleManager::ClashOffsetMsNow() const
{
	const dot::BattleSimulation* Sim = GetSim();
	if (!Sim || !Sim->Clash().active)
	{
		return 0;
	}
	return FMath::RoundToInt(static_cast<float>((GetRenderTick() - Sim->Clash().markerTick) * 1000.0 / dot::kTicksPerSecond));
}

FVector ADoTBattleManager::SoldierWorldPos(int32 Index) const
{
	const dot::BattleSimulation* Sim = GetSim();
	const dot::Soldier& S = Sim->Soldiers()[static_cast<size_t>(Index)];
	const float Alpha = GetAlpha();
	const double Ground = Sim->Grid().HeightAt(S.pos).ToDouble();
	if (static_cast<size_t>(Index) >= State->PrevSoldierPos.size())
	{
		return DoTCoords::SimToWorld(S.pos, Ground + S.height.ToDouble());
	}
	const dot::FixVec2& P0 = State->PrevSoldierPos[static_cast<size_t>(Index)];
	const double X = FMath::Lerp(P0.x.ToDouble(), S.pos.x.ToDouble(), static_cast<double>(Alpha));
	const double Z = FMath::Lerp(P0.z.ToDouble(), S.pos.z.ToDouble(), static_cast<double>(Alpha));
	const double H = FMath::Lerp(State->PrevSoldierHeight[static_cast<size_t>(Index)].ToDouble(), S.height.ToDouble(), static_cast<double>(Alpha));
	return DoTCoords::SimToWorldD(X, Z, Ground + H);
}

FVector ADoTBattleManager::TitanWorldPos(int32 Index) const
{
	const dot::BattleSimulation* Sim = GetSim();
	const dot::Titan& T = Sim->Titans()[static_cast<size_t>(Index)];
	const double Ground = Sim->Grid().HeightAt(T.pos).ToDouble();
	if (static_cast<size_t>(Index) >= State->PrevTitanPos.size())
	{
		return DoTCoords::SimToWorld(T.pos, Ground);
	}
	const dot::FixVec2& P0 = State->PrevTitanPos[static_cast<size_t>(Index)];
	const double Alpha = GetAlpha();
	return DoTCoords::SimToWorldD(FMath::Lerp(P0.x.ToDouble(), T.pos.x.ToDouble(), Alpha), FMath::Lerp(P0.z.ToDouble(), T.pos.z.ToDouble(), Alpha), Ground);
}

float ADoTBattleManager::TitanYaw(int32 Index) const
{
	const dot::BattleSimulation* Sim = GetSim();
	const dot::Titan& T = Sim->Titans()[static_cast<size_t>(Index)];
	const float Current = DoTCoords::SimDirToYaw(T.facing);
	if (static_cast<size_t>(Index) >= State->PrevTitanFacing.size())
	{
		return Current;
	}
	const float Previous = DoTCoords::SimDirToYaw(State->PrevTitanFacing[static_cast<size_t>(Index)]);
	return Previous + FMath::FindDeltaAngleDegrees(Previous, Current) * GetAlpha();
}

int32 ADoTBattleManager::FindTitanIndex(bool bPlayerSide) const
{
	const dot::BattleSimulation* Sim = GetSim();
	if (!Sim)
	{
		return INDEX_NONE;
	}
	const dot::Side Wanted = bPlayerSide ? dot::Side::Attacker : dot::Side::Defender;
	for (size_t I = 0; I < Sim->Titans().size(); ++I)
	{
		if (Sim->Titans()[I].side == Wanted)
		{
			return static_cast<int32>(I);
		}
	}
	return INDEX_NONE;
}
