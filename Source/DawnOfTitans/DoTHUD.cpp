// Source/DawnOfTitans/DoTHUD.cpp
#include "DoTHUD.h"

#include "DoTBattleManager.h"
#include "DoTCoords.h"
#include "DoTPlayerController.h"

#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "EngineUtils.h"
#include "GenericPlatform/GenericApplication.h"
#include "HAL/PlatformTime.h"

#include "DoTSim/BattleSimulation.h"
#include "DoTSim/Content.h"

namespace
{
const FLinearColor GPanel(0.03f, 0.05f, 0.08f, 0.82f);
const FLinearColor GPanelLight(0.10f, 0.13f, 0.18f, 0.90f);
const FLinearColor GGold(0.91f, 0.72f, 0.36f);
const FLinearColor GOwn(0.23f, 0.55f, 0.87f);
const FLinearColor GEnemy(0.85f, 0.28f, 0.23f);
const FLinearColor GText(0.95f, 0.95f, 0.92f);
const FLinearColor GDim(0.55f, 0.57f, 0.60f);

FString ToF(const std::string& S)
{
	return FString(UTF8_TO_TCHAR(S.c_str()));
}

FString FormatTime(int32 Ticks)
{
	const int32 Seconds = FMath::Max(0, Ticks) / dot::kTicksPerSecond;
	return FString::Printf(TEXT("%d:%02d"), Seconds / 60, Seconds % 60);
}

FString GradeName(int32 Grade)
{
	return Grade == 3 ? TEXT("PERFECT") : (Grade == 2 ? TEXT("GOOD") : TEXT("MISS"));
}

const TCHAR* OrderName(const dot::Squad& Sq)
{
	if (Sq.Routing())
	{
		return TEXT("ROUTING");
	}
	if (Sq.InMelee())
	{
		return TEXT("Fighting");
	}
	switch (Sq.order)
	{
	case dot::OrderType::Move: return TEXT("Moving");
	case dot::OrderType::Attack: return TEXT("Attacking");
	case dot::OrderType::Retreat: return TEXT("Retreating");
	case dot::OrderType::CycleWithdraw: return TEXT("Wheeling");
	case dot::OrderType::Skirmish: return TEXT("Kiting");
	default: return Sq.autoTarget != 0 ? TEXT("Engaging") : TEXT("Holding");
	}
}
} // namespace

ADoTBattleManager* ADoTHUD::GetManager() const
{
	if (!CachedManager.IsValid())
	{
		TActorIterator<ADoTBattleManager> It(GetWorld());
		CachedManager = It ? *It : nullptr;
	}
	return CachedManager.Get();
}

ADoTPlayerController* ADoTHUD::GetDoTController() const
{
	return Cast<ADoTPlayerController>(GetOwningPlayerController());
}

bool ADoTHUD::HitTest(const FVector2D& ScreenPos, EDoTHudAction& OutAction, int32& OutParam) const
{
	for (int32 I = Buttons.Num() - 1; I >= 0; --I)
	{
		if (Buttons[I].Rect.IsInside(ScreenPos))
		{
			OutAction = Buttons[I].Action;
			OutParam = Buttons[I].Param;
			return true;
		}
	}
	return false;
}

bool ADoTHUD::IsOverPanel(const FVector2D& ScreenPos) const
{
	for (const FBox2D& Box : Panels)
	{
		if (Box.IsInside(ScreenPos))
		{
			return true;
		}
	}
	return false;
}

void ADoTHUD::ShowToast(const FString& Text, const FLinearColor& Color)
{
	FToast T;
	T.Text = Text;
	T.Color = Color;
	T.Start = HudTime;
	Toasts.Add(T);
	if (Toasts.Num() > 5)
	{
		Toasts.RemoveAt(0);
	}
}

// --- Drawing primitives --------------------------------------------------------------

void ADoTHUD::Panel(float X, float Y, float W, float H, const FLinearColor& Color)
{
	DrawRect(Color, X, Y, W, H);
	Panels.Add(FBox2D(FVector2D(X, Y), FVector2D(X + W, Y + H)));
}

void ADoTHUD::Bar(float X, float Y, float W, float H, float Fraction, const FLinearColor& Color)
{
	DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.6f), X, Y, W, H);
	DrawRect(Color, X, Y, W * FMath::Clamp(Fraction, 0.f, 1.f), H);
}

void ADoTHUD::Label(const FString& Text, float X, float Y, const FLinearColor& Color, float Scale, bool bCenter, UFont* Font)
{
	UFont* UseFont = Font ? Font : GEngine->GetMediumFont();
	const float S = Scale * UiScale;
	if (bCenter)
	{
		float Tw = 0.f;
		float Th = 0.f;
		GetTextSize(Text, Tw, Th, UseFont, S);
		X -= Tw * 0.5f;
		Y -= Th * 0.5f;
	}
	DrawText(Text, Color, X, Y, UseFont, S);
}

void ADoTHUD::Button(float X, float Y, float W, float H, const FString& Text, EDoTHudAction Action, int32 Param, bool bEnabled, bool bHighlight)
{
	const FLinearColor Bg = bHighlight ? FLinearColor(0.35f, 0.27f, 0.08f, 0.95f) : (bEnabled ? GPanelLight : FLinearColor(0.06f, 0.06f, 0.07f, 0.85f));
	Panel(X, Y, W, H, Bg);
	const FLinearColor Border = bHighlight ? GGold : FLinearColor(0.35f, 0.38f, 0.45f);
	DrawLine(X, Y, X + W, Y, Border, 1.5f);
	DrawLine(X, Y + H, X + W, Y + H, Border, 1.5f);
	DrawLine(X, Y, X, Y + H, Border, 1.5f);
	DrawLine(X + W, Y, X + W, Y + H, Border, 1.5f);
	Label(Text, X + W * 0.5f, Y + H * 0.5f, bEnabled ? GText : GDim, 0.9f, true);
	if (bEnabled && Action != EDoTHudAction::None)
	{
		FDoTHudButton B;
		B.Rect = FBox2D(FVector2D(X, Y), FVector2D(X + W, Y + H));
		B.Action = Action;
		B.Param = Param;
		Buttons.Add(B);
	}
}

