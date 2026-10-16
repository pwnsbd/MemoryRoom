#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "AMemoryRoomManager.h"
#include "MemoryRoomThemeAsset.h" // FMemoryRoomWristUIStyle
#include "MemoryRoomUIGlass.h" // MemoryRoomUI::EGlassIcon
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
	ChoosePhotoSource,
	Close
};

// One row on the settings board.
//
// Setting rows (one slim full-width row on a settings tab):
//   Photo spacing                 [ < ]   12   [ > ]
//   <- Label (left)               MinusBox / ValueText / PlusBox (right, painted arrows)
//
// Action rows (Home tab) are one full-width chip (the Minus box) with a painted Icon at its
// left and the Label after it, left-aligned. No Plus box or value.
USTRUCT()
struct FMemoryRoomMenuRow
{
	GENERATED_BODY()

	// Which tab shows this row: 0 Home, 1 Gallery, 2 Atmosphere, 3 Comfort.
	int32 Tab = 0;
	bool bIsSetting = false;
	EMemoryRoomUserSetting Setting = EMemoryRoomUserSetting::TimeOfDay;
	EMemoryRoomMenuAction Action = EMemoryRoomMenuAction::Close;

	UPROPERTY() TObjectPtr<UBoxComponent> MinusBox;
	UPROPERTY() TObjectPtr<UBoxComponent> PlusBox;
	UPROPERTY() TObjectPtr<UStaticMeshComponent> MinusPanel;
	UPROPERTY() TObjectPtr<UStaticMeshComponent> PlusPanel;
	// Setting rows: the slim full-width background, and the painted < > arrows on the two chips.
	UPROPERTY() TObjectPtr<UStaticMeshComponent> RowPanel;
	UPROPERTY() TObjectPtr<UStaticMeshComponent> MinusIcon;
	UPROPERTY() TObjectPtr<UStaticMeshComponent> PlusIcon;
	UPROPERTY() TObjectPtr<UTextRenderComponent> Label;
	UPROPERTY() TObjectPtr<UTextRenderComponent> ValueText;
	// Painted icon plane on action chips (null on setting rows).
	UPROPERTY() TObjectPtr<UStaticMeshComponent> Icon;
	MemoryRoomUI::EGlassIcon IconKind = MemoryRoomUI::EGlassIcon::Folder;

	// Half-extents of each press tile, so the hover pop can rebuild its scale.
	float TileHalfY = 0.0f;
	float TileHalfZ = 0.0f;
	// 0 at rest, 1 fully hovered; eased toward the target every tick.
	float MinusHoverBlend = 0.0f;
	float PlusHoverBlend = 0.0f;
};

// One folder tile in the folder window: glass plane (folder glyph painted in), name, count.
USTRUCT()
struct FMemoryRoomFolderTile
{
	GENERATED_BODY()
	UPROPERTY() TObjectPtr<UBoxComponent> Box;
	UPROPERTY() TObjectPtr<UStaticMeshComponent> Panel;
	UPROPERTY() TObjectPtr<UTextRenderComponent> Text;
	UPROPERTY() TObjectPtr<UTextRenderComponent> Detail;
	FString AlbumId;
	float HoverBlend = 0.0f;
};

// A small button in the folder window (Up, close, page arrows, Use this folder).
USTRUCT()
struct FMemoryRoomFolderButton
{
	GENERATED_BODY()
	UPROPERTY() TObjectPtr<UBoxComponent> Box;
	UPROPERTY() TObjectPtr<UStaticMeshComponent> Panel;
	UPROPERTY() TObjectPtr<UTextRenderComponent> Label;
	// Painted icon plane (null when the button is text only).
	UPROPERTY() TObjectPtr<UStaticMeshComponent> Icon;
	MemoryRoomUI::EGlassIcon IconKind = MemoryRoomUI::EGlassIcon::Folder;
	float HalfY = 2.0f;
	float HalfZ = 2.0f;
	float HoverBlend = 0.0f;
	// True when this button is the always-amber primary action.
	bool bPrimary = false;
	// True for the 2:1 "Up" chip, which has its own glass texture.
	bool bUpShape = false;
};

