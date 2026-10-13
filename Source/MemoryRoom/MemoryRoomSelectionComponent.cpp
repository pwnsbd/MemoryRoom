// Fill out your copyright notice in the Description page of Project Settings.

#include "MemoryRoomSelectionComponent.h"
#include "MemoryRoom.h"

#include "Camera/CameraComponent.h"
#include "Components/SceneComponent.h"
#include "Components/SplineComponent.h"
#include "Components/SplineMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "Math/RotationMatrix.h"
#include "Materials/MaterialInterface.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/SkeletalMesh.h"
#include "IMotionController.h"
#include "MotionControllerComponent.h"
#include "XRDeviceVisualizationComponent.h"
#include "AMemoryRoomManager.h"
#include "MemoryPhotoActor.h"
#include "MemoryPanoramaActor.h"
#include "MemoryRoomMenuActor.h"
#include "MemoryRoomSkyButtonActor.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	const FName DefaultPointerComponentName(TEXT("SelectionRayOrigin"));
	const FName DefaultCameraComponentName(TEXT("Camera"));
}

UMemoryRoomSelectionComponent::UMemoryRoomSelectionComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	TraceDistance = 2000.0f;
	bShowPointer = true;
	BeamWidth = 0.85f;
	HitIndicatorScale = FVector(0.05f);
	bShowHmdTargetX = true;
	HmdTargetXSize = 8.0f;
	HmdTargetXThickness = 1.0f;
	ExpandedDistance = 160.0f;
	ExpandedVerticalOffset = 0.0f;
	ExpandedRotationOffset = FRotator::ZeroRotator;
	GrabDistance = 16.0f;
	GrabVerticalOffset = 2.0f;
	// 1.0 sits the hand right at the photo's edge, so it reads as picking the print up by
	// the side rather than balancing it on the controller.
	GrabRightSideOffsetMultiplier = 1.0f;
	// Photos are authored so their +X face is the image. A controller's +X points where you
	// aim, so this turns the image back toward the player when the hand is held naturally;
	// adjust if your controller component uses different axes.
	GrabRotationOffset = FRotator(0.0f, 180.0f, 0.0f);
	GrabScaleMultiplier = 0.24f;
	MemoryRoomManager = nullptr;
	bDebugTrace = false;
	bDebugSelectionLog = false;
	bLoggedMissingPointer = false;
	ApplyDefaultComponentReferences();

	static ConstructorHelpers::FObjectFinder<UMaterialInterface> DefaultBeamMaterialFinder(TEXT("/Game/material/M_DefaultBeam.M_DefaultBeam"));
	if (DefaultBeamMaterialFinder.Succeeded())
	{
		DefaultBeamMaterial = DefaultBeamMaterialFinder.Object;
		HmdTargetXMaterial = DefaultBeamMaterialFinder.Object;
	}

	static ConstructorHelpers::FObjectFinder<UMaterialInterface> ValidTargetBeamMaterialFinder(TEXT("/Game/material/M_ValidTargetBeam.M_ValidTargetBeam"));
	if (ValidTargetBeamMaterialFinder.Succeeded())
	{
		ValidTargetBeamMaterial = ValidTargetBeamMaterialFinder.Object;
	}

	// A flat plane, not a sphere - it lies on the photo surface like a spark resting there.
	static ConstructorHelpers::FObjectFinder<UStaticMesh> HitIndicatorMeshFinder(TEXT("/Engine/BasicShapes/Plane.Plane"));
	if (HitIndicatorMeshFinder.Succeeded())
	{
		HitIndicatorMesh = HitIndicatorMeshFinder.Object;
	}

	static ConstructorHelpers::FObjectFinder<UStaticMesh> BeamMeshFinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	if (BeamMeshFinder.Succeeded())
	{
		DefaultBeamMesh = BeamMeshFinder.Object;
	}

	static ConstructorHelpers::FObjectFinder<UStaticMesh> XMeshFinder(TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (XMeshFinder.Succeeded())
	{
		DefaultXMesh = XMeshFinder.Object;
	}
}

void UMemoryRoomSelectionComponent::PostLoad()
{
	Super::PostLoad();
	ApplyDefaultComponentReferences();
}

void UMemoryRoomSelectionComponent::PostInitProperties()
{
	Super::PostInitProperties();
	ApplyDefaultComponentReferences();
}

void UMemoryRoomSelectionComponent::BeginPlay()
{
	Super::BeginPlay();
	ApplyDefaultComponentReferences();
	ApplyDefaultAssetReferences();
	ResolveCameraComponent();
	EnsurePointerComponents();
	EnsureLeftPointerComponents();
	EnsureHandVisuals();
}

void UMemoryRoomSelectionComponent::EnsureHandVisuals()
{
	AActor* Owner = GetOwner();
	if (!bEnsureHandVisuals || !Owner)
	{
		return;
	}

	TArray<UMotionControllerComponent*> Controllers;
	Owner->GetComponents<UMotionControllerComponent>(Controllers);
	if (Controllers.Num() == 0)
	{
		return;
	}

	USkeletalMesh* RightMesh = bUseHandMeshes ? LoadObject<USkeletalMesh>(nullptr, *HandMeshPath) : nullptr;
	USkeletalMesh* LeftMesh = bUseHandMeshes ? LoadObject<USkeletalMesh>(nullptr, *LeftHandMeshPath) : nullptr;
	UAnimSequence* RightGraspAnim = RightMesh ? LoadObject<UAnimSequence>(nullptr, *HandGraspAnimPath) : nullptr;
	UAnimSequence* LeftGraspAnim = LeftMesh ? LoadObject<UAnimSequence>(nullptr, *LeftHandGraspAnimPath) : nullptr;
	if (bUseHandMeshes && !RightMesh)
	{
		UE_LOG(LogMemoryRoom, Warning, TEXT("%s could not load the right hand mesh at %s; falling back to the controller model."), *GetNameSafe(this), *HandMeshPath);
	}
	if (bUseHandMeshes && bUseHandMeshOnLeft && !LeftMesh)
	{
		UE_LOG(LogMemoryRoom, Warning, TEXT("%s could not load the left hand mesh at %s; falling back to the controller model."), *GetNameSafe(this), *LeftHandMeshPath);
	}

	TArray<UXRDeviceVisualizationComponent*> Existing;
	Owner->GetComponents<UXRDeviceVisualizationComponent>(Existing);

	// Whatever the hand that already works is doing is the house style: same model source,
	// same custom mesh if it uses one, same material overrides. Copying it is what keeps the
	// two hands looking like a pair instead of two different props. Only used when the hand
	// meshes are unavailable and the controller models stay on.
	const UXRDeviceVisualizationComponent* Style = Existing.Num() > 0 ? Existing[0] : nullptr;

	for (UMotionControllerComponent* Controller : Controllers)
	{
		if (!Controller)
		{
			continue;
		}

		// Only the grip poses carry a hand. An aim pose sits at the same wrist and would
		// draw a second hand floating in front of the first one.
		const FName Source = Controller->GetTrackingMotionSource();
		const bool bIsLeft = Source == IMotionController::LeftHandSourceId || Source == FName(TEXT("LeftGrip"));
		const bool bIsRight = Source == IMotionController::RightHandSourceId || Source == FName(TEXT("RightGrip"));
		if (!bIsLeft && !bIsRight)
		{
			UE_LOG(LogMemoryRoom, Log, TEXT("%s skipped hand for %s (motion source %s is not a grip pose)."), *GetNameSafe(this), *Controller->GetName(), *Source.ToString());
			continue;
		}

		// The left hand can be switched off on its own - see bUseHandMeshOnLeft - in which case
		// it drops through to the stock controller model below.
		USkeletalMesh* HandMesh = bIsLeft ? LeftMesh : RightMesh;
		UAnimSequence* GraspAnim = bIsLeft ? LeftGraspAnim : RightGraspAnim;
		if (HandMesh && (!bIsLeft || bUseHandMeshOnLeft))
		{
			// The hand replaces the controller model rather than sitting on top of it.
			for (UXRDeviceVisualizationComponent* Visualization : Existing)
			{
				if (Visualization && Visualization->GetAttachParent() == Controller)
				{
					Visualization->SetIsVisualizationActive(false);
					Visualization->SetVisibility(false, true);
				}
			}

			const FName HandName = MakeUniqueObjectName(Owner, USkeletalMeshComponent::StaticClass(), *FString::Printf(TEXT("%sHand"), *Controller->GetName()));
			USkeletalMeshComponent* Hand = NewObject<USkeletalMeshComponent>(Owner, HandName);
			if (!Hand)
			{
				continue;
			}

			Hand->SetSkeletalMesh(HandMesh);
			Hand->SetupAttachment(Controller);
			Hand->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			Hand->SetCastShadow(false);
			Hand->SetRelativeLocation(bIsLeft ? LeftHandRelativeLocation : HandRelativeLocation);
			Hand->SetRelativeRotation(bIsLeft ? LeftHandRelativeRotation : RightHandRelativeRotation);
			Hand->RegisterComponent();

			if (GraspAnim)
			{
				// Not played, scrubbed: the clip is the curl from open to fist, and the curl
				// value below picks the frame. Looping is off and the rate is zero so nothing
				// advances on its own.
				Hand->PlayAnimation(GraspAnim, false);
				Hand->SetPlayRate(0.0f);
				Hand->SetPosition(0.0f, false);
			}

			if (bIsLeft)
			{
				LeftHandMeshComponent = Hand;
			}
			else
			{
				RightHandMeshComponent = Hand;
			}

			UE_LOG(LogMemoryRoom, Log, TEXT("%s built a hand mesh for %s (motion source %s, %s)."), *GetNameSafe(this), *Controller->GetName(), *Source.ToString(), bIsLeft ? TEXT("left mesh") : TEXT("right mesh"));
			continue;
		}

		UXRDeviceVisualizationComponent* Visualization = nullptr;
		for (UXRDeviceVisualizationComponent* Candidate : Existing)
		{
			if (Candidate && Candidate->GetAttachParent() == Controller)
			{
				Visualization = Candidate;
				break;
			}
		}

		if (!Visualization)
		{
			const FName VisualizationName = MakeUniqueObjectName(Owner, UXRDeviceVisualizationComponent::StaticClass(), *FString::Printf(TEXT("%sVisualization"), *Controller->GetName()));
			Visualization = NewObject<UXRDeviceVisualizationComponent>(Owner, VisualizationName);
			if (!Visualization)
			{
				continue;
			}

			if (Style)
			{
				Visualization->DisplayModelSource = Style->DisplayModelSource;
				Visualization->CustomDisplayMesh = Style->CustomDisplayMesh;
				Visualization->DisplayMeshMaterialOverrides = Style->DisplayMeshMaterialOverrides;
			}
			Visualization->SetupAttachment(Controller);
			// OnRegister reads MotionSource back off the parent controller, so the model that
			// loads is the one for this hand.
			Visualization->RegisterComponent();
		}

		// The left hand has no proper custom mesh yet - bUseHandMeshOnLeft is off pending real
		// calibration (see memoryroom-left-hand-unresolved in project memory) - so it falls back
		// to the bare stock controller model here, same as the right hand does when hand meshes
		// are off entirely. This used to be force-hidden, back when the left controller was
		// purely decorative (wrist/menu only); now that it is a real second pointer/grab hand
		// (see TickHandInteraction), it needs to be visible like any other controller so there
		// is something to see aiming with. Re-asserted every call, not just on first creation -
		// this controller's visibility was toggled several times earlier in development, and a
		// component instance still carrying an old hidden state should not survive into this.
		Visualization->bIsVisualizationActive = true;
		Visualization->SetVisibility(true, true);

		UE_LOG(LogMemoryRoom, Log, TEXT("%s built a device visualization for %s (motion source %s, model source %s)."), *GetNameSafe(this), *Controller->GetName(), *Source.ToString(), *Visualization->DisplayModelSource.ToString());
	}
}

