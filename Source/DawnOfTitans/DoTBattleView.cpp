// Source/DawnOfTitans/DoTBattleView.cpp
#include "DoTBattleView.h"

#include "DoTBattleManager.h"
#include "DoTCameraPawn.h"
#include "DoTCoords.h"

#include "Components/InstancedStaticMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Math/RandomStream.h"

#include "DoTSim/BattleSimulation.h"
#include "DoTSim/Content.h"

namespace
{
const FLinearColor GAttackerColor(0.10f, 0.33f, 0.95f);
const FLinearColor GDefenderColor(0.85f, 0.13f, 0.10f);

enum EFxKind : int32
{
	Fx_Dust = 0,
	Fx_Shock,
	Fx_Fire,
	Fx_Lightning,
	Fx_Heal,
	Fx_Stagger,
	Fx_Count
};

const FLinearColor GFxColors[Fx_Count] = {
	FLinearColor(0.55f, 0.45f, 0.30f), FLinearColor(0.95f, 0.95f, 1.0f), FLinearColor(1.0f, 0.45f, 0.05f),
	FLinearColor(0.55f, 0.80f, 1.0f), FLinearColor(0.30f, 0.95f, 0.40f), FLinearColor(1.0f, 0.90f, 0.20f)};

FTransform HiddenTransform()
{
	return FTransform(FRotator::ZeroRotator, FVector(0.0, 0.0, -100000.0), FVector(0.001));
}

FLinearColor SideColor(dot::Side S)
{
	return S == dot::Side::Attacker ? GAttackerColor : GDefenderColor;
}

float Smooth01(float T)
{
	T = FMath::Clamp(T, 0.f, 1.f);
	return T * T * (3.f - 2.f * T);
}
} // namespace

ADoTBattleView::ADoTBattleView()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PostPhysics;
	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SceneRoot->SetMobility(EComponentMobility::Static);
	RootComponent = SceneRoot;
}

void ADoTBattleView::Bind(ADoTBattleManager* InManager)
{
	Manager = InManager;
	LoadAssets();
}

void ADoTBattleView::LoadAssets()
{
	if (CubeMesh)
	{
		return;
	}
	CubeMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	CylinderMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	SphereMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	ConeMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cone.Cone"));
	PlaneMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Plane.Plane"));
	BaseMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
}

UMaterialInstanceDynamic* ADoTBattleView::MakeMaterial(const FLinearColor& Color)
{
	UMaterialInstanceDynamic* Mid = UMaterialInstanceDynamic::Create(BaseMaterial, this);
	if (Mid)
	{
		Mid->SetVectorParameterValue(TEXT("Color"), Color);
		Materials.Add(Mid);
	}
	return Mid;
}

UInstancedStaticMeshComponent* ADoTBattleView::MakeIsm(UStaticMesh* Mesh, const FLinearColor& Color, bool bCastShadow, bool bStatic)
{
	UInstancedStaticMeshComponent* Ism = NewObject<UInstancedStaticMeshComponent>(this);
	Ism->SetMobility(bStatic ? EComponentMobility::Static : EComponentMobility::Movable);
	Ism->SetupAttachment(SceneRoot);
	Ism->SetStaticMesh(Mesh);
	Ism->SetMaterial(0, MakeMaterial(Color));
	Ism->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Ism->SetCastShadow(bCastShadow);
	Ism->RegisterComponent();
	return Ism;
}

UStaticMeshComponent* ADoTBattleView::MakePart(UStaticMesh* Mesh, const FLinearColor& Color, USceneComponent* Parent, const FVector& Location,
	const FVector& Scale, bool bStatic, const FRotator& Rotation)
{
	UStaticMeshComponent* Part = NewObject<UStaticMeshComponent>(this);
	Part->SetMobility(bStatic ? EComponentMobility::Static : EComponentMobility::Movable);
	Part->SetupAttachment(Parent);
	Part->SetStaticMesh(Mesh);
	Part->SetMaterial(0, MakeMaterial(Color));
	Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Part->SetRelativeLocationAndRotation(Location, Rotation);
	Part->SetRelativeScale3D(Scale);
	Part->RegisterComponent();
	return Part;
}

USceneComponent* ADoTBattleView::MakePivot(USceneComponent* Parent, const FVector& Location)
{
	USceneComponent* Pivot = NewObject<USceneComponent>(this);
	Pivot->SetMobility(EComponentMobility::Movable);
	Pivot->SetupAttachment(Parent);
	Pivot->SetRelativeLocation(Location);
	Pivot->RegisterComponent();
	DynamicParts.Add(Pivot);
	return Pivot;
}

void ADoTBattleView::SetInstances(UInstancedStaticMeshComponent* Ism, const TArray<FTransform>& Transforms)
{
	if (!Ism)
	{
		return;
	}
	TArray<FTransform> Padded = Transforms;
	const int32 Existing = Ism->GetInstanceCount();
	for (int32 I = Existing; I < Padded.Num(); ++I)
	{
		Ism->AddInstance(HiddenTransform(), true);
	}
	for (int32 I = Padded.Num(); I < Existing; ++I)
	{
		Padded.Add(HiddenTransform());
	}
	if (Padded.Num() > 0)
	{
		Ism->BatchUpdateInstancesTransforms(0, Padded, true, true, true);
	}
}

void ADoTBattleView::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	Time += DeltaSeconds;
	if (!Manager || !Manager->GetSim() || !BaseMaterial)
	{
		return;
	}
	if (BuiltResetCounter != Manager->GetResetCounter())
	{
		RebuildAll();
	}
	HandleEvents();
	UpdateSoldiers();
	UpdateTitans(DeltaSeconds);
	UpdateProjectilesAndZones();
	UpdateFx();
}