/**
 * The in-headset settings board. A column of full-width action chips (icon + label) over a
 * compact grid of [<] value [>] settings.
 * Point the free controller at a < or > and pull the trigger. Can float in front of the
 * player (toggled by a face button) or ride the left controller.
 *
 * Styled as warm-tinted glassmorphism (see MemoryRoomUIGlass.h): a frosted translucent
 * board with smaller glass chips, amber highlight on hover / selection, warm off-white text.
 * Fixed look, not themed. Opens with a short scale-in and closes the same way.
 *
 * "Choose folder" opens the folder window (same glass) beside the board: same plane and
 * scale, to its right, top edges aligned, turned a few degrees inward toward the viewer.
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

	// Kept so the theme code can still call it. The glass menu look is fixed and independent of
	// the theme, so the style is ignored; this only makes sure the glass is painted.
	void ApplyUIStyle(const FMemoryRoomWristUIStyle& NewStyle);

	// Live state of the reveal gate as one short line, for the calibration readout: the roll
	// the gate is measuring and which of its three bands that lands in.
	FString GetWristRevealDebugText() const;

	// The controller component the panel measures roll from, once it has found one.
	USceneComponent* GetAttachedWristComponent() const { return WristComponent.Get(); }

protected:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room|Menu")
	TObjectPtr<AMemoryRoomManager> MemoryRoomManager;

	// --- Board placement (rev 3) ---
	// The board is summoned by the wrist Settings puck - it is NOT always in view. On open it
	// appears BoardDistanceCm ahead of the head with its vertical CENTRE BoardCentreBelowEyeCm
	// below eye level (so placement never depends on the board's height; if the board is tall
	// enough that its top edge would reach eye level, it is lowered to keep the top below the
	// eye), tilted back by BoardTiltDegrees so it faces the head. Press the puck again (or the
	// Close chip) to dismiss it.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room|Menu|Placement", meta = (ClampMin = "30.0"))
	float BoardDistanceCm = 65.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room|Menu|Placement", meta = (ClampMin = "0.0"))
	float BoardCentreBelowEyeCm = 28.0f;

	// Pitch that leans the face back toward the head (the board is below eye level).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room|Menu|Placement", meta = (ClampMin = "0.0", ClampMax = "45.0"))
	float BoardTiltDegrees = 20.0f;

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

	// --- Folder window placement ---
	// The folder window hangs off the board's right edge in the board's own plane and scale
	// (top edges aligned). While it is open the board slides left so the pair is centred.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room|Menu|Folder Window", meta = (ClampMin = "0.0", ClampMax = "20.0"))
	float FolderWindowGapCm = 4.0f;

	// Turns the window toward the viewer (about its own vertical axis). 12 is the most the
	// layout contract allows; 0 keeps it flat with the board.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room|Menu|Folder Window", meta = (ClampMin = "0.0", ClampMax = "12.0"))
	float FolderWindowInwardYawDegrees = 8.0f;

	// --- Style ---

	// Translucent base for every glass surface: one "SlateUI" texture parameter carrying colour
	// and alpha. The engine's Widget3DPassThrough_Translucent by default.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room|Menu|Style")
	TObjectPtr<UMaterialInterface> GlassTranslucentMaterial;

	// Unused (kept so old serialized data does not warn).
	UPROPERTY()
	bool bFlipGlassGradient = false;

	// Glass faces are engine planes; if the painted sheen / glyphs come out upside down in PIE,
	// turn this on (spins every face 180 degrees about its normal).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room|Menu|Style")
	bool bFlipGlassFaces = false;

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
	// Shows only the active tab's rows (and enables only their collision).
	void ApplyActiveTab();
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
	// Paints the glass textures and binds them to the board + folder window planes.
	void RebuildStyleMaterials();
	void ApplyHoverVisuals();
	// Rebuilds one chip's transform from its hover blend: a small grow and a lift toward
	// the viewer. RestX is the chip's resting depth.
	void ApplyTilePop(UStaticMeshComponent* Panel, float RestX, float HalfY, float HalfZ, float Blend) const;
	FMemoryRoomMenuRow* FindRow(UPrimitiveComponent* Component, bool& bOutIsPlus);
	void PressRow(FMemoryRoomMenuRow& Row, bool bIsPlus);
	// Folder window: a separate glass panel in front of the view.
	void OpenFolderBrowser();
	void CloseFolderBrowser();
	void ToggleFolderBrowser();
	void SetFolderBrowserVisible(bool bVisible);
	void RefreshFolderBrowser();
	// Hangs the window off the board's right edge (top-aligned) and works out the pair centre.
	void PositionFolderWindow();
	bool PressFolderWindow(UPrimitiveComponent* Hit);
	bool IsFolderWindowComponent(const UPrimitiveComponent* Component) const;
	void TickFolderWindow(float DeltaSeconds);
	void ApplyFolderWindowScale();
	// Parent of the folder level currently shown (empty = root).
	FString GetParentOfFolder(const FString& FolderId) const;

	UFUNCTION()
	void HandleSettingsChanged();
	UFUNCTION()
	void HandleReloadStarted();
	UFUNCTION()
	void HandleReloadCompleted(int32 PhotoCount, bool bSuccess);

	UPROPERTY() TObjectPtr<USceneComponent> SceneRoot;
	// The frosted glass board behind the rows (a flat plane so its rounded corners show).
	UPROPERTY() TObjectPtr<UStaticMeshComponent> BackgroundPanel;
	UPROPERTY() TObjectPtr<UTextRenderComponent> TitleText;
	// "Photos: <current folder>" under the title.
	UPROPERTY() TObjectPtr<UTextRenderComponent> SubtitleText;
	UPROPERTY() TObjectPtr<UTextRenderComponent> StatusText;
	// Every glass plane (board, chips, folder window), so a face flip can be applied in one go.
	UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> GlassFaces;
	// The text render components use the engine's stock font material, which IS lit - so at
	// night, with almost no scene light reaching the wrist, the labels would go dark even though
	// the glass is unlit. A small always-on, unshadowed light fixed to the board (and a second
	// one on the folder window) keeps the text legible regardless of theme/time of day.
	UPROPERTY() TObjectPtr<UPointLightComponent> BoardLight;
	UPROPERTY() TObjectPtr<UPointLightComponent> FolderLight;

	// Eleven frosted glass textures (each <= 256 px) plus small painted icons, all painted once
	// per style rebuild.
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> BoardGlassMaterial;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> SquareChipMaterial;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> SquareChipHotMaterial;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> WideChipMaterial;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> WideChipHotMaterial;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> UpChipMaterial;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> UpChipHotMaterial;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> FolderWindowMaterial;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> FolderTileMaterial;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> FolderTileHotMaterial;

	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> TabChipMaterial;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> TabChipHotMaterial;

	UPROPERTY() TArray<FMemoryRoomMenuRow> Rows;
	// Home / Gallery / Atmosphere / Comfort strip under the subtitle.
	UPROPERTY() TArray<FMemoryRoomFolderButton> TabButtons;
	int32 ActiveTab = 0;

	// --- Folder window ---
	// Its own root, a child of the board's root: it shares the board's pose, scale and open
	// animation, sits to the board's right, and carries the small inward yaw.
	UPROPERTY() TObjectPtr<USceneComponent> FolderRoot;
	UPROPERTY() TObjectPtr<UStaticMeshComponent> BrowserPanel;
	UPROPERTY() TObjectPtr<UTextRenderComponent> BrowserPathText;
	UPROPERTY() TObjectPtr<UTextRenderComponent> BrowserHintText;
	UPROPERTY() TObjectPtr<UTextRenderComponent> BrowserPageText;
	UPROPERTY() TObjectPtr<UTextRenderComponent> BrowserEmptyText;
	UPROPERTY() FMemoryRoomFolderButton BrowserUp;
	UPROPERTY() FMemoryRoomFolderButton BrowserClose;
	UPROPERTY() FMemoryRoomFolderButton BrowserPrev;
	UPROPERTY() FMemoryRoomFolderButton BrowserNext;
	UPROPERTY() FMemoryRoomFolderButton BrowserUse;
	UPROPERTY() TArray<FMemoryRoomFolderTile> FolderTiles;
	FString BrowserCurrentParentId;
	FString BrowserPendingSelectionId;
	int32 BrowserPage = 0;
	bool bFolderBrowserOpen = false;
	// 0 hidden, 1 fully open; eased toward bFolderBrowserOpen every tick.
	float FolderProgress = 0.0f;
	float BrowserOpenedSeconds = 0.0f;
	int32 BrowserLastAlbumCount = -1;
	bool bBrowserPendingShown = false;
	// Board panel size in cm (before BoardScale), for painting its texture.
	float BoardPanelWidthCm = 34.0f;
	float BoardPanelHeightCm = 40.0f;
	// Board-local Y of the middle of the board + folder window pair, so opening the window can
	// slide the board left by that much (times BoardScale) and keep the pair centred.
	float FolderPairCenterYCm = 0.0f;
	// Board-local Z of the top edge of the glass board, for top-aligning the folder window.
	float BoardTopLocalZCm = 0.0f;
	TWeakObjectPtr<UPrimitiveComponent> HoveredComponent;
	// Fixed warm glass text palette (not themed).
	FColor TitleColor = FColor(255, 246, 234);
	FColor LabelColor = FColor(255, 246, 234);
	FColor DimLabelColor = FColor(179, 172, 164); // ~70% of the title colour
	FColor ValueColor = FColor(255, 222, 168);
	FColor HighlightColor = FColor(255, 236, 200);
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