void UMemoryRoomSelectionComponent::UpdateControllerCalibration(float DeltaTime)
{
	AActor* Owner = GetOwner();
	UWorld* World = GetWorld();
	if (!bShowControllerCalibration || !Owner || !World)
	{
		// Labels are built on demand, so switching the mode off just parks them out of sight.
		for (UTextRenderComponent* Label : CalibrationLabels)
		{
			if (Label)
			{
				Label->SetVisibility(false);
			}
		}
		return;
	}

	FVector ViewLocation = FVector::ZeroVector;
	FRotator ViewRotation = FRotator::ZeroRotator;
	if (APlayerController* PlayerController = World->GetFirstPlayerController())
	{
		PlayerController->GetPlayerViewPoint(ViewLocation, ViewRotation);
	}

	// Every motion controller on the pawn, not just the two that carry hands - an aim pose
	// has its own axes and it is worth being able to see how they differ from the grip.
	TArray<UMotionControllerComponent*> Controllers;
	Owner->GetComponents<UMotionControllerComponent>(Controllers);

	const float AxisLength = FMath::Max(CalibrationAxisLength, 1.0f);
	int32 LabelIndex = 0;

	for (UMotionControllerComponent* Controller : Controllers)
	{
		if (!Controller)
		{
			continue;
		}

		const FVector Origin = Controller->GetComponentLocation();

		// Unreal's own gizmo colours, so an arrow here means what the same arrow means in the
		// editor viewport: X red, Y green, Z blue. The lengths step down as well, so the axes
		// stay tellable apart when a colour is washed out by the sky.
		DrawDebugDirectionalArrow(World, Origin, Origin + Controller->GetForwardVector() * AxisLength, AxisLength * 0.28f, FColor::Red, false, -1.0f, 0, 0.4f);
		DrawDebugDirectionalArrow(World, Origin, Origin + Controller->GetRightVector() * AxisLength * 0.75f, AxisLength * 0.28f, FColor::Green, false, -1.0f, 0, 0.4f);
		DrawDebugDirectionalArrow(World, Origin, Origin + Controller->GetUpVector() * AxisLength * 0.55f, AxisLength * 0.28f, FColor::Blue, false, -1.0f, 0, 0.4f);

		// Build labels lazily, one per controller, and reuse them from then on.
		if (!CalibrationLabels.IsValidIndex(LabelIndex) || !CalibrationLabels[LabelIndex])
		{
			const FName LabelName = MakeUniqueObjectName(Owner, UTextRenderComponent::StaticClass(), *FString::Printf(TEXT("CalibrationLabel%d"), LabelIndex));
			UTextRenderComponent* NewLabel = NewObject<UTextRenderComponent>(Owner, LabelName);
			if (!NewLabel)
			{
				continue;
			}
			NewLabel->SetupAttachment(Owner->GetRootComponent());
			NewLabel->SetHorizontalAlignment(EHTA_Center);
			NewLabel->SetVerticalAlignment(EVRTA_TextCenter);
			NewLabel->SetWorldSize(1.4f);
			NewLabel->SetTextRenderColor(FColor(255, 240, 190));
			NewLabel->SetCastShadow(false);
			NewLabel->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			NewLabel->RegisterComponent();
			CalibrationLabels.SetNum(FMath::Max(CalibrationLabels.Num(), LabelIndex + 1));
			CalibrationLabels[LabelIndex] = NewLabel;
		}

		UTextRenderComponent* Label = CalibrationLabels[LabelIndex];
		++LabelIndex;

		// Names the axis, not what it does - "forward"/"up" are exactly the thing the gizmo is
		// for measuring, and this project has already found a controller where X is not
		// forward and Z is not up. Asserting a direction in the label would just relabel the
		// same confusion the arrows are meant to resolve.
		FString Text = FString::Printf(TEXT("%s\nX red   Y green   Z blue"), *Controller->GetTrackingMotionSource().ToString());

		// The reveal gate's own numbers, straight off the menu actor rather than recomputed,
		// so what this reads out is exactly what the gate is acting on.
		if (const AMemoryRoomMenuActor* Menu = ResolveMenuActorForCalibration())
		{
			if (Controller == Menu->GetAttachedWristComponent())
			{
				Text += TEXT("\n") + Menu->GetWristRevealDebugText();
			}
		}

		Label->SetText(FText::FromString(Text));
		Label->SetVisibility(true);
		// Parked just off the controller and turned to face the headset, so it stays readable
		// however the wrist is held - which is the whole point while rolling it about.
		Label->SetWorldLocation(Origin + Controller->GetUpVector() * AxisLength * 0.9f);
		// +X at the headset, same as the sky puck's own face-the-viewer code - no negation:
		// ToViewer.Rotation() already points +X at the viewer, and text render components
		// read forward along +X, so flipping it here was pointing the label away instead.
		const FVector ToViewer = ViewLocation - Label->GetComponentLocation();
		if (!ToViewer.IsNearlyZero())
		{
			Label->SetWorldRotation(ToViewer.Rotation());
		}
	}

	// Any labels left over from a controller that has gone away.
	for (int32 i = LabelIndex; i < CalibrationLabels.Num(); ++i)
	{
		if (CalibrationLabels[i])
		{
			CalibrationLabels[i]->SetVisibility(false);
		}
	}
}

AMemoryRoomMenuActor* UMemoryRoomSelectionComponent::ResolveMenuActorForCalibration() const
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return nullptr;
	}
	for (TActorIterator<AMemoryRoomMenuActor> It(World); It;)
	{
		return *It;
	}
	return nullptr;
}

void UMemoryRoomSelectionComponent::SetHandCurlInput(float GripValue, float TriggerValue)
{
	// Two inputs, one curl. The grip closes the hand the whole way; the trigger only pulls it
	// part-way, so a trigger press reads as reaching for something and a grip reads as having
	// hold of it. Whichever is asking for more curl wins.
	const float FromGrip = FMath::Clamp(GripValue, 0.0f, 1.0f);
	const float FromTrigger = FMath::Clamp(TriggerValue, 0.0f, 1.0f) * FMath::Clamp(TriggerCurlAmount, 0.0f, 1.0f);
	AnalogHandCurl = FMath::Max(FromGrip, FromTrigger);
}

void UMemoryRoomSelectionComponent::UpdateHandCurl(float DeltaTime)
{
	if (!LeftHandMeshComponent && !RightHandMeshComponent)
	{
		return;
	}

	// The trigger is bound as a press, not an axis, so it gets a short pulse that eases back
	// out - a tap of the finger. Wire SetHandCurlInput to the real axes and this is simply
	// overtaken by the analog value.
	TriggerPulseSeconds = FMath::Max(TriggerPulseSeconds - DeltaTime, 0.0f);
	const float FromTriggerPulse = TriggerPulseSeconds > 0.0f ? FMath::Clamp(TriggerCurlAmount, 0.0f, 1.0f) : 0.0f;

	// A held grab always wins: the fist has to be closed for as long as the photo is held,
	// however the trigger and the analog inputs happen to be sitting.
	TargetHandCurl = FMath::Max3(bGrabCurlHeld ? 1.0f : 0.0f, AnalogHandCurl, FromTriggerPulse);

	// Snapping straight to the target looks like the fingers teleport, so ease in over
	// HandCurlInterpSpeed. At the default this closes in about a tenth of a second - fast
	// enough that the fist still lands on the frame the grab fires.
	CurrentHandCurl = FMath::FInterpTo(CurrentHandCurl, TargetHandCurl, DeltaTime, FMath::Max(HandCurlInterpSpeed, 0.1f));

	for (USkeletalMeshComponent* Hand : { LeftHandMeshComponent.Get(), RightHandMeshComponent.Get() })
	{
		if (!Hand)
		{
			continue;
		}
		if (const UAnimationAsset* Asset = Hand->GetSingleNodeInstance() ? Hand->GetSingleNodeInstance()->GetAnimationAsset() : nullptr)
		{
			if (const UAnimSequence* Sequence = Cast<UAnimSequence>(Asset))
			{
				Hand->SetPosition(CurrentHandCurl * Sequence->GetPlayLength(), false);
			}
		}
	}
}

void UMemoryRoomSelectionComponent::ApplyDefaultComponentReferences()
{
	if (PointerComponent.PathToComponent.IsEmpty() && PointerComponent.ComponentProperty.IsNone() && !PointerComponent.OtherActor.IsValid())
	{
		PointerComponent.PathToComponent = DefaultPointerComponentName.ToString();
	}

	if (CameraComponent.PathToComponent.IsEmpty() && CameraComponent.ComponentProperty.IsNone() && !CameraComponent.OtherActor.IsValid())
	{
		CameraComponent.PathToComponent = DefaultCameraComponentName.ToString();
	}
}

void UMemoryRoomSelectionComponent::ApplyDefaultAssetReferences()
{
	if (!DefaultBeamMaterial)
	{
		DefaultBeamMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/material/M_DefaultBeam.M_DefaultBeam"));
	}
	if (!ValidTargetBeamMaterial)
	{
		ValidTargetBeamMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/material/M_ValidTargetBeam.M_ValidTargetBeam"));
	}
	if (!HitIndicatorMesh)
	{
		HitIndicatorMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	}
	if (!HmdTargetXMaterial)
	{
		HmdTargetXMaterial = DefaultBeamMaterial;
	}
}

void UMemoryRoomSelectionComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	ClosePanorama();
	if (AMemoryPhotoActor* Grabbed = GrabbedPhoto.Get())
	{
		Grabbed->Collapse();
	}
	UpdateHoveredPhoto(HoveredPhoto, nullptr, FVector::ZeroVector);
	UpdateHoveredPhoto(LeftHoveredPhoto, nullptr, FVector::ZeroVector);
	GrabbedPhoto.Reset();
	ExpandedPhoto.Reset();
	Super::EndPlay(EndPlayReason);
}

void UMemoryRoomSelectionComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	UpdateHandCurl(DeltaTime);
	UpdateControllerCalibration(DeltaTime);
	if (IsPanoramaOpen())
	{
		UpdateHoveredPhoto(HoveredPhoto, nullptr, FVector::ZeroVector);
		UpdateHoveredPhoto(LeftHoveredPhoto, nullptr, FVector::ZeroVector);
		UpdatePointerVisual(PointerSplineComponent, BeamSplineMeshComponent, HitIndicatorComponent, FVector::ZeroVector, FVector::ZeroVector, false, false, false, false);
		UpdatePointerVisual(LeftPointerSplineComponent, LeftBeamSplineMeshComponent, LeftHitIndicatorComponent, FVector::ZeroVector, FVector::ZeroVector, false, false, false, false);
		if (AMemoryRoomManager* Manager = ResolveMemoryRoomManager()) Manager->SetGalleryScrollSuppressed(true);
		return;
	}
	UpdateExpandedPhotoZoom(DeltaTime);
	SparkLandFlareElapsed += FMath::Max(DeltaTime, 0.0f);

	USceneComponent* RightPointer = ResolvePointerComponent();
	USceneComponent* LeftPointer = ResolveLeftPointerComponent();

	if (AMemoryPhotoActor* Grabbed = GrabbedPhoto.Get())
	{
		USceneComponent* PrimaryPointer = (PrimaryGrabHand == EMemoryRoomHand::Left) ? LeftPointer : RightPointer;
		USceneComponent* SecondaryPointer = (PrimaryGrabHand == EMemoryRoomHand::Left) ? RightPointer : LeftPointer;
		UpdatePinchZoom(DeltaTime, PrimaryPointer, bSecondaryGrabHandHeld ? SecondaryPointer : nullptr);

		// Both hands' beams stay hidden while anything is held, whichever hand(s) are involved.
		UpdateHoveredPhoto(HoveredPhoto, nullptr, FVector::ZeroVector);
		UpdateHoveredPhoto(LeftHoveredPhoto, nullptr, FVector::ZeroVector);
		UpdatePointerVisual(PointerSplineComponent, BeamSplineMeshComponent, HitIndicatorComponent, FVector::ZeroVector, FVector::ZeroVector, false, false, false, false);
		UpdatePointerVisual(LeftPointerSplineComponent, LeftBeamSplineMeshComponent, LeftHitIndicatorComponent, FVector::ZeroVector, FVector::ZeroVector, false, false, false, false);

		if (PrimaryPointer)
		{
			// Same IsPlacedInWorld() rule as the grab/re-grab entry points (GrabHoveredPhoto):
			// this runs every tick for as long as the hold lasts, so getting the base scale
			// right only at the moment of the grab and not here too would just have this one
			// silently overwrite it one frame later with the wall-grab shrink applied on top
			// of an already-placed size - which is exactly what was happening.
			const float HeldGrabScale = (Grabbed->IsPlacedInWorld() ? 1.0f : GrabScaleMultiplier) * ExpandedPhotoZoomMultiplier;
			Grabbed->UpdateExpandedTargetTransform(BuildGrabTransform(Grabbed, PrimaryPointer, PrimaryGrabHand), HeldGrabScale, true);
		}
		return;
	}

	TickHandInteraction(EMemoryRoomHand::Right, RightPointer, PointerSplineComponent, BeamSplineMeshComponent, HitIndicatorComponent, true, HoveredMenu, HoveredSkyButton, HoveredPhoto);
	TickHandInteraction(EMemoryRoomHand::Left, LeftPointer, LeftPointerSplineComponent, LeftBeamSplineMeshComponent, LeftHitIndicatorComponent, false, LeftHoveredMenu, LeftHoveredSkyButton, LeftHoveredPhoto);

	if (AMemoryRoomManager* Manager = ResolveMemoryRoomManager())
	{
		// Pointing at either piece of UI, with either hand, should not also spin the gallery.
		const bool bEitherHandOnUI = HoveredMenu.IsValid() || HoveredSkyButton.IsValid() || LeftHoveredMenu.IsValid() || LeftHoveredSkyButton.IsValid();
		Manager->SetGalleryScrollSuppressed(bEitherHandOnUI);
	}
}

AMemoryPhotoActor* UMemoryRoomSelectionComponent::TickHandInteraction(EMemoryRoomHand Hand, USceneComponent* Pointer, USplineComponent* Spline, USplineMeshComponent* Beam, UStaticMeshComponent* HitIndicator, bool bAllowHmdFallback, TWeakObjectPtr<AMemoryRoomMenuActor>& HoveredMenuRef, TWeakObjectPtr<AMemoryRoomSkyButtonActor>& HoveredSkyButtonRef, TWeakObjectPtr<AMemoryPhotoActor>& HoveredPhotoRef)
{
	if (!Pointer)
	{
		UpdateHoveredPhoto(HoveredPhotoRef, nullptr, FVector::ZeroVector);
		if (HoveredMenuRef.IsValid())
		{
			HoveredMenuRef->SetHoveredComponent(nullptr);
			HoveredMenuRef.Reset();
		}
		if (HoveredSkyButtonRef.IsValid())
		{
			HoveredSkyButtonRef->SetHovered(false);
			HoveredSkyButtonRef.Reset();
		}
		UpdatePointerVisual(Spline, Beam, HitIndicator, FVector::ZeroVector, FVector::ZeroVector, false, false, false, false);
		return nullptr;
	}

	const FVector Start = Pointer->GetComponentLocation();
	const FVector AimDirection = GetHandAimDirection(Hand, Pointer);
	const FVector TraceEnd = Start + AimDirection * TraceDistance;

	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(MemoryRoomPhotoSelectionTrace), false, GetOwner());
	QueryParams.bReturnPhysicalMaterial = false;

	FHitResult Hit;
	AMemoryPhotoActor* HitPhoto = nullptr;
	AMemoryRoomMenuActor* HitMenu = nullptr;
	AMemoryRoomSkyButtonActor* HitSkyButton = nullptr;
	if (UWorld* World = GetWorld())
	{
		World->LineTraceSingleByChannel(Hit, Start, TraceEnd, ECC_Visibility, QueryParams);
		HitMenu = Cast<AMemoryRoomMenuActor>(Hit.GetActor());
		if (HitMenu && !HitMenu->IsMenuComponent(Hit.GetComponent()))
		{
			HitMenu = nullptr;
		}
		HitSkyButton = HitMenu ? nullptr : Cast<AMemoryRoomSkyButtonActor>(Hit.GetActor());
		if (HitSkyButton && !HitSkyButton->IsButtonComponent(Hit.GetComponent()))
		{
			HitSkyButton = nullptr;
		}
		HitPhoto = (HitMenu || HitSkyButton) ? nullptr : Cast<AMemoryPhotoActor>(Hit.GetActor());

#if !UE_BUILD_SHIPPING
		if (bDebugTrace)
		{
			const bool bPhotoHit = HitPhoto != nullptr;
			const FColor TraceColor = bPhotoHit ? FColor::Green : FColor::Red;
			DrawDebugLine(World, Start, TraceEnd, TraceColor, false, 0.0f, 0, 1.0f);
			if (Hit.bBlockingHit)
			{
				DrawDebugSphere(World, Hit.ImpactPoint, bPhotoHit ? 8.0f : 5.0f, 12, bPhotoHit ? FColor::Cyan : FColor::Yellow, false, 0.0f);
			}
		}
#endif
	}

	const bool bBlockingHit = Hit.bBlockingHit;
	const bool bValidMenuHit = HitMenu != nullptr && bBlockingHit;
	const bool bValidSkyButtonHit = HitSkyButton != nullptr && bBlockingHit && !bValidMenuHit;
	const bool bValidPhotoHit = HitPhoto != nullptr && bBlockingHit && !bValidMenuHit && !bValidSkyButtonHit;
	const FVector PointerEnd = bBlockingHit ? Hit.ImpactPoint : TraceEnd;
	const bool bPointerIsCamera = bAllowHmdFallback && Pointer == ResolveCameraComponent();
	// Suppress the hit indicator on the sky button: the sphere is wider than the puck and
	// would engulf it, and the puck's own hover highlight already reads as feedback.
	const bool bShowHitIndicator = bBlockingHit && !bValidSkyButtonHit;
	// The menu board's press targets are 4 cm square - the photo-sized spark swallows one
	// whole, so it shrinks to MenuHitIndicatorRadius whenever the trace is on the board.
	const float IndicatorRadiusScale = bValidMenuHit ? FMath::Clamp(MenuHitIndicatorRadius / FMath::Max(TipSparkRadius, 0.1f), 0.05f, 1.0f) : 1.0f;
	UpdatePointerVisual(Spline, Beam, HitIndicator, Start, PointerEnd, bValidPhotoHit || bValidMenuHit || bValidSkyButtonHit, bShowHitIndicator, !bPointerIsCamera, bPointerIsCamera, IndicatorRadiusScale);

	if (HoveredMenuRef.IsValid() && HoveredMenuRef.Get() != HitMenu)
	{
		HoveredMenuRef->SetHoveredComponent(nullptr);
	}
	HoveredMenuRef = HitMenu;
	if (HoveredMenuRef.IsValid())
	{
		HoveredMenuRef->SetHoveredComponent(Hit.GetComponent());
	}

	if (HoveredSkyButtonRef.IsValid() && HoveredSkyButtonRef.Get() != HitSkyButton)
	{
		HoveredSkyButtonRef->SetHovered(false);
	}
	HoveredSkyButtonRef = bValidSkyButtonHit ? HitSkyButton : nullptr;
	if (HoveredSkyButtonRef.IsValid())
	{
		HoveredSkyButtonRef->SetHovered(true);
	}

	UpdateHoveredPhoto(HoveredPhotoRef, bValidPhotoHit ? HitPhoto : nullptr, Start);
	return HoveredPhotoRef.Get();
}

void UMemoryRoomSelectionComponent::UpdatePinchZoom(float DeltaTime, USceneComponent* PrimaryPointer, USceneComponent* SecondaryPointer)
{
	if (!PrimaryPointer || !SecondaryPointer)
	{
		return;
	}
	const float CurrentDistance = FVector::Dist(PrimaryPointer->GetComponentLocation(), SecondaryPointer->GetComponentLocation());
	const float StretchRatio = CurrentDistance / FMath::Max(PinchBaseDistance, 1.0f);
	ExpandedPhotoZoomMultiplier = FMath::Clamp(PinchBaseZoomMultiplier * StretchRatio, MinZoomMultiplier, MaxZoomMultiplier);
}

