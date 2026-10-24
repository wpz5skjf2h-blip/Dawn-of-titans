// Source/DawnOfTitans/DoTCameraPawn.h
// Battle camera (master prompt 4.4): perspective, pitch eases 45 deg..58 deg with zoom,
// pan with inertia, pinch/wheel zoom, titan follow, distance-scaled camera shake.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"

#include "DoTCameraPawn.generated.h"

class UCameraComponent;

UCLASS()
class DAWNOFTITANS_API ADoTCameraPawn : public APawn
{
	GENERATED_BODY()

public:
	ADoTCameraPawn();
	virtual void Tick(float DeltaSeconds) override;

	/** Battlefield bounds in world units (cm), XY plane. */
	void SetBounds(const FBox2D& InBounds);
	void PanByScreenDelta(const FVector2D& DeltaPixels, const FVector2D& ViewportSize);
	void PanWorld(const FVector& DeltaWorld);
	void Zoom(float Factor);
	void SetZoomDistance(float DistanceCm);
	void FocusOn(const FVector& Point);
	void SetFollowPoint(bool bEnabled, const FVector& Point);
	/** Adds a shake impulse; amplitude in cm, already scaled by distance by the caller. */
	void AddShake(float AmplitudeCm);
	void SetShakeScale(float InScale) { ShakeScale = InScale; }
	float GetDistance() const { return Distance; }
	FVector GetFocus() const { return Focus; }

	UPROPERTY(VisibleAnywhere, Category = "Camera")
	TObjectPtr<UCameraComponent> Camera;

	UPROPERTY(EditAnywhere, Category = "Camera")
	float MinDistance = 4500.f;

	UPROPERTY(EditAnywhere, Category = "Camera")
	float MaxDistance = 15000.f;

private:
	void ApplyTransform();

	FVector Focus = FVector::ZeroVector;
	FVector PanVelocity = FVector::ZeroVector;
	FBox2D Bounds = FBox2D(FVector2D(-100000.0, -100000.0), FVector2D(100000.0, 100000.0));
	float Distance = 9500.f;
	float ShakeAmplitude = 0.f;
	float ShakeScale = 1.f;
	float ShakeTime = 0.f;
	bool bFollow = false;
	FVector FollowPoint = FVector::ZeroVector;
	bool bDraggedThisFrame = false;
};