void ADoTBattleView::RebuildAll()
{
	BuiltResetCounter = Manager->GetResetCounter();
	if (!bTerrainBuilt)
	{
		BuildTerrain();
		bTerrainBuilt = true;
		const dot::MapDef& Map = Manager->GetSim()->Map();
		if (APlayerController* PC = GetWorld()->GetFirstPlayerController())
		{
			if (ADoTCameraPawn* Cam = Cast<ADoTCameraPawn>(PC->GetPawn()))
			{
				Cam->SetBounds(FBox2D(FVector2D(0.0, 0.0), FVector2D(Map.depth.ToDouble() * 100.0, Map.width.ToDouble() * 100.0)));
				Cam->FocusOn(DoTCoords::SimToWorldD(Map.width.ToDouble() * 0.5, 55.0));
				Cam->SetZoomDistance(11000.f);
			}
		}
	}
	ClearDynamic();
	BuildUnits();
}

void ADoTBattleView::BuildTerrain()
{
	const dot::MapDef& Map = Manager->GetSim()->Map();
	const double W = Map.width.ToDouble();
	const double D = Map.depth.ToDouble();
	auto Add = [this](UStaticMeshComponent* C) { TerrainParts.Add(C); };

	// Ground and a darker skirt around the playable area.
	Add(MakePart(PlaneMesh, FLinearColor(0.10f, 0.22f, 0.06f), SceneRoot, DoTCoords::SimToWorldD(W * 0.5, D * 0.5, -0.05), FVector(D + 80.0, W + 80.0, 1.0), true));
	Add(MakePart(PlaneMesh, FLinearColor(0.22f, 0.42f, 0.12f), SceneRoot, DoTCoords::SimToWorldD(W * 0.5, D * 0.5, 0.0), FVector(D, W, 1.0), true));

	UInstancedStaticMeshComponent* Trunks = MakeIsm(CylinderMesh, FLinearColor(0.25f, 0.15f, 0.07f), true, true);
	UInstancedStaticMeshComponent* Crowns = MakeIsm(ConeMesh, FLinearColor(0.05f, 0.25f, 0.07f), true, true);
	TerrainParts.Add(Trunks);
	TerrainParts.Add(Crowns);
	FRandomStream Stream(12345);

	for (int32 FeatureIndex = 0; FeatureIndex < static_cast<int32>(Map.features.size()); ++FeatureIndex)
	{
		const dot::MapFeature& F = Map.features[static_cast<size_t>(FeatureIndex)];
		const double Cx = F.circle ? F.center.x.ToDouble() : (F.min.x.ToDouble() + F.max.x.ToDouble()) * 0.5;
		const double Cz = F.circle ? F.center.z.ToDouble() : (F.min.z.ToDouble() + F.max.z.ToDouble()) * 0.5;
		const double Sx = F.circle ? F.radius.ToDouble() * 2.0 : F.max.x.ToDouble() - F.min.x.ToDouble();
		const double Sz = F.circle ? F.radius.ToDouble() * 2.0 : F.max.z.ToDouble() - F.min.z.ToDouble();
		switch (F.type)
		{
		// Round water (lakes, marshes) is a flat cylinder; its top sits just under a ford plane.
		case dot::FeatureType::DeepWater:
			Add(MakePart(F.circle ? CylinderMesh : PlaneMesh, FLinearColor(0.05f, 0.15f, 0.45f), SceneRoot, DoTCoords::SimToWorldD(Cx, Cz, F.circle ? 0.0 : 0.04),
				FVector(Sz, Sx, F.circle ? 0.08 : 1.0), true));
			break;
		case dot::FeatureType::ShallowWater:
			Add(MakePart(F.circle ? CylinderMesh : PlaneMesh, FLinearColor(0.20f, 0.42f, 0.60f), SceneRoot, DoTCoords::SimToWorldD(Cx, Cz, F.circle ? 0.02 : 0.06),
				FVector(Sz, Sx, F.circle ? 0.08 : 1.0), true));
			break;
		case dot::FeatureType::Road:
			Add(MakePart(PlaneMesh, FLinearColor(0.55f, 0.45f, 0.30f), SceneRoot, DoTCoords::SimToWorldD(Cx, Cz, 0.08), FVector(Sz, Sx, 1.0), true));
			break;
		case dot::FeatureType::Hill:
			Add(MakePart(SphereMesh, FLinearColor(0.30f, 0.42f, 0.16f), SceneRoot, DoTCoords::SimToWorldD(Cx, Cz, 0.0),
				FVector(Sz, Sx, F.height.ToDouble() * 2.0), true));
			break;
		case dot::FeatureType::Rock:
			Add(MakePart(CubeMesh, FLinearColor(0.35f, 0.34f, 0.33f), SceneRoot, DoTCoords::SimToWorldD(Cx, Cz, F.radius.ToDouble() * 0.4),
				FVector(Sz * 0.8, Sx * 0.9, F.radius.ToDouble() * 1.2), true, FRotator(0.f, Stream.FRandRange(0.f, 90.f), 0.f)));
			break;
		case dot::FeatureType::Ruins:
			for (int32 K = 0; K < 6; ++K)
			{
				const double Px = Cx + Stream.FRandRange(-0.4f, 0.4f) * Sx;
				const double Pz = Cz + Stream.FRandRange(-0.4f, 0.4f) * Sz;
				Add(MakePart(CubeMesh, FLinearColor(0.45f, 0.40f, 0.33f), SceneRoot, DoTCoords::SimToWorldD(Px, Pz, 0.8), FVector(3.0, 0.6, 1.6), true,
					FRotator(0.f, Stream.FRandRange(0.f, 180.f), 0.f)));
			}
			break;
		case dot::FeatureType::Forest:
		{
			const double Radius = F.circle ? F.radius.ToDouble() : FMath::Min(Sx, Sz) * 0.5;
			const int32 Trees = FMath::Clamp(static_cast<int32>(3.14159 * Radius * Radius / 22.0), 4, 120);
			for (int32 K = 0; K < Trees; ++K)
			{
				const double Angle = Stream.FRandRange(0.f, 2.f * UE_PI);
				const double R = Radius * FMath::Sqrt(Stream.FRand());
				const double Px = Cx + FMath::Cos(Angle) * R;
				const double Pz = Cz + FMath::Sin(Angle) * R;
				const float Size = Stream.FRandRange(0.8f, 1.3f);
				Trunks->AddInstance(FTransform(FRotator::ZeroRotator, DoTCoords::SimToWorldD(Px, Pz, 1.5 * Size), FVector(0.6, 0.6, 3.0) * Size), true);
				Crowns->AddInstance(FTransform(FRotator::ZeroRotator, DoTCoords::SimToWorldD(Px, Pz, 5.5 * Size), FVector(3.2, 3.2, 6.0) * Size), true);
			}
			break;
		}
		default: break;
		}
	}

	// Persistent effect and projectile instancers.
	for (int32 K = 0; K < Fx_Count; ++K)
	{
		FxIsms.Add(MakeIsm(K == Fx_Fire || K == Fx_Lightning || K == Fx_Stagger ? SphereMesh : CylinderMesh, GFxColors[K], false, false));
	}
	FxTransforms.SetNum(Fx_Count);
	ArrowIsm = MakeIsm(CubeMesh, FLinearColor(0.85f, 0.80f, 0.65f), false, false);
	BoulderIsm = MakeIsm(SphereMesh, FLinearColor(0.75f, 0.35f, 0.05f), true, false);
	HealZoneIsm = MakeIsm(CylinderMesh, FLinearColor(0.20f, 0.85f, 0.30f), false, false);
	TelegraphIsm = MakeIsm(CylinderMesh, FLinearColor(0.95f, 0.25f, 0.05f), false, false);
	HammerIsm = MakeIsm(CubeMesh, FLinearColor(0.60f, 0.62f, 0.70f), true, false);
}