void UMemoryRoomSelectionComponent::ActivateHoveredPhoto(EMemoryRoomHand Hand)
{
	if (IsPanoramaOpen()) { ClosePanorama(); return; }
	// Before any of the early-outs below: the finger moves because the trigger was pulled,
	// not because the pull happened to land on something.
	TriggerPulseSeconds = FMath::Max(TriggerPulseDuration, 0.0f);

	// Pulling the trigger on the hand currently holding a photo sends it straight back to the
	// gallery instead of placing it on release - un-places it first if it was already a placed
	// copy you re-grabbed (AMemoryRoomManager::ReturnPlacedPhotoToGallery destroys that
	// standalone actor), otherwise this is exactly what letting go would already have done for
	// a fresh grab, just without having to also relax the grip.
	if (AMemoryPhotoActor* Grabbed = GrabbedPhoto.Get())
	{
		if (Hand == PrimaryGrabHand)
		{
			AMemoryRoomManager* Manager = ResolveMemoryRoomManager();
			if (Manager && Manager->IsPlacedPhotoActor(Grabbed))
			{
				Manager->ReturnPlacedPhotoToGallery(Grabbed->GetLoadedFilePath());
			}
			else
			{
				Grabbed->Collapse();
			}

			if (ExpandedPhoto.Get() == Grabbed)
			{
				ExpandedPhoto.Reset();
			}
			GrabbedPhoto.Reset();
			bSecondaryGrabHandHeld = false;
			bGrabCurlHeld = false;

			if (bDebugSelectionLog)
			{
				UE_LOG(LogMemoryRoom, Log, TEXT("MemoryRoom selection: %s hand sent held photo back to the gallery"), Hand == EMemoryRoomHand::Left ? TEXT("left") : TEXT("right"));
			}
		}
		return;
	}

	const bool bIsLeft = Hand == EMemoryRoomHand::Left;
	TWeakObjectPtr<AMemoryRoomMenuActor>& HoveredMenuRef = bIsLeft ? LeftHoveredMenu : HoveredMenu;
	TWeakObjectPtr<AMemoryRoomSkyButtonActor>& HoveredSkyButtonRef = bIsLeft ? LeftHoveredSkyButton : HoveredSkyButton;
	TWeakObjectPtr<AMemoryPhotoActor>& HoveredPhotoRef = bIsLeft ? LeftHoveredPhoto : HoveredPhoto;

	if (AMemoryRoomMenuActor* Menu = HoveredMenuRef.Get())
	{
		if (Menu->PressHoveredButton())
		{
			return;
		}
	}

	if (AMemoryRoomSkyButtonActor* SkyButton = HoveredSkyButtonRef.Get())
	{
		if (SkyButton->PressButton())
		{
			return;
		}
	}

	AMemoryPhotoActor* Hovered = HoveredPhotoRef.Get();
	if (!Hovered)
	{
		return;
	}

	if (bDebugSelectionLog)
	{
		UE_LOG(LogMemoryRoom, Log, TEXT("MemoryRoom selection: photo selected %s"), *GetNameSafe(Hovered));
	}

	if (Hovered->IsExpanded())
	{
		Hovered->Collapse();
		if (bDebugSelectionLog)
		{
			UE_LOG(LogMemoryRoom, Log, TEXT("MemoryRoom selection: photo collapsed %s"), *GetNameSafe(Hovered));
		}
		if (ExpandedPhoto.Get() == Hovered)
		{
			ExpandedPhoto.Reset();
		}
		return;
	}

	if (AMemoryPhotoActor* CurrentExpanded = ExpandedPhoto.Get())
	{
		if (CurrentExpanded != Hovered)
		{
			CurrentExpanded->Collapse();
			if (bDebugSelectionLog)
			{
				UE_LOG(LogMemoryRoom, Log, TEXT("MemoryRoom selection: photo collapsed %s"), *GetNameSafe(CurrentExpanded));
			}
		}
	}

	const FTransform ExpandTransform = BuildExpandedTransform(Hovered);
	Hovered->Expand(ExpandTransform);
	ExpandedPhoto = Hovered;
	// Same transform Expand() just used, cached for zoom to re-push against later without
	// recomputing it against the (by then possibly moved) live camera pose.
	ResetExpandedPhotoZoom(ExpandTransform);
	if (Hovered->IsPanoramaCandidate()) OpenPanorama(Hovered);
	Hovered->OnDestroyed.AddUniqueDynamic(this, &UMemoryRoomSelectionComponent::ClearDestroyedReferences);
	if (bDebugSelectionLog)
	{
		UE_LOG(LogMemoryRoom, Log, TEXT("MemoryRoom selection: photo expanded %s"), *GetNameSafe(Hovered));
	}
}

void UMemoryRoomSelectionComponent::GrabHoveredPhoto(EMemoryRoomHand Hand)
{
	if (IsPanoramaOpen()) { ClosePanorama(); return; }
	// Set before the early-outs, so the hand closes the instant the grab input fires whether
	// or not there was a photo under it to take. Shared across both hands since only the right
	// hand mesh is ever actually shown (bUseHandMeshOnLeft) - true while anything is held.
	bGrabCurlHeld = true;

	const bool bIsLeft = Hand == EMemoryRoomHand::Left;
	TWeakObjectPtr<AMemoryRoomMenuActor>& HoveredMenuRef = bIsLeft ? LeftHoveredMenu : HoveredMenu;
	TWeakObjectPtr<AMemoryRoomSkyButtonActor>& HoveredSkyButtonRef = bIsLeft ? LeftHoveredSkyButton : HoveredSkyButton;
	TWeakObjectPtr<AMemoryPhotoActor>& HoveredPhotoRef = bIsLeft ? LeftHoveredPhoto : HoveredPhoto;

	if (HoveredMenuRef.IsValid() || HoveredSkyButtonRef.IsValid())
	{
		return;
	}

	AMemoryPhotoActor* Hovered = HoveredPhotoRef.Get();
	USceneComponent* Pointer = bIsLeft ? ResolveLeftPointerComponent() : ResolvePointerComponent();

	if (AMemoryPhotoActor* CurrentGrabbed = GrabbedPhoto.Get())
	{
		if (Hand == PrimaryGrabHand && Hovered == CurrentGrabbed)
		{
			// Re-pressing grab on the same photo with the hand already holding it should
			// refresh its hand transform, not lose release state or reset whatever zoom is
			// already dialed in. Same IsPlacedInWorld() check as the fresh-grab case below -
			// this photo was already mid-hold when this fired, so whichever base scale that
			// hold started at stays in effect.
			const float RefreshGrabScale = (CurrentGrabbed->IsPlacedInWorld() ? 1.0f : GrabScaleMultiplier) * ExpandedPhotoZoomMultiplier;
			CurrentGrabbed->ExpandWithScale(BuildGrabTransform(CurrentGrabbed, Pointer, Hand), RefreshGrabScale);
			UpdateHoveredPhoto(HoveredPhotoRef, nullptr, FVector::ZeroVector);
			return;
		}

		// The other hand already holds a photo - squeezing this hand's grip joins a two-hand
		// pinch (see UpdatePinchZoom) instead of trying to grab a second, different photo, since
		// only one photo is ever held at a time. Not conditioned on this hand aiming at the held
		// photo first: it is already small and in the other hand, so requiring a precise second
		// aim on top of that would make a natural two-handed stretch fiddlier than it should be
		// - the mere fact that a photo is already held is enough intent.
		if (Hand != PrimaryGrabHand && !bSecondaryGrabHandHeld)
		{
			USceneComponent* PrimaryPointer = (PrimaryGrabHand == EMemoryRoomHand::Left) ? ResolveLeftPointerComponent() : ResolvePointerComponent();
			if (PrimaryPointer && Pointer)
			{
				bSecondaryGrabHandHeld = true;
				PinchBaseDistance = FMath::Max(FVector::Dist(PrimaryPointer->GetComponentLocation(), Pointer->GetComponentLocation()), 1.0f);
				PinchBaseZoomMultiplier = ExpandedPhotoZoomMultiplier;
				if (bDebugSelectionLog)
				{
					UE_LOG(LogMemoryRoom, Log, TEXT("MemoryRoom selection: second hand joined pinch on %s"), *GetNameSafe(CurrentGrabbed));
				}
			}
		}
		return;
	}

	if (!Hovered)
	{
		return;
	}

	if (AMemoryPhotoActor* CurrentExpanded = ExpandedPhoto.Get())
	{
		if (CurrentExpanded != Hovered)
		{
			CurrentExpanded->Collapse();
		}
	}

	// A grabbed photo's transform (position and scale both) is rebuilt every tick from the
	// live hand pose (see TickComponent), so unlike a plain expand there is no fixed base
	// transform for zoom to reuse - the FTransform passed here is never read once grabbed.
	ResetExpandedPhotoZoom(FTransform::Identity);
	// A fresh grab off the wall shrinks to the usual hand-held size; re-grabbing a photo that
	// is already placed keeps it at whatever size it is currently showing instead of shrinking
	// it again on top of that - GrabScaleMultiplier already applied once when it was first
	// taken off the wall, and this actor's own OriginalTransform now reflects its placed size,
	// not its wall size, so reapplying the same multiplier here would compound smaller every
	// place-then-re-grab cycle.
	const float GrabScale = Hovered->IsPlacedInWorld() ? 1.0f : GrabScaleMultiplier;
	Hovered->ExpandWithScale(BuildGrabTransform(Hovered, Pointer, Hand), GrabScale);
	GrabbedPhoto = Hovered;
	ExpandedPhoto = Hovered;
	PrimaryGrabHand = Hand;
	bSecondaryGrabHandHeld = false;
	if (AMemoryRoomManager* Manager = ResolveMemoryRoomManager())
	{
		Manager->NotifyPhotoGrabbed(Hovered->GetActorLocation());
	}
	UpdateHoveredPhoto(HoveredPhotoRef, nullptr, FVector::ZeroVector);
	Hovered->OnDestroyed.AddUniqueDynamic(this, &UMemoryRoomSelectionComponent::ClearDestroyedReferences);

	if (bDebugSelectionLog)
	{
		UE_LOG(LogMemoryRoom, Log, TEXT("MemoryRoom selection: photo grabbed to %s hand %s"), bIsLeft ? TEXT("left") : TEXT("right"), *GetNameSafe(Hovered));
	}
}

void UMemoryRoomSelectionComponent::ReleaseGrabbedPhoto(EMemoryRoomHand Hand)
{
	AMemoryPhotoActor* Grabbed = GrabbedPhoto.Get();
	if (!Grabbed)
	{
		bGrabCurlHeld = false;
		return;
	}

	if (Hand == PrimaryGrabHand)
	{
		if (bSecondaryGrabHandHeld)
		{
			// Hand off to the hand still gripping rather than dropping it. BuildGrabTransform
			// reads PrimaryGrabHand fresh every tick, so this alone is enough - the photo pops
			// to align with the new primary hand's pose next tick, which is expected when going
			// from two hands to one.
			PrimaryGrabHand = EMemoryRoomHand::Left == PrimaryGrabHand ? EMemoryRoomHand::Right : EMemoryRoomHand::Left;
			bSecondaryGrabHandHeld = false;
			// Whatever stretch it was at becomes the new resting size rather than snapping back.
			PinchBaseZoomMultiplier = ExpandedPhotoZoomMultiplier;

			if (bDebugSelectionLog)
			{
				UE_LOG(LogMemoryRoom, Log, TEXT("MemoryRoom selection: primary hand released, %s hand keeps holding %s"), PrimaryGrabHand == EMemoryRoomHand::Left ? TEXT("left") : TEXT("right"), *GetNameSafe(Grabbed));
			}
			return;
		}

		// A plain release places the photo you are holding wherever you let go - the same
		// actor keeps the job permanently (AMemoryRoomManager::PlacePhotoInWorld), leaving its
		// old gallery slot as an empty gap rather than duplicating it, so do NOT also
		// Grabbed->Collapse() it back to the wall afterward - that is exactly what
		// PlacePhotoInWorld's own internal Collapse() call already eases it away from doing.
		// Pulling the trigger while still holding sends it back without placing it instead -
		// see ActivateHoveredPhoto.
		USceneComponent* Pointer = (Hand == EMemoryRoomHand::Left) ? ResolveLeftPointerComponent() : ResolvePointerComponent();
		const FTransform PlaceTransform = BuildGrabTransform(Grabbed, Pointer, Hand);
		if (AMemoryRoomManager* Manager = ResolveMemoryRoomManager())
		{
			Manager->PlacePhotoInWorld(Grabbed, PlaceTransform);
		}
		else
		{
			// No manager to hand this off to - fall back to the pre-placement behaviour rather
			// than leaving the photo stuck at its held pose.
			Grabbed->Collapse();
		}

		if (ExpandedPhoto.Get() == Grabbed)
		{
			ExpandedPhoto.Reset();
		}
		GrabbedPhoto.Reset();
		bSecondaryGrabHandHeld = false;
		bGrabCurlHeld = false;

		if (bDebugSelectionLog)
		{
			UE_LOG(LogMemoryRoom, Log, TEXT("MemoryRoom selection: photo released and placed %s"), *GetNameSafe(Grabbed));
		}
		return;
	}

	if (bSecondaryGrabHandHeld)
	{
		// The pinch-only hand let go; the primary hand keeps holding normally.
		bSecondaryGrabHandHeld = false;
		if (bDebugSelectionLog)
		{
			UE_LOG(LogMemoryRoom, Log, TEXT("MemoryRoom selection: pinch hand released, still held by %s hand"), PrimaryGrabHand == EMemoryRoomHand::Left ? TEXT("left") : TEXT("right"));
		}
	}
}

