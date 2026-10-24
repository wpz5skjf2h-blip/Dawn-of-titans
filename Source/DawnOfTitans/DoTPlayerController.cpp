// Source/DawnOfTitans/DoTPlayerController.cpp
#include "DoTPlayerController.h"

#include "DoTBattleManager.h"
#include "DoTCameraPawn.h"
#include "DoTCoords.h"
#include "DoTHUD.h"

#include "EngineUtils.h"
#include "HAL/PlatformTime.h"
#include "InputCoreTypes.h"

#include "DoTSim/BattleSimulation.h"

#include <vector>

namespace
{
struct FPathPoint
{
	double X = 0.0;
	double Z = 0.0;
};

double PointLineDistance(const FPathPoint& P, const FPathPoint& A, const FPathPoint& B)
{
	const double Dx = B.X - A.X;
	const double Dz = B.Z - A.Z;
	const double LenSq = Dx * Dx + Dz * Dz;
	if (LenSq < 1e-9)
	{
		return FMath::Sqrt((P.X - A.X) * (P.X - A.X) + (P.Z - A.Z) * (P.Z - A.Z));
	}
	const double T = FMath::Clamp(((P.X - A.X) * Dx + (P.Z - A.Z) * Dz) / LenSq, 0.0, 1.0);
	const double Px = A.X + Dx * T;
	const double Pz = A.Z + Dz * T;
	return FMath::Sqrt((P.X - Px) * (P.X - Px) + (P.Z - Pz) * (P.Z - Pz));
}

/** Ramer-Douglas-Peucker simplification. */
void Simplify(const std::vector<FPathPoint>& In, size_t First, size_t Last, double Epsilon, std::vector<bool>& Keep)
{
	if (Last <= First + 1)
	{
		return;
	}
	double MaxDist = 0.0;
	size_t Index = First;
	for (size_t I = First + 1; I < Last; ++I)
	{
		const double D = PointLineDistance(In[I], In[First], In[Last]);
		if (D > MaxDist)
		{
			MaxDist = D;
			Index = I;
		}
	}
	if (MaxDist > Epsilon)
	{
		Keep[Index] = true;
		Simplify(In, First, Index, Epsilon, Keep);
		Simplify(In, Index, Last, Epsilon, Keep);
	}
}

/** Chaikin corner cutting (one iteration). */
std::vector<FPathPoint> Chaikin(const std::vector<FPathPoint>& In)
{
	if (In.size() < 3)
	{
		return In;
	}
	std::vector<FPathPoint> Out;
	Out.push_back(In.front());
	for (size_t I = 0; I + 1 < In.size(); ++I)
	{
		const FPathPoint& A = In[I];
		const FPathPoint& B = In[I + 1];
		Out.push_back({A.X * 0.75 + B.X * 0.25, A.Z * 0.75 + B.Z * 0.25});
		Out.push_back({A.X * 0.25 + B.X * 0.75, A.Z * 0.25 + B.Z * 0.75});
	}
	Out.push_back(In.back());
	return Out;
}
} // namespace

ADoTPlayerController::ADoTPlayerController()
{
	bShowMouseCursor = true;
	bEnableClickEvents = false;
	bEnableTouchEvents = false;
	PrimaryActorTick.bCanEverTick = true;
}

void ADoTPlayerController::BeginPlay()
{
	Super::BeginPlay();
	FInputModeGameAndUI Mode;
	Mode.SetHideCursorDuringCapture(false);
	SetInputMode(Mode);
	ActivateTouchInterface(nullptr);
}

ADoTBattleManager* ADoTPlayerController::GetManager() const
{
	if (!CachedManager.IsValid())
	{
		TActorIterator<ADoTBattleManager> It(GetWorld());
		CachedManager = It ? *It : nullptr;
	}
	return CachedManager.Get();
}

ADoTCameraPawn* ADoTPlayerController::GetCameraPawn() const
{
	return Cast<ADoTCameraPawn>(GetPawn());
}

ADoTHUD* ADoTPlayerController::GetDoTHud() const
{
	return Cast<ADoTHUD>(GetHUD());
}

