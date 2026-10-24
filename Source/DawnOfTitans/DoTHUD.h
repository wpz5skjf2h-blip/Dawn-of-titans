// Source/DawnOfTitans/DoTHUD.h
// Canvas battle HUD (master prompt 15.4): timer, stars, power bars, squad cards, titan panel with
// abilities and the Clash timing ring, Valor and spell cards, world banners, path previews,
// radial command menu, deployment and results panels, toasts and a performance readout.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"

#include "DoTHUD.generated.h"

class ADoTBattleManager;
class ADoTPlayerController;
class UFont;

namespace dot
{
class BattleSimulation;
} // namespace dot

enum class EDoTHudAction : uint8
{
	None,
	SelectSquad,
	SelectAll,
	SelectTitan,
	Ability,
	Spell,
	Clash,
	Fight,
	AutoDeploy,
	Pause,
	Speed,
	AutoAll,
	Restart,
	Replay,
	SaveReplay,
	EnemyAi,
	FollowTitan,
	SetFormation,
	SetStance,
	Halt,
	Retreat,
	ToggleAuto,
	CloseRadial
};

struct FDoTHudButton
{
	FBox2D Rect;
	EDoTHudAction Action = EDoTHudAction::None;
	int32 Param = 0;
};

UCLASS()
class DAWNOFTITANS_API ADoTHUD : public AHUD
{
	GENERATED_BODY()

public:
	virtual void DrawHUD() override;

	/** Returns the topmost HUD button under a screen position (from the last drawn frame). */
	bool HitTest(const FVector2D& ScreenPos, EDoTHudAction& OutAction, int32& OutParam) const;
	/** True if the position is over any HUD panel (so world input is ignored). */
	bool IsOverPanel(const FVector2D& ScreenPos) const;
	void ShowToast(const FString& Text, const FLinearColor& Color);
	float GetUiScale() const { return UiScale; }

private:
	void DrawWorldOverlays();
	void DrawTopBar();
	void DrawSquadCards();
	void DrawTitanPanel();
	void DrawSpells();
	void DrawRadial();
	void DrawDeployPanel();
	void DrawResults();
	void DrawToasts();
	void DrawPerf();
	void CollectToasts();

	void Panel(float X, float Y, float W, float H, const FLinearColor& Color);
	void Bar(float X, float Y, float W, float H, float Fraction, const FLinearColor& Color);
	void Label(const FString& Text, float X, float Y, const FLinearColor& Color, float Scale = 1.f, bool bCenter = false, UFont* Font = nullptr);
	void Button(float X, float Y, float W, float H, const FString& Text, EDoTHudAction Action, int32 Param, bool bEnabled = true, bool bHighlight = false);
	void Circle(const FVector2D& Center, float Radius, const FLinearColor& Color, float Thickness, int32 Segments = 32);
	void WorldCircle(const FVector& Center, float RadiusCm, const FLinearColor& Color, float Thickness, int32 Segments = 32);
	bool ToScreen(const FVector& World, FVector2D& Out);
	/** Refreshes the safe-area insets and picks the UI scale for this screen. */
	void UpdateLayout(const dot::BattleSimulation* Sim, double Now);

	ADoTBattleManager* GetManager() const;
	mutable TWeakObjectPtr<ADoTBattleManager> CachedManager;
	ADoTPlayerController* GetDoTController() const;

	TArray<FDoTHudButton> Buttons;
	TArray<FBox2D> Panels;
	struct FToast
	{
		FString Text;
		FLinearColor Color;
		float Start = 0.f;
	};
	TArray<FToast> Toasts;
	float UiScale = 1.f;
	float SafeL = 0.f;
	float SafeT = 0.f;
	float SafeR = 0.f;
	float SafeB = 0.f;
	double SafeAreaTime = -10.0;
	FVector2D LastClip = FVector2D::ZeroVector;
	float HudTime = 0.f;
	double LastFrameTime = 0.0;
	float FpsSmoothed = 60.f;
};