void UMemoryRoomSelectionComponent::SelectHoveredPhoto()
{
	// Backward compatibility for older Blueprint/input bindings that used this single
	// action for both grip pressed and grip released. If a photo is already grabbed,
	// treat the next call as release; otherwise grab the current hover target.
	if (GrabbedPhoto.IsValid())
	{
		ReleaseGrabbedPhoto();
		return;
	}

	GrabHoveredPhoto();
}

void UMemoryRoomSelectionComponent::AddScrollInput(float Amount)
{
	if (RouteScrollInputToExpandedNav(Amount))
	{
		return;
	}
	if (AMemoryRoomManager* Manager = ResolveMemoryRoomManager())
	{
		Manager->AddScrollInput(Amount);
	}
}

void UMemoryRoomSelectionComponent::AddForwardScrollInput(float Amount)
{
	if (RouteScrollInputToExpandedNav(FMath::Abs(Amount)))
	{
		return;
	}
	if (AMemoryRoomManager* Manager = ResolveMemoryRoomManager())
	{
		Manager->AddForwardScrollInput(Amount);
	}
}

void UMemoryRoomSelectionComponent::AddBackwardScrollInput(float Amount)
{
	if (RouteScrollInputToExpandedNav(-FMath::Abs(Amount)))
	{
		return;
	}
	if (AMemoryRoomManager* Manager = ResolveMemoryRoomManager())
	{
		Manager->AddBackwardScrollInput(Amount);
	}
}

void UMemoryRoomSelectionComponent::StopScrollInput()
{
	bExpandedNavArmed = true;
	if (AMemoryRoomManager* Manager = ResolveMemoryRoomManager())
	{
		Manager->StopScrollInput();
	}
}

void UMemoryRoomSelectionComponent::SetLocomotionMoveInput(FVector2D Value)
{
	if (AMemoryRoomManager* Manager = ResolveMemoryRoomManager())
	{
		Manager->SetLocomotionMoveInput(Value);
	}
}

void UMemoryRoomSelectionComponent::StopLocomotionMoveInput()
{
	if (AMemoryRoomManager* Manager = ResolveMemoryRoomManager())
	{
		Manager->StopLocomotionMoveInput();
	}
}

void UMemoryRoomSelectionComponent::SetTurnInput(float Value)
{
	if (AMemoryRoomManager* Manager = ResolveMemoryRoomManager())
	{
		Manager->SetTurnInput(Value);
	}
}

void UMemoryRoomSelectionComponent::StopTurnInput()
{
	if (AMemoryRoomManager* Manager = ResolveMemoryRoomManager())
	{
		Manager->StopTurnInput();
	}
}

void UMemoryRoomSelectionComponent::ToggleFloat()
{
	if (AMemoryRoomManager* Manager = ResolveMemoryRoomManager())
	{
		Manager->ToggleFloat();
	}
}

bool UMemoryRoomSelectionComponent::RouteScrollInputToExpandedNav(float SignedAmount)
{
	if (IsPanoramaOpen()) return true;
	AMemoryPhotoActor* NavTarget = GrabbedPhoto.IsValid() ? GrabbedPhoto.Get() : ExpandedPhoto.Get();
	if (!NavTarget)
	{
		return false;
	}
	UpdateExpandedPhotoNavAxis(SignedAmount, NavTarget);
	return true;
}

void UMemoryRoomSelectionComponent::UpdateExpandedPhotoNavAxis(float SignedAmount, AMemoryPhotoActor* NavTarget)
{
	// Same dead zone the manager applies to gallery scrolling, so a resting thumbstick reads
	// as neutral here too.
	const float ReleaseThreshold = 0.25f;
	const float FireThreshold = 0.6f;

	if (FMath::Abs(SignedAmount) < ReleaseThreshold)
	{
		bExpandedNavArmed = true;
		return;
	}

	if (!bExpandedNavArmed || FMath::Abs(SignedAmount) < FireThreshold)
	{
		return;
	}

	bExpandedNavArmed = false;
	AdvanceExpandedPhoto(NavTarget, SignedAmount > 0.0f ? 1 : -1);
}

void UMemoryRoomSelectionComponent::AddZoomInput(float Amount)
{
	if (IsPanoramaOpen()) return;
	const float DeadZone = 0.2f;
	PendingZoomAxis = FMath::Abs(Amount) > DeadZone ? Amount : 0.0f;
}

void UMemoryRoomSelectionComponent::ResetExpandedPhotoZoom(const FTransform& BaseTransform)
{
	ExpandedPhotoZoomMultiplier = 1.0f;
	CachedExpandedBaseTransform = BaseTransform;
}

void UMemoryRoomSelectionComponent::UpdateExpandedPhotoZoom(float DeltaTime)
{
	AMemoryPhotoActor* Target = GrabbedPhoto.IsValid() ? GrabbedPhoto.Get() : ExpandedPhoto.Get();
	if (!Target)
	{
		// Nothing to zoom - do not let a held-down stick silently build up a multiplier that
		// would suddenly apply to whatever photo is opened next.
		PendingZoomAxis = 0.0f;
		ExpandedPhotoZoomMultiplier = 1.0f;
		return;
	}

	if (FMath::IsNearlyZero(PendingZoomAxis))
	{
		return;
	}

	const float GrowthRate = FMath::Exp(PendingZoomAxis * ZoomSpeed * DeltaTime);
	ExpandedPhotoZoomMultiplier = FMath::Clamp(ExpandedPhotoZoomMultiplier * GrowthRate, MinZoomMultiplier, MaxZoomMultiplier);

	if (!GrabbedPhoto.IsValid())
	{
		// Grabbed photos get a full transform re-derived from the hand every tick already (see
		// TickComponent), which now reads ExpandedPhotoZoomMultiplier itself - nothing more to
		// do here. A plain expanded photo only gets placed once by Expand(), so re-push it here
		// with the cached placement and the new scale.
		Target->UpdateExpandedTargetTransform(CachedExpandedBaseTransform, Target->GetExpandedScale() * ExpandedPhotoZoomMultiplier, true);
	}
}

void UMemoryRoomSelectionComponent::AdvanceExpandedPhoto(AMemoryPhotoActor* NavTarget, int32 Direction)
{
	AMemoryRoomManager* Manager = ResolveMemoryRoomManager();
	if (!NavTarget || !Manager)
	{
		return;
	}

	const int32 Count = Manager->GetPhotoCount();
	const int32 CurrentIndex = NavTarget->GetAssignedPhotoIndex();
	if (Count <= 0 || CurrentIndex == INDEX_NONE)
	{
		return;
	}

	int32 NewIndex = (CurrentIndex + Direction) % Count;
	if (NewIndex < 0)
	{
		NewIndex += Count;
	}

	NavTarget->AssignPhotoIndex(NewIndex);

	if (bDebugSelectionLog)
	{
		UE_LOG(LogMemoryRoom, Log, TEXT("MemoryRoom selection: expanded photo navigated %s to index %d"), Direction > 0 ? TEXT("forward") : TEXT("backward"), NewIndex);
	}
}

USceneComponent* UMemoryRoomSelectionComponent::ResolvePointerComponent()
{
	if (CachedPointerComponent.IsValid())
	{
		if (CachedPointerComponent->IsA<UCameraComponent>())
		{
			if (USceneComponent* ControllerPointer = ResolveControllerPointerComponent())
			{
				CachedPointerComponent = ControllerPointer;
				return CachedPointerComponent.Get();
			}
		}
		return CachedPointerComponent.Get();
	}

	AActor* Owner = GetOwner();
	UActorComponent* Component = PointerComponent.GetComponent(Owner);
	CachedPointerComponent = Cast<USceneComponent>(Component);
	if (!CachedPointerComponent.IsValid())
	{
		CachedPointerComponent = ResolveComponentByName(DefaultPointerComponentName);
	}
	if (!CachedPointerComponent.IsValid())
	{
		CachedPointerComponent = ResolveComponentByName(TEXT("RightController"));
	}

	if (!CachedPointerComponent.IsValid())
	{
		CachedPointerComponent = ResolveControllerPointerComponent();
		if (CachedPointerComponent.IsValid())
		{
			if (!bLoggedMissingPointer)
			{
				UE_LOG(LogMemoryRoom, Log, TEXT("%s has no PointerComponent assigned; using %s as the default controller pointer source."), *GetNameSafe(this), *GetNameSafe(CachedPointerComponent.Get()));
				bLoggedMissingPointer = true;
			}
			return CachedPointerComponent.Get();
		}
	}

	if (!CachedPointerComponent.IsValid())
	{
		CachedPointerComponent = ResolveCameraComponent();
		if (CachedPointerComponent.IsValid())
		{
			if (!bLoggedMissingPointer)
			{
				UE_LOG(LogMemoryRoom, Log, TEXT("%s has no PointerComponent assigned and no controller component was found; using the camera with an HMD target X instead of a beam."), *GetNameSafe(this));
				bLoggedMissingPointer = true;
			}
			return CachedPointerComponent.Get();
		}
	}

	if (!CachedPointerComponent.IsValid() && !bLoggedMissingPointer)
	{
		UE_LOG(LogMemoryRoom, Warning, TEXT("%s has no valid PointerComponent or CameraComponent assigned; photo selection tracing is disabled."), *GetNameSafe(this));
		bLoggedMissingPointer = true;
	}

	return CachedPointerComponent.Get();
}

USceneComponent* UMemoryRoomSelectionComponent::ResolveComponentByName(FName ComponentName) const
{
	AActor* Owner = GetOwner();
	if (!Owner || ComponentName.IsNone())
	{
		return nullptr;
	}

	TArray<USceneComponent*> SceneComponents;
	Owner->GetComponents<USceneComponent>(SceneComponents);
	const FString RequestedName = ComponentName.ToString();
	for (USceneComponent* SceneComponent : SceneComponents)
	{
		if (!SceneComponent)
		{
			continue;
		}

		const FString ActualName = SceneComponent->GetName();
		if (SceneComponent->GetFName() == ComponentName || ActualName.Equals(RequestedName, ESearchCase::IgnoreCase) || ActualName.Contains(RequestedName, ESearchCase::IgnoreCase))
		{
			return SceneComponent;
		}
	}

	return nullptr;
}