bool ADoTPlayerController::ScreenToGround(const FVector2D& Screen, FVector& OutWorld) const
{
	FVector Origin;
	FVector Direction;
	if (!DeprojectScreenPositionToWorld(static_cast<float>(Screen.X), static_cast<float>(Screen.Y), Origin, Direction) || FMath::Abs(Direction.Z) < 1e-4)
	{
		return false;
	}
	const double T = -Origin.Z / Direction.Z;
	if (T < 0.0)
	{
		return false;
	}
	OutWorld = Origin + Direction * T;
	return true;
}

bool ADoTPlayerController::GetPointerGround(FVector& OutWorld) const
{
	return ScreenToGround(LastPos, OutWorld);
}

int32 ADoTPlayerController::PickUnit(const FVector& World, bool bEnemy) const
{
	const ADoTBattleManager* Manager = GetManager();
	const dot::BattleSimulation* Sim = Manager ? Manager->GetSim() : nullptr;
	if (!Sim)
	{
		return -1;
	}
	const dot::Side Side = bEnemy ? dot::Side::Defender : dot::Side::Attacker;
	const dot::FixVec2 P = DoTCoords::WorldToSim(World);
	const int Titan = Sim->FindTitanAt(Side, P, dot::fx::Int(3));
	if (Titan >= 0)
	{
		return Sim->Titans()[static_cast<size_t>(Titan)].id;
	}
	const int Squad = Sim->FindSquadAt(Side, P, dot::fx::Int(5));
	if (Squad >= 0 && !bEnemy && Sim->Squads()[static_cast<size_t>(Squad)].summoned)
	{
		return -1; // raised skeletons cannot be given orders
	}
	if (Squad >= 0 && (!bEnemy || Sim->IsSquadVisibleTo(dot::Side::Attacker, Squad)))
	{
		return Sim->Squads()[static_cast<size_t>(Squad)].id;
	}
	return -1;
}

void ADoTPlayerController::Select(int32 UnitId)
{
	Selected = UnitId;
	TargetingMode = 0;
}

void ADoTPlayerController::Send(uint8 Type, int32 UnitId, const FVector* World, int32 TargetId, int32 Param, const std::vector<dot::FixVec2>* Waypoints)
{
	ADoTBattleManager* Manager = GetManager();
	if (!Manager)
	{
		return;
	}
	dot::BattleCommand Cmd;
	Cmd.side = dot::Side::Attacker;
	Cmd.type = static_cast<dot::CommandType>(Type);
	Cmd.unitId = UnitId;
	if (World)
	{
		Cmd.point = DoTCoords::WorldToSim(*World);
	}
	Cmd.targetId = TargetId;
	Cmd.param = Param;
	if (Waypoints)
	{
		Cmd.waypoints = *Waypoints;
	}
	Manager->QueueCommand(Cmd);
}

void ADoTPlayerController::CastSpellAt(int32 Slot, const FVector& World)
{
	Send(static_cast<uint8>(dot::CommandType::CastSpell), 0, &World, 0, Slot);
}

void ADoTPlayerController::CastAbilityAt(int32 Slot, const FVector& World)
{
	const ADoTBattleManager* Manager = GetManager();
	const int32 Index = Manager ? Manager->FindTitanIndex(true) : INDEX_NONE;
	if (Index != INDEX_NONE)
	{
		Send(static_cast<uint8>(dot::CommandType::CastAbility), dot::kTitanIdBase + Index, &World, 0, Slot);
	}
}

void ADoTPlayerController::SendClashInput()
{
	const ADoTBattleManager* Manager = GetManager();
	if (Manager && Manager->GetSim() && Manager->GetSim()->Clash().active)
	{
		Send(static_cast<uint8>(dot::CommandType::ClashInput), 0, nullptr, 0, Manager->ClashOffsetMsNow());
	}
}