void ADoTHUD::Circle(const FVector2D& Center, float Radius, const FLinearColor& Color, float Thickness, int32 Segments)
{
	FVector2D Prev = Center + FVector2D(Radius, 0.f);
	for (int32 I = 1; I <= Segments; ++I)
	{
		const float A = 2.f * UE_PI * I / Segments;
		const FVector2D P = Center + FVector2D(FMath::Cos(A) * Radius, FMath::Sin(A) * Radius);
		DrawLine(Prev.X, Prev.Y, P.X, P.Y, Color, Thickness);
		Prev = P;
	}
}

void ADoTHUD::WorldCircle(const FVector& Center, float RadiusCm, const FLinearColor& Color, float Thickness, int32 Segments)
{
	FVector2D Prev;
	bool bHavePrev = false;
	for (int32 I = 0; I <= Segments; ++I)
	{
		const float A = 2.f * UE_PI * I / Segments;
		FVector2D P;
		if (ToScreen(Center + FVector(FMath::Cos(A) * RadiusCm, FMath::Sin(A) * RadiusCm, 20.f), P))
		{
			if (bHavePrev)
			{
				DrawLine(Prev.X, Prev.Y, P.X, P.Y, Color, Thickness);
			}
			Prev = P;
			bHavePrev = true;
		}
		else
		{
			bHavePrev = false;
		}
	}
}

void ADoTHUD::UpdateLayout(const dot::BattleSimulation* Sim, double Now)
{
	const FVector2D Clip(Canvas->ClipX, Canvas->ClipY);
	if (Now - SafeAreaTime > 1.0 || !Clip.Equals(LastClip, 0.5))
	{
		// Notch, rounded corners and home indicator on phones (zero on desktop).
		FDisplayMetrics Metrics;
		FDisplayMetrics::RebuildDisplayMetrics(Metrics);
		const FVector4 Pad = Metrics.TitleSafePaddingSize;
		SafeL = FMath::Clamp(static_cast<float>(Pad.X), 0.f, Canvas->ClipX * 0.2f);
		SafeT = FMath::Clamp(static_cast<float>(Pad.Y), 0.f, Canvas->ClipY * 0.2f);
		SafeR = FMath::Clamp(static_cast<float>(Pad.Z), 0.f, Canvas->ClipX * 0.2f);
		SafeB = FMath::Clamp(static_cast<float>(Pad.W), 0.f, Canvas->ClipY * 0.2f);
		SafeAreaTime = Now;
		LastClip = Clip;
	}

	// Phones get larger touch targets: the layout is designed for a 680 px tall screen there,
	// for 1080 px on desktop. Either way the bottom row and the top bar must fit the usable width.
#if PLATFORM_ANDROID || PLATFORM_IOS
	const float DesignHeight = 680.f;
#else
	const float DesignHeight = 1080.f;
#endif
	const float Desired = FMath::Clamp((Canvas->ClipY - SafeT - SafeB) / DesignHeight, 0.4f, 3.f);
	int32 OwnSquads = 0;
	int32 Spells = 3;
	if (Sim)
	{
		for (const dot::Squad& Sq : Sim->Squads())
		{
			OwnSquads += (Sq.side == dot::Side::Attacker && !Sq.summoned) ? 1 : 0;
		}
		Spells = static_cast<int32>(Sim->GetSide(dot::Side::Attacker).spells.size());
	}
	const float BottomWidth = 40.f + 78.f + OwnSquads * 158.f + 24.f + Spells * 138.f;
	const float TopWidth = 40.f + 340.f + 20.f + 300.f + 20.f + 768.f;
	const float Usable = FMath::Max(1.f, Canvas->ClipX - SafeL - SafeR);
	UiScale = FMath::Max(0.35f, FMath::Min(Desired, Usable / FMath::Max(BottomWidth, TopWidth)));
}

bool ADoTHUD::ToScreen(const FVector& World, FVector2D& Out)
{
	const FVector P = Project(World);
	if (P.Z <= 0.0)
	{
		return false;
	}
	Out = FVector2D(P.X, P.Y);
	return true;
}

// --- Frame -------------------------------------------------------------------------------

void ADoTHUD::DrawHUD()
{
	Super::DrawHUD();
	Buttons.Reset();
	Panels.Reset();
	if (!Canvas)
	{
		return;
	}
	const double Now = FPlatformTime::Seconds();
	if (LastFrameTime > 0.0)
	{
		const float Dt = static_cast<float>(Now - LastFrameTime);
		HudTime += Dt;
		if (Dt > 0.f)
		{
			FpsSmoothed = FpsSmoothed * 0.95f + (1.f / Dt) * 0.05f;
		}
	}
	LastFrameTime = Now;

	ADoTBattleManager* Manager = GetManager();
	const dot::BattleSimulation* Sim = Manager ? Manager->GetSim() : nullptr;
	UpdateLayout(Sim, Now);
	if (!Sim)
	{
		const FString Message = (Manager && !Manager->GetLastError().IsEmpty()) ? Manager->GetLastError() : FString(TEXT("Loading battle..."));
		Label(Message, Canvas->ClipX * 0.5f, Canvas->ClipY * 0.5f, GEnemy, 1.2f, true);
		return;
	}
	CollectToasts();
	DrawWorldOverlays();
	DrawTopBar();
	DrawSquadCards();
	DrawTitanPanel();
	DrawSpells();
	if (Sim->Phase() == dot::BattlePhase::Deploy)
	{
		DrawDeployPanel();
	}
	DrawRadial();
	DrawToasts();
	if (Sim->IsFinished())
	{
		DrawResults();
	}
	DrawPerf();
}