void ADoTBattleView::ClearDynamic()
{
	for (UInstancedStaticMeshComponent* Ism : SoldierIsms)
	{
		if (Ism)
		{
			Ism->DestroyComponent();
		}
	}
	SoldierIsms.Reset();
	for (USceneComponent* Part : DynamicParts)
	{
		if (Part)
		{
			Part->DestroyComponent();
		}
	}
	DynamicParts.Reset();
	TitanVisuals.Reset();
	IsmTransforms.Reset();
	SoldierIsmIndex.Reset();
	SoldierInstance.Reset();
	SoldierScale.Reset();
	Effects.Reset();
}

void ADoTBattleView::BuildUnits()
{
	const dot::BattleSimulation* Sim = Manager->GetSim();
	const auto& Soldiers = Sim->Soldiers();
	TMap<FString, int32> GroupIndex;
	SoldierIsmIndex.Init(INDEX_NONE, static_cast<int32>(Soldiers.size()));
	SoldierInstance.Init(INDEX_NONE, static_cast<int32>(Soldiers.size()));
	SoldierScale.Init(FVector::OneVector, static_cast<int32>(Soldiers.size()));

	for (const dot::Squad& Sq : Sim->Squads())
	{
		const dot::TroopDef& TD = Sim->TroopOf(Sq);
		const FString Key = FString::Printf(TEXT("%d:%s"), static_cast<int32>(Sq.side), UTF8_TO_TCHAR(TD.id.c_str()));
		UStaticMesh* Mesh = CylinderMesh;
		FVector Scale(0.7, 0.7, 1.7);
		float Tint = 1.f;
		if (TD.id == "troop.swordsmen") { Mesh = CubeMesh; Scale = FVector(0.6, 1.0, 1.8); Tint = 0.8f; }
		else if (TD.id == "troop.spearmen") { Scale = FVector(0.55, 0.55, 2.4); Tint = 1.15f; }
		else if (TD.id == "troop.archers") { Mesh = ConeMesh; Scale = FVector(0.8, 0.8, 1.8); Tint = 1.35f; }
		else if (TD.id == "troop.outriders") { Mesh = CubeMesh; Scale = FVector(2.4, 0.8, 1.6); Tint = 0.7f; }
		else if (TD.id == "troop.knights") { Mesh = CubeMesh; Scale = FVector(2.6, 1.1, 2.0); Tint = 0.5f; }
		else if (TD.id == "troop.skeleton") { Scale = FVector(0.5, 0.5, 1.7); Tint = 0.45f; }
		else if (TD.id == "troop.grenadiers") { Scale = FVector(0.65, 0.65, 1.7); Tint = 1.25f; }
		else if (TD.id == "troop.chainbreakers") { Mesh = CubeMesh; Scale = FVector(0.7, 0.9, 1.7); Tint = 0.65f; }
		else if (TD.id == "troop.priests") { Mesh = ConeMesh; Scale = FVector(0.9, 0.9, 2.0); Tint = 1.5f; }
		else if (TD.id == "troop.pikemen") { Scale = FVector(0.6, 0.6, 2.6); Tint = 1.1f; }
		else if (TD.id == "troop.goliaths") { Mesh = CubeMesh; Scale = FVector(1.6, 1.4, 4.0); Tint = 0.6f; }
		else if (TD.id == "troop.panthers") { Mesh = CubeMesh; Scale = FVector(2.0, 0.7, 1.3); Tint = 0.4f; }
		int32* Found = GroupIndex.Find(Key);
		int32 Group = Found ? *Found : INDEX_NONE;
		if (Group == INDEX_NONE)
		{
			const FLinearColor Base = SideColor(Sq.side);
			const FLinearColor Color(FMath::Min(Base.R * Tint, 1.f), FMath::Min(Base.G * Tint, 1.f), FMath::Min(Base.B * Tint, 1.f));
			SoldierIsms.Add(MakeIsm(Mesh, Color, true, false));
			IsmTransforms.AddDefaulted();
			Group = SoldierIsms.Num() - 1;
			GroupIndex.Add(Key, Group);
		}
		for (const int32 M : Sq.members)
		{
			SoldierIsmIndex[M] = Group;
			SoldierInstance[M] = IsmTransforms[Group].Num();
			SoldierScale[M] = Scale;
			IsmTransforms[Group].Add(HiddenTransform());
		}
	}

	for (const dot::Titan& T : Sim->Titans())
	{
		FDoTTitanVisual V;
		const float H = T.height.ToFloat() * 100.f;
		const float R = T.radius.ToFloat() * 100.f;
		V.HeightCm = H;
		const bool bThor = Sim->TitanDefOf(T).id == "titan.thor";
		const FLinearColor Skin = bThor ? FLinearColor(0.55f, 0.60f, 0.72f) : FLinearColor(0.75f, 0.47f, 0.18f);
		const FLinearColor Accent = SideColor(T.side);
		V.Root = MakePivot(SceneRoot, DoTCoords::SimToWorld(T.pos));
		V.Body = MakePivot(V.Root, FVector::ZeroVector);
		auto Part = [&](USceneComponent* Parent, UStaticMesh* Mesh, const FLinearColor& Color, const FVector& Loc, const FVector& ScaleCm)
		{
			DynamicParts.Add(MakePart(Mesh, Color, Parent, Loc, ScaleCm / 100.f, false));
		};
		Part(V.Body, CubeMesh, Accent, FVector(0.f, 0.f, 0.47f * H), FVector(0.9f * R, 1.3f * R, 0.10f * H));
		Part(V.Body, CubeMesh, Skin, FVector(0.f, 0.f, 0.66f * H), FVector(0.9f * R, 1.5f * R, 0.30f * H));
		Part(V.Body, SphereMesh, Skin, FVector(0.f, 0.f, 0.88f * H), FVector(0.75f * R, 0.75f * R, 0.8f * R));
		V.HipL = MakePivot(V.Body, FVector(0.f, -0.4f * R, 0.45f * H));
		V.HipR = MakePivot(V.Body, FVector(0.f, 0.4f * R, 0.45f * H));
		Part(V.HipL, CylinderMesh, Skin, FVector(0.f, 0.f, -0.225f * H), FVector(0.5f * R, 0.5f * R, 0.45f * H));
		Part(V.HipR, CylinderMesh, Skin, FVector(0.f, 0.f, -0.225f * H), FVector(0.5f * R, 0.5f * R, 0.45f * H));
		V.ShoulderL = MakePivot(V.Body, FVector(0.f, -1.0f * R, 0.78f * H));
		V.ShoulderR = MakePivot(V.Body, FVector(0.f, 1.0f * R, 0.78f * H));
		Part(V.ShoulderL, CubeMesh, Skin, FVector(0.f, 0.f, -0.18f * H), FVector(0.38f * R, 0.38f * R, 0.38f * H));
		Part(V.ShoulderR, CubeMesh, Skin, FVector(0.f, 0.f, -0.18f * H), FVector(0.38f * R, 0.38f * R, 0.38f * H));
		if (bThor)
		{
			// Mjolnir-like hammer: handle + head.
			Part(V.ShoulderR, CylinderMesh, FLinearColor(0.30f, 0.20f, 0.10f), FVector(0.f, 0.f, -0.40f * H), FVector(0.12f * R, 0.12f * R, 0.12f * H));
			Part(V.ShoulderR, CubeMesh, FLinearColor(0.45f, 0.47f, 0.52f), FVector(0.f, 0.f, -0.48f * H), FVector(0.8f * R, 0.5f * R, 0.5f * R));
		}
		else
		{
			Part(V.ShoulderR, SphereMesh, Skin * 0.8f, FVector(0.f, 0.f, -0.38f * H), FVector(0.6f * R, 0.6f * R, 0.6f * R));
			Part(V.ShoulderL, SphereMesh, Skin * 0.8f, FVector(0.f, 0.f, -0.38f * H), FVector(0.6f * R, 0.6f * R, 0.6f * R));
		}
		V.Ring = MakePart(CylinderMesh, Accent, V.Root, FVector(0.f, 0.f, 6.f), FVector(2.6f * R, 2.6f * R, 4.f) / 100.f, false);
		DynamicParts.Add(V.Ring);
		V.LastPos = DoTCoords::SimToWorld(T.pos);
		TitanVisuals.Add(V);
	}
}