void ADoTPlayerController::PlayerTick(float DeltaTime)
{
	Super::PlayerTick(DeltaTime);
	ADoTBattleManager* Manager = GetManager();
	const dot::BattleSimulation* Sim = Manager ? Manager->GetSim() : nullptr;
	if (!Sim)
	{
		return;
	}

	// Drop selections of units that no longer exist.
	if (Selected > 0 && !Sim->UnitAlive(Selected))
	{
		Selected = -1;
	}

	float TouchX = 0.f;
	float TouchY = 0.f;
	bool bTouch1 = false;
	GetInputTouchState(ETouchIndex::Touch1, TouchX, TouchY, bTouch1);
	float Touch2X = 0.f;
	float Touch2Y = 0.f;
	bool bTouch2 = false;
	GetInputTouchState(ETouchIndex::Touch2, Touch2X, Touch2Y, bTouch2);

	if (bTouch1 && bTouch2)
	{
		UpdatePinch();
		bPointerDown = false;
		bDragging = bPanning = bDrawing = bDeployDrag = false;
		DrawPoints.Reset();
		return;
	}
	bPinching = false;

	FVector2D Pos = LastPos;
	bool bPressed = false;
	bool bRight = false;
#if PLATFORM_ANDROID || PLATFORM_IOS
	const bool bUseTouch = true;
#else
	const bool bUseTouch = bTouch1;
#endif
	if (bUseTouch)
	{
		bPressed = bTouch1;
		if (bTouch1)
		{
			Pos = FVector2D(TouchX, TouchY);
		}
	}
	else
	{
		float MouseX = 0.f;
		float MouseY = 0.f;
		if (GetMousePosition(MouseX, MouseY))
		{
			Pos = FVector2D(MouseX, MouseY);
		}
		const bool bLeft = IsInputKeyDown(EKeys::LeftMouseButton);
		const bool bRightDown = IsInputKeyDown(EKeys::RightMouseButton);
		bPressed = bLeft || bRightDown;
		bRight = bRightDown && !bLeft;
	}

	if (!bPointerDown && bPressed)
	{
		OnPointerDown(Pos, bRight);
	}
	else if (bPointerDown && bPressed)
	{
		OnPointerMove(Pos);
	}
	else if (bPointerDown && !bPressed)
	{
		OnPointerUp(LastPos);
	}
	if (!bPointerDown)
	{
		LastPos = Pos;
	}

	// Long press (300 ms) on an own unit opens the command radial.
	if (bPointerDown && !bDragging && !bLongPressFired && DragUnit > 0 && DraggingSpell < 0 && FPlatformTime::Seconds() - DownTime >= 0.3)
	{
		bLongPressFired = true;
		bRadialOpen = true;
		RadialCenter = DownPos;
		RadialUnit = DragUnit;
		Selected = DragUnit;
	}

	if (ADoTCameraPawn* Cam = GetCameraPawn())
	{
		if (WasInputKeyJustPressed(EKeys::MouseScrollUp))
		{
			Cam->Zoom(0.88f);
		}
		if (WasInputKeyJustPressed(EKeys::MouseScrollDown))
		{
			Cam->Zoom(1.12f);
		}
		const int32 TitanIndex = Manager->FindTitanIndex(true);
		const bool bFollow = bFollowTitan && TitanIndex != INDEX_NONE && Sim->Titans()[static_cast<size_t>(TitanIndex)].Alive();
		Cam->SetFollowPoint(bFollow, bFollow ? Manager->TitanWorldPos(TitanIndex) : FVector::ZeroVector);
	}
	UpdateKeyboard(DeltaTime);
}

void ADoTPlayerController::UpdatePinch()
{
	ADoTCameraPawn* Cam = GetCameraPawn();
	if (!Cam)
	{
		return;
	}
	float X1 = 0.f;
	float Y1 = 0.f;
	float X2 = 0.f;
	float Y2 = 0.f;
	bool bA = false;
	bool bB = false;
	GetInputTouchState(ETouchIndex::Touch1, X1, Y1, bA);
	GetInputTouchState(ETouchIndex::Touch2, X2, Y2, bB);
	const float Distance = FVector2D::Distance(FVector2D(X1, Y1), FVector2D(X2, Y2));
	if (!bPinching)
	{
		bPinching = true;
		PinchStartDistance = FMath::Max(Distance, 1.f);
		PinchStartZoom = Cam->GetDistance();
		return;
	}
	Cam->SetZoomDistance(PinchStartZoom * PinchStartDistance / FMath::Max(Distance, 1.f));
}

