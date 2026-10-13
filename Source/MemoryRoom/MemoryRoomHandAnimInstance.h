// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "MemoryRoomHandAnimInstance.generated.h"

class UAnimSequence;

// Drives the Meta OpenXR hand from controller input without an Animation Blueprint, the way the
// Meta Interaction SDK's QuestControllerHandsAnimInstance does: the Idle pose is the base, the
// middle/ring/little fingers blend toward GripTrigger_Down by the grip alpha, the index finger
// blends toward FrontTrigger_Down by the trigger alpha, and the wrist, palm and thumb stay Idle so
// the wrist solve in UMemoryRoomHandVisualsComponent keeps seeing a stable wrist bone.
UCLASS()
class MEMORYROOM_API UMemoryRoomHandAnimInstance : public UAnimInstance
{
	GENERATED_BODY()

public:
	// The three full poses to blend. All three must be for this hand's skeleton.
	void SetPoses(UAnimSequence* InIdle, UAnimSequence* InGrip, UAnimSequence* InTrigger);

	// Raw 0..1 input; smoothing and the dead zone are applied inside.
	void SetInput(float InGrip, float InTrigger);

	float GetGripAlpha() const { return GripAlpha; }
	float GetTriggerAlpha() const { return TriggerAlpha; }

	// SDK: QuestControllerAnimInstance InterpolationSpeed / TriggerDeadZone.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room|Hands")
	float InterpolationSpeed = 20.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room|Hands")
	float DeadZone = 0.05f;

protected:
	virtual FAnimInstanceProxy* CreateAnimInstanceProxy() override;

private:
	friend struct FMemoryRoomHandAnimProxy;

	UPROPERTY(Transient)
	TObjectPtr<UAnimSequence> IdleAnim;

	UPROPERTY(Transient)
	TObjectPtr<UAnimSequence> GripAnim;

	UPROPERTY(Transient)
	TObjectPtr<UAnimSequence> TriggerAnim;

	float GripTarget = 0.0f;
	float TriggerTarget = 0.0f;
	// Smoothed values the proxy writes back each update; read by the game thread.
	float GripAlpha = 0.0f;
	float TriggerAlpha = 0.0f;
};