void ADoTBattleView::RegisterNewSoldiers()
{
	const dot::BattleSimulation* Sim = Manager->GetSim();
	const int32 Known = SoldierIsmIndex.Num();
	const int32 Total = static_cast<int32>(Sim->Soldiers().size());
	if (Total <= Known)
	{
		return;
	}
	SoldierIsmIndex.SetNum(Total);
	SoldierInstance.SetNum(Total);
	SoldierScale.SetNum(Total);
	for (int32 I = Known; I < Total; ++I)
	{
		SoldierIsmIndex[I] = INDEX_NONE;
		SoldierInstance[I] = INDEX_NONE;
		SoldierScale[I] = FVector(0.5, 0.5, 1.7);
	}
	// One instanced mesh per raised squad, bone-pale with a hint of the caster's colour.
	for (const dot::Squad& Sq : Sim->Squads())
	{
		if (Sq.members.empty() || Sq.members.front() < Known)
		{
			continue;
		}
		const FLinearColor Tint = SideColor(Sq.side);
		const FLinearColor Bone(0.85f, 0.82f, 0.72f);
		SoldierIsms.Add(MakeIsm(CylinderMesh, FMath::Lerp(Bone, Tint, 0.3f), true, false));
		IsmTransforms.AddDefaulted();
		const int32 Group = SoldierIsms.Num() - 1;
		for (const int32 M : Sq.members)
		{
			SoldierIsmIndex[M] = Group;
			SoldierInstance[M] = IsmTransforms[Group].Num();
			IsmTransforms[Group].Add(HiddenTransform());
		}
	}
}