void ADoTPlayerController::OnPointerDown(const FVector2D& Pos, bool bRight)
{
	bPointerDown = true;
	bRightButton = bRight;
	DownPos = LastPos = Pos;
	DownTime = FPlatformTime::Seconds();
	bDragging = bPanning = bDrawing = bDeployDrag = bLongPressFired = false;
	DragUnit = -1;
	DraggingSpell = -1;
	PressedAction = EDoTHudAction::None;
	PressedParam = 0;

	ADoTHUD* Hud = GetDoTHud();
	EDoTHudAction Action = EDoTHudAction::None;
	int32 Param = 0;
	if (Hud && Hud->HitTest(Pos, Action, Param))
	{
		PressedAction = Action;
		PressedParam = Param;
		if (Action == EDoTHudAction::Spell)
		{
			DraggingSpell = Param;
		}
		else if (Action == EDoTHudAction::SelectSquad || Action == EDoTHudAction::SelectTitan)
		{
			DragUnit = Param;
		}
		else if (Action == EDoTHudAction::SelectAll)
		{
			DragUnit = 0;
		}
		return;
	}
	if (bRadialOpen)
	{
		bRadialOpen = false;
		PressedAction = EDoTHudAction::CloseRadial;
		return;
	}
	if (Hud && Hud->IsOverPanel(Pos))
	{
		PressedAction = EDoTHudAction::CloseRadial; // swallow presses on panels
		return;
	}
	FVector World;
	if (TargetingMode == 0 && !bRight && ScreenToGround(Pos, World))
	{
		DragUnit = PickUnit(World, false);
	}
	if (bRight && Selected != -1)
	{
		DragUnit = Selected;
	}
}

void ADoTPlayerController::OnPointerMove(const FVector2D& Pos)
{
	const ADoTBattleManager* Manager = GetManager();
	const dot::BattleSimulation* Sim = Manager ? Manager->GetSim() : nullptr;
	ADoTHUD* Hud = GetDoTHud();
	const float Threshold = 12.f * (Hud ? Hud->GetUiScale() : 1.f);
	if (!bDragging && FVector2D::Distance(Pos, DownPos) > Threshold && !bLongPressFired)
	{
		bDragging = true;
		const dot::BattlePhase Phase = Sim ? Sim->Phase() : dot::BattlePhase::Finished;
		if (DraggingSpell >= 0)
		{
			// Spell card drag: cast on release.
		}
		else if (DragUnit > 0 && Phase == dot::BattlePhase::Deploy)
		{
			bDeployDrag = true;
		}
		else if (DragUnit != -1 && TargetingMode == 0 && Phase != dot::BattlePhase::Finished && PressedAction != EDoTHudAction::CloseRadial)
		{
			bDrawing = true;
			DrawUnit = DragUnit;
			DrawPoints.Reset();
			if (Sim && DragUnit > 0)
			{
				DrawPoints.Add(DoTCoords::SimToWorld(Sim->UnitPosition(DragUnit)));
			}
		}
		else if (PressedAction == EDoTHudAction::None)
		{
			bPanning = true;
		}
	}
	if (bPanning)
	{
		if (ADoTCameraPawn* Cam = GetCameraPawn())
		{
			int32 SizeX = 0;
			int32 SizeY = 0;
			GetViewportSize(SizeX, SizeY);
			Cam->PanByScreenDelta(Pos - LastPos, FVector2D(SizeX, SizeY));
		}
	}
	if (bDrawing)
	{
		FVector World;
		const bool bOverPanel = Hud && Hud->IsOverPanel(Pos);
		if (!bOverPanel && ScreenToGround(Pos, World))
		{
			double Length = 0.0;
			for (int32 I = 1; I < DrawPoints.Num(); ++I)
			{
				Length += FVector::Dist2D(DrawPoints[I - 1], DrawPoints[I]);
			}
			if ((DrawPoints.Num() == 0 || FVector::Dist2D(World, DrawPoints.Last()) >= 50.0) && Length < 40000.0)
			{
				DrawPoints.Add(World);
			}
		}
	}
	LastPos = Pos;
}