void ADoTHUD::CollectToasts()
{
	ADoTBattleManager* Manager = GetManager();
	const dot::BattleSimulation* Sim = Manager->GetSim();
	for (const dot::SimEvent& E : Manager->GetFrameEvents())
	{
		const bool bOwn = E.side == dot::Side::Attacker;
		switch (E.type)
		{
		case dot::SimEventType::BattleStarted: ShowToast(TEXT("FIGHT!"), GGold); break;
		case dot::SimEventType::ClashStarted: ShowToast(TEXT("TITAN CLASH!  Tap CLASH when the ring meets the circle (Space)"), GText); break;
		case dot::SimEventType::ClashExchange:
			ShowToast(FString::Printf(TEXT("Exchange: you %s  vs  enemy %s"), *GradeName(E.a), *GradeName(E.b)), E.value == 0 ? GGold : GEnemy);
			break;
		case dot::SimEventType::TitanStaggered:
			ShowToast(bOwn ? FString(TEXT("Your titan is STAGGERED!")) : FString(TEXT("Enemy titan STAGGERED! Unleash everything!")), bOwn ? GEnemy : GGold);
			break;
		case dot::SimEventType::TitanDied:
			ShowToast(bOwn ? FString(TEXT("Your titan has fallen!")) : FString(TEXT("The enemy titan falls!")), bOwn ? GEnemy : GGold);
			break;
		case dot::SimEventType::TitanBound:
			ShowToast(bOwn ? FString(TEXT("Your titan is BOUND in chains!")) : FString(TEXT("Chainbreakers bound the enemy titan! Strike now!")), bOwn ? GEnemy : GGold);
			break;
		case dot::SimEventType::SquadRouted:
		{
			const int Index = Sim->SquadIndexFromId(E.a);
			if (Index >= 0)
			{
				const FString Name = ToF(Sim->TroopOf(Sim->Squads()[static_cast<size_t>(Index)]).name);
				ShowToast(bOwn ? FString::Printf(TEXT("Your %s are routing!"), *Name) : FString::Printf(TEXT("Enemy %s routed!"), *Name), bOwn ? GEnemy : GOwn);
			}
			break;
		}
		case dot::SimEventType::StarEarned:
		{
			static const TCHAR* const Names[3] = {TEXT("50% of the enemy destroyed"), TEXT("Enemy titan defeated"), TEXT("Total victory")};
			ShowToast(FString::Printf(TEXT("Star earned: %s"), Names[FMath::Clamp(E.a, 0, 2)]), GGold);
			break;
		}
		case dot::SimEventType::SpellCast:
			if (!bOwn && E.a >= 0)
			{
				ShowToast(FString::Printf(TEXT("Enemy casts %s"), *ToF(Sim->Content().abilities[static_cast<size_t>(E.a)].name)), GEnemy);
			}
			break;
		default: break;
		}
	}
}