void ADoTBattleView::UpdateSoldiers()
{
	RegisterNewSoldiers();
	const dot::BattleSimulation* Sim = Manager->GetSim();
	const auto& Soldiers = Sim->Soldiers();
	const double RenderTick = Manager->GetRenderTick();
	for (TArray<FTransform>& Arr : IsmTransforms)
	{
		for (FTransform& Tr : Arr)
		{
			Tr = HiddenTransform();
		}
	}
	const int32 Count = FMath::Min(static_cast<int32>(Soldiers.size()), SoldierIsmIndex.Num());
	for (int32 I = 0; I < Count; ++I)
	{
		const dot::Soldier& S = Soldiers[static_cast<size_t>(I)];
		const int32 Group = SoldierIsmIndex[I];
		if (Group == INDEX_NONE || S.state == dot::SoldierState::Left)
		{
			continue;
		}
		const FVector Scale = SoldierScale[I];
		const float Half = static_cast<float>(Scale.Z) * 50.f;
		FVector Loc = Manager->SoldierWorldPos(I);
		const float Yaw = DoTCoords::SimDirToYaw(S.facing);
		FRotator Rot(0.f, Yaw, 0.f);
		if (S.state == dot::SoldierState::Dead)
		{
			const float DeadSeconds = static_cast<float>((RenderTick - S.deathTick) / dot::kTicksPerSecond);
			if (DeadSeconds > 17.f)
			{
				continue;
			}
			Rot = FRotator(0.f, Yaw, 90.f);
			Loc.Z += 25.f - FMath::Max(0.f, DeadSeconds - 15.f) * 40.f;
		}
		else if (S.state == dot::SoldierState::Airborne)
		{
			Rot = FRotator(static_cast<float>(FMath::Fmod(RenderTick * 23.0, 360.0)), Yaw, static_cast<float>(FMath::Fmod(RenderTick * 17.0, 360.0)));
			Loc.Z += Half;
		}
		else if (S.state == dot::SoldierState::KnockedDown)
		{
			Rot = FRotator(0.f, Yaw, 80.f);
			Loc.Z += 30.f;
		}
		else
		{
			Loc.Z += Half;
			// Small lunge when striking.
			const double SinceAttack = RenderTick - S.lastAttackTick;
			if (SinceAttack >= 0.0 && SinceAttack < 6.0)
			{
				Loc += DoTCoords::SimDirToWorld(S.facing) * (25.0 * (1.0 - SinceAttack / 6.0));
			}
		}
		IsmTransforms[Group][SoldierInstance[I]] = FTransform(Rot, Loc, Scale);
	}
	for (int32 G = 0; G < SoldierIsms.Num(); ++G)
	{
		SetInstances(SoldierIsms[G], IsmTransforms[G]);
	}
}

