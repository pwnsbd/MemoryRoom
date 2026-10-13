// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Engine/EngineTypes.h"
#include "MemoryRoomSelectionComponent.generated.h"

class AMemoryPhotoActor;
class AMemoryPanoramaActor;
class AMemoryRoomManager;
class AMemoryRoomMenuActor;
class AMemoryRoomSkyButtonActor;
class UCameraComponent;
class UMaterialInterface;
class UStaticMesh;
class UStaticMeshComponent;
class UTextRenderComponent;
class USceneComponent;
class USplineComponent;
class USplineMeshComponent;

// Which controller an interaction call came from. Right is the default on every entry point
// below so every existing Blueprint binding (all on the right controller) keeps working
// unchanged; the left overloads are new bindings only.
UENUM(BlueprintType)
enum class EMemoryRoomHand : uint8
{
	Right,
	Left
};

UCLASS(ClassGroup=(MemoryRoom), meta=(BlueprintSpawnableComponent))
class MEMORYROOM_API UMemoryRoomSelectionComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UMemoryRoomSelectionComponent();

	virtual void PostLoad() override;
	virtual void PostInitProperties() override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	UFUNCTION(BlueprintCallable, Category = "Memory Room|Selection")
	void ActivateHoveredPhoto(EMemoryRoomHand Hand = EMemoryRoomHand::Right);

	UFUNCTION(BlueprintCallable, Category = "Memory Room|360")
	bool OpenPanorama(AMemoryPhotoActor* Photo);

	UFUNCTION(BlueprintCallable, Category = "Memory Room|360")
	void ClosePanorama();

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Memory Room|360")
	bool IsPanoramaOpen() const { return PanoramaViewer.IsValid(); }

	// If the other hand is already holding a photo and this hand is hovering that same photo,
	// this joins a two-hand pinch instead of grabbing a second photo - see UpdatePinchZoom.
	UFUNCTION(BlueprintCallable, Category = "Memory Room|Selection")
	void GrabHoveredPhoto(EMemoryRoomHand Hand = EMemoryRoomHand::Right);

	// Releasing the hand currently driving position hands control to the other hand if it is
	// still gripping (the photo keeps following whichever hand is left); releasing the second,
	// pinch-only hand just drops the pinch and leaves the first hand holding normally.
	UFUNCTION(BlueprintCallable, Category = "Memory Room|Selection")
	void ReleaseGrabbedPhoto(EMemoryRoomHand Hand = EMemoryRoomHand::Right);

	UFUNCTION(BlueprintCallable, Category = "Memory Room|Selection", meta = (DeprecatedFunction, DeprecationMessage = "Use GrabHoveredPhoto on grip press and ReleaseGrabbedPhoto on grip release. This legacy action toggles grab/release for old bindings."))
	void SelectHoveredPhoto();

	UFUNCTION(BlueprintCallable, Category = "Memory Room|Scrolling")
	void AddScrollInput(float Amount);

	UFUNCTION(BlueprintCallable, Category = "Memory Room|Scrolling")
	void AddForwardScrollInput(float Amount = 1.0f);

	UFUNCTION(BlueprintCallable, Category = "Memory Room|Scrolling")
	void AddBackwardScrollInput(float Amount = 1.0f);

	UFUNCTION(BlueprintCallable, Category = "Memory Room|Scrolling")
	void StopScrollInput();

	// Left-stick locomotion - forwards to AMemoryRoomManager. Wire IA_MemoryRoomMove
	// (Axis2D) Triggered -> SetLocomotionMoveInput, Completed -> StopLocomotionMoveInput,
	// and IA_MemoryRoomFloat (digital) Triggered -> ToggleFloat, all off this component.
	UFUNCTION(BlueprintCallable, Category = "Memory Room|Locomotion")
	void SetLocomotionMoveInput(FVector2D Value);

	UFUNCTION(BlueprintCallable, Category = "Memory Room|Locomotion")
	void StopLocomotionMoveInput();

	// Right-stick smooth turn (X axis only) - forwards to AMemoryRoomManager. Wire
	// IA_MemoryRoomTurn (Axis1D) Triggered -> SetTurnInput, Completed -> StopTurnInput.
	UFUNCTION(BlueprintCallable, Category = "Memory Room|Locomotion")
	void SetTurnInput(float Value);

	UFUNCTION(BlueprintCallable, Category = "Memory Room|Locomotion")
	void StopTurnInput();

	UFUNCTION(BlueprintCallable, Category = "Memory Room|Locomotion")
	void ToggleFloat();

	// Bind to the off-hand thumbstick's Y axis. Only does anything while a photo is expanded
	// or grabbed - positive zooms in, matching AddScrollInput's own up-is-positive convention.
	// No effect otherwise (there is nothing to zoom).
	UFUNCTION(BlueprintCallable, Category = "Memory Room|Zoom")
	void AddZoomInput(float Amount);

	// Analog hand curl. Wire the trigger and grip axes here if you want the fingers to follow
	// the pull; the grab and release calls above already drive it on their own otherwise.
	UFUNCTION(BlueprintCallable, Category = "Memory Room|Controllers")
	void SetHandCurlInput(float GripValue, float TriggerValue);

