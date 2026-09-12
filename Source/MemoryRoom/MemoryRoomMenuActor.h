#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "AMemoryRoomManager.h"
#include "MemoryRoomThemeAsset.h" // FMemoryRoomWristUIStyle
#include "MemoryRoomMenuActor.generated.h"

class UBoxComponent;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UPointLightComponent;
class UStaticMeshComponent;
class UTextRenderComponent;
class UHapticFeedbackEffect_Base;

UENUM()
enum class EMemoryRoomMenuAction : uint8
{
	ResetView,
	JumpToNewest,
	ReloadPhotos,
	Close
};

// One tile on the settings panel:
//
//        PHOTO SPACING        <- Label
//     [ < ]   12   [ > ]      <- MinusBox / ValueText / PlusBox
//
// Action tiles use a single wide press (the Minus box) with the Label inside it and no
// Plus box or value.
USTRUCT()
struct FMemoryRoomMenuRow
{
	GENERATED_BODY()

	bool bIsSetting = false;
	EMemoryRoomUserSetting Setting = EMemoryRoomUserSetting::TimeOfDay;
	EMemoryRoomMenuAction Action = EMemoryRoomMenuAction::Close;

	UPROPERTY() TObjectPtr<UBoxComponent> MinusBox;
	UPROPERTY() TObjectPtr<UBoxComponent> PlusBox;
	UPROPERTY() TObjectPtr<UStaticMeshComponent> MinusPanel;
	UPROPERTY() TObjectPtr<UStaticMeshComponent> PlusPanel;
	UPROPERTY() TObjectPtr<UTextRenderComponent> MinusGlyph;
	UPROPERTY() TObjectPtr<UTextRenderComponent> PlusGlyph;
	UPROPERTY() TObjectPtr<UTextRenderComponent> Label;
	UPROPERTY() TObjectPtr<UTextRenderComponent> ValueText;

	// Half-width of each press tile, so the hover pop can rebuild its scale.
	float TileHalfY = 0.0f;
	// 0 at rest, 1 fully hovered; eased toward the target every tick.
	float MinusHoverBlend = 0.0f;
	float PlusHoverBlend = 0.0f;
};

/**
 * The in-headset settings board. Rows of [<] LABEL value [>] plus a few action buttons.
 * Point the free controller at a < or > and pull the trigger. Can float in front of the
 * player (toggled by a face button) or ride the left controller.
 *
 * Styled as lantern glass (see MemoryRoomUIGlass.h): a dark warm slab lit by an ember
 * along its lower edge, gold hairline rules under the title and between the two blocks,
 * ember tiles that warm and lift under the pointer, small-caps labels, gold values. Opens
 * with a short scale-in and closes the same way.
 */
UCLASS(Blueprintable)
class MEMORYROOM_API AMemoryRoomMenuActor : public AActor
{
	GENERATED_BODY()

public:
	AMemoryRoomMenuActor();
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	UFUNCTION(BlueprintCallable, Category = "Memory Room|Menu")
	void ToggleMenu();

	UFUNCTION(BlueprintCallable, Category = "Memory Room|Menu")
	void OpenMenu();

	UFUNCTION(BlueprintCallable, Category = "Memory Room|Menu")
	void CloseMenu();

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Memory Room|Menu")
	bool IsMenuOpen() const { return bMenuOpen; }

	// Called by the pointer.
	bool IsMenuComponent(const UPrimitiveComponent* Component) const;
	void SetHoveredComponent(UPrimitiveComponent* Component);
	bool PressHoveredButton();

	// Repaints the slab, tiles, rules and text from the active theme's wrist-UI style.
	void ApplyUIStyle(const FMemoryRoomWristUIStyle& NewStyle);

	// Live state of the reveal gate as one short line, for the calibration readout: the roll
	// the gate is measuring and which of its three bands that lands in.
	FString GetWristRevealDebugText() const;

	// The controller component the panel measures roll from, once it has found one.
	USceneComponent* GetAttachedWristComponent() const { return WristComponent.Get(); }

protected:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room|Menu")
	TObjectPtr<AMemoryRoomManager> MemoryRoomManager;

	// --- Clipboard placement ---
	// The clipboard is summoned by the wrist Settings puck - it is NOT always in view. On
	// open it appears this far in front of the head, at VerticalOffsetCm below eye level,
	// turned to face you. Press the puck again (or the Close button) to dismiss it.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room|Menu|Placement", meta = (ClampMin = "40.0"))
	float SummonDistanceCm = 75.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room|Menu|Placement")
	float SummonHeightOffsetCm = -22.0f;

	// Tilt the top of the board back a little, like a lectern.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room|Menu|Placement", meta = (ClampMin = "-30.0", ClampMax = "30.0"))
	float MenuPitchDegrees = 6.0f;

	// Float the board a fixed distance in front of the view once revealed, instead of riding
	// the wrist. The wrist-roll gesture below is unaffected either way - it only reads
	// WristComponent's roll to decide when to open/close, never where the panel displays -
	// so this is purely a "does a steadier, farther target land easier" experiment: on a
	// wrist panel, both the pointer (other hand) and the target (this wrist) are moving,
	// which stacks two sources of aim error; a panel fixed in world space the moment it opens
	// removes the second one.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room|Menu")
	bool bFloatInFrontOfView = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room|Menu", meta = (ClampMin = "40.0"))
	float OpenDistanceCm = 90.0f;