void ADoTBattleView::UpdateTitans(float DeltaSeconds)
{
	const dot::BattleSimulation* Sim = Manager->GetSim();
	const double RenderTick = Manager->GetRenderTick();
	for (int32 I = 0; I < TitanVisuals.Num() && I < static_cast<int32>(Sim->Titans().size()); ++I)
	{
		FDoTTitanVisual& V = TitanVisuals[I];
		const dot::Titan& T = Sim->Titans()[static_cast<size_t>(I)];
		const float H = V.HeightCm;
		FVector Pos = Manager->TitanWorldPos(I);
		if (T.dead)
		{
			if (V.DeathStart < 0.f)
			{
				V.DeathStart = Time;
				V.DeathYaw = DoTCoords::SimDirToYaw(T.fallDir);
			}
			const float Since = Time - V.DeathStart;
			const float Fall = Smooth01(Since / 1.4f);
			if (Since > 20.f)
			{
				Pos.Z -= (Since - 20.f) * 120.f;
			}
			V.Root->SetVisibility(Since < 26.f, true);
			V.Root->SetWorldLocationAndRotation(Pos, FRotator(-88.f * Fall, V.DeathYaw, 0.f));
			continue;
		}
		if (T.state == dot::TitanState::Leaping && T.leapEnd > T.leapStart)
		{
			const double A = FMath::Clamp((RenderTick - T.leapStart) / static_cast<double>(T.leapEnd - T.leapStart), 0.0, 1.0);
			Pos.Z += FMath::Sin(A * UE_DOUBLE_PI) * 900.0;
		}
		V.Root->SetWorldLocationAndRotation(Pos, FRotator(0.f, Manager->TitanYaw(I), 0.f));

		// Walk cycle from distance travelled.
		const float Moved = static_cast<float>(FVector::Dist2D(Pos, V.LastPos));
		V.LastPos = Pos;
		const float StepCm = FMath::Max(T.stepLength.ToFloat() * 100.f, 100.f);
		V.WalkPhase += Moved / StepCm * UE_PI;
		const bool bMoving = Moved > 0.5f;
		const float Swing = bMoving ? FMath::Sin(V.WalkPhase) * 24.f : 0.f;

		FRotator HipL(-Swing, 0.f, 0.f);
		FRotator HipR(Swing, 0.f, 0.f);
		FRotator ArmL(Swing * 0.5f, 0.f, 0.f);
		FRotator ArmR(-Swing * 0.5f, 0.f, 0.f);
		FRotator BodyRot = FRotator::ZeroRotator;
		FVector BodyOffset = FVector::ZeroVector;

		const float TA = (Time - V.AttackStart) / 0.55f;
		if (TA >= 0.f && TA <= 1.f)
		{
			if (V.AttackCombo == 0)
			{
				ArmR = FRotator(90.f, FMath::Lerp(70.f, -60.f, Smooth01(TA)), 0.f);
			}
			else if (V.AttackCombo == 1)
			{
				ArmL = FRotator(90.f, FMath::Lerp(-70.f, 60.f, Smooth01(TA)), 0.f);
			}
			else
			{
				const float Pitch = TA < 0.45f ? FMath::Lerp(0.f, 170.f, Smooth01(TA / 0.45f)) : FMath::Lerp(170.f, 40.f, Smooth01((TA - 0.45f) / 0.55f));
				ArmL = FRotator(Pitch, 0.f, 0.f);
				ArmR = FRotator(Pitch, 0.f, 0.f);
				BodyRot.Pitch = TA > 0.45f ? -8.f : 4.f;
			}
		}
		const float TS = (Time - V.StompStart) / 0.6f;
		if (TS >= 0.f && TS <= 1.f)
		{
			HipR = FRotator(TS < 0.5f ? FMath::Lerp(0.f, 45.f, TS * 2.f) : FMath::Lerp(45.f, 0.f, (TS - 0.5f) * 2.f), 0.f, 0.f);
			BodyOffset.Z = TS > 0.5f ? -0.04f * H * (1.f - TS) : 0.f;
		}
		const float TG = (Time - V.GrabStart) / 1.0f;
		if (TG >= 0.f && TG <= 1.f)
		{
			ArmR = FRotator(TG < 0.5f ? FMath::Lerp(0.f, 100.f, TG * 2.f) : FMath::Lerp(100.f, 175.f, (TG - 0.5f) * 2.f), 0.f, 0.f);
		}
		const float TC = (Time - V.CastStart) / 0.7f;
		if (TC >= 0.f && TC <= 1.f)
		{
			ArmL = FRotator(150.f * FMath::Sin(TC * UE_PI), 0.f, 0.f);
			ArmR = ArmL;
		}
		if (T.state == dot::TitanState::Clashing)
		{
			ArmL = FRotator(95.f, 0.f, 0.f);
			ArmR = FRotator(95.f, 0.f, 0.f);
			BodyRot.Pitch = -10.f + FMath::Sin(Time * 6.f) * 3.f;
		}
		else if (T.state == dot::TitanState::Staggered)
		{
			BodyRot.Pitch = 12.f;
			BodyRot.Roll = FMath::Sin(Time * 9.f) * 7.f;
			ArmL = FRotator(-20.f, 0.f, -25.f);
			ArmR = FRotator(-20.f, 0.f, 25.f);
		}
		V.Body->SetRelativeLocationAndRotation(BodyOffset, BodyRot);
		V.HipL->SetRelativeRotation(HipL);
		V.HipR->SetRelativeRotation(HipR);
		V.ShoulderL->SetRelativeRotation(ArmL);
		V.ShoulderR->SetRelativeRotation(ArmR);
	}
	(void)DeltaSeconds;
}