protected:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room|Selection")
	FComponentReference PointerComponent;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room|Selection")
	FComponentReference CameraComponent;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room|Scrolling")
	TObjectPtr<AMemoryRoomManager> MemoryRoomManager;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room|Selection", meta = (ClampMin = "0.0"))
	float TraceDistance;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room|Pointer")
	bool bShowPointer;

	// Kept for compatibility; BeamStartWidth/BeamEndWidth are what shape the thread now.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room|Pointer", meta = (ClampMin = "0.1"))
	float BeamWidth;

	// The thread is thin at the hand and tapers almost to nothing at the far end, so it
	// reads as a wisp of light being cast rather than a laser.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room|Pointer", meta = (ClampMin = "0.05", UIMax = "3.0"))
	float BeamStartWidth = 0.85f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room|Pointer", meta = (ClampMin = "0.0", UIMax = "2.0"))
	float BeamEndWidth = 0.12f;

	// Downward sag of the thread as a fraction of its length, like a line with weight. The
	// spline tangent shift this produces is Sag*4 (see UpdatePointerVisual), so even 0.05 bows
	// the visual beam by ~20% of its own length - harmless at the old thick beam width and a
	// long free-aim distance, but at menu range (panel ~90cm out, button rows only ~9-10cm
	// apart) and the current thin taper, that bow reads as a real vertical offset between the
	// beam you see and the dead-straight line the trace actually fires, which is what made
	// pointing at one row land on another. Zero keeps the rendered thread exactly on the trace.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room|Pointer", meta = (ClampMin = "0.0", ClampMax = "0.4"))
	float BeamSagFraction = 0.0f;

	// Gentle width shimmer so the thread feels alive.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room|Pointer", meta = (ClampMin = "0.0", ClampMax = "0.5"))
	float BeamShimmerAmount = 0.12f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room|Pointer")
	TObjectPtr<UMaterialInterface> DefaultBeamMaterial;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room|Pointer")
	TObjectPtr<UMaterialInterface> ValidTargetBeamMaterial;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room|Pointer")
	TObjectPtr<UStaticMesh> HitIndicatorMesh;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room|Pointer", meta = (ClampMin = "0.01"))
	FVector HitIndicatorScale;

	// Radius (cm) of the spark that rests where the thread lands on a photo. Pulses gently.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room|Pointer", meta = (ClampMin = "0.1", UIMax = "8.0"))
	float TipSparkRadius = 2.4f;

	// The faint aiming dot shown when the thread is not on a photo.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room|Pointer", meta = (ClampMin = "0.0", UIMax = "4.0"))
	float TipIdleRadius = 0.8f;

	// Same spark, but sized for the menu board instead of a photo: the board's press targets
	// are 4 cm square (PressHalfY/PressHalfZ in MemoryRoomMenuActor.cpp), so TipSparkRadius's
	// 2.4 (a 4.8 cm disc, before the landing flare swells it further) was covering the whole
	// button rather than marking a landing point on it. Used in place of TipSparkRadius/
	// TipIdleRadius whenever the hit is on the menu board.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room|Pointer", meta = (ClampMin = "0.1", UIMax = "4.0"))
	float MenuHitIndicatorRadius = 0.9f;

	// The spark briefly flares when it first lands on a photo, then settles - a small "it
	// noticed you" beat. Strength is how many times bigger it swells at the peak.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room|Pointer", meta = (ClampMin = "0.0", UIMax = "5.0"))
	float TipLandFlareStrength = 2.4f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room|Pointer", meta = (ClampMin = "0.05", UIMax = "1.5"))
	float TipLandFlareSeconds = 0.28f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room|Pointer")
	bool bShowHmdTargetX;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room|Pointer", meta = (ClampMin = "0.1"))
	float HmdTargetXSize;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room|Pointer", meta = (ClampMin = "0.1"))
	float HmdTargetXThickness;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room|Pointer")
	TObjectPtr<UMaterialInterface> HmdTargetXMaterial;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room|Expanded", meta = (ClampMin = "0.0"))
	float ExpandedDistance;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room|Expanded")
	float ExpandedVerticalOffset;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room|Expanded")
	FRotator ExpandedRotationOffset;

	// How far ahead of the controller the photo is held, in centimetres along the hand's
	// own forward axis.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room|Grab", meta = (ClampMin = "0.0"))
	float GrabDistance;

	// Offset along the hand's own up axis (raise/lower the held photo relative to the grip).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room|Grab")
	float GrabVerticalOffset;

	// Moves the grabbed photo centre toward its right edge by this fraction of its visual
	// half-width, so the hand holds it by the side like a physical print. 0 = held centred.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room|Grab")
	float GrabRightSideOffsetMultiplier;

	// Fixed rotation of the photo relative to the controller. The photo then rides the
	// controller's full 6DOF pose - twist, tilt and roll all carry - so this only sets
	// which way the image faces in the hand. Tune once for your pawn's controller axes.
	// Right hand only - the right controller's own +X is its aim direction, which is what this
	// constant assumes. The left controller's aim is not its +X (see quest-grip-pose-axes in
	// project memory), so BuildHandGrabOrientation solves the left hand's orientation directly
	// from its measured aim/up axes instead of using a fixed offset like this one.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room|Grab")
	FRotator GrabRotationOffset;

	// Extra rotation applied on top of the left hand's solved grab orientation. The solve uses
	// the same measured aim/up facts already verified for the wrist-reveal gate (see
	// quest-grip-pose-axes in project memory), but has not itself been checked in headset for
	// grabbing - if a left-handed grab shows the photo rolled or backwards, adjust this in 90
	// degree steps rather than re-deriving the whole thing.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room|Grab")
	FRotator LeftGrabRotationTrim = FRotator::ZeroRotator;

	// Size of the held photo as a fraction of its gallery size. ~0.15-0.2 reads as a
	// print in the hand; 1.0 is the full wall-sized image.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room|Grab", meta = (ClampMin = "0.01", UIMax = "1.0"))
	float GrabScaleMultiplier;

	// How fast AddZoomInput's axis grows/shrinks the zoom multiplier - applied as a continuous
	// exponential rate (Multiplier *= e^(Axis * ZoomSpeed * DeltaTime)) rather than a linear
	// add, so zooming feels the same rate proportionally whether the photo is currently small
	// or large.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room|Zoom", meta = (ClampMin = "0.05"))
	float ZoomSpeed = 1.2f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room|Zoom", meta = (ClampMin = "0.05", UIMax = "1.0"))
	float MinZoomMultiplier = 0.4f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room|Zoom", meta = (ClampMin = "1.0", UIMax = "6.0"))
	float MaxZoomMultiplier = 3.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room|Debug")
	bool bDebugTrace;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room|Debug")
	bool bDebugSelectionLog;

	// Draws each motion controller's own axes at the controller, with a label naming its
	// motion source. Answers "which way is this controller actually pointing" directly,
	// instead of guessing offsets and rotations from the outside. Off now that the numbers it
	// gave (see quest-grip-pose-axes in project memory) are baked into the hand rotations and
	// the wrist-reveal gate; flip back on if either ever needs re-measuring.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room|Calibration")
	bool bShowControllerCalibration = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room|Calibration", meta = (ClampMin = "1.0"))
	float CalibrationAxisLength = 12.0f;

	// A UMotionControllerComponent draws nothing on its own - the model comes from a child
	// UXRDeviceVisualizationComponent, and BP_VRPawn only ever had one, on the right hand.
	// With this on, any hand controller missing one gets a visualization built to match the
	// one that already exists, so both hands show the same model in the same style.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room|Controllers")
	bool bEnsureHandVisuals = true;

	// Draw the XR template's mannequin hand in place of the controller model. Off falls back
	// to the plain controller models on both hands.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room|Controllers")
	bool bUseHandMeshes = true;

	// The Meta OpenXR hands (copied from the Interaction SDK into Content/MetaHands) each have
	// their own mesh, so both hands are on. Turning this off drops the left controller back to
	// the plain stock model, same as bUseHandMeshes being off would give it.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room|Controllers")
	bool bUseHandMeshOnLeft = true;

	// Left and right have separate meshes and clips, so nothing is mirrored. Pointing these back
	// at /Game/XRMannequins/Meshes/SKM_MannyXR_right (right only) restores the old mannequin.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room|Controllers")
	FString RightHandMeshPath = TEXT("/Game/MetaHands/Models/Hands/SK_OpenXRHand_Right.SK_OpenXRHand_Right");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room|Controllers")
	FString LeftHandMeshPath = TEXT("/Game/MetaHands/Models/Hands/SK_OpenXRHand_Left.SK_OpenXRHand_Left");

	// The open-to-fist clip the curl scrubs through, one per hand like the meshes.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room|Controllers")
	FString RightHandGraspAnimPath = TEXT("/Game/MetaHands/Models/Hands/Animations/Anim_OpenXRHand_Right_MetaQuestPro_GripTrigger_Down.Anim_OpenXRHand_Right_MetaQuestPro_GripTrigger_Down");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room|Controllers")
	FString LeftHandGraspAnimPath = TEXT("/Game/MetaHands/Models/Hands/Animations/Anim_OpenXRHand_Left_MetaQuestPro_GripTrigger_Down.Anim_OpenXRHand_Left_MetaQuestPro_GripTrigger_Down");

	// Meta's static controller model, attached to each grip controller at identity so the hand
	// holds it. The stock device-visualization models are hidden on any side that has a hand.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room|Controllers")
	bool bShowControllerModels = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room|Controllers")
	FString LeftControllerMeshPath = TEXT("/Game/MetaHands/Models/Controllers/MetaQuestPro/SM_MetaQuestPro_Left.SM_MetaQuestPro_Left");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room|Controllers")
	FString RightControllerMeshPath = TEXT("/Game/MetaHands/Models/Controllers/MetaQuestPro/SM_MetaQuestPro_Right.SM_MetaQuestPro_Right");

	// Where the wrist sits relative to the controller, right hand. Starting value taken from
	// the Interaction SDK's controller-driven hand target (IsdkXRUtils.h, expressed in its
	// controller-mesh space), so it is a first guess in grip-pose axes, not a calibrated one.
	// Locations are still that SDK guess; only the rotations below have been calibrated.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room|Controllers")
	FVector RightHandRelativeLocation = FVector(-10.5f, 3.0f, -4.2f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room|Controllers")
	FVector LeftHandRelativeLocation = FVector(-10.5f, -3.0f, -4.2f);

	// Rotation was calibrated in PIE on 2026-10-13 for the left hand (Pitch 0 Yaw 0 Roll 0 looks
	// right, hand sitting on its own grip-pose controller). The right is assumed symmetric -
	// separate mirrored mesh on the matching grip pose - and is unverified on device.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room|Controllers")
	FRotator RightHandRelativeRotation = FRotator(0.0f, 0.0f, 0.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room|Controllers")
	FRotator LeftHandRelativeRotation = FRotator(0.0f, 0.0f, 0.0f);

	// How far the trigger alone curls the hand. The grip always curls it the whole way.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room|Controllers", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float TriggerCurlAmount = 0.55f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room|Controllers", meta = (ClampMin = "0.1"))
	float HandCurlInterpSpeed = 22.0f;

	// How long a trigger press holds the partial curl, since the trigger is bound as a press
	// rather than an axis.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room|Controllers", meta = (ClampMin = "0.0"))
	float TriggerPulseDuration = 0.18f;

private:
	void ApplyDefaultAssetReferences();
	void ApplyDefaultComponentReferences();
	USceneComponent* ResolvePointerComponent();
	USceneComponent* ResolveComponentByName(FName ComponentName) const;
	USceneComponent* ResolveControllerPointerComponent() const;
	UCameraComponent* ResolveCameraComponent();
	AMemoryRoomManager* ResolveMemoryRoomManager();
	void EnsurePointerComponents();
	void EnsureHandVisuals();
	void UpdateHandCurl(float DeltaTime);
	void UpdateControllerCalibration(float DeltaTime);
	AMemoryRoomMenuActor* ResolveMenuActorForCalibration() const;
	void UpdatePointerVisual(USplineComponent* Spline, USplineMeshComponent* Beam, UStaticMeshComponent* HitIndicator, const FVector& Start, const FVector& End, bool bValidTarget, bool bShowHitIndicator, bool bUseBeamVisual, bool bUseHmdTargetX, float IndicatorRadiusScale = 1.0f);
	void UpdateHmdTargetXVisual(const FVector& Start, const FVector& End, bool bVisible);
	void HideHmdTargetXVisual();
	void UpdateHoveredPhoto(TWeakObjectPtr<AMemoryPhotoActor>& HoveredPhotoRef, AMemoryPhotoActor* NewHoveredPhoto, const FVector& HoverSourceLocation);
	FTransform BuildExpandedTransform(AMemoryPhotoActor* Photo) const;
	FTransform BuildGrabTransform(AMemoryPhotoActor* Photo, USceneComponent* Pointer, EMemoryRoomHand Hand) const;
	UFUNCTION()
	void ClearDestroyedReferences(AActor* DestroyedActor);

	// AMemoryRoomManager::OnPlacedPhotoUnplaced - drops GrabbedPhoto/ExpandedPhoto if they
	// point at Actor, so a reset (or a specific return-to-gallery) that lands on whatever you
	// currently happen to be holding actually sticks instead of being silently re-placed the
	// moment you next let go (PlacePhotoInWorld treats any release as a first-time placement
	// again once IsPlacedInWorld() reads false).
	UFUNCTION()
	void HandlePlacedPhotoUnplaced(AMemoryPhotoActor* Actor);

	// --- Left controller: independent pointer, mirroring the right hand's own trace/hover. ---
	USceneComponent* ResolveLeftPointerComponent();
	void EnsureLeftPointerComponents();

	// The right controller's own +X happens to be its aim direction, so GetForwardVector() has
	// always doubled as "aim" for it. The left controller's does not (+X is up, aim is -Z - see
	// quest-grip-pose-axes in project memory), so every left-hand trace/orientation goes through
	// this instead of GetForwardVector() directly.
	FVector GetHandAimDirection(EMemoryRoomHand Hand, const USceneComponent* Pointer) const;

	// Same idea for "up": the right controller's local +Z is up (GetUpVector() already correct
	// for it); the left's local +X is up instead (GetForwardVector()). Used for GrabVerticalOffset
	// and for solving the left hand's grab orientation.
	FVector GetHandUpDirection(EMemoryRoomHand Hand, const USceneComponent* Pointer) const;

	// One hand's full trace-and-hover pass: casts from Pointer along that hand's own aim
	// direction, resolves which of menu/sky-button/photo it lands on, updates that hand's own
	// beam/hit-indicator visuals, and writes the result into the three hover refs (each hand
	// gets its own, so both can hover different things at once). Returns the hit photo, if any,
	// so callers with more to do (grab-eligibility) do not need to re-trace.
	AMemoryPhotoActor* TickHandInteraction(EMemoryRoomHand Hand, USceneComponent* Pointer, USplineComponent* Spline, USplineMeshComponent* Beam, UStaticMeshComponent* HitIndicator, bool bAllowHmdFallback, TWeakObjectPtr<AMemoryRoomMenuActor>& HoveredMenuRef, TWeakObjectPtr<AMemoryRoomSkyButtonActor>& HoveredSkyButtonRef, TWeakObjectPtr<AMemoryPhotoActor>& HoveredPhotoRef);

	// Right hand keeps its proven fixed-offset solve (GrabRotationOffset). Left is solved from
	// its own measured aim/up axes instead of a guessed constant - see the property comments.
	FQuat BuildHandGrabOrientation(EMemoryRoomHand Hand, const USceneComponent* Pointer) const;

	// Distance between the two hands while both grip the same held photo drives
	// ExpandedPhotoZoomMultiplier, on top of whatever AddZoomInput already dialed in - stretch
	// apart to zoom in, bring together to zoom out, exactly like a two-finger pinch.
	void UpdatePinchZoom(float DeltaTime, USceneComponent* PrimaryPointer, USceneComponent* SecondaryPointer);

	EMemoryRoomHand PrimaryGrabHand = EMemoryRoomHand::Right;
	bool bSecondaryGrabHandHeld = false;
	float PinchBaseDistance = 0.0f;
	float PinchBaseZoomMultiplier = 1.0f;

	// Same scroll axis that moves the gallery, repurposed: while a photo is expanded or grabbed
	// it isn't part of the pool's viewing-window reflow (LayoutVirtualizedGallery skips any
	// actor with IsExpanded()), so nudging its own AssignedPhotoIndex is safe and picked up
	// automatically next tick by ProcessVirtualizedExpandedSelection, which loads whatever file
	// GalleryPhotoFiles[AssignedPhotoIndex] now points at - no separate load path needed.
	// SignedAmount uses the same sign convention as a raw AddScrollInput call.
	bool RouteScrollInputToExpandedNav(float SignedAmount);
	void UpdateExpandedPhotoNavAxis(float SignedAmount, AMemoryPhotoActor* NavTarget);
	void AdvanceExpandedPhoto(AMemoryPhotoActor* NavTarget, int32 Direction);

	// Hysteresis for the axis-to-step conversion above: armed again only once the stick returns
	// near neutral, so one flick past FireThreshold advances exactly one photo rather than
	// spraying steps every tick the stick stays deflected.
	bool bExpandedNavArmed = true;

	// Drives ExpandedPhotoZoomMultiplier from PendingZoomAxis each tick, then either lets the
	// existing grabbed-photo transform update (which already runs every tick) read the new
	// multiplier, or - for a merely-expanded, not-grabbed photo, which otherwise only gets
	// placed once - re-pushes CachedExpandedBaseTransform with the new scale.
	void UpdateExpandedPhotoZoom(float DeltaTime);
	void ResetExpandedPhotoZoom(const FTransform& BaseTransform);

	float PendingZoomAxis = 0.0f;
	// Relative to GetExpandedScale(), not to the actor's tiny wall-layout scale. Reset to 1 on
	// every fresh Expand()/Grab so each photo you open starts at its normal size.
	float ExpandedPhotoZoomMultiplier = 1.0f;
	// Location/rotation only (its scale is ignored) - the placement a plain expand computed
	// once against the viewer, reused so zoom changes size without turning a static expanded
	// photo into a camera-follower.
	FTransform CachedExpandedBaseTransform = FTransform::Identity;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UTextRenderComponent>> CalibrationLabels;

	UPROPERTY(Transient)
	TObjectPtr<USkeletalMeshComponent> LeftHandMeshComponent;

	UPROPERTY(Transient)
	TObjectPtr<USkeletalMeshComponent> RightHandMeshComponent;

	// Where the fingers are being asked to go, and where they actually are this frame.
	float TargetHandCurl = 0.0f;
	float CurrentHandCurl = 0.0f;
	// The three things that can ask for curl: a held grab, an analog axis, a trigger tap.
	bool bGrabCurlHeld = false;
	float AnalogHandCurl = 0.0f;
	float TriggerPulseSeconds = 0.0f;

	UPROPERTY(Transient)
	TObjectPtr<USplineComponent> PointerSplineComponent;

	UPROPERTY(Transient)
	TObjectPtr<USplineMeshComponent> BeamSplineMeshComponent;

	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> HitIndicatorComponent;

	// Left hand's own beam/spark, built by EnsureLeftPointerComponents. No HmdTargetX pair -
	// the camera-as-pointer fallback only ever makes sense for the one "main" pointer when no
	// controller exists on the pawn at all, and the left hand only ever exists as a controller.
	UPROPERTY(Transient)
	TObjectPtr<USplineComponent> LeftPointerSplineComponent;

	UPROPERTY(Transient)
	TObjectPtr<USplineMeshComponent> LeftBeamSplineMeshComponent;

	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> LeftHitIndicatorComponent;

	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> HmdTargetXComponentA;

	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> HmdTargetXComponentB;

	UPROPERTY(Transient)
	TObjectPtr<UStaticMesh> DefaultBeamMesh;

	UPROPERTY(Transient)
	TObjectPtr<UStaticMesh> DefaultXMesh;

	TWeakObjectPtr<AMemoryPhotoActor> HoveredPhoto;
	TWeakObjectPtr<AMemoryRoomMenuActor> HoveredMenu;
	TWeakObjectPtr<AMemoryRoomSkyButtonActor> HoveredSkyButton;
	// Left hand's own hover trackers - kept separate so each hand can hover something different
	// at the same time (the photo's own hover glow is shared state, though, so two hands
	// hovering the same photo at once is a known minor edge case: whichever hand's update runs
	// last each tick wins the glow's source location).
	TWeakObjectPtr<AMemoryPhotoActor> LeftHoveredPhoto;
	TWeakObjectPtr<AMemoryRoomMenuActor> LeftHoveredMenu;
	TWeakObjectPtr<AMemoryRoomSkyButtonActor> LeftHoveredSkyButton;
	TWeakObjectPtr<AMemoryPanoramaActor> PanoramaViewer;
	TWeakObjectPtr<AMemoryPhotoActor> ExpandedPhoto;
	TWeakObjectPtr<AMemoryPhotoActor> GrabbedPhoto;
	TWeakObjectPtr<USceneComponent> CachedPointerComponent;
	TWeakObjectPtr<USceneComponent> CachedLeftPointerComponent;
	TWeakObjectPtr<UCameraComponent> CachedCameraComponent;
	TWeakObjectPtr<AMemoryRoomManager> CachedMemoryRoomManager;
	bool bLoggedMissingPointer;
	bool bLoggedMissingLeftPointer = false;
	// Set once OnPlacedPhotoUnplaced is bound, the first time ResolveMemoryRoomManager finds a
	// manager - guards against binding twice, not against retrying while none exists yet.
	bool bBoundPlacedPhotoDelegate = false;

	// Seconds since the spark last landed on a fresh photo; large means "not flaring".
	float SparkLandFlareElapsed = 1000.0f;
};