void ADoTPlayerController::OnPointerUp(const FVector2D& Pos)
{
	bPointerDown = false;
	ADoTHUD* Hud = GetDoTHud();
	const double Now = FPlatformTime::Seconds();
	if (bLongPressFired)
	{
		bDragging = bPanning = bDrawing = bDeployDrag = false;
		PressedAction = EDoTHudAction::None;
		return;
	}
	if (PressedAction != EDoTHudAction::None)
	{
		if (DraggingSpell >= 0)
		{
			FVector World;
			if (bDragging && !(Hud && Hud->IsOverPanel(Pos)) && ScreenToGround(Pos, World))
			{
				CastSpellAt(DraggingSpell, World);
				TargetingMode = 0;
			}
			else if (!bDragging)
			{
				HandleHudAction(EDoTHudAction::Spell, DraggingSpell);
			}
			DraggingSpell = -1;
		}
		else if (bDrawing)
		{
			FinalizePath();
		}
		else if (!bDragging && PressedAction != EDoTHudAction::CloseRadial)
		{
			EDoTHudAction Action = EDoTHudAction::None;
			int32 Param = 0;
			if (Hud && Hud->HitTest(Pos, Action, Param) && Action == PressedAction && Param == PressedParam)
			{
				HandleHudAction(Action, Param);
			}
		}
		bDragging = bPanning = bDrawing = bDeployDrag = false;
		PressedAction = EDoTHudAction::None;
		DragUnit = -1;
		return;
	}
	if (bDrawing)
	{
		FinalizePath();
	}
	else if (bDeployDrag)
	{
		FVector World;
		if (ScreenToGround(Pos, World))
		{
			Send(static_cast<uint8>(dot::CommandType::DeployMove), DragUnit, &World, 0, 0);
			Selected = DragUnit;
		}
	}
	else if (!bDragging)
	{
		const float Radius = 40.f * (Hud ? Hud->GetUiScale() : 1.f);
		const bool bDouble = Now - LastTapTime < 0.3 && FVector2D::Distance(Pos, LastTapPos) < Radius;
		HandleWorldTap(Pos, bDouble, bRightButton);
		LastTapTime = bDouble ? -10.0 : Now;
		LastTapPos = Pos;
	}
	bDragging = bPanning = bDrawing = bDeployDrag = false;
	DragUnit = -1;
}

void ADoTPlayerController::HandleWorldTap(const FVector2D& Pos, bool bDouble, bool bRight)
{
	FVector World;
	const ADoTBattleManager* Manager = GetManager();
	const dot::BattleSimulation* Sim = Manager ? Manager->GetSim() : nullptr;
	if (!Sim || !ScreenToGround(Pos, World))
	{
		return;
	}
	if (TargetingMode >= 10)
	{
		CastSpellAt(TargetingMode - 10, World);
		TargetingMode = 0;
		return;
	}
	if (TargetingMode >= 1)
	{
		CastAbilityAt(TargetingMode - 1, World);
		TargetingMode = 0;
		return;
	}
	const int32 Enemy = PickUnit(World, true);
	const int32 Own = PickUnit(World, false);
	if (Sim->Phase() == dot::BattlePhase::Deploy && !bRight)
	{
		if (Own != -1)
		{
			Select(Own);
		}
		else if (Selected > 0)
		{
			Send(static_cast<uint8>(dot::CommandType::DeployMove), Selected, &World, 0, 0);
		}
		return;
	}
	auto Order = [&]()
	{
		if (Enemy != -1)
		{
			Send(static_cast<uint8>(bDouble ? dot::CommandType::Charge : dot::CommandType::Attack), Selected, &World, Enemy, 0);
		}
		else
		{
			Send(static_cast<uint8>(bDouble ? dot::CommandType::Charge : dot::CommandType::Move), Selected, &World, 0, 0);
		}
	};
	if (bRight)
	{
		if (Selected != -1)
		{
			Order();
		}
		return;
	}
	if (Own != -1 && Enemy == -1)
	{
		Select(Own);
		return;
	}
	if (Selected != -1)
	{
		Order();
		return;
	}
	if (Own != -1)
	{
		Select(Own);
	}
}