void ADoTBattleView::UpdateProjectilesAndZones()
{
	const dot::BattleSimulation* Sim = Manager->GetSim();
	const dot::SimContent* Content = Manager->GetContent();
	const double RenderTick = Manager->GetRenderTick();
	const dot::NavGrid& Grid = Sim->Grid();

	TArray<FTransform> Arrows;
	TArray<FTransform> Boulders;
	for (const dot::Projectile& P : Sim->Projectiles())
	{
		if (!P.active)
		{
			continue;
		}
		const double Span = FMath::Max(1.0, static_cast<double>(P.impactTick - P.fireTick));
		const double T = FMath::Clamp((RenderTick - P.fireTick) / Span, 0.0, 1.0);
		const double Dist = (P.to - P.from).Length().ToDouble();
		const bool bThrown = P.kind == dot::ProjectileKind::ThrownSoldier;
		const bool bFirebomb = P.kind == dot::ProjectileKind::Firebomb;
		const double Apex = bThrown ? 12.0 : bFirebomb ? 2.0 + Dist * 0.3 : 1.0 + Dist * 0.12;
		const double X = FMath::Lerp(P.from.x.ToDouble(), P.to.x.ToDouble(), T);
		const double Z = FMath::Lerp(P.from.z.ToDouble(), P.to.z.ToDouble(), T);
		const double H0 = P.fromHeight.ToDouble();
		const double H1 = Grid.HeightAt(P.to).ToDouble() + 0.5;
		const double Hgt = FMath::Lerp(H0, H1, T) + 4.0 * Apex * T * (1.0 - T);
		const FVector Loc = DoTCoords::SimToWorldD(X, Z, Hgt);
		const double Slope = (H1 - H0 + 4.0 * Apex * (1.0 - 2.0 * T)) / FMath::Max(Dist, 0.1);
		const float Yaw = DoTCoords::SimDirToYaw((P.to - P.from).Normalized());
		const FRotator Rot(FMath::RadiansToDegrees(FMath::Atan(static_cast<float>(Slope))), Yaw, 0.f);
		if (bThrown)
		{
			Boulders.Add(FTransform(FRotator(static_cast<float>(RenderTick * 20.0), Yaw, 0.f), Loc, FVector(1.6)));
		}
		else if (bFirebomb)
		{
			Boulders.Add(FTransform(FRotator(static_cast<float>(RenderTick * 30.0), Yaw, 0.f), Loc, FVector(0.5)));
		}
		else
		{
			Arrows.Add(FTransform(Rot, Loc, FVector(1.0, 0.06, 0.06)));
		}
	}

	TArray<FTransform> Telegraphs;
	TArray<FTransform> Hammers;
	for (const dot::PendingImpact& I : Sim->PendingImpacts())
	{
		if (!I.active || !Content || I.ability < 0)
		{
			continue;
		}
		const dot::AbilityDef& A = Content->abilities[static_cast<size_t>(I.ability)];
		if (A.delivery == dot::Delivery::Projectile)
		{
			const double Travel = FMath::Max(1.0, A.range.ToDouble() * dot::kTicksPerSecond / FMath::Max(A.projectileSpeed.ToDouble(), 1.0));
			const double Elapsed = RenderTick - I.castTick;
			const bool bReturn = (I.impactTick - I.castTick) > Travel + 1.0;
			double U = -1.0;
			if (!bReturn && Elapsed <= Travel)
			{
				U = Elapsed / Travel;
			}
			else if (bReturn && Elapsed > Travel)
			{
				U = 1.0 - (Elapsed - Travel) / Travel;
			}
			if (U >= 0.0 && U <= 1.0)
			{
				const double X = FMath::Lerp(I.origin.x.ToDouble(), I.point.x.ToDouble(), U);
				const double Z = FMath::Lerp(I.origin.z.ToDouble(), I.point.z.ToDouble(), U);
				Hammers.Add(FTransform(FRotator(static_cast<float>(RenderTick * 40.0), 0.f, 0.f), DoTCoords::SimToWorldD(X, Z, 6.0), FVector(2.2, 1.4, 1.4)));
			}
			continue;
		}
		const double Radius = FMath::Max(A.radius.ToDouble(), 2.0);
		Telegraphs.Add(FTransform(FRotator::ZeroRotator, DoTCoords::SimToWorld(I.point, Grid.HeightAt(I.point).ToDouble() + 0.1), FVector(Radius * 2.0, Radius * 2.0, 0.02)));
		const double Span = FMath::Max(1.0, static_cast<double>(I.impactTick - I.castTick));
		const double T = FMath::Clamp((RenderTick - I.castTick) / Span, 0.0, 1.0);
		Boulders.Add(FTransform(FRotator::ZeroRotator, DoTCoords::SimToWorld(I.point, (1.0 - T) * 60.0 + 1.0), FVector(3.0)));
	}

	TArray<FTransform> Zones;
	for (const dot::Zone& Z : Sim->Zones())
	{
		if (!Z.active)
		{
			continue;
		}
		const double Pulse = 1.0 + 0.05 * FMath::Sin(RenderTick * 0.4);
		const double D = Z.radius.ToDouble() * 2.0 * Pulse;
		Zones.Add(FTransform(FRotator::ZeroRotator, DoTCoords::SimToWorld(Z.center, Grid.HeightAt(Z.center).ToDouble() + 0.12), FVector(D, D, 0.03)));
	}
	// Grenadier fire patches share the zone discs.
	for (const dot::FirePatch& F : Sim->FirePatches())
	{
		const double D = F.radius.ToDouble() * 2.0;
		Zones.Add(FTransform(FRotator::ZeroRotator, DoTCoords::SimToWorld(F.center, Grid.HeightAt(F.center).ToDouble() + 0.1), FVector(D, D, 0.03)));
	}
	SetInstances(ArrowIsm, Arrows);
	SetInstances(BoulderIsm, Boulders);
	SetInstances(TelegraphIsm, Telegraphs);
	SetInstances(HammerIsm, Hammers);
	SetInstances(HealZoneIsm, Zones);
}

void ADoTBattleView::SpawnFx(int32 Kind, const FVector& Location, float Duration, float StartScale, float EndScale, float Height)
{
	if (Effects.Num() > 256)
	{
		return;
	}
	FDoTFx Fx;
	Fx.Kind = Kind;
	Fx.Location = Location;
	Fx.Start = Time;
	Fx.Duration = Duration;
	Fx.StartScale = StartScale;
	Fx.EndScale = EndScale;
	Fx.Height = Height;
	Effects.Add(Fx);
}

void ADoTBattleView::Shake(const FVector& Location, float BaseAmplitude)
{
	APlayerController* PC = GetWorld()->GetFirstPlayerController();
	ADoTCameraPawn* Cam = PC ? Cast<ADoTCameraPawn>(PC->GetPawn()) : nullptr;
	if (!Cam)
	{
		return;
	}
	const float DistMeters = static_cast<float>(FVector::Dist2D(Cam->GetFocus(), Location)) / 100.f;
	const float Falloff = FMath::Clamp(1.f - DistMeters / 70.f, 0.f, 1.f);
	if (Falloff > 0.f)
	{
		Cam->AddShake(BaseAmplitude * Falloff);
	}
}