USceneComponent* UMemoryRoomSelectionComponent::ResolveControllerPointerComponent() const
{
	AActor* Owner = GetOwner();
	if (!Owner)
	{
		return nullptr;
	}

	TArray<USceneComponent*> SceneComponents;
	Owner->GetComponents<USceneComponent>(SceneComponents);

	USceneComponent* FirstControllerComponent = nullptr;
	for (USceneComponent* SceneComponent : SceneComponents)
	{
		if (!SceneComponent || SceneComponent->IsA<UCameraComponent>())
		{
			continue;
		}

		const FString ComponentName = SceneComponent->GetName();
		const bool bLooksLikeController = ComponentName.Contains(TEXT("Controller"), ESearchCase::IgnoreCase) || ComponentName.Contains(TEXT("Motion"), ESearchCase::IgnoreCase);
		if (!bLooksLikeController)
		{
			continue;
		}

		if (ComponentName.Contains(TEXT("Right"), ESearchCase::IgnoreCase))
		{
			return SceneComponent;
		}

		if (!FirstControllerComponent)
		{
			FirstControllerComponent = SceneComponent;
		}
	}

	return FirstControllerComponent;
}

USceneComponent* UMemoryRoomSelectionComponent::ResolveLeftPointerComponent()
{
	if (CachedLeftPointerComponent.IsValid())
	{
		return CachedLeftPointerComponent.Get();
	}

	AActor* Owner = GetOwner();
	if (!Owner)
	{
		return nullptr;
	}

	// Same motion-source check EnsureHandVisuals already uses to tell the hands apart - more
	// reliable than name-matching, since it is what the controller actually reports itself as.
	TArray<UMotionControllerComponent*> Controllers;
	Owner->GetComponents<UMotionControllerComponent>(Controllers);
	for (UMotionControllerComponent* Controller : Controllers)
	{
		if (!Controller)
		{
			continue;
		}
		const FName Source = Controller->GetTrackingMotionSource();
		if (Source == IMotionController::LeftHandSourceId || Source == FName(TEXT("LeftGrip")))
		{
			CachedLeftPointerComponent = Controller;
			return Controller;
		}
	}

	if (!bLoggedMissingLeftPointer)
	{
		UE_LOG(LogMemoryRoom, Warning, TEXT("%s could not find a left motion controller component; the left hand will have no pointer."), *GetNameSafe(this));
		bLoggedMissingLeftPointer = true;
	}
	return nullptr;
}

UCameraComponent* UMemoryRoomSelectionComponent::ResolveCameraComponent()
{
	if (CachedCameraComponent.IsValid())
	{
		return CachedCameraComponent.Get();
	}

	AActor* Owner = GetOwner();
	CachedCameraComponent = Cast<UCameraComponent>(CameraComponent.GetComponent(Owner));
	if (!CachedCameraComponent.IsValid())
	{
		CachedCameraComponent = Cast<UCameraComponent>(ResolveComponentByName(DefaultCameraComponentName));
	}
	if (!CachedCameraComponent.IsValid() && Owner)
	{
		CachedCameraComponent = Owner->FindComponentByClass<UCameraComponent>();
	}

	return CachedCameraComponent.Get();
}

AMemoryRoomManager* UMemoryRoomSelectionComponent::ResolveMemoryRoomManager()
{
	AMemoryRoomManager* Found = nullptr;

	if (MemoryRoomManager)
	{
		CachedMemoryRoomManager = MemoryRoomManager;
		Found = MemoryRoomManager;
	}
	else if (CachedMemoryRoomManager.IsValid())
	{
		Found = CachedMemoryRoomManager.Get();
	}
	else if (UWorld* World = GetWorld())
	{
		for (TActorIterator<AMemoryRoomManager> It(World); It;)
		{
			CachedMemoryRoomManager = *It;
			Found = CachedMemoryRoomManager.Get();
			break;
		}
	}

	// One-time binding, the first time a manager actually turns up - see
	// HandlePlacedPhotoUnplaced.
	if (Found && !bBoundPlacedPhotoDelegate)
	{
		Found->OnPlacedPhotoUnplaced.AddDynamic(this, &UMemoryRoomSelectionComponent::HandlePlacedPhotoUnplaced);
		bBoundPlacedPhotoDelegate = true;
	}

	return Found;
}

void UMemoryRoomSelectionComponent::EnsurePointerComponents()
{
	AActor* Owner = GetOwner();
	if (!Owner || PointerSplineComponent)
	{
		return;
	}

	PointerSplineComponent = NewObject<USplineComponent>(Owner, TEXT("MemoryRoomPointerSpline"));
	if (PointerSplineComponent)
	{
		PointerSplineComponent->SetMobility(EComponentMobility::Movable);
		PointerSplineComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		PointerSplineComponent->RegisterComponent();
		PointerSplineComponent->SetHiddenInGame(true);
	}

	BeamSplineMeshComponent = NewObject<USplineMeshComponent>(Owner, TEXT("MemoryRoomPointerBeam"));
	if (BeamSplineMeshComponent)
	{
		BeamSplineMeshComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		BeamSplineMeshComponent->SetGenerateOverlapEvents(false);
		BeamSplineMeshComponent->SetMobility(EComponentMobility::Movable);
		BeamSplineMeshComponent->SetStaticMesh(DefaultBeamMesh);
		BeamSplineMeshComponent->SetForwardAxis(ESplineMeshAxis::Z);
		BeamSplineMeshComponent->SetCastShadow(false);
		BeamSplineMeshComponent->bCastDynamicShadow = false;
		const float InitialRadius = FMath::Max(BeamStartWidth, 0.05f) / 100.0f;
		BeamSplineMeshComponent->SetStartScale(FVector2D(InitialRadius, InitialRadius));
		BeamSplineMeshComponent->SetEndScale(FVector2D(FMath::Max(BeamEndWidth, 0.0f) / 100.0f, FMath::Max(BeamEndWidth, 0.0f) / 100.0f));
		if (DefaultBeamMaterial)
		{
			BeamSplineMeshComponent->SetMaterial(0, DefaultBeamMaterial);
		}
		BeamSplineMeshComponent->RegisterComponent();
		BeamSplineMeshComponent->SetHiddenInGame(true);
	}

	HitIndicatorComponent = NewObject<UStaticMeshComponent>(Owner, TEXT("MemoryRoomPointerHitIndicator"));
	if (HitIndicatorComponent)
	{
		HitIndicatorComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		HitIndicatorComponent->SetGenerateOverlapEvents(false);
		HitIndicatorComponent->SetMobility(EComponentMobility::Movable);
		HitIndicatorComponent->SetStaticMesh(HitIndicatorMesh);
		HitIndicatorComponent->SetCastShadow(false);
		HitIndicatorComponent->bCastDynamicShadow = false;
		if (UMaterialInterface* SparkMaterial = ValidTargetBeamMaterial ? ValidTargetBeamMaterial : DefaultBeamMaterial)
		{
			HitIndicatorComponent->SetMaterial(0, SparkMaterial);
		}
		HitIndicatorComponent->RegisterComponent();
		HitIndicatorComponent->SetHiddenInGame(true);
	}

	HmdTargetXComponentA = NewObject<UStaticMeshComponent>(Owner, TEXT("MemoryRoomHmdTargetXA"));
	HmdTargetXComponentB = NewObject<UStaticMeshComponent>(Owner, TEXT("MemoryRoomHmdTargetXB"));
	UStaticMeshComponent* XComponents[] = { HmdTargetXComponentA, HmdTargetXComponentB };
	for (UStaticMeshComponent* XComponent : XComponents)
	{
		if (XComponent)
		{
			XComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			XComponent->SetGenerateOverlapEvents(false);
			XComponent->SetMobility(EComponentMobility::Movable);
			XComponent->SetStaticMesh(DefaultXMesh);
			UMaterialInterface* XMaterial = HmdTargetXMaterial ? HmdTargetXMaterial : DefaultBeamMaterial;
			if (XMaterial)
			{
				XComponent->SetMaterial(0, XMaterial);
			}
			XComponent->RegisterComponent();
			XComponent->SetHiddenInGame(true);
		}
	}
}

void UMemoryRoomSelectionComponent::EnsureLeftPointerComponents()
{
	AActor* Owner = GetOwner();
	if (!Owner || LeftPointerSplineComponent)
	{
		return;
	}

	LeftPointerSplineComponent = NewObject<USplineComponent>(Owner, TEXT("MemoryRoomLeftPointerSpline"));
	if (LeftPointerSplineComponent)
	{
		LeftPointerSplineComponent->SetMobility(EComponentMobility::Movable);
		LeftPointerSplineComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		LeftPointerSplineComponent->RegisterComponent();
		LeftPointerSplineComponent->SetHiddenInGame(true);
	}

	LeftBeamSplineMeshComponent = NewObject<USplineMeshComponent>(Owner, TEXT("MemoryRoomLeftPointerBeam"));
	if (LeftBeamSplineMeshComponent)
	{
		LeftBeamSplineMeshComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		LeftBeamSplineMeshComponent->SetGenerateOverlapEvents(false);
		LeftBeamSplineMeshComponent->SetMobility(EComponentMobility::Movable);
		LeftBeamSplineMeshComponent->SetStaticMesh(DefaultBeamMesh);
		LeftBeamSplineMeshComponent->SetForwardAxis(ESplineMeshAxis::Z);
		LeftBeamSplineMeshComponent->SetCastShadow(false);
		LeftBeamSplineMeshComponent->bCastDynamicShadow = false;
		const float InitialRadius = FMath::Max(BeamStartWidth, 0.05f) / 100.0f;
		LeftBeamSplineMeshComponent->SetStartScale(FVector2D(InitialRadius, InitialRadius));
		LeftBeamSplineMeshComponent->SetEndScale(FVector2D(FMath::Max(BeamEndWidth, 0.0f) / 100.0f, FMath::Max(BeamEndWidth, 0.0f) / 100.0f));
		if (DefaultBeamMaterial)
		{
			LeftBeamSplineMeshComponent->SetMaterial(0, DefaultBeamMaterial);
		}
		LeftBeamSplineMeshComponent->RegisterComponent();
		LeftBeamSplineMeshComponent->SetHiddenInGame(true);
	}

	LeftHitIndicatorComponent = NewObject<UStaticMeshComponent>(Owner, TEXT("MemoryRoomLeftPointerHitIndicator"));
	if (LeftHitIndicatorComponent)
	{
		LeftHitIndicatorComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		LeftHitIndicatorComponent->SetGenerateOverlapEvents(false);
		LeftHitIndicatorComponent->SetMobility(EComponentMobility::Movable);
		LeftHitIndicatorComponent->SetStaticMesh(HitIndicatorMesh);
		LeftHitIndicatorComponent->SetCastShadow(false);
		LeftHitIndicatorComponent->bCastDynamicShadow = false;
		if (UMaterialInterface* SparkMaterial = ValidTargetBeamMaterial ? ValidTargetBeamMaterial : DefaultBeamMaterial)
		{
			LeftHitIndicatorComponent->SetMaterial(0, SparkMaterial);
		}
		LeftHitIndicatorComponent->RegisterComponent();
		LeftHitIndicatorComponent->SetHiddenInGame(true);
	}
}