void ADoTHUD::DrawWorldOverlays()
{
	ADoTBattleManager* Manager = GetManager();
	const dot::BattleSimulation* Sim = Manager->GetSim();
	ADoTPlayerController* PC = GetDoTController();
	const int32 Selected = PC ? PC->GetSelectedUnit() : -1;
	const dot::NavGrid& Grid = Sim->Grid();

	if (Sim->Phase() == dot::BattlePhase::Deploy)
	{
		const dot::MapDef& Map = Sim->Map();
		const FVector A = DoTCoords::SimToWorld(Map.deployMin[0], 0.3);
		const FVector B = DoTCoords::SimToWorld(dot::FixVec2(Map.deployMax[0].x, Map.deployMin[0].z), 0.3);
		const FVector C = DoTCoords::SimToWorld(Map.deployMax[0], 0.3);
		const FVector D = DoTCoords::SimToWorld(dot::FixVec2(Map.deployMin[0].x, Map.deployMax[0].z), 0.3);
		const FVector Corners[5] = {A, B, C, D, A};
		for (int32 I = 0; I < 4; ++I)
		{
			FVector2D P0;
			FVector2D P1;
			if (ToScreen(Corners[I], P0) && ToScreen(Corners[I + 1], P1))
			{
				DrawLine(P0.X, P0.Y, P1.X, P1.Y, GOwn, 3.f);
			}
		}
	}

	for (const dot::Squad& Sq : Sim->Squads())
	{
		if (!Sq.Active() || !Sim->IsSquadVisibleTo(dot::Side::Attacker, Sq.id - 1))
		{
			continue;
		}
		const bool bOwn = Sq.side == dot::Side::Attacker;
		const FVector Center = DoTCoords::SimToWorld(Sq.centroid, Grid.HeightAt(Sq.centroid).ToDouble());
		const bool bSelected = bOwn && (Selected == Sq.id || Selected == 0);
		if (bSelected)
		{
			WorldCircle(Center, Sq.radius.ToFloat() * 100.f + 150.f, GGold, 2.f, 28);
			// Remaining path.
			FVector Prev = DoTCoords::SimToWorld(Sq.leader, 0.3);
			for (size_t I = Sq.pathIndex; I < Sq.path.size(); ++I)
			{
				const FVector Next = DoTCoords::SimToWorld(Sq.path[I], 0.3);
				FVector2D P0;
				FVector2D P1;
				if (ToScreen(Prev, P0) && ToScreen(Next, P1))
				{
					DrawLine(P0.X, P0.Y, P1.X, P1.Y, FLinearColor(0.9f, 0.85f, 0.6f, 0.7f), 1.5f);
				}
				Prev = Next;
			}
		}
		FVector2D Banner;
		if (ToScreen(Center + FVector(0.0, 0.0, 450.0), Banner))
		{
			const dot::TroopDef& TD = Sim->TroopOf(Sq);
			FString Text = FString::Printf(TEXT("%s %d"), *ToF(TD.shortName), Sq.aliveCount);
			if (Sq.Routing())
			{
				Text += TEXT(" ROUTING");
			}
			else if (Sq.autoControl && bOwn)
			{
				Text += TEXT(" A");
			}
			const FLinearColor Color = bOwn ? GOwn : GEnemy;
			float Tw = 0.f;
			float Th = 0.f;
			GetTextSize(Text, Tw, Th, GEngine->GetSmallFont(), UiScale);
			DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.55f), Banner.X - Tw * 0.5f - 4.f, Banner.Y - Th * 0.5f - 2.f, Tw + 8.f, Th + 4.f);
			DrawRect(Color, Banner.X - Tw * 0.5f - 4.f, Banner.Y + Th * 0.5f + 1.f, (Tw + 8.f) * FMath::Clamp(Sq.morale.ToFloat() / 100.f, 0.f, 1.f), 3.f);
			Label(Text, Banner.X, Banner.Y, bSelected ? GGold : Color, 0.85f, true, GEngine->GetSmallFont());
			if (bOwn)
			{
				// Banner hit box makes squads easy to select in crowds.
				FDoTHudButton B;
				B.Rect = FBox2D(FVector2D(Banner.X - Tw * 0.5f - 10.f, Banner.Y - Th), FVector2D(Banner.X + Tw * 0.5f + 10.f, Banner.Y + Th));
				B.Action = EDoTHudAction::SelectSquad;
				B.Param = Sq.id;
				Buttons.Add(B);
			}
		}
	}

	for (int32 I = 0; I < static_cast<int32>(Sim->Titans().size()); ++I)
	{
		const dot::Titan& T = Sim->Titans()[static_cast<size_t>(I)];
		if (!T.Alive())
		{
			continue;
		}
		const bool bOwn = T.side == dot::Side::Attacker;
		const FVector Pos = Manager->TitanWorldPos(I);
		if (bOwn && Selected == T.id)
		{
			WorldCircle(Pos, T.radius.ToFloat() * 100.f + 200.f, GGold, 2.5f, 32);
		}
		FVector2D Head;
		if (ToScreen(Pos + FVector(0.0, 0.0, T.height.ToDouble() * 100.0 + 350.0), Head))
		{
			const float Bw = 150.f * UiScale;
			const float Bh = 9.f * UiScale;
			Bar(Head.X - Bw * 0.5f, Head.Y, Bw, Bh, T.hp.ToFloat() / T.maxHp.ToFloat(), bOwn ? GOwn : GEnemy);
			Bar(Head.X - Bw * 0.5f, Head.Y + Bh + 2.f, Bw, 4.f * UiScale, 1.f - T.poiseDamage.ToFloat() / FMath::Max(T.maxPoise.ToFloat(), 1.f), GGold);
			FString Name = ToF(Sim->TitanDefOf(T).name);
			if (T.state == dot::TitanState::Staggered)
			{
				Name += TEXT("  STAGGERED");
			}
			else if (T.state == dot::TitanState::Clashing)
			{
				Name += TEXT("  CLASH");
			}
			Label(Name, Head.X, Head.Y - 14.f * UiScale, bOwn ? GOwn : GEnemy, 0.9f, true);
			if (bOwn)
			{
				FDoTHudButton B;
				B.Rect = FBox2D(FVector2D(Head.X - Bw * 0.5f, Head.Y - 28.f * UiScale), FVector2D(Head.X + Bw * 0.5f, Head.Y + 20.f * UiScale));
				B.Action = EDoTHudAction::SelectTitan;
				B.Param = T.id;
				Buttons.Add(B);
			}
		}
	}

	if (!PC)
	{
		return;
	}
	// Path being drawn, with ETA ticks every 5 seconds of travel.
	const TArray<FVector>& Draw = PC->GetDrawPoints();
	if (PC->IsDrawingPath() && Draw.Num() > 1)
	{
		float Speed = 4.f;
		const int32 Unit = Selected;
		if (dot::IsSquadId(Unit))
		{
			const int Index = Sim->SquadIndexFromId(Unit);
			if (Index >= 0)
			{
				Speed = Sim->Squads()[static_cast<size_t>(Index)].speed.ToFloat();
			}
		}
		double Travelled = 0.0;
		double NextTick = Speed * 5.0 * 100.0;
		for (int32 I = 1; I < Draw.Num(); ++I)
		{
			FVector2D P0;
			FVector2D P1;
			if (ToScreen(Draw[I - 1] + FVector(0.0, 0.0, 30.0), P0) && ToScreen(Draw[I] + FVector(0.0, 0.0, 30.0), P1))
			{
				DrawLine(P0.X, P0.Y, P1.X, P1.Y, GOwn, 4.f);
			}
			Travelled += FVector::Dist2D(Draw[I - 1], Draw[I]);
			if (Travelled >= NextTick)
			{
				NextTick += Speed * 5.0 * 100.0;
				FVector2D Tick;
				if (ToScreen(Draw[I], Tick))
				{
					DrawRect(GText, Tick.X - 4.f, Tick.Y - 4.f, 8.f, 8.f);
				}
			}
		}
	}
	// Targeting reticle for abilities and spells.
	const int32 Mode = PC->GetTargetingMode();
	const int32 DragSpell = PC->GetDraggingSpell();
	FVector Ground;
	if ((Mode != 0 || DragSpell >= 0) && PC->GetPointerGround(Ground))
	{
		float Radius = 6.f;
		const int32 SpellSlot = DragSpell >= 0 ? DragSpell : (Mode >= 10 ? Mode - 10 : -1);
		const dot::SideState& Side = Sim->GetSide(dot::Side::Attacker);
		if (SpellSlot >= 0 && SpellSlot < static_cast<int32>(Side.spells.size()))
		{
			Radius = Sim->Content().abilities[static_cast<size_t>(Side.spells[static_cast<size_t>(SpellSlot)])].radius.ToFloat();
		}
		else if (Mode >= 1 && Mode <= 2)
		{
			const int32 TitanIndex = Manager->FindTitanIndex(true);
			if (TitanIndex != INDEX_NONE)
			{
				const int AbilityIndex = Sim->TitanDefOf(Sim->Titans()[static_cast<size_t>(TitanIndex)]).abilities[Mode - 1];
				if (AbilityIndex >= 0)
				{
					const dot::AbilityDef& A = Sim->Content().abilities[static_cast<size_t>(AbilityIndex)];
					Radius = A.radius > dot::Fix64::Zero() ? A.radius.ToFloat() : A.width.ToFloat() * 0.5f + 2.f;
				}
			}
		}
		WorldCircle(Ground, Radius * 100.f, FLinearColor(1.f, 0.6f, 0.1f), 3.f, 36);
	}
}

