// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MemoryRoomSkyDomeActor.h"
#include "MemoryRoomThemeAsset.h" // FMemoryRoomWristUIStyle
#include "MemoryRoomSkyButtonActor.generated.h"

class AMemoryRoomMenuActor;
class UBoxComponent;
class UHapticFeedbackEffect_Base;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UPrimitiveComponent;
class USceneComponent;
class UStaticMeshComponent;
class UTextRenderComponent;

UENUM(BlueprintType)
enum class EMemoryRoomWristButtonKind : uint8
{
	// Disc shows the live sky; press cycles Dawn / Sunset / Night.
	TimeOfDay,
	// Dark glass disc with a three-bar menu glyph; press toggles the settings board.
	Settings,
	// Dark glass disc with a single-bar glyph; press un-places every freely placed photo
	// (AMemoryRoomManager::ResetAllPlacedPhotos), sending them all back to the gallery.
	ResetPlacements
};

/**
 * A small watch face that rides on the player's left controller. Point at it with the other
 * hand and pull the trigger. The Kind decides what it does.
 *
 * Built like a watch: a dark glass case, a gold bezel ring, and the face - either a live
 * piece of the sky or dark glass with a menu glyph. The ring brightens under the pointer.
 *
 * Hit testing goes through the same Visibility line trace the menu board uses, so
 * UMemoryRoomSelectionComponent drives hover and press for both.
 */
UCLASS(Blueprintable)
class MEMORYROOM_API AMemoryRoomSkyButtonActor : public AActor
{
	GENERATED_BODY()

public:
	AMemoryRoomSkyButtonActor();

	virtual void Tick(float DeltaSeconds) override;

	// True when the component belongs to this button, so the pointer knows it hit a button
	// rather than a photo.
	bool IsButtonComponent(const UPrimitiveComponent* Component) const;
	void SetHovered(bool bNewHovered);
	bool IsHovered() const { return bHovered; }

	// Advances the time of day. Returns false when no sky dome could be found.
	UFUNCTION(BlueprintCallable, Category = "Memory Room|Sky Button")
	bool PressButton();

	// Finds a wrist button of the given kind, spawning one if it is missing. ExtraOffset is
	// added to the puck's attach location before it first attaches (used to space two pucks).
	static AMemoryRoomSkyButtonActor* FindOrSpawn(UWorld* World, EMemoryRoomWristButtonKind Kind, const FVector& ExtraOffset = FVector::ZeroVector, TSubclassOf<AMemoryRoomSkyButtonActor> ButtonClass = nullptr);

	EMemoryRoomWristButtonKind GetKind() const { return Kind; }

	// Repaints the case, ring, glyph and label from the active theme's wrist-UI style.
	void ApplyUIStyle(const FMemoryRoomWristUIStyle& NewStyle);

protected:
	virtual void BeginPlay() override;

	// What this puck does. Set before BeginPlay; the manager spawns one of each.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room|Sky Button")
	EMemoryRoomWristButtonKind Kind = EMemoryRoomWristButtonKind::TimeOfDay;

	// Matched against the pawn's scene components by name; the first component containing
	// this text and looking like a controller wins.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room|Sky Button")
	FName AttachControllerNameFilter = TEXT("Left");

	// Offset from the controller origin, roughly where a watch face would sit. In the left
	// controller's grip-pose axes (see quest-grip-pose-axes in project memory) +Z is backward,
	// toward the player/wrist side, away from the aim/fingers - pushed further out along it than
	// the original 4.5 now that the left hand is a busy grab/select hand too, so the puck sits
	// nearer the forearm and clear of whatever the hand is doing up front.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room|Sky Button")
	FVector AttachRelativeLocation = FVector(2.0f, 0.0f, 7.0f);

	// Scales the whole puck (case, ring, face, glyph, label - all under SceneRoot). Kept small
	// so the row of pucks barely intrudes on the wrist; the settings puck now just summons the
	// clipboard so it does not need to be a big target.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room|Sky Button", meta = (ClampMin = "0.05", UIMin = "0.05"))
	float ButtonScale = 0.45f;

