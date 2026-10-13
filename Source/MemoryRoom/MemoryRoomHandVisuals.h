// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "MemoryRoomHandVisuals.generated.h"

class UMotionControllerComponent;
class USkeletalMeshComponent;
class UAnimSequence;
class UInputAction;
class UInputMappingContext;
class UMemoryRoomHandAnimInstance;

// Draws the Meta Quest Pro controllers and the Meta OpenXR hands holding them, placed the way the
// Meta Interaction SDK places them (UIsdkControllerVisualsRigComponent::ApplyControllerDrivenHandOffset):
// the controller mesh sits on a clean grip-pose motion controller, the hand is attached to the
// controller mesh, and the hand is moved until its wrist bone lands on the SDK's target transform.
// The fingers follow the real controller: grip squeezes the middle/ring/little fingers and the
// trigger curls the index finger (UMemoryRoomHandAnimInstance), read through a runtime Enhanced
// Input context this component adds itself. SetCurl is an extra minimum grip so a grab-hold still
// closes the hand. Its grip controllers are tagged "MemoryRoomVisualGrip" so pointer lookups skip them.
UCLASS(ClassGroup = (MemoryRoom), meta = (BlueprintSpawnableComponent))
class MEMORYROOM_API UMemoryRoomHandVisualsComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UMemoryRoomHandVisualsComponent();

	// 0 = open hand, 1 = closed fist. A floor for that side's grip alpha: the hand closes at least this far.
	UFUNCTION(BlueprintCallable, Category = "Memory Room|Hands")
	void SetCurl(bool bLeft, float Value);

	USkeletalMeshComponent* GetHandMesh(bool bLeft) const { return bLeft ? LeftSide.Hand : RightSide.Hand; }
	USkeletalMeshComponent* GetControllerMesh(bool bLeft) const { return bLeft ? LeftSide.Controller : RightSide.Controller; }
	UMotionControllerComponent* GetGripController(bool bLeft) const { return bLeft ? LeftSide.Grip : RightSide.Grip; }

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room|Hands")
	bool bShowControllers = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room|Hands")
	FString LeftControllerMeshPath = TEXT("/Game/MetaHands/Models/Controllers/MetaQuestPro/SK_MetaQuestPro_Left.SK_MetaQuestPro_Left");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room|Hands")
	FString RightControllerMeshPath = TEXT("/Game/MetaHands/Models/Controllers/MetaQuestPro/SK_MetaQuestPro_Right.SK_MetaQuestPro_Right");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room|Hands")
	FString LeftHandMeshPath = TEXT("/Game/MetaHands/Models/Hands/SK_OpenXRHand_Left.SK_OpenXRHand_Left");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room|Hands")
	FString RightHandMeshPath = TEXT("/Game/MetaHands/Models/Hands/SK_OpenXRHand_Right.SK_OpenXRHand_Right");

	// The open pose and the single-pose clips the fingers blend toward, one per hand.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room|Hands")
	FString LeftIdleAnimPath = TEXT("/Game/MetaHands/Models/Hands/Animations/Anim_OpenXRHand_Left_MetaQuestPro_Idle.Anim_OpenXRHand_Left_MetaQuestPro_Idle");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room|Hands")
	FString RightIdleAnimPath = TEXT("/Game/MetaHands/Models/Hands/Animations/Anim_OpenXRHand_Right_MetaQuestPro_Idle.Anim_OpenXRHand_Right_MetaQuestPro_Idle");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room|Hands")
	FString LeftTriggerAnimPath = TEXT("/Game/MetaHands/Models/Hands/Animations/Anim_OpenXRHand_Left_MetaQuestPro_FrontTrigger_Down.Anim_OpenXRHand_Left_MetaQuestPro_FrontTrigger_Down");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room|Hands")
	FString RightTriggerAnimPath = TEXT("/Game/MetaHands/Models/Hands/Animations/Anim_OpenXRHand_Right_MetaQuestPro_FrontTrigger_Down.Anim_OpenXRHand_Right_MetaQuestPro_FrontTrigger_Down");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room|Hands")
	FString LeftGraspAnimPath = TEXT("/Game/MetaHands/Models/Hands/Animations/Anim_OpenXRHand_Left_MetaQuestPro_GripTrigger_Down.Anim_OpenXRHand_Left_MetaQuestPro_GripTrigger_Down");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room|Hands")
	FString RightGraspAnimPath = TEXT("/Game/MetaHands/Models/Hands/Animations/Anim_OpenXRHand_Right_MetaQuestPro_GripTrigger_Down.Anim_OpenXRHand_Right_MetaQuestPro_GripTrigger_Down");

	// SDK: IsdkXRUtils::OXR::ControllerRelativeRootTransform.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room|Hands")
	FVector ControllerMeshRelativeLocation = FVector(-4.1f, 0.0f, 0.0f);

	// Where the wrist bone sits relative to the controller mesh. SDK:
	// IsdkXRUtils::TargetControllerDrivenHandTransformLeft / Right.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room|Hands")
	FVector LeftWristTargetLocation = FVector(-10.5f, -3.0f, -4.2f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room|Hands")
	FRotator LeftWristTargetRotation = FRotator(90.0f, -15.0f, 0.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room|Hands")
	FVector RightWristTargetLocation = FVector(-10.5f, 3.0f, -4.2f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room|Hands")
	FRotator RightWristTargetRotation = FRotator(-90.0f, -165.0f, 0.0f);

protected:
	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
	struct FSide
	{
		UMotionControllerComponent* Grip = nullptr;
		USkeletalMeshComponent* Controller = nullptr;
		USkeletalMeshComponent* Hand = nullptr;
		UAnimSequence* GraspAnim = nullptr;
		UMemoryRoomHandAnimInstance* AnimInstance = nullptr;
		UInputAction* GripAction = nullptr;
		UInputAction* TriggerAction = nullptr;
		float Curl = 0.0f;
		FTransform WristTarget = FTransform::Identity;
	};

	void BuildSide(bool bLeft, FSide& Side);
	void SolveWrist(FSide& Side) const;
	// Builds the runtime Enhanced Input actions and context. Retried each tick until the pawn has
	// a local player to hold them (it is not possessed yet at BeginPlay).
	void TryRegisterInput();
	void CreateInputObjects();
	void ReadInput(FSide& Side) const;

	UPROPERTY(Transient)
	TObjectPtr<UInputMappingContext> InputContext;

	bool bInputRegistered = false;

	// Held in UPROPERTYs so the garbage collector sees the raw pointers in FSide.
	UPROPERTY(Transient)
	TArray<TObjectPtr<UObject>> KeepAlive;

	FSide LeftSide;
	FSide RightSide;
};
