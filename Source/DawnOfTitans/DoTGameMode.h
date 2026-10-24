// Source/DawnOfTitans/DoTGameMode.h
// Builds the whole battle scene from code: lighting, battle manager (simulation) and view.
// Works on any level, including an empty one (/Engine/Maps/Entry by default).
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"

#include "DoTGameMode.generated.h"

class ADoTBattleManager;
class ADoTBattleView;

UCLASS()
class DAWNOFTITANS_API ADoTGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	ADoTGameMode();
	virtual void StartPlay() override;

	/** Scenario relative to Content/Data (overridable with -scenario=...). */
	UPROPERTY(EditAnywhere, Category = "Battle")
	FString ScenarioPath = TEXT("scenarios/m0_sandbox.json");

	/** recruit, veteran, warlord or nightmare (overridable with -enemyai=...). */
	UPROPERTY(EditAnywhere, Category = "Battle")
	FString EnemyAiProfile = TEXT("veteran");

	/** Spawn a sun, sky light and atmosphere if the level has no directional light. */
	UPROPERTY(EditAnywhere, Category = "Battle")
	bool bSpawnLighting = true;

private:
	void SpawnLighting();

	UPROPERTY()
	TObjectPtr<ADoTBattleManager> BattleManager;

	UPROPERTY()
	TObjectPtr<ADoTBattleView> BattleView;
};