	// Turns the puck to face the headset so the label stays readable at any wrist angle.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room|Sky Button")
	bool bFacePlayer = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room|Sky Button", meta = (ClampMin = "1.0", UIMin = "1.0"))
	float HoverScaleMultiplier = 1.18f;

	// How quickly the hover scale settles; higher is snappier.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room|Sky Button", meta = (ClampMin = "1.0", UIMax = "40.0"))
	float HoverAnimationSpeed = 14.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room|Sky Button")
	FColor IdleLabelColor = FColor(219, 209, 189);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room|Sky Button")
	FColor HoveredLabelColor = FColor(255, 199, 107);

	// Base for the case, ring and glyph: an unlit material with a "Texture" parameter into
	// Emissive. The engine's EmissiveTexturedMaterial by default; an authored material with
	// the same parameter can replace it.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room|Sky Button|Style")
	TObjectPtr<UMaterialInterface> GlassBaseMaterial;

	// Flip if the ember glow lands at the top of the settings face instead of the bottom.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room|Sky Button|Style")
	bool bFlipGlassGradient = false;

	// Extra spacing between the letters of the label.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room|Sky Button|Style", meta = (ClampMin = "0.0", UIMax = "6.0"))
	float LabelTracking = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room|Sky Button")
	TObjectPtr<UHapticFeedbackEffect_Base> PressHapticEffect;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room|Sky Button")
	TSubclassOf<AMemoryRoomSkyDomeActor> SkyDomeClass;

private:
	void BuildComponents();
	bool AttachToPlayerController();
	// Binds the disc to the sky dome's material and live gradient texture.
	void EnsurePuckMaterial();
	// Paints the case, ring, glyph and (for Settings) the face from Style.
	void RebuildStyleMaterials();
	AMemoryRoomSkyDomeActor* ResolveSkyDome();
	void RefreshLabel();
	void ApplyHoverVisuals();
	void TickHoverScale(float DeltaSeconds);

	UFUNCTION()
	void HandleTimeOfDayChanged(EMemoryRoomTimeOfDay TimeOfDay);

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Memory Room|Sky Button|Components", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<USceneComponent> SceneRoot;

	// The face: the live sky (TimeOfDay) or dark glass (Settings).
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Memory Room|Sky Button|Components", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UStaticMeshComponent> PuckMesh;

	// A thin gold ring just behind the face, wide enough to show as a rim around it.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Memory Room|Sky Button|Components", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UStaticMeshComponent> BezelRing;

	// The dark glass case behind the ring.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Memory Room|Sky Button|Components", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UStaticMeshComponent> CaseMesh;

	// Three gold bars on the Settings face - the menu glyph. Hidden on the TimeOfDay puck.
	UPROPERTY()
	TArray<TObjectPtr<UStaticMeshComponent>> GlyphBars;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Memory Room|Sky Button|Components", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UBoxComponent> Collision;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Memory Room|Sky Button|Components", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UTextRenderComponent> Label;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> PuckMaterialInstance;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> GlassMaterial;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> AccentMaterial;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> HighlightMaterial;

	AMemoryRoomMenuActor* ResolveSettingsMenu();

	FMemoryRoomWristUIStyle Style;
	TWeakObjectPtr<AMemoryRoomSkyDomeActor> CachedSkyDome;
	TWeakObjectPtr<AMemoryRoomMenuActor> CachedSettingsMenu;
	float AttachRetrySeconds = 0.0f;
	// 0 at rest, 1 fully hovered; eased toward the target every tick.
	float HoverBlend = 0.0f;
	// Guards against a held trigger firing PressButton every frame (which flickered the
	// settings board open/closed).
	double LastPressSeconds = -10.0;
	bool bHovered = false;
	bool bStyled = false;
	bool bAttachedToController = false;
	bool bLoggedAttachFailure = false;
};