void ADoTHUD::DrawTopBar()
{
	ADoTBattleManager* Manager = GetManager();
	const dot::BattleSimulation* Sim = Manager->GetSim();
	const float S = UiScale;
	const float W = Canvas->ClipX - SafeR;
	const float L = SafeL;
	const float T = SafeT;

	// Timer and stars (top-left).
	Panel(L + 20.f * S, T + 20.f * S, 340.f * S, 92.f * S, GPanel);
	FString Timer;
	switch (Sim->Phase())
	{
	case dot::BattlePhase::Deploy: Timer = TEXT("DEPLOY"); break;
	case dot::BattlePhase::Countdown: Timer = FString::Printf(TEXT("%d..."), Sim->CountdownTicksRemaining() / dot::kTicksPerSecond + 1); break;
	case dot::BattlePhase::Battle: Timer = FormatTime(Sim->TimerTicksRemaining()); break;
	default: Timer = TEXT("FINISHED"); break;
	}
	if (Manager->IsReplaying())
	{
		Timer += TEXT("  [REPLAY]");
	}
	Label(Timer, L + 36.f * S, T + 28.f * S, GText, 1.6f, false, GEngine->GetLargeFont());
	static const TCHAR* const StarNames[3] = {TEXT("50%"), TEXT("TITAN"), TEXT("ALL")};
	for (int32 I = 0; I < 3; ++I)
	{
		const bool bMet = Sim->ObjectiveMet(I);
		const float X = L + (36.f + I * 105.f) * S;
		DrawRect(bMet ? GGold : GDim, X, T + 80.f * S, 14.f * S, 14.f * S);
		Label(StarNames[I], X + 20.f * S, T + 76.f * S, bMet ? GGold : GDim, 0.8f);
	}

	// Army power bars, centred in the gap between the timer panel and the buttons.
	const float GapStart = L + 380.f * S;
	const float GapEnd = W - 788.f * S;
	const float Bw = FMath::Min(420.f * S, FMath::Max(120.f * S, GapEnd - GapStart - 20.f * S));
	const float Bx = (GapStart + GapEnd) * 0.5f - Bw * 0.5f;
	Panel(Bx - 10.f * S, T + 20.f * S, Bw + 20.f * S, 64.f * S, GPanel);
	const dot::SideState& Own = Sim->GetSide(dot::Side::Attacker);
	const dot::SideState& Enemy = Sim->GetSide(dot::Side::Defender);
	const float OwnFrac = Own.startPower > 0 ? static_cast<float>(Own.startPower - Own.destroyedPower) / Own.startPower : 0.f;
	const float EnemyFrac = Enemy.startPower > 0 ? static_cast<float>(Enemy.startPower - Enemy.destroyedPower) / Enemy.startPower : 0.f;
	Label(FString::Printf(TEXT("YOUR ARMY %d%%"), FMath::RoundToInt(OwnFrac * 100.f)), Bx, T + 26.f * S, GOwn, 0.8f);
	Bar(Bx, T + 44.f * S, Bw * 0.48f, 14.f * S, OwnFrac, GOwn);
	Label(FString::Printf(TEXT("ENEMY %d%%"), FMath::RoundToInt(EnemyFrac * 100.f)), Bx + Bw * 0.52f, T + 26.f * S, GEnemy, 0.8f);
	Bar(Bx + Bw * 0.52f, T + 44.f * S, Bw * 0.48f, 14.f * S, EnemyFrac, GEnemy);

	// Controls (top-right).
	const float Bh = 50.f * S;
	float X = W - 20.f * S;
	auto Right = [&](float Width, const FString& Text, EDoTHudAction Action, bool bHighlight)
	{
		X -= Width * S;
		Button(X, T + 20.f * S, Width * S, Bh, Text, Action, 0, true, bHighlight);
		X -= 8.f * S;
	};
	Right(110.f, Manager->IsPaused() ? TEXT("Resume") : TEXT("Pause"), EDoTHudAction::Pause, Manager->IsPaused());
	Right(80.f, FString::Printf(TEXT("%.0fx"), Manager->GetSpeed()), EDoTHudAction::Speed, Manager->GetSpeed() > 1.f);
	Right(120.f, Manager->IsAllAuto() ? TEXT("Auto: ON") : TEXT("Auto: OFF"), EDoTHudAction::AutoAll, Manager->IsAllAuto());
	ADoTPlayerController* PC = GetDoTController();
	Right(110.f, TEXT("Follow"), EDoTHudAction::FollowTitan, PC && PC->IsFollowingTitan());
	Right(190.f, FString::Printf(TEXT("Enemy: %s"), *Manager->EnemyAiProfile), EDoTHudAction::EnemyAi, false);
	Right(110.f, TEXT("Restart"), EDoTHudAction::Restart, false);
}

void ADoTHUD::DrawSquadCards()
{
	ADoTBattleManager* Manager = GetManager();
	const dot::BattleSimulation* Sim = Manager->GetSim();
	ADoTPlayerController* PC = GetDoTController();
	const int32 Selected = PC ? PC->GetSelectedUnit() : -1;
	const float S = UiScale;
	const float H = Canvas->ClipY;
	const float CardW = 150.f * S;
	const float CardH = 112.f * S;
	const float Y = H - SafeB - CardH - 20.f * S;
	float X = SafeL + 20.f * S;
	Button(X, Y, 70.f * S, CardH, TEXT("ALL"), EDoTHudAction::SelectAll, 0, true, Selected == 0);
	X += 78.f * S;
	for (const dot::Squad& Sq : Sim->Squads())
	{
		if (Sq.side != dot::Side::Attacker || Sq.summoned)
		{
			continue;
		}
		const dot::TroopDef& TD = Sim->TroopOf(Sq);
		const bool bSelected = Selected == Sq.id || Selected == 0;
		const bool bAlive = Sq.Active();
		Button(X, Y, CardW, CardH, TEXT(""), EDoTHudAction::SelectSquad, Sq.id, bAlive, bSelected && bAlive);
		const FLinearColor Text = bAlive ? GText : GDim;
		Label(FString::Printf(TEXT("%s"), *ToF(TD.name)), X + 8.f * S, Y + 6.f * S, Text, 0.85f);
		Label(ToF(TD.tiers[static_cast<size_t>(FMath::Max(0, Sq.tier - 1))].name) + FString::Printf(TEXT("  T%d"), Sq.tier), X + 8.f * S, Y + 26.f * S, GDim, 0.7f);
		if (!bAlive)
		{
			Label(Sq.leftField ? TEXT("FLED") : TEXT("LOST"), X + CardW * 0.5f, Y + CardH * 0.65f, GEnemy, 1.0f, true);
			X += CardW + 8.f * S;
			continue;
		}
		float HpSum = 0.f;
		for (const int32 M : Sq.members)
		{
			const dot::Soldier& Sol = Sim->Soldiers()[static_cast<size_t>(M)];
			if (Sol.Alive())
			{
				HpSum += Sol.hp.ToFloat();
			}
		}
		const float HpMax = FMath::Max(1.f, Sq.hp.ToFloat() * Sq.startCount);
		Label(FString::Printf(TEXT("%d / %d"), Sq.aliveCount, Sq.startCount), X + 8.f * S, Y + 46.f * S, GText, 0.8f);
		Bar(X + 8.f * S, Y + 68.f * S, CardW - 16.f * S, 8.f * S, HpSum / HpMax, GOwn);
		Bar(X + 8.f * S, Y + 80.f * S, CardW - 16.f * S, 5.f * S, Sq.morale.ToFloat() / 100.f, GGold);
		Label(FString::Printf(TEXT("%s | %s%s"), OrderName(Sq), ANSI_TO_TCHAR(dot::ToString(Sq.formation)), Sq.autoControl ? TEXT(" | AUTO") : TEXT("")),
			X + 8.f * S, Y + 88.f * S, GDim, 0.65f);
		X += CardW + 8.f * S;
	}
}

