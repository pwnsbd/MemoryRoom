// Fill out your copyright notice in the Description page of Project Settings.

#include "MemoryRoomHandVisuals.h"
#include "MemoryRoom.h"

#include "Animation/AnimSequence.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/LocalPlayer.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "EnhancedInputSubsystems.h"
#include "EnhancedPlayerInput.h"
#include "GameFramework/Actor.h"
#include "GameFramework/PlayerController.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "MemoryRoomHandAnimInstance.h"
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
	// After animation: the wrist solve reads the bone the finger blend just evaluated.
	PrimaryComponentTick.TickGroup = TG_PostPhysics;
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
	CreateInputObjects();
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
	UAnimSequence* IdleAnim = LoadObject<UAnimSequence>(nullptr, bLeft ? *LeftIdleAnimPath : *RightIdleAnimPath);
	UAnimSequence* TriggerAnim = LoadObject<UAnimSequence>(nullptr, bLeft ? *LeftTriggerAnimPath : *RightTriggerAnimPath);
	if (!IdleAnim || !TriggerAnim)
	{
		UE_LOG(LogMemoryRoom, Warning, TEXT("%s could not load the %s idle or trigger animation; the fingers will not follow the controller."), *GetNameSafe(this), bLeft ? TEXT("left") : TEXT("right"));
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
	// The AIM pose, not the grip: the SDK switches its controller to LeftAim/RightAim whenever a
	// controller is tracked (IsdkFromOpenXRControllerDataSource.cpp:181-184), so its controller-mesh
	// and wrist targets are relative to the aim pose. On the grip pose everything sits ~90 deg off.
	Side.Grip->SetTrackingMotionSource(bLeft ? FName(TEXT("LeftAim")) : FName(TEXT("RightAim")));
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
	// Native anim instance, no Animation Blueprint: it blends the idle/grip/trigger clips itself.
	Side.Hand->SetAnimationMode(EAnimationMode::AnimationBlueprint);
	Side.Hand->SetAnimInstanceClass(UMemoryRoomHandAnimInstance::StaticClass());
	Side.Hand->RegisterComponent();
	if (IdleAnim && Side.GraspAnim && TriggerAnim)
	{
		Side.AnimInstance = Cast<UMemoryRoomHandAnimInstance>(Side.Hand->GetAnimInstance());
		if (Side.AnimInstance)
		{
			Side.AnimInstance->SetPoses(IdleAnim, Side.GraspAnim, TriggerAnim);
			KeepAlive.Add(IdleAnim);
			KeepAlive.Add(TriggerAnim);
		}
	}

	UE_LOG(LogMemoryRoom, Log, TEXT("%s built %s controller and hand on %s (parent %s)."), *GetNameSafe(this), *SidePrefix, *Side.Grip->GetName(), *GetNameSafe(Parent));
}

void UMemoryRoomHandVisualsComponent::SetCurl(bool bLeft, float Value)
{
	(bLeft ? LeftSide : RightSide).Curl = FMath::Clamp(Value, 0.0f, 1.0f);
}

void UMemoryRoomHandVisualsComponent::CreateInputObjects()
{
	// Runtime Enhanced Input: XR axes only reach the game through it, and the pawn's own IMC does not
	// map grip/trigger. Same keys in another context are fine as long as these actions do not consume.
	InputContext = NewObject<UInputMappingContext>(this, TEXT("MemoryRoomHandFingerContext"));
	KeepAlive.Add(InputContext);
	struct FBinding { FSide* Side; bool bGrip; const TCHAR* Name; FKey Key; };
	const FBinding Bindings[] = {
		{ &LeftSide, true, TEXT("LeftGrip"), EKeys::OculusTouch_Left_Grip_Axis },
		{ &LeftSide, false, TEXT("LeftTrigger"), EKeys::OculusTouch_Left_Trigger_Axis },
		{ &RightSide, true, TEXT("RightGrip"), EKeys::OculusTouch_Right_Grip_Axis },
		{ &RightSide, false, TEXT("RightTrigger"), EKeys::OculusTouch_Right_Trigger_Axis },
	};
	for (const FBinding& Binding : Bindings)
	{
		UInputAction* Action = NewObject<UInputAction>(this, *FString::Printf(TEXT("MemoryRoomHand%s"), Binding.Name));
		Action->ValueType = EInputActionValueType::Axis1D;
		Action->bConsumeInput = false;
		InputContext->MapKey(Action, Binding.Key);
		KeepAlive.Add(Action);
		(Binding.bGrip ? Binding.Side->GripAction : Binding.Side->TriggerAction) = Action;
	}
}

void UMemoryRoomHandVisualsComponent::TryRegisterInput()
{
	UWorld* World = GetWorld();
	APlayerController* Controller = World ? World->GetFirstPlayerController() : nullptr;
	ULocalPlayer* LocalPlayer = Controller ? Controller->GetLocalPlayer() : nullptr;
	UEnhancedInputLocalPlayerSubsystem* Subsystem = LocalPlayer ? LocalPlayer->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>() : nullptr;
	if (!Subsystem || !InputContext)
	{
		return;
	}
	// Far below any gameplay context so this never shadows an existing mapping.
	Subsystem->AddMappingContext(InputContext, -1000);
	bInputRegistered = true;
}

void UMemoryRoomHandVisualsComponent::ReadInput(FSide& Side) const
{
	APlayerController* Controller = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
	const UEnhancedPlayerInput* PlayerInput = Controller ? Cast<UEnhancedPlayerInput>(Controller->PlayerInput) : nullptr;
	float Grip = 0.0f;
	float Trigger = 0.0f;
	if (PlayerInput && Side.GripAction && Side.TriggerAction)
	{
		Grip = PlayerInput->GetActionValue(Side.GripAction).Get<float>();
		Trigger = PlayerInput->GetActionValue(Side.TriggerAction).Get<float>();
	}
	// SetCurl is a floor on the grip so a grab-hold still closes the hand with no squeeze.
	Side.AnimInstance->SetInput(FMath::Max(Grip, Side.Curl), Trigger);
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

	if (!bInputRegistered)
	{
		TryRegisterInput();
	}

	// Every tick, not just until first converged: the finger blend moves the finger bones and the
	// skeleton only settles after the first evaluated frames, so the wrist is re-checked and
	// the correction is a no-op once it is on target.
	for (FSide* Side : { &LeftSide, &RightSide })
	{
		if (Side->AnimInstance)
		{
			ReadInput(*Side);
		}
		if (Side->Hand && Side->Controller)
		{
			SolveWrist(*Side);
		}
	}
}