	// Where the panel's base (see BaseAnchorShiftZ in BuildBoard) lands relative to eye
	// height. The panel is base-anchored and about 38.5 cm tall at the default BoardScale, so
	// -24 puts its vertical centre about 5 cm below eye level rather than mostly above it.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room|Menu")
	float VerticalOffsetCm = -24.0f;

	// When riding the controller: which one, and where relative to it.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room|Menu")
	FName AttachControllerNameFilter = TEXT("Left");

	// Offset from the controller, in the controller's own measured grip-pose axes (see
	// quest-grip-pose-axes in project memory): +X is up out of the top of the controller -
	// the watch face where the wrist pucks sit - and +Z is backward, toward the player, with
	// aim along -Z. -X is the far side from the watch face, which is the side that swings
	// toward the eyes on the reveal roll, so the panel is offset mostly along -X with a small
	// +Z pull toward the player for a comfortable reading distance.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room|Menu")
	FVector AttachRelativeLocation = FVector(-14.0f, 0.0f, 6.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room|Menu", meta = (ClampMin = "0.05", UIMin = "0.05", UIMax = "3.0"))
	float BoardScale = 0.9f;

	// Off by default: the settings puck on the left wrist opens the board. Turn on to also
	// bind a controller button. Binding both is what used to make the board flicker.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room|Menu")
	bool bBindToggleInput = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room|Menu")
	FName ToggleMenuInputActionName = TEXT("IA_ToggleMemoryRoomMenu");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room|Menu")
	TObjectPtr<UHapticFeedbackEffect_Base> PressHapticEffect;

	// --- Style ---

	// Base for every surface: an unlit material with a "Texture" parameter into Emissive.
	// The engine's EmissiveTexturedMaterial by default.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room|Menu|Style")
	TObjectPtr<UMaterialInterface> GlassBaseMaterial;

	// Unused on the clipboard (kept so old serialized data does not warn).
	UPROPERTY()
	bool bFlipGlassGradient = false;

	// Clipboard colours. All unlit, so they read the same day or night.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room|Menu|Style")
	FLinearColor BoardColor = FLinearColor(0.020f, 0.016f, 0.013f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room|Menu|Style")
	FLinearColor PaperColor = FLinearColor(0.62f, 0.585f, 0.52f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room|Menu|Style")
	FLinearColor InkColor = FLinearColor(0.055f, 0.045f, 0.038f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room|Menu|Style")
	FLinearColor ClipColor = FLinearColor(0.10f, 0.10f, 0.11f);

	// Extra spacing between the letters of the small-caps labels. The title gets double.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room|Menu|Style", meta = (ClampMin = "0.0", UIMax = "6.0"))
	float LabelTracking = 1.2f;

	// How far a tile grows under the pointer, as a fraction of its size.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room|Menu|Style", meta = (ClampMin = "0.0", UIMax = "0.5"))
	float TileHoverPop = 0.10f;

	// How quickly the tile pop settles; higher is snappier.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room|Menu|Style", meta = (ClampMin = "1.0", UIMax = "40.0"))
	float HoverAnimationSpeed = 14.0f;

	// The scale-in when the board opens (and the scale-out when it closes). 0 snaps.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room|Menu|Style", meta = (ClampMin = "0.0", UIMax = "1.0"))
	float OpenAnimationSeconds = 0.18f;

	// --- Wrist-roll reveal ---
	// Roll is measured around the forearm and reported as degrees away from the watch-check
	// pose, so 0 is "the back of the wrist is facing your eyes" and 180 is "the palm is".
	// The panel comes up on the roll away from the watch check, never on the watch check
	// itself: that twist is the tighter one to hold, and it is already the gesture for
	// reading the wrist pucks.
	//
	// Off by default now: the left controller went from purely decorative (wrist/menu only) to
	// a full second pointer/grab hand, so twisting it to open the menu now fights normal use -
	// every ordinary rotation of an active hand risks tripping the gesture. The settings puck
	// still opens the board directly (ToggleMenu, called from AMemoryRoomSkyButtonActor's own
	// press handling) - it sits at a fixed spot on the wrist and already turns to face the
	// player (bFacePlayer), so the right controller can point at and press it with no twist
	// needed either way. TickWristReveal early-returns with no side effects while this is off,
	// so turning it back on needs nothing else touched.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room|Menu|Wrist Reveal")
	bool bRevealByWristRotation = false;

	// Roll at least this far from the watch-check pose to bring the panel up.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room|Menu|Wrist Reveal", meta = (ClampMin = "60.0", ClampMax = "180.0"))
	float RevealRollDegrees = 110.0f;

	// Anything inside this much of the watch-check pose is the watch check, and stays
	// hidden. A hand at rest sits around 30-40 degrees, so the panel is down by default.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room|Menu|Wrist Reveal", meta = (ClampMin = "0.0", ClampMax = "120.0"))
	float WatchCheckRollDegrees = 55.0f;

	// Which way round the forearm counts as away from the watch check. The default is worked
	// out for a left hand holding the controller; flip it if the panel only ever appears on
	// the twist you were trying to avoid.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room|Menu|Wrist Reveal")
	bool bInvertRevealRollDirection = false;

	// Hold the reveal pose this long before the panel appears, so rolling through it on the
	// way to something else does not flash it up.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room|Menu|Wrist Reveal", meta = (ClampMin = "0.0", UIMax = "1.0"))
	float RevealHoldSeconds = 0.18f;

private:
	void BuildBoard();
	// Places the clipboard a fixed distance in front of the head, facing the viewer.
	void PositionMenuInFrontOfView();
	void ApplyBoardScale();
	void BindInput();
	void ResolveManager();
	bool AttachToLeftController();
	USceneComponent* FindWristComponent() const;
	void TickWristAttach(float DeltaSeconds);
	void TickWristReveal(float DeltaSeconds);
	void TickFacePlayer();
	void TickOpenAnimation(float DeltaSeconds);
	void TickHoverAnimation(float DeltaSeconds);
	// Hides (or restores) every wrist puck while this menu is open, so they do not sit
	// visually buried under the panel. The panel's own Close tile covers closing it back up.
	void SetOtherWristButtonsHidden(bool bShouldHide);
	// Degrees of roll around the forearm away from the watch-check pose, signed by which way
	// round the wrist turned. Returns false when there is no wrist or view to measure from.
	float GetWristRollDegrees(bool& bOutValid) const;
	void RefreshText();
	// Paints the slab, tiles and rules from Style.
	void RebuildStyleMaterials();
	void ApplyHoverVisuals();
	// Rebuilds one tile's transform from its hover blend: a small grow and a lift toward
	// the viewer.
	void ApplyTilePop(UStaticMeshComponent* Panel, float HalfY, float Blend) const;
	FMemoryRoomMenuRow* FindRow(UPrimitiveComponent* Component, bool& bOutIsPlus);
	void PressRow(FMemoryRoomMenuRow& Row, bool bIsPlus);

	UFUNCTION()
	void HandleSettingsChanged();
	UFUNCTION()
	void HandleReloadStarted();
	UFUNCTION()
	void HandleReloadCompleted(int32 PhotoCount, bool bSuccess);

	UPROPERTY() TObjectPtr<USceneComponent> SceneRoot;
	// The dark backing board.
	UPROPERTY() TObjectPtr<UStaticMeshComponent> BackgroundPanel;
	// The cream page that sits in front of the board and carries the text.
	UPROPERTY() TObjectPtr<UStaticMeshComponent> PaperPanel;
	// The metal clip across the top.
	UPROPERTY() TObjectPtr<UStaticMeshComponent> ClipPanel;
	// Printed ink rules: one under the title, one between the settings and the actions.
	UPROPERTY() TObjectPtr<UStaticMeshComponent> TitleRule;
	UPROPERTY() TObjectPtr<UStaticMeshComponent> SectionRule;
	UPROPERTY() TObjectPtr<UTextRenderComponent> TitleText;
	UPROPERTY() TObjectPtr<UTextRenderComponent> StatusText;
	// The glass panel is painted emissive so it stays visible at night (see MemoryRoomUIGlass),
	// but the text render components use the engine's stock font material, which IS lit - so at
	// night, with almost no scene light reaching the wrist, the labels go dark even though the
	// glass around them keeps glowing. A small always-on, unshadowed light fixed to the board
	// keeps the text legible regardless of theme/time of day, without hunting down and swapping
	// in a replacement font material.
	UPROPERTY() TObjectPtr<UPointLightComponent> BoardLight;

	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> BoardMaterial;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> PaperMaterial;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> InkMaterial;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> ClipMaterial;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> TileMaterial;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> TileHotMaterial;

	UPROPERTY() TArray<FMemoryRoomMenuRow> Rows;
	TWeakObjectPtr<UPrimitiveComponent> HoveredComponent;
	// The palette in effect. Struct defaults are the Hearth look; ApplyUIStyle replaces it.
	FMemoryRoomWristUIStyle Style;
	// Printed-ink text colours on the cream page (dark on light).
	FColor TitleColor = FColor(36, 28, 22);
	FColor LabelColor = FColor(58, 48, 40);
	FColor DimLabelColor = FColor(104, 92, 80);
	FColor ValueColor = FColor(104, 52, 26);
	FColor HighlightColor = FColor(170, 74, 20);
	// 0 hidden, 1 fully open; eased toward bMenuOpen every tick.
	float OpenProgress = 0.0f;
	bool bMenuOpen = false;
	bool bStyled = false;
	bool bAttachedToController = false;
	// True while the panel is up because of the wrist roll rather than a puck press, so a
	// hand-opened panel is not yanked shut the moment the wrist passes the watch check.
	bool bWristGestureOwnsMenu = false;
	TWeakObjectPtr<USceneComponent> WristComponent;
	float WristRetrySeconds = 0.0f;
	float RevealHeldSeconds = 0.0f;
	FString StatusMessage;
};