void ADoTPlayerController::FinalizePath()
{
	bDrawing = false;
	if (DrawPoints.Num() < 2 || DrawUnit == -1)
	{
		DrawPoints.Reset();
		return;
	}
	std::vector<FPathPoint> Points;
	for (const FVector& P : DrawPoints)
	{
		Points.push_back({P.Y / 100.0, P.X / 100.0}); // world -> sim (x, z)
	}
	Points = Chaikin(Chaikin(Points));
	std::vector<bool> Keep(Points.size(), false);
	Keep.front() = Keep.back() = true;
	Simplify(Points, 0, Points.size() - 1, 0.75, Keep);
	std::vector<FPathPoint> Simplified;
	for (size_t I = 0; I < Points.size(); ++I)
	{
		if (Keep[I])
		{
			Simplified.push_back(Points[I]);
		}
	}
	// Drop the starting point (the unit itself) and cap at 24 waypoints.
	if (Simplified.size() > 1 && DrawUnit > 0)
	{
		Simplified.erase(Simplified.begin());
	}
	std::vector<dot::FixVec2> Waypoints;
	const size_t Count = Simplified.size();
	const size_t MaxPoints = 24;
	for (size_t I = 0; I < FMath::Min(Count, MaxPoints); ++I)
	{
		const size_t Source = Count <= MaxPoints ? I : (I * (Count - 1)) / (MaxPoints - 1);
		Waypoints.emplace_back(dot::Fix64::FromDouble(Simplified[Source].X), dot::Fix64::FromDouble(Simplified[Source].Z));
	}
	const FVector End = DrawPoints.Last();
	const int32 Enemy = PickUnit(End, true);
	Send(static_cast<uint8>(dot::CommandType::Path), DrawUnit, &End, Enemy == -1 ? 0 : Enemy, 0, &Waypoints);
	Selected = DrawUnit;
	DrawPoints.Reset();
}

void ADoTPlayerController::HandleHudAction(EDoTHudAction Action, int32 Param)
{
	ADoTBattleManager* Manager = GetManager();
	const dot::BattleSimulation* Sim = Manager ? Manager->GetSim() : nullptr;
	if (!Manager || !Sim)
	{
		return;
	}
	auto FocusOnUnit = [this, Sim](int32 Unit)
	{
		if (ADoTCameraPawn* Cam = GetCameraPawn())
		{
			Cam->FocusOn(DoTCoords::SimToWorld(Sim->UnitPosition(Unit)));
		}
	};
	switch (Action)
	{
	case EDoTHudAction::SelectSquad:
	case EDoTHudAction::SelectTitan:
		if (Selected == Param)
		{
			FocusOnUnit(Param);
		}
		Select(Param);
		break;
	case EDoTHudAction::SelectAll: Select(0); break;
	case EDoTHudAction::Ability:
	{
		const int32 Index = Manager->FindTitanIndex(true);
		if (Index == INDEX_NONE || !Sim->Titans()[static_cast<size_t>(Index)].Alive())
		{
			break;
		}
		const dot::Titan& T = Sim->Titans()[static_cast<size_t>(Index)];
		const int AbilityIndex = Sim->TitanDefOf(T).abilities[FMath::Clamp(Param, 0, 1)];
		if (AbilityIndex >= 0 && Sim->Content().abilities[static_cast<size_t>(AbilityIndex)].shape == dot::TargetShape::Self)
		{
			const FVector Here = DoTCoords::SimToWorld(T.pos);
			CastAbilityAt(Param, Here);
			TargetingMode = 0;
		}
		else
		{
			TargetingMode = TargetingMode == 1 + Param ? 0 : 1 + Param;
		}
		break;
	}
	case EDoTHudAction::Spell: TargetingMode = TargetingMode == 10 + Param ? 0 : 10 + Param; break;
	case EDoTHudAction::Clash: SendClashInput(); break;
	case EDoTHudAction::Fight: Send(static_cast<uint8>(dot::CommandType::StartBattle), 0, nullptr, 0, 0); break;
	case EDoTHudAction::AutoDeploy: Send(static_cast<uint8>(dot::CommandType::AutoDeploy), 0, nullptr, 0, 0); break;
	case EDoTHudAction::Pause: Manager->TogglePause(); break;
	case EDoTHudAction::Speed: Manager->CycleSpeed(); break;
	case EDoTHudAction::AutoAll: Manager->SetAllAuto(!Manager->IsAllAuto()); break;
	case EDoTHudAction::Restart:
		Manager->RestartBattle(false);
		Selected = -1;
		TargetingMode = 0;
		break;
	case EDoTHudAction::Replay:
		Manager->StartReplayPlayback();
		Selected = -1;
		break;
	case EDoTHudAction::SaveReplay:
	{
		FString Path;
		if (ADoTHUD* Hud = GetDoTHud())
		{
			Hud->ShowToast(Manager->SaveReplay(Path) ? FString::Printf(TEXT("Replay saved: %s"), *Path) : FString(TEXT("Could not save replay")),
				FLinearColor::White);
		}
		break;
	}
	case EDoTHudAction::EnemyAi:
		Manager->CycleEnemyAi();
		Selected = -1;
		break;
	case EDoTHudAction::FollowTitan: bFollowTitan = !bFollowTitan; break;
	case EDoTHudAction::SetFormation:
		Send(static_cast<uint8>(dot::CommandType::SetFormation), RadialUnit, nullptr, 0, Param);
		bRadialOpen = false;
		break;
	case EDoTHudAction::SetStance:
		Send(static_cast<uint8>(dot::CommandType::SetStance), RadialUnit, nullptr, 0, Param);
		bRadialOpen = false;
		break;
	case EDoTHudAction::Halt:
		Send(static_cast<uint8>(dot::CommandType::Halt), RadialUnit != -1 ? RadialUnit : Selected, nullptr, 0, 0);
		bRadialOpen = false;
		break;
	case EDoTHudAction::Retreat:
		Send(static_cast<uint8>(dot::CommandType::Retreat), RadialUnit != -1 ? RadialUnit : Selected, nullptr, 0, 0);
		bRadialOpen = false;
		break;
	case EDoTHudAction::ToggleAuto:
	{
		bool bAuto = false;
		if (dot::IsTitanId(RadialUnit))
		{
			const int Index = Sim->TitanIndexFromId(RadialUnit);
			bAuto = Index >= 0 && Sim->Titans()[static_cast<size_t>(Index)].autoControl;
		}
		else
		{
			const int Index = Sim->SquadIndexFromId(RadialUnit);
			bAuto = Index >= 0 && Sim->Squads()[static_cast<size_t>(Index)].autoControl;
		}
		Send(static_cast<uint8>(dot::CommandType::SetAuto), RadialUnit, nullptr, 0, bAuto ? 0 : 1);
		bRadialOpen = false;
		break;
	}
	case EDoTHudAction::CloseRadial: bRadialOpen = false; break;
	default: break;
	}
}

