// Source/DawnOfTitans/DoTGameMode.cpp
#include "DoTGameMode.h"

#include "DawnOfTitans.h"
#include "DoTBattleManager.h"
#include "DoTBattleView.h"
#include "DoTCameraPawn.h"
#include "DoTHUD.h"
#include "DoTPlayerController.h"

#include "Components/DirectionalLightComponent.h"
#include "Components/LightComponent.h"
#include "Components/SkyLightComponent.h"
#include "Engine/DirectionalLight.h"
#include "Engine/SkyLight.h"
#include "Engine/World.h"
#include "EngineUtils.h"

ADoTGameMode::ADoTGameMode()
{
	DefaultPawnClass = ADoTCameraPawn::StaticClass();
	PlayerControllerClass = ADoTPlayerController::StaticClass();
	HUDClass = ADoTHUD::StaticClass();
}

void ADoTGameMode::StartPlay()
{
	Super::StartPlay();
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	if (bSpawnLighting)
	{
		SpawnLighting();
	}
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	BattleManager = World->SpawnActor<ADoTBattleManager>(ADoTBattleManager::StaticClass(), FTransform::Identity, Params);
	if (!BattleManager)
	{
		UE_LOG(LogDoT, Error, TEXT("Could not spawn the battle manager"));
		return;
	}
	BattleManager->ScenarioPath = ScenarioPath;
	BattleManager->EnemyAiProfile = EnemyAiProfile;
	if (!BattleManager->Initialize())
	{
		UE_LOG(LogDoT, Error, TEXT("Battle failed to initialize: %s"), *BattleManager->GetLastError());
	}
	BattleView = World->SpawnActor<ADoTBattleView>(ADoTBattleView::StaticClass(), FTransform::Identity, Params);
	if (BattleView)
	{
		BattleView->Bind(BattleManager);
	}
}

void ADoTGameMode::SpawnLighting()
{
	UWorld* World = GetWorld();
	if (TActorIterator<ADirectionalLight>(World))
	{
		return; // the level already has its own lighting
	}
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	const FTransform SunTransform(FRotator(-48.f, 35.f, 0.f));

	// Deferred spawning so mobility and capture mode are set before the components register.
	if (ADirectionalLight* Sun = World->SpawnActorDeferred<ADirectionalLight>(ADirectionalLight::StaticClass(), SunTransform, nullptr, nullptr,
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn))
	{
		Sun->GetRootComponent()->SetMobility(EComponentMobility::Movable);
		Sun->FinishSpawning(SunTransform);
		if (ULightComponent* Light = Sun->GetLightComponent())
		{
			Light->SetIntensity(7.f);
			Light->SetCastShadows(true);
		}
		if (UDirectionalLightComponent* SunLight = Cast<UDirectionalLightComponent>(Sun->GetLightComponent()))
		{
			SunLight->SetAtmosphereSunLight(true);
		}
	}
	// Atmosphere and fog are spawned by class path so no extra headers are needed.
	if (UClass* AtmosphereClass = FindObject<UClass>(nullptr, TEXT("/Script/Engine.SkyAtmosphere")))
	{
		World->SpawnActor<AActor>(AtmosphereClass, FTransform::Identity, Params);
	}
	if (UClass* FogClass = FindObject<UClass>(nullptr, TEXT("/Script/Engine.ExponentialHeightFog")))
	{
		World->SpawnActor<AActor>(FogClass, FTransform::Identity, Params);
	}
	if (ASkyLight* Sky = World->SpawnActorDeferred<ASkyLight>(ASkyLight::StaticClass(), FTransform::Identity, nullptr, nullptr,
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn))
	{
		USkyLightComponent* SkyComponent = Sky->GetLightComponent();
		if (SkyComponent)
		{
			SkyComponent->SetMobility(EComponentMobility::Movable);
#if !(PLATFORM_ANDROID || PLATFORM_IOS)
			// Real-time capture follows the sky atmosphere; phones capture once instead (cheaper).
			SkyComponent->bRealTimeCapture = true;
#endif
		}
		Sky->FinishSpawning(FTransform::Identity);
#if PLATFORM_ANDROID || PLATFORM_IOS
		if (SkyComponent)
		{
			SkyComponent->RecaptureSky();
		}
#endif
	}
}
