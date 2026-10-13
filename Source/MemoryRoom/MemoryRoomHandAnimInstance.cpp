// Fill out your copyright notice in the Description page of Project Settings.

#include "MemoryRoomHandAnimInstance.h"

#include "Animation/AnimInstanceProxy.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimationPoseData.h"
#include "Animation/AnimNodeBase.h"
#include "BoneContainer.h"

namespace
{
	enum class EFingerGroup : uint8
	{
		Idle,    // wrist, palm, thumb and anything unknown: always the Idle pose
		Grip,    // middle, ring, little
		Trigger, // index
	};

	// Bone names follow IsdkHandPoseData.cpp: XRHand_<Finger><Joint>.
	EFingerGroup GroupForBone(const FString& BoneName)
	{
		if (BoneName.StartsWith(TEXT("XRHand_Index")))
		{
			return EFingerGroup::Trigger;
		}
		if (BoneName.StartsWith(TEXT("XRHand_Middle")) || BoneName.StartsWith(TEXT("XRHand_Ring")) || BoneName.StartsWith(TEXT("XRHand_Little")))
		{
			return EFingerGroup::Grip;
		}
		return EFingerGroup::Idle;
	}
}

// Lives on the animation worker thread; game-thread values are copied in PreUpdate.
struct FMemoryRoomHandAnimProxy : public FAnimInstanceProxy
{
	FMemoryRoomHandAnimProxy(UAnimInstance* InInstance) : FAnimInstanceProxy(InInstance) {}

	virtual void PreUpdate(UAnimInstance* InAnimInstance, float DeltaSeconds) override
	{
		FAnimInstanceProxy::PreUpdate(InAnimInstance, DeltaSeconds);

		UMemoryRoomHandAnimInstance* Instance = Cast<UMemoryRoomHandAnimInstance>(InAnimInstance);
		if (!Instance)
		{
			return;
		}
		Idle = Instance->IdleAnim;
		Grip = Instance->GripAnim;
		Trigger = Instance->TriggerAnim;

		// Below the dead zone the finger stays fully open, like the SDK.
		const float GripGoal = Instance->GripTarget < Instance->DeadZone ? 0.0f : Instance->GripTarget;
		const float TriggerGoal = Instance->TriggerTarget < Instance->DeadZone ? 0.0f : Instance->TriggerTarget;
		GripAlpha = FMath::FInterpTo(GripAlpha, GripGoal, DeltaSeconds, Instance->InterpolationSpeed);
		TriggerAlpha = FMath::FInterpTo(TriggerAlpha, TriggerGoal, DeltaSeconds, Instance->InterpolationSpeed);
		Instance->GripAlpha = GripAlpha;
		Instance->TriggerAlpha = TriggerAlpha;
	}

	virtual bool Evaluate(FPoseContext& Output) override
	{
		if (!Idle.IsValid())
		{
			return false;
		}

		// Every clip is a single full pose, so each is sampled at time zero.
		const FAnimExtractContext Extract(0.0, false);
		FAnimationPoseData OutputData(Output);
		Idle->GetAnimationPose(OutputData, Extract);

		const bool bGrip = Grip.IsValid() && GripAlpha > KINDA_SMALL_NUMBER;
		const bool bTrigger = Trigger.IsValid() && TriggerAlpha > KINDA_SMALL_NUMBER;
		if (!bGrip && !bTrigger)
		{
			return true;
		}

		const FBoneContainer& Bones = Output.Pose.GetBoneContainer();
		if (Bones.GetSerialNumber() != GroupSerial || Groups.Num() != Output.Pose.GetNumBones())
		{
			Groups.Reset();
			for (FCompactPoseBoneIndex BoneIndex : Output.Pose.ForEachBoneIndex())
			{
				const int32 MeshIndex = Bones.MakeMeshPoseIndex(BoneIndex).GetInt();
				Groups.Add(GroupForBone(Bones.GetReferenceSkeleton().GetBoneName(MeshIndex).ToString()));
			}
			GroupSerial = Bones.GetSerialNumber();
		}

		FPoseContext GripPose(this);
		FPoseContext TriggerPose(this);
		if (bGrip)
		{
			FAnimationPoseData GripData(GripPose);
			Grip->GetAnimationPose(GripData, Extract);
		}
		if (bTrigger)
		{
			FAnimationPoseData TriggerData(TriggerPose);
			Trigger->GetAnimationPose(TriggerData, Extract);
		}

		for (FCompactPoseBoneIndex BoneIndex : Output.Pose.ForEachBoneIndex())
		{
			const EFingerGroup Group = Groups[BoneIndex.GetInt()];
			if (Group == EFingerGroup::Grip && bGrip)
			{
				const FTransform Base = Output.Pose[BoneIndex];
				Output.Pose[BoneIndex].Blend(Base, GripPose.Pose[BoneIndex], GripAlpha);
			}
			else if (Group == EFingerGroup::Trigger && bTrigger)
			{
				const FTransform Base = Output.Pose[BoneIndex];
				Output.Pose[BoneIndex].Blend(Base, TriggerPose.Pose[BoneIndex], TriggerAlpha);
			}
		}
		return true;
	}

	TWeakObjectPtr<UAnimSequence> Idle;
	TWeakObjectPtr<UAnimSequence> Grip;
	TWeakObjectPtr<UAnimSequence> Trigger;
	float GripAlpha = 0.0f;
	float TriggerAlpha = 0.0f;
	TArray<EFingerGroup> Groups;
	uint16 GroupSerial = 0;
};

void UMemoryRoomHandAnimInstance::SetPoses(UAnimSequence* InIdle, UAnimSequence* InGrip, UAnimSequence* InTrigger)
{
	IdleAnim = InIdle;
	GripAnim = InGrip;
	TriggerAnim = InTrigger;
}

void UMemoryRoomHandAnimInstance::SetInput(float InGrip, float InTrigger)
{
	GripTarget = FMath::Clamp(InGrip, 0.0f, 1.0f);
	TriggerTarget = FMath::Clamp(InTrigger, 0.0f, 1.0f);
}

FAnimInstanceProxy* UMemoryRoomHandAnimInstance::CreateAnimInstanceProxy()
{
	return new FMemoryRoomHandAnimProxy(this);
}