void ADoTHUD::DrawTitanPanel()
{
	ADoTBattleManager* Manager = GetManager();
	const dot::BattleSimulation* Sim = Manager->GetSim();
	ADoTPlayerController* PC = GetDoTController();
	const int32 Index = Manager->FindTitanIndex(true);
	if (Index == INDEX_NONE)
	{
		return;
	}
	const dot::Titan& T = Sim->Titans()[static_cast<size_t>(Index)];
	const float S = UiScale;
	const float W = Canvas->ClipX;
	const float H = Canvas->ClipY;
	const float PanelW = 300.f * S;
	const float PanelH = 112.f * S;
	const float AbW = 120.f * S;
	const float Total = PanelW + 2.f * (AbW + 8.f * S);
	const float X = (SafeL + W - SafeR) * 0.5f - Total * 0.5f + 120.f * S;
	const float Y = H - SafeB - PanelH - 150.f * S;
	const bool bSelected = PC && PC->GetSelectedUnit() == T.id;
	Button(X, Y, PanelW, PanelH, TEXT(""), EDoTHudAction::SelectTitan, T.id, T.Alive(), bSelected);
	const dot::TitanDef& TD = Sim->TitanDefOf(T);
	Label(FString::Printf(TEXT("%s, %s"), *ToF(TD.name), *ToF(TD.title)), X + 10.f * S, Y + 6.f * S, T.Alive() ? GGold : GDim, 0.85f);
	Label(FString::Printf(TEXT("Lv %d  %d*"), T.level, T.stars), X + 10.f * S, Y + 26.f * S, GDim, 0.7f);
	if (!T.Alive())
	{
		Label(TEXT("FALLEN"), X + PanelW * 0.5f, Y + PanelH * 0.65f, GEnemy, 1.2f, true);
		return;
	}
	Label(TEXT("HP"), X + 10.f * S, Y + 46.f * S, GDim, 0.65f);
	Bar(X + 50.f * S, Y + 50.f * S, PanelW - 60.f * S, 10.f * S, T.hp.ToFloat() / T.maxHp.ToFloat(), GOwn);
	Label(TEXT("Poise"), X + 10.f * S, Y + 64.f * S, GDim, 0.65f);
	Bar(X + 50.f * S, Y + 68.f * S, PanelW - 60.f * S, 7.f * S, 1.f - T.poiseDamage.ToFloat() / FMath::Max(1.f, T.maxPoise.ToFloat()), GGold);
	Label(TEXT("Rage"), X + 10.f * S, Y + 80.f * S, GDim, 0.65f);
	Bar(X + 50.f * S, Y + 84.f * S, PanelW - 60.f * S, 7.f * S, T.rage.ToFloat() / 100.f, FLinearColor(0.9f, 0.3f, 0.1f));
	Label(T.autoControl ? TEXT("AUTO") : TEXT(""), X + PanelW - 50.f * S, Y + 6.f * S, GGold, 0.7f);

	const int32 Mode = PC ? PC->GetTargetingMode() : 0;
	for (int32 Slot = 0; Slot < 2; ++Slot)
	{
		const int AbilityIndex = TD.abilities[Slot];
		if (AbilityIndex < 0)
		{
			continue;
		}
		const dot::AbilityDef& A = Sim->Content().abilities[static_cast<size_t>(AbilityIndex)];
		const float Ax = X + PanelW + 8.f * S + Slot * (AbW + 8.f * S);
		const int32 CooldownTicks = FMath::Max(0, T.abilityReadyTick[Slot] - Sim->CurrentTick());
		const bool bReady = CooldownTicks == 0 && T.rage >= dot::fx::Int(A.rageCost) && T.clashIndex < 0;
		Button(Ax, Y, AbW, PanelH, TEXT(""), EDoTHudAction::Ability, Slot, true, Mode == 1 + Slot);
		Label(ToF(A.name), Ax + AbW * 0.5f, Y + 26.f * S, bReady ? GText : GDim, 0.75f, true);
		Label(FString::Printf(TEXT("%s  |  %d rage"), Slot == 0 ? TEXT("Q") : TEXT("E"), A.rageCost), Ax + AbW * 0.5f, Y + 52.f * S, GDim, 0.65f, true);
		if (CooldownTicks > 0)
		{
			Label(FString::Printf(TEXT("%.1fs"), CooldownTicks / static_cast<float>(dot::kTicksPerSecond)), Ax + AbW * 0.5f, Y + 82.f * S, GEnemy, 0.9f, true);
		}
		else if (bReady)
		{
			Label(TEXT("READY"), Ax + AbW * 0.5f, Y + 82.f * S, GGold, 0.8f, true);
		}
	}

	// Titan Clash timing ring.
	const dot::ClashState& Clash = Sim->Clash();
	if (Clash.active)
	{
		const FVector2D Center(W * 0.5f, H * 0.45f);
		const float Base = 70.f * S;
		const double RenderTick = Manager->GetRenderTick();
		const double Window = FMath::Max(1, Clash.markerTick - Clash.windupStart);
		const float T01 = static_cast<float>(FMath::Clamp((RenderTick - Clash.windupStart) / Window, 0.0, 1.3));
		const float RingRadius = Base * FMath::Max(0.3f, 3.f - 2.f * T01);
		Panel(Center.X - 150.f * S, Center.Y - 150.f * S, 300.f * S, 330.f * S, FLinearColor(0.f, 0.f, 0.f, 0.35f));
		Circle(Center, Base, GGold, 4.f, 48);
		Circle(Center, Base * 1.1f, FLinearColor(GGold.R, GGold.G, GGold.B, 0.35f), 2.f, 48);
		Circle(Center, RingRadius, GText, 3.f, 48);
		Label(FString::Printf(TEXT("CLASH  %d / 3"), Clash.exchange + 1), Center.X, Center.Y - 120.f * S, GText, 1.1f, true);
		Button(Center.X - 120.f * S, Center.Y + 100.f * S, 240.f * S, 64.f * S, TEXT("CLASH!"), EDoTHudAction::Clash, 0, true, FMath::Abs(T01 - 1.f) < 0.08f);
	}
}

