// Fill out your copyright notice in the Description page of Project Settings.

#include "MemoryRoomMetaRig.h"
#include "MemoryRoom.h"

#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "EnhancedInputSubsystems.h"
#include "GameFramework/Actor.h"
#include "GameFramework/PlayerController.h"
#include "InputMappingContext.h"
#include "MotionControllerComponent.h"
#include "Rig/IsdkControllerRigComponent.h"
#include "Rig/IsdkGrabInteractionRigComponent.h"
#include "Rig/IsdkPokeInteractionRigComponent.h"
#include "Rig/IsdkRayInteractionRigComponent.h"
#include "XRDeviceVisualizationComponent.h"

namespace
{
	// Marks the runtime controllers that only carry the SDK rigs; pointer lookups skip them.
	const FName MetaRigVisualGripTag(TEXT("MemoryRoomVisualGrip"));
	// Where the SDK ships the controller-animation mapping context (RayForge loads the same path).
	const TCHAR* const MetaRigAnimationContextPath = TEXT("/OculusInteraction/Inputs/IMC_IsdkControllerAnimation.IMC_IsdkControllerAnimation");
}

UMemoryRoomMetaRigComponent::UMemoryRoomMetaRigComponent()
{
	// Only ticks until the animation context is added, then switches itself off.
	PrimaryComponentTick.bCanEverTick = true;
}

void UMemoryRoomMetaRigComponent::BeginPlay()
{
	Super::BeginPlay();

	AActor* Owner = GetOwner();
	if (!Owner)
	{
		return;
	}

	// The stock device models are never wanted: the SDK rigs draw every controller and hand.
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

	LeftController = BuildSide(true);
	RightController = BuildSide(false);

	AnimationContext = LoadObject<UInputMappingContext>(nullptr, MetaRigAnimationContextPath);
	if (!AnimationContext)
	{
		UE_LOG(LogMemoryRoom, Warning, TEXT("%s could not load %s; the Meta hands will not animate."), *GetNameSafe(this), MetaRigAnimationContextPath);
	}
	TryAddAnimationContext();
	SetComponentTickEnabled(!bAnimationContextAdded && AnimationContext != nullptr);
}

UMotionControllerComponent* UMemoryRoomMetaRigComponent::BuildSide(bool bLeft)
{
	AActor* Owner = GetOwner();

	// Share the parent of the pawn's own controller on this side so the tracking space matches.
	const FString SidePrefix = bLeft ? TEXT("Left") : TEXT("Right");
	TArray<UMotionControllerComponent*> PawnControllers;
	Owner->GetComponents<UMotionControllerComponent>(PawnControllers);
	UMotionControllerComponent* ParentSource = nullptr;
	for (UMotionControllerComponent* Existing : PawnControllers)
	{
		if (Existing && !Existing->ComponentHasTag(MetaRigVisualGripTag) && Existing->GetTrackingMotionSource().ToString().StartsWith(SidePrefix))
		{
			ParentSource = Existing;
			break;
		}
	}
	USceneComponent* Parent = ParentSource ? ParentSource->GetAttachParent() : nullptr;

	// Names keep the "...Grip" pattern the old hand visuals used: the menu actor's wrist lookup
	// name-matches pawn components, so renaming them could change what it attaches to.
	const TCHAR* GripName = bLeft ? TEXT("MemoryRoomLeftGrip") : TEXT("MemoryRoomRightGrip");
	UMotionControllerComponent* Controller = NewObject<UMotionControllerComponent>(Owner, MakeUniqueObjectName(Owner, UMotionControllerComponent::StaticClass(), GripName));
	// The AIM pose: the SDK switches its controller to LeftAim/RightAim itself whenever a controller
	// is tracked, so starting there keeps the pre-tracking frame consistent and gives the left laser its origin.
	Controller->SetTrackingMotionSource(bLeft ? FName(TEXT("LeftAim")) : FName(TEXT("RightAim")));
	Controller->ComponentTags.Add(MetaRigVisualGripTag);
	Controller->SetupAttachment(Parent ? Parent : Owner->GetRootComponent());
	Controller->SetRelativeTransform(FTransform::Identity);
	Controller->RegisterComponent();

	UIsdkControllerRigComponent* Rig = nullptr;
	if (bLeft)
	{
		Rig = NewObject<UIsdkControllerRigComponentLeft>(Owner, MakeUniqueObjectName(Owner, UIsdkControllerRigComponentLeft::StaticClass(), TEXT("MemoryRoomLeftMetaRig")));
	}
	else
	{
		Rig = NewObject<UIsdkControllerRigComponentRight>(Owner, MakeUniqueObjectName(Owner, UIsdkControllerRigComponentRight::StaticClass(), TEXT("MemoryRoomRightMetaRig")));
	}
	Rig->SetupAttachment(Controller);
	// Visuals only: switch the SDK's own interactors off so they never compete with MemoryRoom's
	// laser / selection / grab (or draw a second ray). Their enabled conditionals watch IsActive.
	if (Rig->GetRayInteraction())
	{
		Rig->GetRayInteraction()->Deactivate();
	}
	if (Rig->GetPokeInteraction())
	{
		Rig->GetPokeInteraction()->Deactivate();
	}
	if (Rig->GetGrabInteraction())
	{
		Rig->GetGrabInteraction()->Deactivate();
	}
	Rig->RegisterComponent();
	(bLeft ? LeftRig : RightRig) = Rig;

	UE_LOG(LogMemoryRoom, Log, TEXT("%s built %s Meta controller rig on %s (parent %s)."), *GetNameSafe(this), *SidePrefix, *Controller->GetName(), *GetNameSafe(Parent));
	return Controller;
}

void UMemoryRoomMetaRigComponent::TryAddAnimationContext()
{
	UWorld* World = GetWorld();
	APlayerController* PlayerController = World ? World->GetFirstPlayerController() : nullptr;
	ULocalPlayer* LocalPlayer = PlayerController ? PlayerController->GetLocalPlayer() : nullptr;
	UEnhancedInputLocalPlayerSubsystem* Subsystem = LocalPlayer ? LocalPlayer->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>() : nullptr;
	if (!Subsystem || !AnimationContext)
	{
		return;
	}
	// Priority 0, as RayForge: the rigs read grip/trigger through this context, which the SDK leaves to the pawn to add.
	Subsystem->AddMappingContext(AnimationContext, 0);
	bAnimationContextAdded = true;
}

void UMemoryRoomMetaRigComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	// The local player may not exist yet at BeginPlay; retry until it does.
	if (!bAnimationContextAdded)
	{
		TryAddAnimationContext();
	}
	if (bAnimationContextAdded)
	{
		SetComponentTickEnabled(false);
	}
}
