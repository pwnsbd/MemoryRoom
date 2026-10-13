// Fill out your copyright notice in the Description page of Project Settings.

#include "MemoryRoomHandVisuals.h"
#include "MemoryRoom.h"

#include "Animation/AnimSequence.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "GameFramework/Actor.h"
#include "IMotionController.h"
#include "MotionControllerComponent.h"
#include "XRDeviceVisualizationComponent.h"

namespace
{
	// Marks the runtime grip controllers that only carry hand and controller meshes.
	const FName VisualGripTag(TEXT("MemoryRoomVisualGrip"));
	// The OpenXR hand skeleton's wrist bone (IsdkXRUtils::WristBoneName).
	const FName WristBoneName(TEXT("XRHand_Wrist"));
	// IsdkControllerVisualsRigComponent's OffsetConvergedTolerance.
	constexpr float WristConvergedTolerance = 1.e-4f;
}

UMemoryRoomHandVisualsComponent::UMemoryRoomHandVisualsComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
}

void UMemoryRoomHandVisualsComponent::BeginPlay()
{
	Super::BeginPlay();

	AActor* Owner = GetOwner();
	if (!Owner)
	{
		return;
	}

	// The stock device models are never wanted: this component draws every controller itself.
	TArray<UXRDeviceVisualizationComponent*> Visualizations;
	Owner->GetComponents<UXRDeviceVisualizationComponent>(Visualizations);
	for (UXRDeviceVisualizationComponent* Visualization : Visualizations)
	{
		if (Visualization)
		{
			Visualization->SetIsVisualizationActive(false);
			Visualization->SetVisibility(false, true);
		}
	}

	LeftSide.WristTarget = FTransform(LeftWristTargetRotation, LeftWristTargetLocation, FVector::OneVector);
	RightSide.WristTarget = FTransform(RightWristTargetRotation, RightWristTargetLocation, FVector::OneVector);
	BuildSide(true, LeftSide);
	BuildSide(false, RightSide);
}