void UMemoryRoomSelectionComponent::UpdatePointerVisual(USplineComponent* Spline, USplineMeshComponent* Beam, UStaticMeshComponent* HitIndicator, const FVector& Start, const FVector& End, bool bValidTarget, bool bShowHitIndicator, bool bUseBeamVisual, bool bUseHmdTargetX, float IndicatorRadiusScale)
{
	const float Time = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0f;

	if (Spline && Beam)
	{
		if (Beam->GetStaticMesh() != DefaultBeamMesh)
		{
			Beam->SetStaticMesh(DefaultBeamMesh);
		}

		const bool bHasBeamMesh = Beam->GetStaticMesh() != nullptr;
		const FVector BeamVector = End - Start;
		const float BeamLength = BeamVector.Size();
		const bool bVisible = bShowPointer && bUseBeamVisual && bHasBeamMesh && BeamLength > KINDA_SMALL_NUMBER;
		Spline->SetHiddenInGame(!bVisible);
		Beam->SetHiddenInGame(!bVisible);
		if (bVisible)
		{
			const FVector LocalEnd = BeamVector;
			const FVector Direction = LocalEnd.GetSafeNormal();

			// A thread with a little weight: the ends angle so the hermite curve of the
			// spline mesh dips below the straight line by roughly BeamSagFraction of its
			// length. Kept small on short casts so it never droops onto the floor.
			const float Sag = BeamLength * FMath::Clamp(BeamSagFraction, 0.0f, 0.4f);
			const FVector StraightTangent = Direction * BeamLength;
			const FVector StartTangent = StraightTangent - FVector(0.0f, 0.0f, Sag * 4.0f);
			const FVector EndTangent = StraightTangent + FVector(0.0f, 0.0f, Sag * 4.0f);

			Spline->SetWorldLocation(Start);
			Spline->ClearSplinePoints(false);
			Spline->AddSplinePoint(FVector::ZeroVector, ESplineCoordinateSpace::Local, false);
			Spline->AddSplinePoint(LocalEnd, ESplineCoordinateSpace::Local, true);

			Beam->SetWorldLocation(Start);
			Beam->SetStartAndEnd(FVector::ZeroVector, StartTangent, LocalEnd, EndTangent, true);

			// Thin at the hand, tapering almost to nothing at the tip; a slow shimmer keeps
			// it from reading as a solid rod.
			const float Shimmer = 1.0f + FMath::Clamp(BeamShimmerAmount, 0.0f, 0.5f) * FMath::Sin(Time * 6.0f);
			const float StartRadius = FMath::Max(BeamStartWidth, 0.05f) * Shimmer / 100.0f;
			const float EndRadius = FMath::Max(BeamEndWidth, 0.0f) / 100.0f;
			Beam->SetStartScale(FVector2D(StartRadius, StartRadius));
			Beam->SetEndScale(FVector2D(EndRadius, EndRadius));

			UMaterialInterface* BeamMaterial = bValidTarget && ValidTargetBeamMaterial ? ValidTargetBeamMaterial : DefaultBeamMaterial;
			if (BeamMaterial)
			{
				Beam->SetMaterial(0, BeamMaterial);
			}
		}
	}

	if (HitIndicator)
	{
		if (HitIndicator->GetStaticMesh() != HitIndicatorMesh)
		{
			HitIndicator->SetStaticMesh(HitIndicatorMesh);
		}
		const bool bIndicatorVisible = bShowPointer && bUseBeamVisual && bShowHitIndicator && HitIndicatorMesh != nullptr;
		HitIndicator->SetHiddenInGame(!bIndicatorVisible);
		if (bIndicatorVisible)
		{
			// A flat spark resting where the thread lands. On a photo it swells and turns
			// slowly; off a target it is just a faint aiming dot.
			const float BaseRadius = (bValidTarget ? FMath::Max(TipSparkRadius, 0.1f) : FMath::Max(TipIdleRadius, 0.0f)) * FMath::Max(IndicatorRadiusScale, 0.05f);
			float Pulse = bValidTarget ? (1.0f + 0.18f * FMath::Sin(Time * 3.2f)) : 1.0f;

			// Landing flare: a quick swell that eases back over TipLandFlareSeconds.
			const float FlareDuration = FMath::Max(TipLandFlareSeconds, 0.05f);
			if (bValidTarget && SparkLandFlareElapsed < FlareDuration)
			{
				const float FlareAlpha = 1.0f - SparkLandFlareElapsed / FlareDuration;
				Pulse += FMath::Max(TipLandFlareStrength, 0.0f) * FlareAlpha * FlareAlpha;
			}
			const float PlaneHalfExtent = 50.0f; // the engine plane is 100x100 uu
			const float PlaneScale = FMath::Max(BaseRadius * Pulse / PlaneHalfExtent, 0.001f);

			// Face the controller so it reads as lying flat against whatever it is on.
			const FVector FaceDirection = (Start - End).GetSafeNormal();
			const float SpinDegrees = bValidTarget ? Time * 24.0f : 0.0f;
			const FRotator FaceRotation = FaceDirection.IsNearlyZero()
				? FRotator::ZeroRotator
				: (FRotationMatrix::MakeFromZ(FaceDirection).Rotator() + FRotator(0.0f, 0.0f, SpinDegrees));

			HitIndicator->SetWorldLocation(End + FaceDirection * 0.3f);
			HitIndicator->SetWorldRotation(FaceRotation);
			HitIndicator->SetWorldScale3D(FVector(PlaneScale, PlaneScale, PlaneScale));

			UMaterialInterface* SparkMaterial = bValidTarget && ValidTargetBeamMaterial ? ValidTargetBeamMaterial : DefaultBeamMaterial;
			if (SparkMaterial && HitIndicator->GetMaterial(0) != SparkMaterial)
			{
				HitIndicator->SetMaterial(0, SparkMaterial);
			}
		}
	}

	// The HmdTargetX reticle is a single shared visual (there is only ever one "no controller
	// found" fallback pointer, never a per-hand pair of them), so only the primary/right call
	// site is allowed to touch it - see bUseHmdTargetX's callers.
	if (bUseHmdTargetX || Spline == PointerSplineComponent)
	{
		UpdateHmdTargetXVisual(Start, End, bShowPointer && bUseHmdTargetX && bShowHmdTargetX);
	}
}

void UMemoryRoomSelectionComponent::UpdateHmdTargetXVisual(const FVector& Start, const FVector& End, bool bVisible)
{
	const bool bHasXMesh = DefaultXMesh != nullptr && HmdTargetXComponentA && HmdTargetXComponentB;
	if (!bVisible || !bHasXMesh)
	{
		HideHmdTargetXVisual();
		return;
	}

	const FVector ToTarget = End - Start;
	if (ToTarget.IsNearlyZero())
	{
		HideHmdTargetXVisual();
		return;
	}

	FVector Normal = -ToTarget.GetSafeNormal();
	FVector Right = FVector::CrossProduct(FVector::UpVector, Normal).GetSafeNormal();
	if (Right.IsNearlyZero())
	{
		Right = FVector::RightVector;
	}
	const FVector Up = FVector::CrossProduct(Normal, Right).GetSafeNormal();
	const FVector DiagonalA = (Right + Up).GetSafeNormal();
	const FVector DiagonalB = (Right - Up).GetSafeNormal();
	const FVector XScale(FMath::Max(HmdTargetXSize, 0.1f) / 100.0f, FMath::Max(HmdTargetXThickness, 0.1f) / 100.0f, FMath::Max(HmdTargetXThickness, 0.1f) / 100.0f);
	UMaterialInterface* XMaterial = HmdTargetXMaterial ? HmdTargetXMaterial : DefaultBeamMaterial;

	UStaticMeshComponent* XComponents[] = { HmdTargetXComponentA, HmdTargetXComponentB };
	const FVector Diagonals[] = { DiagonalA, DiagonalB };
	for (int32 Index = 0; Index < 2; ++Index)
	{
		UStaticMeshComponent* XComponent = XComponents[Index];
		if (!XComponent)
		{
			continue;
		}
		if (XComponent->GetStaticMesh() != DefaultXMesh)
		{
			XComponent->SetStaticMesh(DefaultXMesh);
		}
		if (XMaterial)
		{
			XComponent->SetMaterial(0, XMaterial);
		}
		XComponent->SetHiddenInGame(false);
		XComponent->SetWorldLocation(End);
		XComponent->SetWorldRotation(FRotationMatrix::MakeFromXZ(Diagonals[Index], Normal).Rotator());
		XComponent->SetWorldScale3D(XScale);
	}
}

void UMemoryRoomSelectionComponent::HideHmdTargetXVisual()
{
	if (HmdTargetXComponentA)
	{
		HmdTargetXComponentA->SetHiddenInGame(true);
	}
	if (HmdTargetXComponentB)
	{
		HmdTargetXComponentB->SetHiddenInGame(true);
	}
}

void UMemoryRoomSelectionComponent::UpdateHoveredPhoto(TWeakObjectPtr<AMemoryPhotoActor>& HoveredPhotoRef, AMemoryPhotoActor* NewHoveredPhoto, const FVector& HoverSourceLocation)
{
	if (HoveredPhotoRef.Get() == NewHoveredPhoto)
	{
		if (NewHoveredPhoto)
		{
			NewHoveredPhoto->SetHoverSourceLocation(HoverSourceLocation);
		}
		return;
	}

	AMemoryPhotoActor* OldHovered = HoveredPhotoRef.Get();
	if (OldHovered)
	{
		OldHovered->SetHovered(false);
	}

	HoveredPhotoRef = NewHoveredPhoto;
	if (NewHoveredPhoto)
	{
		NewHoveredPhoto->SetHoverSourceLocation(HoverSourceLocation);
		NewHoveredPhoto->SetHovered(true);
		NewHoveredPhoto->OnDestroyed.AddUniqueDynamic(this, &UMemoryRoomSelectionComponent::ClearDestroyedReferences);
		// Kick the spark's landing flare.
		SparkLandFlareElapsed = 0.0f;
		// Theme hover cue, fired on the hover-changed edge.
		if (AMemoryRoomManager* Manager = ResolveMemoryRoomManager())
		{
			Manager->NotifyPhotoHovered(NewHoveredPhoto->GetActorLocation());
		}
	}

	if (bDebugSelectionLog)
	{
		UE_LOG(LogMemoryRoom, Log, TEXT("MemoryRoom selection: hover changed from %s to %s"), *GetNameSafe(OldHovered), *GetNameSafe(NewHoveredPhoto));
	}
}