void ADoTPlayerController::UpdateKeyboard(float DeltaTime)
{
	ADoTBattleManager* Manager = GetManager();
	const dot::BattleSimulation* Sim = Manager ? Manager->GetSim() : nullptr;
	if (!Sim)
	{
		return;
	}
	// 1..6 select the player's squads in order.
	const FKey SquadKeys[6] = {EKeys::One, EKeys::Two, EKeys::Three, EKeys::Four, EKeys::Five, EKeys::Six};
	std::vector<int32> OwnSquads;
	for (const dot::Squad& Sq : Sim->Squads())
	{
		if (Sq.side == dot::Side::Attacker && !Sq.summoned)
		{
			OwnSquads.push_back(Sq.id);
		}
	}
	for (int32 K = 0; K < 6; ++K)
	{
		if (WasInputKeyJustPressed(SquadKeys[K]) && K < static_cast<int32>(OwnSquads.size()) && Sim->UnitAlive(OwnSquads[static_cast<size_t>(K)]))
		{
			HandleHudAction(EDoTHudAction::SelectSquad, OwnSquads[static_cast<size_t>(K)]);
		}
	}
	if (WasInputKeyJustPressed(EKeys::Tilde))
	{
		Select(0);
	}
	if (WasInputKeyJustPressed(EKeys::T))
	{
		const int32 Index = Manager->FindTitanIndex(true);
		if (Index != INDEX_NONE)
		{
			HandleHudAction(EDoTHudAction::SelectTitan, dot::kTitanIdBase + Index);
		}
	}
	if (WasInputKeyJustPressed(EKeys::Q)) HandleHudAction(EDoTHudAction::Ability, 0);
	if (WasInputKeyJustPressed(EKeys::E)) HandleHudAction(EDoTHudAction::Ability, 1);
	if (WasInputKeyJustPressed(EKeys::Z)) HandleHudAction(EDoTHudAction::Spell, 0);
	if (WasInputKeyJustPressed(EKeys::X)) HandleHudAction(EDoTHudAction::Spell, 1);
	if (WasInputKeyJustPressed(EKeys::C)) HandleHudAction(EDoTHudAction::Spell, 2);
	if (WasInputKeyJustPressed(EKeys::V)) HandleHudAction(EDoTHudAction::Spell, 3);

	if (dot::IsSquadId(Selected) && (WasInputKeyJustPressed(EKeys::F) || WasInputKeyJustPressed(EKeys::G)))
	{
		const dot::Squad& Sq = Sim->Squads()[static_cast<size_t>(Sim->SquadIndexFromId(Selected))];
		const dot::TroopDef& TD = Sim->TroopOf(Sq);
		if (WasInputKeyJustPressed(EKeys::F))
		{
			int32 Next = static_cast<int32>(Sq.formation);
			for (int32 Step = 0; Step < 5; ++Step)
			{
				Next = (Next + 1) % static_cast<int32>(dot::Formation::Count);
				if (TD.AllowsFormation(static_cast<dot::Formation>(Next)))
				{
					break;
				}
			}
			Send(static_cast<uint8>(dot::CommandType::SetFormation), Selected, nullptr, 0, Next);
		}
		else
		{
			Send(static_cast<uint8>(dot::CommandType::SetStance), Selected, nullptr, 0, (static_cast<int32>(Sq.stance) + 1) % static_cast<int32>(dot::Stance::Count));
		}
	}
	if (WasInputKeyJustPressed(EKeys::H) && Selected != -1) Send(static_cast<uint8>(dot::CommandType::Halt), Selected, nullptr, 0, 0);
	if (WasInputKeyJustPressed(EKeys::R) && Selected != -1) Send(static_cast<uint8>(dot::CommandType::Retreat), Selected, nullptr, 0, 0);
	if (WasInputKeyJustPressed(EKeys::SpaceBar))
	{
		if (Sim->Clash().active)
		{
			SendClashInput();
		}
		else
		{
			Manager->TogglePause();
		}
	}
	if (WasInputKeyJustPressed(EKeys::Tab)) Manager->CycleSpeed();
	if (WasInputKeyJustPressed(EKeys::Enter) && Sim->Phase() == dot::BattlePhase::Deploy) HandleHudAction(EDoTHudAction::Fight, 0);
	if (WasInputKeyJustPressed(EKeys::Escape))
	{
		TargetingMode = 0;
		bRadialOpen = false;
		Selected = -1;
	}
	if (WasInputKeyJustPressed(EKeys::F5)) HandleHudAction(EDoTHudAction::Restart, 0);
	if (WasInputKeyJustPressed(EKeys::F6)) HandleHudAction(EDoTHudAction::Replay, 0);
	if (WasInputKeyJustPressed(EKeys::F7)) HandleHudAction(EDoTHudAction::EnemyAi, 0);
	if (WasInputKeyJustPressed(EKeys::F8)) HandleHudAction(EDoTHudAction::AutoAll, 0);
	if (WasInputKeyJustPressed(EKeys::F9)) HandleHudAction(EDoTHudAction::SaveReplay, 0);

	if (ADoTCameraPawn* Cam = GetCameraPawn())
	{
		FVector Pan = FVector::ZeroVector;
		if (IsInputKeyDown(EKeys::W) || IsInputKeyDown(EKeys::Up)) Pan.X += 1.0;
		if (IsInputKeyDown(EKeys::S) || IsInputKeyDown(EKeys::Down)) Pan.X -= 1.0;
		if (IsInputKeyDown(EKeys::D) || IsInputKeyDown(EKeys::Right)) Pan.Y += 1.0;
		if (IsInputKeyDown(EKeys::A) || IsInputKeyDown(EKeys::Left)) Pan.Y -= 1.0;
		if (!Pan.IsZero())
		{
			Cam->PanWorld(Pan.GetSafeNormal() * Cam->GetDistance() * 0.8f * DeltaTime);
		}
	}
}