void ADoTHUD::DrawSpells()
{
	ADoTBattleManager* Manager = GetManager();
	const dot::BattleSimulation* Sim = Manager->GetSim();
	ADoTPlayerController* PC = GetDoTController();
	const dot::SideState& Side = Sim->GetSide(dot::Side::Attacker);
	const float S = UiScale;
	const float W = Canvas->ClipX;
	const float H = Canvas->ClipY;
	const float CardW = 130.f * S;
	const float CardH = 112.f * S;
	const int32 Count = static_cast<int32>(Side.spells.size());
	float X = W - SafeR - 20.f * S - Count * (CardW + 8.f * S);
	const float Y = H - SafeB - CardH - 20.f * S;

	// Valor pips.
	const int32 Valor = static_cast<int32>(Side.valor.FloorToInt());
	Panel(X, Y - 40.f * S, Count * (CardW + 8.f * S) - 8.f * S, 32.f * S, GPanel);
	Label(TEXT("VALOR"), X + 8.f * S, Y - 36.f * S, GGold, 0.75f);
	for (int32 I = 0; I < 10; ++I)
	{
		DrawRect(I < Valor ? GGold : FLinearColor(0.2f, 0.2f, 0.22f), X + (90.f + I * 22.f) * S, Y - 30.f * S, 16.f * S, 14.f * S);
	}
	const int32 Mode = PC ? PC->GetTargetingMode() : 0;
	static const TCHAR* const Keys[4] = {TEXT("Z"), TEXT("X"), TEXT("C"), TEXT("V")};
	for (int32 Slot = 0; Slot < Count; ++Slot)
	{
		const dot::AbilityDef& A = Sim->Content().abilities[static_cast<size_t>(Side.spells[static_cast<size_t>(Slot)])];
		const int32 CooldownTicks = FMath::Max(0, Side.spellReadyTick[static_cast<size_t>(Slot)] - Sim->CurrentTick());
		const bool bReady = CooldownTicks == 0 && Valor >= A.valorCost && Sim->Phase() == dot::BattlePhase::Battle;
		Button(X, Y, CardW, CardH, TEXT(""), EDoTHudAction::Spell, Slot, true, Mode == 10 + Slot);
		Label(ToF(A.name), X + CardW * 0.5f, Y + 24.f * S, bReady ? GText : GDim, 0.75f, true);
		Label(FString::Printf(TEXT("%s  |  %d valor"), Keys[FMath::Clamp(Slot, 0, 3)], A.valorCost), X + CardW * 0.5f, Y + 50.f * S, GDim, 0.65f, true);
		if (CooldownTicks > 0)
		{
			Label(FString::Printf(TEXT("%.0fs"), CooldownTicks / static_cast<float>(dot::kTicksPerSecond)), X + CardW * 0.5f, Y + 82.f * S, GEnemy, 0.9f, true);
		}
		else
		{
			Label(bReady ? TEXT("DRAG TO CAST") : TEXT("..."), X + CardW * 0.5f, Y + 82.f * S, bReady ? GGold : GDim, 0.6f, true);
		}
		X += CardW + 8.f * S;
	}
}

void ADoTHUD::DrawRadial()
{
	ADoTPlayerController* PC = GetDoTController();
	ADoTBattleManager* Manager = GetManager();
	const dot::BattleSimulation* Sim = Manager->GetSim();
	if (!PC || !PC->IsRadialOpen())
	{
		return;
	}
	const int32 Unit = PC->GetRadialUnit();
	const float S = UiScale;
	const FVector2D C = PC->GetRadialCenter();
	struct FItem
	{
		FString Text;
		EDoTHudAction Action;
		int32 Param;
		bool bOn;
	};
	TArray<FItem> Items;
	if (dot::IsSquadId(Unit))
	{
		const int Index = Sim->SquadIndexFromId(Unit);
		if (Index < 0)
		{
			return;
		}
		const dot::Squad& Sq = Sim->Squads()[static_cast<size_t>(Index)];
		const dot::TroopDef& TD = Sim->TroopOf(Sq);
		for (int32 F = 0; F < static_cast<int32>(dot::Formation::Count); ++F)
		{
			if (TD.AllowsFormation(static_cast<dot::Formation>(F)))
			{
				Items.Add({ANSI_TO_TCHAR(dot::ToString(static_cast<dot::Formation>(F))), EDoTHudAction::SetFormation, F, static_cast<int32>(Sq.formation) == F});
			}
		}
		for (int32 St = 0; St < static_cast<int32>(dot::Stance::Count); ++St)
		{
			Items.Add({ANSI_TO_TCHAR(dot::ToString(static_cast<dot::Stance>(St))), EDoTHudAction::SetStance, St, static_cast<int32>(Sq.stance) == St});
		}
		Items.Add({Sq.autoControl ? TEXT("Auto: ON") : TEXT("Auto: OFF"), EDoTHudAction::ToggleAuto, 0, Sq.autoControl});
	}
	else if (dot::IsTitanId(Unit))
	{
		const int Index = Sim->TitanIndexFromId(Unit);
		const bool bAuto = Index >= 0 && Sim->Titans()[static_cast<size_t>(Index)].autoControl;
		Items.Add({bAuto ? TEXT("Auto: ON") : TEXT("Auto: OFF"), EDoTHudAction::ToggleAuto, 0, bAuto});
	}
	Items.Add({TEXT("Halt"), EDoTHudAction::Halt, 0, false});
	Items.Add({TEXT("Retreat"), EDoTHudAction::Retreat, 0, false});
	Items.Add({TEXT("Close"), EDoTHudAction::CloseRadial, 0, false});
	const float Radius = 150.f * S;
	const float Bw = 120.f * S;
	const float Bh = 46.f * S;
	for (int32 I = 0; I < Items.Num(); ++I)
	{
		const float A = 2.f * UE_PI * I / Items.Num() - UE_PI * 0.5f;
		const float X = FMath::Clamp(static_cast<float>(C.X) + FMath::Cos(A) * Radius - Bw * 0.5f, SafeL, Canvas->ClipX - SafeR - Bw);
		const float Y = FMath::Clamp(static_cast<float>(C.Y) + FMath::Sin(A) * Radius - Bh * 0.5f, SafeT, Canvas->ClipY - SafeB - Bh);
		Button(X, Y, Bw, Bh, Items[I].Text, Items[I].Action, Items[I].Param, true, Items[I].bOn);
	}
}