void ADoTBattleView::HandleEvents()
{
	const dot::BattleSimulation* Sim = Manager->GetSim();
	const dot::NavGrid& Grid = Sim->Grid();
	for (const dot::SimEvent& E : Manager->GetFrameEvents())
	{
		const FVector Where = DoTCoords::SimToWorld(E.position, Grid.HeightAt(E.position).ToDouble());
		auto TitanVisual = [this](int32 Id) -> FDoTTitanVisual*
		{
			const int32 Index = Id - dot::kTitanIdBase;
			return TitanVisuals.IsValidIndex(Index) ? &TitanVisuals[Index] : nullptr;
		};
		switch (E.type)
		{
		case dot::SimEventType::TitanAttack:
			if (FDoTTitanVisual* V = TitanVisual(E.a))
			{
				V->AttackStart = Time;
				V->AttackCombo = E.value;
			}
			break;
		case dot::SimEventType::TitanSmash:
			SpawnFx(Fx_Dust, Where, 0.6f, 2.f, 12.f);
			Shake(Where, 45.f);
			break;
		case dot::SimEventType::TitanStomp:
			if (FDoTTitanVisual* V = TitanVisual(E.a))
			{
				V->StompStart = Time - 0.25f;
			}
			SpawnFx(Fx_Dust, Where, 0.8f, 3.f, 18.f);
			Shake(Where, 70.f);
			break;
		case dot::SimEventType::TitanFootstep:
			SpawnFx(Fx_Dust, Where, 0.4f, 1.f, 5.f);
			Shake(Where, 10.f);
			break;
		case dot::SimEventType::TitanGrab:
			if (FDoTTitanVisual* V = TitanVisual(E.a))
			{
				V->GrabStart = Time;
			}
			break;
		case dot::SimEventType::TitanThrowImpact:
			SpawnFx(Fx_Dust, Where, 0.6f, 2.f, 9.f);
			Shake(Where, 25.f);
			break;
		case dot::SimEventType::AbilityCast:
			if (FDoTTitanVisual* V = TitanVisual(E.a))
			{
				V->CastStart = Time;
			}
			break;
		case dot::SimEventType::AbilityImpact:
			SpawnFx(Fx_Lightning, Where + FVector(0.0, 0.0, 300.0), 0.45f, 3.f, 14.f);
			SpawnFx(Fx_Shock, Where, 0.5f, 2.f, 16.f);
			Shake(Where, 35.f);
			break;
		case dot::SimEventType::SpellImpact:
			SpawnFx(Fx_Fire, Where, 0.7f, 3.f, 13.f);
			SpawnFx(Fx_Dust, Where, 0.9f, 2.f, 16.f);
			Shake(Where, 60.f);
			break;
		case dot::SimEventType::SpellCast:
			SpawnFx(Fx_Heal, Where, 0.6f, 1.f, 10.f);
			break;
		case dot::SimEventType::ClashStarted:
		case dot::SimEventType::ClashExchange:
			SpawnFx(Fx_Shock, Where + FVector(0.0, 0.0, 800.0), 0.5f, 3.f, 22.f, 0.3f);
			SpawnFx(Fx_Lightning, Where + FVector(0.0, 0.0, 1100.0), 0.3f, 2.f, 6.f);
			Shake(Where, 50.f);
			break;
		case dot::SimEventType::TitanStaggered:
			SpawnFx(Fx_Stagger, Where + FVector(0.0, 0.0, 2000.0), 1.2f, 2.f, 4.f);
			break;
		case dot::SimEventType::TitanDied:
			SpawnFx(Fx_Dust, Where, 1.6f, 4.f, 40.f);
			Shake(Where, 120.f);
			break;
		case dot::SimEventType::ChargeImpact:
			SpawnFx(Fx_Dust, Where, 0.5f, 1.f, 6.f);
			break;
		case dot::SimEventType::FirebombImpact:
			SpawnFx(Fx_Dust, Where, 0.5f, 1.f, 5.f);
			break;
		case dot::SimEventType::TitanBound:
			SpawnFx(Fx_Stagger, Where + FVector(0.0, 0.0, 2000.0), 1.4f, 2.f, 5.f);
			SpawnFx(Fx_Shock, Where, 0.6f, 3.f, 14.f);
			break;
		default: break;
		}
	}
}

void ADoTBattleView::UpdateFx()
{
	for (TArray<FTransform>& Arr : FxTransforms)
	{
		Arr.Reset();
	}
	for (int32 I = Effects.Num() - 1; I >= 0; --I)
	{
		const FDoTFx& Fx = Effects[I];
		const float T = (Time - Fx.Start) / FMath::Max(Fx.Duration, 0.01f);
		if (T >= 1.f)
		{
			Effects.RemoveAtSwap(I);
			continue;
		}
		const float S = FMath::Lerp(Fx.StartScale, Fx.EndScale, T);
		const bool bSphere = Fx.Kind == Fx_Fire || Fx.Kind == Fx_Lightning || Fx.Kind == Fx_Stagger;
		const FVector Scale = bSphere ? FVector(S * (1.f - 0.4f * T)) : FVector(S, S, Fx.Height);
		FxTransforms[Fx.Kind].Add(FTransform(FRotator::ZeroRotator, Fx.Location + FVector(0.0, 0.0, 15.0), Scale));
	}
	for (int32 K = 0; K < FxIsms.Num() && K < FxTransforms.Num(); ++K)
	{
		SetInstances(FxIsms[K], FxTransforms[K]);
	}
}
