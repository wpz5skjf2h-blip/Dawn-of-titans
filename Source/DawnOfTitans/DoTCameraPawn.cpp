// Source/DawnOfTitans/DoTCameraPawn.cpp
#include "DoTCameraPawn.h"

#include "Camera/CameraComponent.h"

ADoTCameraPawn::ADoTCameraPawn()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PostPhysics;
	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	RootComponent = Camera;
	// 38 deg vertical FOV (4.4) is about 62 deg horizontal at 16:9.
	Camera->SetFieldOfView(62.f);
	Camera->bConstrainAspectRatio = false;
}

void ADoTCameraPawn::SetBounds(const FBox2D& InBounds)
{
	// Allow looking 15 m beyond the playable area.
	Bounds = FBox2D(InBounds.Min - FVector2D(1500.0, 1500.0), InBounds.Max + FVector2D(1500.0, 1500.0));
	Focus.X = FMath::Clamp(Focus.X, Bounds.Min.X, Bounds.Max.X);
	Focus.Y = FMath::Clamp(Focus.Y, Bounds.Min.Y, Bounds.Max.Y);
}

void ADoTCameraPawn::PanByScreenDelta(const FVector2D& DeltaPixels, const FVector2D& ViewportSize)
{
	if (ViewportSize.X <= 1.0)
	{
		return;
	}
	// World units per pixel at the focus distance; "grab the ground" semantics.
	const float HalfFov = FMath::DegreesToRadians(Camera->FieldOfView * 0.5f);
	const float WorldPerPixel = 2.f * Distance * FMath::Tan(HalfFov) / static_cast<float>(ViewportSize.X);
	const FVector Delta(DeltaPixels.Y * WorldPerPixel * 1.3f, -DeltaPixels.X * WorldPerPixel, 0.f);
	PanWorld(Delta);
	PanVelocity = Delta / FMath::Max(GetWorld()->GetDeltaSeconds(), 0.001f);
	bDraggedThisFrame = true;
	bFollow = false;
}

void ADoTCameraPawn::PanWorld(const FVector& DeltaWorld)
{
	Focus += FVector(DeltaWorld.X, DeltaWorld.Y, 0.f);
	Focus.X = FMath::Clamp(Focus.X, Bounds.Min.X, Bounds.Max.X);
	Focus.Y = FMath::Clamp(Focus.Y, Bounds.Min.Y, Bounds.Max.Y);
}

void ADoTCameraPawn::Zoom(float Factor)
{
	Distance = FMath::Clamp(Distance * Factor, MinDistance, MaxDistance);
}

void ADoTCameraPawn::SetZoomDistance(float DistanceCm)
{
	Distance = FMath::Clamp(DistanceCm, MinDistance, MaxDistance);
}

void ADoTCameraPawn::FocusOn(const FVector& Point)
{
	Focus = FVector(Point.X, Point.Y, 0.f);
	PanVelocity = FVector::ZeroVector;
	PanWorld(FVector::ZeroVector);
}

void ADoTCameraPawn::SetFollowPoint(bool bEnabled, const FVector& Point)
{
	bFollow = bEnabled;
	FollowPoint = Point;
}

void ADoTCameraPawn::AddShake(float AmplitudeCm)
{
	ShakeAmplitude = FMath::Min(ShakeAmplitude + AmplitudeCm * ShakeScale, 120.f);
}

void ADoTCameraPawn::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (bFollow)
	{
		const FVector Target(FollowPoint.X, FollowPoint.Y, 0.f);
		Focus = FMath::VInterpTo(Focus, Target, DeltaSeconds, 3.f);
	}
	else if (!bDraggedThisFrame && !PanVelocity.IsNearlyZero(1.f))
	{
		// Inertia: 0.92 damping per frame at 60 fps.
		PanWorld(PanVelocity * DeltaSeconds);
		PanVelocity *= FMath::Pow(0.92f, DeltaSeconds * 60.f);
	}
	bDraggedThisFrame = false;
	ShakeTime += DeltaSeconds;
	ShakeAmplitude = FMath::Max(0.f, ShakeAmplitude - ShakeAmplitude * FMath::Min(1.f, DeltaSeconds * 12.f));
	ApplyTransform();
}

void ADoTCameraPawn::ApplyTransform()
{
	const float T = FMath::Clamp((Distance - MinDistance) / FMath::Max(MaxDistance - MinDistance, 1.f), 0.f, 1.f);
	const float Pitch = FMath::Lerp(45.f, 58.f, T);
	const FRotator Rotation(-Pitch, 0.f, 0.f);
	FVector Location = Focus - Rotation.Vector() * Distance;
	if (ShakeAmplitude > 0.5f)
	{
		Location += FVector(FMath::Sin(ShakeTime * 71.f), FMath::Sin(ShakeTime * 53.f + 1.3f), FMath::Sin(ShakeTime * 89.f + 2.1f)) * ShakeAmplitude;
	}
	SetActorLocationAndRotation(Location, Rotation);
}