void ADoTHUD::DrawDeployPanel()
{
	const float S = UiScale;
	const float W = Canvas->ClipX;
	const float H = Canvas->ClipY;
	const float Pw = 560.f * S;
	const float Px = W * 0.5f - Pw * 0.5f;
	const float Py = SafeT + H * 0.16f;
	Panel(Px, Py, Pw, 150.f * S, GPanel);
	Label(TEXT("Deployment"), W * 0.5f, Py + 22.f * S, GGold, 1.2f, true);
	Label(TEXT("Drag squads or your titan inside the blue zone, then FIGHT."), W * 0.5f, Py + 52.f * S, GText, 0.8f, true);
	Button(Px + 40.f * S, Py + 80.f * S, 220.f * S, 56.f * S, TEXT("Auto-Deploy"), EDoTHudAction::AutoDeploy, 0, true, false);
#if PLATFORM_ANDROID || PLATFORM_IOS
	const TCHAR* const FightText = TEXT("FIGHT!");
#else
	const TCHAR* const FightText = TEXT("FIGHT!  (Enter)");
#endif
	Button(Px + Pw - 260.f * S, Py + 80.f * S, 220.f * S, 56.f * S, FightText, EDoTHudAction::Fight, 0, true, true);
}

void ADoTHUD::DrawResults()
{
	ADoTBattleManager* Manager = GetManager();
	const dot::BattleSimulation* Sim = Manager->GetSim();
	const dot::BattleResult& R = Sim->Result();
	const float S = UiScale;
	const float W = Canvas->ClipX;
	const float H = Canvas->ClipY;
	const float Pw = 640.f * S;
	const float Ph = 380.f * S;
	const float Px = W * 0.5f - Pw * 0.5f;
	const float Py = H * 0.5f - Ph * 0.5f - 60.f * S;
	Panel(Px, Py, Pw, Ph, FLinearColor(0.02f, 0.03f, 0.05f, 0.92f));
	const bool bWin = R.outcome == dot::BattleOutcome::Victory;
	Label(bWin ? TEXT("VICTORY") : (R.surrendered ? TEXT("SURRENDERED") : TEXT("DEFEAT")), W * 0.5f, Py + 40.f * S, bWin ? GGold : GEnemy, 2.0f, true,
		GEngine->GetLargeFont());
	for (int32 I = 0; I < 3; ++I)
	{
		DrawRect(R.objectives[I] ? GGold : GDim, W * 0.5f - 75.f * S + I * 55.f * S, Py + 80.f * S, 40.f * S, 40.f * S);
	}
	const FString Lines[4] = {
		FString::Printf(TEXT("Duration %s   |   Enemy destroyed %d%%"), *FormatTime(R.durationTicks), R.destructionPct),
		FString::Printf(TEXT("Your soldiers: %d survived, %d fell"), R.attackerSurvivors, R.attackerLost),
		FString::Printf(TEXT("Enemy soldiers: %d survived, %d fell"), R.defenderSurvivors, R.defenderLost),
		FString::Printf(TEXT("Battle score %lld   |   Enemy AI %s"), static_cast<long long>(R.battleScore), *Manager->EnemyAiProfile)};
	for (int32 I = 0; I < 4; ++I)
	{
		Label(Lines[I], W * 0.5f, Py + (150.f + I * 30.f) * S, GText, 0.85f, true);
	}
	const float By = Py + Ph - 80.f * S;
	const float Bw = 140.f * S;
	Button(Px + 20.f * S, By, Bw, 56.f * S, TEXT("Restart"), EDoTHudAction::Restart, 0, true, true);
	Button(Px + 30.f * S + Bw, By, Bw, 56.f * S, TEXT("Replay"), EDoTHudAction::Replay, 0, true, false);
	Button(Px + 40.f * S + Bw * 2.f, By, Bw, 56.f * S, TEXT("Save replay"), EDoTHudAction::SaveReplay, 0, true, false);
	Button(Px + 50.f * S + Bw * 3.f, By, Bw, 56.f * S, TEXT("Harder AI"), EDoTHudAction::EnemyAi, 0, true, false);
}

void ADoTHUD::DrawToasts()
{
	const float S = UiScale;
	const float W = Canvas->ClipX;
	float Y = Canvas->ClipY * 0.24f;
	for (int32 I = Toasts.Num() - 1; I >= 0; --I)
	{
		const float Age = HudTime - Toasts[I].Start;
		if (Age > 3.5f)
		{
			Toasts.RemoveAt(I);
		}
	}
	for (const FToast& T : Toasts)
	{
		float Tw = 0.f;
		float Th = 0.f;
		GetTextSize(T.Text, Tw, Th, GEngine->GetMediumFont(), S);
		DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.55f), W * 0.5f - Tw * 0.5f - 12.f, Y - 4.f, Tw + 24.f, Th + 8.f);
		Label(T.Text, W * 0.5f - Tw * 0.5f, Y, T.Color, 1.f);
		Y += Th + 12.f;
	}
}

void ADoTHUD::DrawPerf()
{
	ADoTBattleManager* Manager = GetManager();
	const dot::BattleSimulation* Sim = Manager->GetSim();
	int32 Alive = 0;
	for (const dot::Soldier& S : Sim->Soldiers())
	{
		Alive += S.Alive() ? 1 : 0;
	}
	const FString Text = FString::Printf(TEXT("FPS %.0f  |  sim %.2f ms/tick  |  soldiers %d  |  tick %d"), FpsSmoothed, Manager->GetSimMsPerTick(), Alive,
		Sim->CurrentTick());
	Label(Text, Canvas->ClipX - SafeR - 20.f * UiScale - 520.f * UiScale, SafeT + 80.f * UiScale, GDim, 0.65f, false, GEngine->GetSmallFont());
}
