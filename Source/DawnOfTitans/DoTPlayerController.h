// Source/DawnOfTitans/DoTPlayerController.h
// Battle input (master prompt 4.5): one-finger/mouse commands, path drawing with smoothing,
// double-tap charge, long-press command radial, pinch/wheel zoom, keyboard shortcuts.
// Every input becomes a deterministic dot::BattleCommand queued on the battle manager.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"

#include "DoTHUD.h"

#include <vector>

#include "DoTPlayerController.generated.h"

class ADoTBattleManager;
class ADoTCameraPawn;

namespace dot
{
struct FixVec2;
}

UCLASS()
class DAWNOFTITANS_API ADoTPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	ADoTPlayerController();
	virtual void BeginPlay() override;
	virtual void PlayerTick(float DeltaTime) override;

	/** -1 = nothing, 0 = all squads, 1..999 = squad id, >= 1000 = titan id. */
	int32 GetSelectedUnit() const { return Selected; }
	bool IsDrawingPath() const { return bDrawing; }
	const TArray<FVector>& GetDrawPoints() const { return DrawPoints; }
	/** 0 = none, 1/2 = titan ability 1/2, 10 + n = spell slot n. */
	int32 GetTargetingMode() const { return TargetingMode; }
	int32 GetDraggingSpell() const { return DraggingSpell; }
	bool GetPointerGround(FVector& OutWorld) const;
	bool IsRadialOpen() const { return bRadialOpen; }
	FVector2D GetRadialCenter() const { return RadialCenter; }
	int32 GetRadialUnit() const { return RadialUnit; }
	bool IsFollowingTitan() const { return bFollowTitan; }

	void HandleHudAction(EDoTHudAction Action, int32 Param);

private:
	ADoTBattleManager* GetManager() const;
	mutable TWeakObjectPtr<ADoTBattleManager> CachedManager;
	ADoTCameraPawn* GetCameraPawn() const;
	ADoTHUD* GetDoTHud() const;

	bool ScreenToGround(const FVector2D& Screen, FVector& OutWorld) const;
	int32 PickUnit(const FVector& World, bool bEnemy) const;
	void Select(int32 UnitId);

	void OnPointerDown(const FVector2D& Pos, bool bRight);
	void OnPointerMove(const FVector2D& Pos);
	void OnPointerUp(const FVector2D& Pos);
	void HandleWorldTap(const FVector2D& Pos, bool bDouble, bool bRight);
	void FinalizePath();
	void UpdateKeyboard(float DeltaTime);
	void UpdatePinch();

	void Send(uint8 Type, int32 UnitId, const FVector* World, int32 TargetId, int32 Param, const std::vector<dot::FixVec2>* Waypoints = nullptr);
	void CastSpellAt(int32 Slot, const FVector& World);
	void CastAbilityAt(int32 Slot, const FVector& World);
	void SendClashInput();

	int32 Selected = -1;
	int32 TargetingMode = 0;
	int32 DraggingSpell = -1;

	bool bPointerDown = false;
	bool bRightButton = false;
	bool bDragging = false;
	bool bPanning = false;
	bool bDrawing = false;
	bool bDeployDrag = false;
	bool bLongPressFired = false;
	FVector2D DownPos = FVector2D::ZeroVector;
	FVector2D LastPos = FVector2D::ZeroVector;
	double DownTime = 0.0;
	double LastTapTime = -10.0;
	FVector2D LastTapPos = FVector2D::ZeroVector;
	int32 DragUnit = -1;
	int32 DrawUnit = -1;
	TArray<FVector> DrawPoints;
	EDoTHudAction PressedAction = EDoTHudAction::None;
	int32 PressedParam = 0;

	bool bPinching = false;
	float PinchStartDistance = 0.f;
	float PinchStartZoom = 0.f;

	bool bRadialOpen = false;
	FVector2D RadialCenter = FVector2D::ZeroVector;
	int32 RadialUnit = -1;
	bool bFollowTitan = false;
};
