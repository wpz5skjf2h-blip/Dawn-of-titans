// Source/DawnOfTitans/DoTBattleManager.h
// Owns the deterministic simulation: loads JSON content, steps the sim at a fixed 30 Hz,
// queues player commands, records replays and provides interpolated positions to the view.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Templates/SharedPointer.h"

#include <vector>

#include "DoTBattleManager.generated.h"

namespace dot
{
class BattleSimulation;
struct SimContent;
struct BattleCommand;
struct SimEvent;
} // namespace dot

struct FDoTBattleState;

UCLASS()
class DAWNOFTITANS_API ADoTBattleManager : public AActor
{
	GENERATED_BODY()

public:
	ADoTBattleManager();
	virtual void Tick(float DeltaSeconds) override;

	/** Scenario file relative to Content/Data. Can be overridden with -scenario=... on the command line. */
	UPROPERTY(EditAnywhere, Category = "Battle")
	FString ScenarioPath = TEXT("scenarios/m0_sandbox.json");

	/** AI profile for the enemy side (recruit, veteran, warlord, nightmare). */
	UPROPERTY(EditAnywhere, Category = "Battle")
	FString EnemyAiProfile = TEXT("veteran");

	/** Loads content and the scenario, then starts the battle in the deployment phase. */
	bool Initialize();
	bool RestartBattle(bool bSkipDeploy);
	void StartReplayPlayback();
	bool SaveReplay(FString& OutPath) const;
	void QueueCommand(const dot::BattleCommand& Command);
	void CycleEnemyAi();
	void TogglePause() { bPaused = !bPaused; }
	void CycleSpeed();
	void SetAllAuto(bool bAuto);

	const dot::BattleSimulation* GetSim() const;
	const dot::SimContent* GetContent() const;
	const std::vector<dot::SimEvent>& GetFrameEvents() const;
	const FString& GetLastError() const { return LastError; }
	bool IsReplaying() const { return bReplaying; }
	bool IsPaused() const { return bPaused; }
	float GetSpeed() const { return Speed; }
	bool IsAllAuto() const { return bAllAuto; }
	int32 GetResetCounter() const { return ResetCounter; }
	float GetSimMsPerTick() const { return SimMsPerTick; }
	/** Interpolation factor between the previous and the current simulation tick. */
	float GetAlpha() const;
	/** Current render time in ticks (between the previous and the current simulated tick). */
	double GetRenderTick() const;
	/** Offset of "now" from the current Clash marker, in milliseconds (for ClashInput). */
	int32 ClashOffsetMsNow() const;

	FVector SoldierWorldPos(int32 Index) const;
	FVector TitanWorldPos(int32 Index) const;
	float TitanYaw(int32 Index) const;
	int32 FindTitanIndex(bool bPlayerSide) const;

private:
	void StepOnce();
	void SnapshotPrevious();

	// Shared (not unique) pointer: its deleter is captured where the state is created, so
	// UHT-generated code can destroy the actor without seeing the full FDoTBattleState type.
	TSharedPtr<FDoTBattleState> State;
	FString LastError;
	double Accumulator = 0.0;
	float Speed = 1.f;
	bool bPaused = false;
	bool bReplaying = false;
	bool bAllAuto = false;
	int32 ResetCounter = 0;
	float SimMsPerTick = 0.f;
};
