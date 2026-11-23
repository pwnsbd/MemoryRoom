// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "MemoryRoomMetaRig.generated.h"

class UIsdkControllerRigComponent;
class UInputMappingContext;
class UMotionControllerComponent;

// Draws the player's hands and controllers with the Meta Interaction SDK's controller rigs, set up the
// way RayForge's pawn does it. Each side gets its own runtime motion controller (tagged
// "MemoryRoomVisualGrip" so MemoryRoom's pointer lookups skip it) with a UIsdkControllerRigComponent
// underneath; the SDK animates the hand, fingers and buttons from the controller input through
// IMC_IsdkControllerAnimation, which this component adds to the local player.
//
// The rigs only provide visuals here. Their ray / poke / grab interaction sub-components are
// deactivated so they cannot compete with MemoryRoom's own laser, selection and grab. The Blueprint
// pawn's own LeftController / RightController are left untouched.
UCLASS()
class MEMORYROOM_API UMemoryRoomMetaRigComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UMemoryRoomMetaRigComponent();

	// The left AIM-pose controller; MemoryRoom's left laser starts from it. Null until BeginPlay ran.
	UMotionControllerComponent* GetLeftAimController() const { return LeftController; }

protected:
	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
	UMotionControllerComponent* BuildSide(bool bLeft);
	void TryAddAnimationContext();

	UPROPERTY(Transient)
	TObjectPtr<UMotionControllerComponent> LeftController;

	UPROPERTY(Transient)
	TObjectPtr<UMotionControllerComponent> RightController;

	UPROPERTY(Transient)
	TObjectPtr<UIsdkControllerRigComponent> LeftRig;

	UPROPERTY(Transient)
	TObjectPtr<UIsdkControllerRigComponent> RightRig;

	UPROPERTY(Transient)
	TObjectPtr<UInputMappingContext> AnimationContext;

	bool bAnimationContextAdded = false;
};
