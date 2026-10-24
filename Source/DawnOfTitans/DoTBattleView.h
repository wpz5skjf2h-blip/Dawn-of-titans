// Source/DawnOfTitans/DoTBattleView.h
// Presentation of the battle: terrain from MapDef, soldiers as GPU-instanced placeholder meshes,
// titans as articulated placeholder giants, projectiles, spell zones, and cosmetic effects.
// It only reads the simulation; it never changes it (master prompt 4.16, 18.2).
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"

#include "DoTBattleView.generated.h"

class ADoTBattleManager;
class ADoTCameraPawn;
class UInstancedStaticMeshComponent;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UStaticMesh;
class UStaticMeshComponent;

/** Articulated placeholder titan (all parts are plain static mesh components). */
struct FDoTTitanVisual
{
	USceneComponent* Root = nullptr;
	USceneComponent* Body = nullptr;
	USceneComponent* ShoulderL = nullptr;
	USceneComponent* ShoulderR = nullptr;
	USceneComponent* HipL = nullptr;
	USceneComponent* HipR = nullptr;
	UStaticMeshComponent* Ring = nullptr;
	float HeightCm = 1800.f;
	float WalkPhase = 0.f;
	FVector LastPos = FVector::ZeroVector;
	float AttackStart = -100.f;
	int32 AttackCombo = 0;
	float StompStart = -100.f;
	float CastStart = -100.f;
	float GrabStart = -100.f;
	float DeathStart = -1.f;
	float DeathYaw = 0.f;
};

struct FDoTFx
{
	int32 Kind = 0; // index into FxIsms
	FVector Location = FVector::ZeroVector;
	float Start = 0.f;
	float Duration = 0.5f;
	float StartScale = 1.f;
	float EndScale = 3.f;
	float Height = 0.05f;
};

UCLASS()
class DAWNOFTITANS_API ADoTBattleView : public AActor
{
	GENERATED_BODY()

public:
	ADoTBattleView();
	virtual void Tick(float DeltaSeconds) override;

	void Bind(ADoTBattleManager* InManager);

private:
	void LoadAssets();
	void RebuildAll();
	void BuildTerrain();
	void BuildUnits();
	void ClearDynamic();
	void UpdateSoldiers();
	void UpdateTitans(float DeltaSeconds);
	void UpdateProjectilesAndZones();
	void HandleEvents();
	void UpdateFx();
	void SpawnFx(int32 Kind, const FVector& Location, float Duration, float StartScale, float EndScale, float Height = 0.05f);
	void Shake(const FVector& Location, float BaseAmplitude);

	UMaterialInstanceDynamic* MakeMaterial(const FLinearColor& Color);
	UInstancedStaticMeshComponent* MakeIsm(UStaticMesh* Mesh, const FLinearColor& Color, bool bCastShadow, bool bStatic);
	UStaticMeshComponent* MakePart(UStaticMesh* Mesh, const FLinearColor& Color, USceneComponent* Parent, const FVector& Location, const FVector& Scale,
		bool bStatic, const FRotator& Rotation = FRotator::ZeroRotator);
	USceneComponent* MakePivot(USceneComponent* Parent, const FVector& Location);
	static void SetInstances(UInstancedStaticMeshComponent* Ism, const TArray<FTransform>& Transforms);

	UPROPERTY()
	TObjectPtr<ADoTBattleManager> Manager;

	UPROPERTY()
	TObjectPtr<USceneComponent> SceneRoot;

	UPROPERTY()
	TObjectPtr<UStaticMesh> CubeMesh;
	UPROPERTY()
	TObjectPtr<UStaticMesh> CylinderMesh;
	UPROPERTY()
	TObjectPtr<UStaticMesh> SphereMesh;
	UPROPERTY()
	TObjectPtr<UStaticMesh> ConeMesh;
	UPROPERTY()
	TObjectPtr<UStaticMesh> PlaneMesh;
	UPROPERTY()
	TObjectPtr<UMaterialInterface> BaseMaterial;

	UPROPERTY()
	TArray<TObjectPtr<UMaterialInstanceDynamic>> Materials;
	UPROPERTY()
	TArray<TObjectPtr<USceneComponent>> TerrainParts;
	UPROPERTY()
	TArray<TObjectPtr<USceneComponent>> DynamicParts;
	UPROPERTY()
	TArray<TObjectPtr<UInstancedStaticMeshComponent>> SoldierIsms;
	UPROPERTY()
	TArray<TObjectPtr<UInstancedStaticMeshComponent>> FxIsms;
	UPROPERTY()
	TObjectPtr<UInstancedStaticMeshComponent> ArrowIsm;
	UPROPERTY()
	TObjectPtr<UInstancedStaticMeshComponent> BoulderIsm;
	UPROPERTY()
	TObjectPtr<UInstancedStaticMeshComponent> HealZoneIsm;
	UPROPERTY()
	TObjectPtr<UInstancedStaticMeshComponent> TelegraphIsm;
	UPROPERTY()
	TObjectPtr<UInstancedStaticMeshComponent> HammerIsm;

	/** Soldier index -> (ISM index, instance index, half height cm, mesh scale). */
	TArray<int32> SoldierIsmIndex;
	TArray<int32> SoldierInstance;
	TArray<FVector> SoldierScale;
	TArray<TArray<FTransform>> IsmTransforms;
	TArray<FDoTTitanVisual> TitanVisuals;
	TArray<FDoTFx> Effects;
	TArray<TArray<FTransform>> FxTransforms;

	int32 BuiltResetCounter = -1;
	bool bTerrainBuilt = false;
	float Time = 0.f;
};