void UMemoryRoomHandVisualsComponent::BuildSide(bool bLeft, FSide& Side)
{
	AActor* Owner = GetOwner();
	const FString& ControllerPath = bLeft ? LeftControllerMeshPath : RightControllerMeshPath;
	const FString& HandPath = bLeft ? LeftHandMeshPath : RightHandMeshPath;
	const FString& AnimPath = bLeft ? LeftGraspAnimPath : RightGraspAnimPath;

	USkeletalMesh* ControllerMesh = LoadObject<USkeletalMesh>(nullptr, *ControllerPath);
	if (!ControllerMesh)
	{
		UE_LOG(LogMemoryRoom, Warning, TEXT("%s could not load the %s controller mesh at %s; that side shows nothing."), *GetNameSafe(this), bLeft ? TEXT("left") : TEXT("right"), *ControllerPath);
		return;
	}
	USkeletalMesh* HandMesh = LoadObject<USkeletalMesh>(nullptr, *HandPath);
	if (!HandMesh)
	{
		UE_LOG(LogMemoryRoom, Warning, TEXT("%s could not load the %s hand mesh at %s; that side shows nothing."), *GetNameSafe(this), bLeft ? TEXT("left") : TEXT("right"), *HandPath);
		return;
	}
	Side.GraspAnim = LoadObject<UAnimSequence>(nullptr, *AnimPath);
	if (!Side.GraspAnim)
	{
		UE_LOG(LogMemoryRoom, Warning, TEXT("%s could not load the %s grasp animation at %s; the fingers will not curl."), *GetNameSafe(this), bLeft ? TEXT("left") : TEXT("right"), *AnimPath);
	}
	else
	{
		KeepAlive.Add(Side.GraspAnim);
	}

	// Share the parent of the pawn's own controller on this side so the tracking space matches.
	const FString SidePrefix = bLeft ? TEXT("Left") : TEXT("Right");
	TArray<UMotionControllerComponent*> PawnControllers;
	Owner->GetComponents<UMotionControllerComponent>(PawnControllers);
	UMotionControllerComponent* ParentSource = nullptr;
	for (UMotionControllerComponent* Controller : PawnControllers)
	{
		if (Controller && !Controller->ComponentHasTag(VisualGripTag) && Controller->GetTrackingMotionSource().ToString().StartsWith(SidePrefix))
		{
			ParentSource = Controller;
			break;
		}
	}
	USceneComponent* Parent = ParentSource ? ParentSource->GetAttachParent() : nullptr;

	// Names keep the "...Grip" / "...GripHand" pattern the old code used: the menu actor's wrist
	// lookup name-matches pawn components, so renaming them could change what it attaches to.
	const TCHAR* GripName = bLeft ? TEXT("MemoryRoomLeftGrip") : TEXT("MemoryRoomRightGrip");
	Side.Grip = NewObject<UMotionControllerComponent>(Owner, MakeUniqueObjectName(Owner, UMotionControllerComponent::StaticClass(), GripName));
	Side.Grip->SetTrackingMotionSource(bLeft ? IMotionController::LeftHandSourceId : IMotionController::RightHandSourceId);
	Side.Grip->ComponentTags.Add(VisualGripTag);
	Side.Grip->SetupAttachment(Parent ? Parent : Owner->GetRootComponent());
	Side.Grip->SetRelativeTransform(FTransform::Identity);
	Side.Grip->RegisterComponent();

	// Controller mesh: SDK ControllerRelativeRootTransform, no rotation, scale 1.
	Side.Controller = NewObject<USkeletalMeshComponent>(Owner, MakeUniqueObjectName(Owner, USkeletalMeshComponent::StaticClass(), *FString::Printf(TEXT("%sModel"), GripName)));
	Side.Controller->SetSkeletalMesh(ControllerMesh);
	Side.Controller->SetupAttachment(Side.Grip);
	Side.Controller->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Side.Controller->SetCastShadow(false);
	Side.Controller->SetRelativeTransform(FTransform(FRotator::ZeroRotator, ControllerMeshRelativeLocation, FVector::OneVector));
	Side.Controller->SetHiddenInGame(!bShowControllers);
	Side.Controller->RegisterComponent();

	// Hand: snapped to the controller mesh, then walked onto the wrist target by SolveWrist.
	Side.Hand = NewObject<USkeletalMeshComponent>(Owner, MakeUniqueObjectName(Owner, USkeletalMeshComponent::StaticClass(), *FString::Printf(TEXT("%sHand"), GripName)));
	Side.Hand->SetSkeletalMesh(HandMesh);
	Side.Hand->SetupAttachment(Side.Controller);
	Side.Hand->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Side.Hand->SetCastShadow(false);
	Side.Hand->SetRelativeTransform(FTransform::Identity);
	Side.Hand->RegisterComponent();
	if (Side.GraspAnim)
	{
		// Not played, scrubbed: SetCurl picks the frame. Rate zero so nothing advances on its own.
		Side.Hand->PlayAnimation(Side.GraspAnim, false);
		Side.Hand->SetPlayRate(0.0f);
		Side.Hand->SetPosition(0.0f, false);
	}

	UE_LOG(LogMemoryRoom, Log, TEXT("%s built %s controller and hand on %s (parent %s)."), *GetNameSafe(this), *SidePrefix, *Side.Grip->GetName(), *GetNameSafe(Parent));
}

void UMemoryRoomHandVisualsComponent::SetCurl(bool bLeft, float Value)
{
	const FSide& Side = bLeft ? LeftSide : RightSide;
	if (!Side.Hand || !Side.GraspAnim)
	{
		return;
	}
	Side.Hand->SetPosition(FMath::Clamp(Value, 0.0f, 1.0f) * Side.GraspAnim->GetPlayLength(), false);
}

void UMemoryRoomHandVisualsComponent::SolveWrist(FSide& Side) const
{
	// Same operation order as UIsdkControllerVisualsRigComponent::ApplyControllerDrivenHandOffset.
	const FTransform WristBoneWorldTransform = Side.Hand->GetSocketTransform(WristBoneName, RTS_World);
	const FTransform WristBoneRelativeTransform = WristBoneWorldTransform.GetRelativeTransform(Side.Controller->GetComponentTransform());
	const FTransform AdjustmentTransform = WristBoneRelativeTransform.Inverse() * Side.WristTarget;
	if (AdjustmentTransform.Equals(FTransform::Identity, WristConvergedTolerance))
	{
		return;
	}
	Side.Hand->SetRelativeTransform(Side.Hand->GetRelativeTransform() * AdjustmentTransform);
}

void UMemoryRoomHandVisualsComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	// Every tick, not just until first converged: the curl clip moves the finger bones and the
	// skeleton only settles after the first evaluated frames, so the wrist is re-checked and
	// the correction is a no-op once it is on target.
	for (FSide* Side : { &LeftSide, &RightSide })
	{
		if (Side->Hand && Side->Controller)
		{
			SolveWrist(*Side);
		}
	}
}