FTransform UMemoryRoomSelectionComponent::BuildExpandedTransform(AMemoryPhotoActor* Photo) const
{
	FVector ViewLocation = FVector::ZeroVector;
	FVector ViewForward = FVector::ForwardVector;
	bool bHasViewTransform = false;

	const UCameraComponent* Camera = CachedCameraComponent.Get();
	if (!Camera)
	{
		Camera = const_cast<UMemoryRoomSelectionComponent*>(this)->ResolveCameraComponent();
	}

	if (Camera)
	{
		ViewLocation = Camera->GetComponentLocation();
		ViewForward = Camera->GetForwardVector();
		bHasViewTransform = true;
	}
	else if (const UWorld* World = GetWorld())
	{
		if (const APlayerController* PlayerController = World->GetFirstPlayerController())
		{
			// Never fall back to the photo transform: that only scales/opens the photo in place when a BP loses its camera reference.
			FRotator ViewRotation = FRotator::ZeroRotator;
			PlayerController->GetPlayerViewPoint(ViewLocation, ViewRotation);
			ViewForward = ViewRotation.Vector();
			bHasViewTransform = true;
		}
	}

	if (!bHasViewTransform)
	{
		if (const AActor* Owner = GetOwner())
		{
			FRotator ViewRotation = FRotator::ZeroRotator;
			Owner->GetActorEyesViewPoint(ViewLocation, ViewRotation);
			ViewForward = ViewRotation.Vector();
			bHasViewTransform = true;
		}
		else if (const USceneComponent* Pointer = CachedPointerComponent.Get())
		{
			ViewLocation = Pointer->GetComponentLocation();
			ViewForward = Pointer->GetForwardVector();
			bHasViewTransform = true;
		}
	}

	if (!bHasViewTransform)
	{
		return Photo ? Photo->GetActorTransform() : FTransform::Identity;
	}

	FVector HorizontalForward = FVector(ViewForward.X, ViewForward.Y, 0.0f).GetSafeNormal();
	if (HorizontalForward.IsNearlyZero())
	{
		HorizontalForward = FVector::ForwardVector;
	}

	// Keep the expanded photo's visible image center at headset eye level and place it roughly two arm lengths away.
	// Using the horizontal headset forward vector prevents looking up/down from moving the photo above/below the HMD.
	// AMemoryPhotoActor::Expand corrects for any mesh/blueprint pivot offset so this location means image center, not actor origin.
	const FVector TargetLocation = ViewLocation + HorizontalForward * ExpandedDistance + FVector::UpVector * ExpandedVerticalOffset;
	FVector ToCamera = ViewLocation - TargetLocation;
	if (ToCamera.IsNearlyZero())
	{
		ToCamera = -HorizontalForward;
	}

	FRotator TargetRotation = FRotationMatrix::MakeFromXZ(ToCamera.GetSafeNormal(), FVector::UpVector).Rotator();
	TargetRotation += ExpandedRotationOffset;
	TargetRotation.Roll = 0.0f;

	const FVector TargetScale = Photo ? Photo->GetActorScale3D() : FVector::OneVector;
	return FTransform(TargetRotation, TargetLocation, TargetScale);
}

FVector UMemoryRoomSelectionComponent::GetHandAimDirection(EMemoryRoomHand Hand, const USceneComponent* Pointer) const
{
	if (!Pointer)
	{
		return FVector::ForwardVector;
	}
	// The right controller follows the standard convention - local +X is its aim direction, the
	// same axis GetForwardVector() already returns. The left controller does not: its local +Z
	// is backward/toward the player, so its aim is the opposite of GetUpVector() (local +Z) -
	// wait, local +Z read through GetUpVector()? No: GetUpVector() returns local +Z rotated to
	// world, and the left's local +Z is measured as backward, so -GetUpVector() is forward/aim.
	// See quest-grip-pose-axes in project memory for the measurement this is built from.
	return Hand == EMemoryRoomHand::Left ? -Pointer->GetUpVector() : Pointer->GetForwardVector();
}

FVector UMemoryRoomSelectionComponent::GetHandUpDirection(EMemoryRoomHand Hand, const USceneComponent* Pointer) const
{
	if (!Pointer)
	{
		return FVector::UpVector;
	}
	// Right's local +Z is up, same as GetUpVector(). Left's local +X (back of the wrist) is up
	// instead, which GetForwardVector() returns.
	return Hand == EMemoryRoomHand::Left ? Pointer->GetForwardVector() : Pointer->GetUpVector();
}

FQuat UMemoryRoomSelectionComponent::BuildHandGrabOrientation(EMemoryRoomHand Hand, const USceneComponent* Pointer) const
{
	if (!Pointer)
	{
		return FQuat::Identity;
	}
	if (Hand != EMemoryRoomHand::Left)
	{
		// Proven right-hand solve, unchanged: the controller's own +X is its aim, so this fixed
		// offset is all that is needed.
		return Pointer->GetComponentQuat() * GrabRotationOffset.Quaternion();
	}

	// Photos are authored so local +X is the image face and (matching the right hand, which
	// never touches Z) local +Z is the image's own up edge. Solved directly from the left
	// controller's measured aim/up axes, rather than a fixed constant like the right hand's -
	// unlike the right, the left's own local +X is not its aim direction, so the same kind of
	// fixed offset would not carry over. Not yet verified in headset for grabbing specifically
	// (only for the wrist-reveal gate that these same axis facts were originally measured for)
	// - nudge LeftGrabRotationTrim in 90 degree steps if a held photo faces or rolls wrong.
	const FVector Aim = GetHandAimDirection(Hand, Pointer);
	const FVector Up = GetHandUpDirection(Hand, Pointer);
	const FQuat Solved = FRotationMatrix::MakeFromXZ(-Aim, Up).ToQuat();
	return Solved * LeftGrabRotationTrim.Quaternion();
}

FTransform UMemoryRoomSelectionComponent::BuildGrabTransform(AMemoryPhotoActor* Photo, USceneComponent* Pointer, EMemoryRoomHand Hand) const
{
	FVector SourceLocation = FVector::ZeroVector;
	FQuat TargetQuat = FQuat::Identity;
	FVector AimDirection = FVector::ForwardVector;
	FVector UpDirection = FVector::UpVector;
	bool bHasSourceTransform = false;

	if (Pointer)
	{
		SourceLocation = Pointer->GetComponentLocation();
		AimDirection = GetHandAimDirection(Hand, Pointer);
		UpDirection = GetHandUpDirection(Hand, Pointer);
		TargetQuat = BuildHandGrabOrientation(Hand, Pointer);
		bHasSourceTransform = true;
	}
	else if (const UCameraComponent* Camera = CachedCameraComponent.Get())
	{
		SourceLocation = Camera->GetComponentLocation();
		AimDirection = Camera->GetForwardVector();
		UpDirection = Camera->GetUpVector();
		TargetQuat = Camera->GetComponentQuat() * GrabRotationOffset.Quaternion();
		bHasSourceTransform = true;
	}

	if (!bHasSourceTransform)
	{
		return Photo ? Photo->GetActorTransform() : FTransform::Identity;
	}

	// Held a fixed offset ahead of the grip along the hand's own aim/up axes (not always local
	// +X/+Z - see GetHandAimDirection/GetHandUpDirection), so it stays put relative to the
	// controller no matter where you point. Mathematically identical to the old
	// HandQuat.RotateVector(GrabDistance, 0, GrabVerticalOffset) for the right hand, since its
	// aim/up axes are exactly local +X/+Z.
	FVector TargetLocation = SourceLocation + AimDirection * GrabDistance + UpDirection * GrabVerticalOffset;

	// Optionally hold the print by its right edge rather than its centre. UpdateExpandedTargetTransform
	// treats the target as the photo's visible centre, so shift the centre away from the hand
	// by a half-width. GetPhotoVisualHalfWidthLocal is the width at actor scale 1, and the
	// held photo ends up at GrabScaleMultiplier, so scale by that here.
	if (Photo && !FMath::IsNearlyZero(GrabRightSideOffsetMultiplier))
	{
		const float HeldHalfWidth = Photo->GetPhotoVisualHalfWidthLocal() * FMath::Max(GrabScaleMultiplier, 0.01f) * GrabRightSideOffsetMultiplier;
		// +Y after the grab rotation is the image's left, so shifting the centre that way
		// leaves the hand at the right edge. Flip the sign of GrabRightSideOffsetMultiplier
		// to hold the other edge - which is exactly what the left hand needs: a print picked up
		// naturally is held by whichever edge is nearest that hand, the right edge for the
		// right hand and the left edge for the left, not the same edge for both.
		const float HandSideSign = (Hand == EMemoryRoomHand::Left) ? -1.0f : 1.0f;
		TargetLocation += TargetQuat.GetAxisY() * HeldHalfWidth * HandSideSign;
	}

	const FVector TargetScale = Photo ? Photo->GetActorScale3D() : FVector::OneVector;
	return FTransform(TargetQuat, TargetLocation, TargetScale);
}

void UMemoryRoomSelectionComponent::ClearDestroyedReferences(AActor* DestroyedActor)
{
	if (HoveredPhoto.Get() == DestroyedActor)
	{
		HoveredPhoto.Reset();
	}
	if (ExpandedPhoto.Get() == DestroyedActor)
	{
		ExpandedPhoto.Reset();
	}
	if (GrabbedPhoto.Get() == DestroyedActor)
	{
		GrabbedPhoto.Reset();
	}
}

void UMemoryRoomSelectionComponent::HandlePlacedPhotoUnplaced(AMemoryPhotoActor* Actor)
{
	if (!Actor || GrabbedPhoto.Get() != Actor)
	{
		return;
	}

	// Whichever hand was holding it just lost its photo out from under it - a reset (or a
	// return-to-gallery aimed specifically at this one) fired while it happened to be
	// re-grabbed. Drop the hold outright rather than letting the next release silently
	// re-place it (PlacePhotoInWorld would otherwise see IsPlacedInWorld() now false and treat
	// that release as a brand new, first-time placement).
	if (ExpandedPhoto.Get() == Actor)
	{
		ExpandedPhoto.Reset();
	}
	GrabbedPhoto.Reset();
	bGrabCurlHeld = false;
	bSecondaryGrabHandHeld = false;

	// Still mid-hold (bExpanded) as far as the actor itself knows - nothing else is going to
	// clear that now that this component has let go of it. Collapse() settles it, which is
	// also what flips bExpanded back to false and lets LayoutVirtualizedGallery pick it back
	// up into a normal wall slot on the very next layout pass.
	Actor->Collapse();

	if (bDebugSelectionLog)
	{
		UE_LOG(LogMemoryRoom, Log, TEXT("MemoryRoom selection: held photo %s was un-placed elsewhere (reset or return) - hold dropped"), *GetNameSafe(Actor));
	}
}

bool UMemoryRoomSelectionComponent::OpenPanorama(AMemoryPhotoActor* Photo)
{
	if (!Photo || !Photo->IsPanoramaCandidate() || GrabbedPhoto.IsValid() || !GetWorld()) return false;
	if (IsPanoramaOpen()) ClosePanorama();
	AMemoryPanoramaActor* Viewer = GetWorld()->SpawnActor<AMemoryPanoramaActor>();
	if (!Viewer) return false;
	if (!Viewer->Open(Photo)) { Viewer->Destroy(); return false; }
	if (ExpandedPhoto.IsValid() && ExpandedPhoto.Get() != Photo) ExpandedPhoto->Collapse();
	if (!Photo->IsExpanded()) Photo->Expand(BuildExpandedTransform(Photo));
	ExpandedPhoto = Photo;
	PanoramaViewer = Viewer;
	PendingZoomAxis = 0.0f;
	if (AMemoryRoomManager* Manager = ResolveMemoryRoomManager()) { Manager->StopScrollInput(); Manager->SetGalleryScrollSuppressed(true); }
	return true;
}

void UMemoryRoomSelectionComponent::ClosePanorama()
{
	if (AMemoryPanoramaActor* Viewer = PanoramaViewer.Get())
	{
		Viewer->Destroy();
		if (ExpandedPhoto.IsValid()) ExpandedPhoto->Collapse();
		ExpandedPhoto.Reset();
	}
	PanoramaViewer.Reset();
	PendingZoomAxis = 0.0f;
	if (AMemoryRoomManager* Manager = ResolveMemoryRoomManager()) Manager->SetGalleryScrollSuppressed(false);
}
