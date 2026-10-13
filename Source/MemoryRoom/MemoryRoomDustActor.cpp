// Fill out your copyright notice in the Description page of Project Settings.

#include "MemoryRoomDustActor.h"
#include "MemoryRoom.h"

#include "Components/InstancedStaticMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "MemoryRoomSkyDomeActor.h"
#include "UObject/ConstructorHelpers.h"

AMemoryRoomDustActor::AMemoryRoomDustActor()
{
	PrimaryActorTick.bCanEverTick = true;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);

	MoteMeshes = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("MoteMeshes"));
	MoteMeshes->SetupAttachment(SceneRoot);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereMesh(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	if (SphereMesh.Succeeded())
	{
		DustSphereMesh = SphereMesh.Object;
		MoteMeshes->SetStaticMesh(DustSphereMesh);
	}
	static ConstructorHelpers::FObjectFinder<UStaticMesh> PlaneMesh(TEXT("/Engine/BasicShapes/Plane.Plane"));
	if (PlaneMesh.Succeeded())
	{
		LeafPlaneMesh = PlaneMesh.Object;
	}
	MoteMeshes->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	MoteMeshes->SetCollisionProfileName(TEXT("NoCollision"));
	MoteMeshes->SetCastShadow(false);
	MoteMeshes->bCastDynamicShadow = false;
	MoteMeshes->bReceivesDecals = false;
	MoteMeshes->bAffectDistanceFieldLighting = false;
	// The field follows the player, so it is always on screen; skip the culling work.
	MoteMeshes->SetBoundsScale(4.0f);
}

void AMemoryRoomDustActor::BeginPlay()
{
	Super::BeginPlay();
	BuildMotes();
}

void AMemoryRoomDustActor::BuildMotes()
{
	if (!MoteMeshes)
	{
		return;
	}

	MoteMeshes->ClearInstances();
	Motes.Reset();
	ActiveMoteCount = 0;
	// Drop the old material wrapper so it re-wraps the new style's mesh material.
	MoteMeshes->SetMaterial(0, nullptr);
	MoteMaterialInstance = nullptr;

	const int32 SafeCount = FMath::Clamp(MoteCount, 0, 2000);
	if (SafeCount <= 0)
	{
		return;
	}

	Motes.Reserve(SafeCount);
	FRandomStream Random(DustSeed);
	const float Radius = FMath::Max(FieldRadiusCm, 100.0f);
	const bool bLeaf = MoteStyle == EMemoryRoomMoteStyle::Leaf;

	// Both the engine sphere and the engine plane are 100cm across, so the same mesh
	// (chosen here, per style) and the same /100 scale math below work for either.
	MoteMeshes->SetStaticMesh(bLeaf && LeafPlaneMesh ? LeafPlaneMesh : DustSphereMesh);

	TArray<FTransform> InstanceTransforms;
	InstanceTransforms.Reserve(SafeCount);

	for (int32 Index = 0; Index < SafeCount; ++Index)
	{
		FMote Mote;
		Mote.BasePosition = FVector(
			Random.FRandRange(-Radius, Radius),
			Random.FRandRange(-Radius, Radius),
			Random.FRandRange(-Radius * 0.55f, Radius * 0.9f));
		Mote.DriftAxis = Random.GetUnitVector();
		Mote.Phase = Random.FRandRange(0.0f, TWO_PI);
		Mote.Rate = Random.FRandRange(0.25f, 0.85f);
		// A spread of sizes stops the field reading as a regular grid of identical dots.
		Mote.Scale = Random.FRandRange(0.55f, 1.45f);
		Mote.TwinklePhase = Random.FRandRange(0.0f, TWO_PI);
		Mote.TwinkleRate = Random.FRandRange(FMath::Max(TwinkleSpeedMin, 0.05f), FMath::Max(TwinkleSpeedMax, TwinkleSpeedMin + 0.05f));
		Mote.TumbleRateDegPerSec = Random.FRandRange(FMath::Max(TumbleSpeedDegPerSecMin, 0.0f), FMath::Max(TumbleSpeedDegPerSecMax, TumbleSpeedDegPerSecMin + 1.0f));
		if (Random.FRand() > 0.5f)
		{
			Mote.TumbleRateDegPerSec = -Mote.TumbleRateDegPerSec;
		}
		Motes.Add(Mote);

		const float SizeCm = bLeaf ? LeafSizeCm : MoteSizeCm;
		const float MoteRadiusCm = FMath::Max(SizeCm, 0.1f) * Mote.Scale;
		InstanceTransforms.Emplace(FQuat::Identity, Mote.BasePosition, FVector(MoteRadiusCm / 100.0f));
	}

	// The engine plane is single-sided, so each leaf gets a second plane flipped over it
	// (instances SafeCount..2*SafeCount-1, driven in lockstep from Tick) to show from both sides.
	if (bLeaf)
	{
		for (int32 Index = 0; Index < SafeCount; ++Index)
		{
			InstanceTransforms.Add(InstanceTransforms[Index]);
		}
	}

	MoteMeshes->AddInstances(InstanceTransforms, false, false);
	ActiveMoteCount = SafeCount;

	EnsureMoteMaterial();
	ApplyDustTintToMaterial();

	UE_LOG(LogMemoryRoom, Log, TEXT("%s built %d dust motes."), *GetName(), SafeCount);
}

void AMemoryRoomDustActor::EnsureMoteMaterial()
{
	if (MoteMaterialInstance || !MoteMeshes)
	{
		return;
	}

	if (MoteMaterial)
	{
		MoteMeshes->SetMaterial(0, MoteMaterial);
	}

	// Wrap whatever ended up on slot 0 (the override, or the engine sphere's own material)
	// so the tint can be pushed at runtime.
	MoteMaterialInstance = MoteMeshes->CreateDynamicMaterialInstance(0);
}

void AMemoryRoomDustActor::ApplyDustTintToMaterial()
{
	if (!MoteMaterialInstance)
	{
		return;
	}

	for (const FName& ParameterName : DustTintParameterNames)
	{
		if (!ParameterName.IsNone())
		{
			MoteMaterialInstance->SetVectorParameterValue(ParameterName, GetEffectiveTint());
		}
	}
}

FLinearColor AMemoryRoomDustActor::GetEffectiveTint() const
{
	// Leaves fall back to an autumn orange when the theme leaves the tint at neutral white.
	if (MoteStyle == EMemoryRoomMoteStyle::Leaf && DustTint.Equals(FLinearColor::White, 0.02f))
	{
		return FLinearColor(0.85f, 0.38f, 0.08f, 1.0f);
	}
	return DustTint;
}

void AMemoryRoomDustActor::SetMoteStyle(EMemoryRoomMoteStyle NewStyle)
{
	if (MoteStyle == NewStyle)
	{
		return;
	}
	MoteStyle = NewStyle;
	// Before BeginPlay the field does not exist yet; BeginPlay builds it in the new style.
	if (HasActorBegunPlay())
	{
		BuildMotes();
		SetDensityLevel(DensityLevel);
	}
}

#if WITH_EDITOR
void AMemoryRoomDustActor::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	if (PropertyChangedEvent.GetPropertyName() == GET_MEMBER_NAME_CHECKED(AMemoryRoomDustActor, MoteStyle) && HasActorBegunPlay())
	{
		BuildMotes();
	}
}
#endif

void AMemoryRoomDustActor::SetDustTint(FLinearColor NewTint)
{
	DustTint = NewTint;
	EnsureMoteMaterial();
	ApplyDustTintToMaterial();
}

AMemoryRoomSkyDomeActor* AMemoryRoomDustActor::ResolveSkyDome()
{
	if (CachedSkyDome.IsValid())
	{
		return CachedSkyDome.Get();
	}

	if (UWorld* World = GetWorld())
	{
		for (TActorIterator<AMemoryRoomSkyDomeActor> It(World); It;)
		{
			CachedSkyDome = *It;
			break;
		}
	}
	return CachedSkyDome.Get();
}

void AMemoryRoomDustActor::SetDensityLevel(int32 Level)
{
	DensityLevel = FMath::Clamp(Level, 0, 3);
	const bool bOff = DensityLevel == 0;
	if (MoteMeshes)
	{
		MoteMeshes->SetVisibility(!bOff);
	}
	SetActorTickEnabled(!bOff);
	if (bOff && MoteMeshes)
	{
		// Collapse every instance so nothing lingers on screen.
		for (int32 Index = 0; Index < Motes.Num(); ++Index)
		{
			MoteMeshes->UpdateInstanceTransform(Index, FTransform(FQuat::Identity, FieldCenter, FVector::ZeroVector), true, false, false);
		}
		MoteMeshes->MarkRenderStateDirty();
	}
}

void AMemoryRoomDustActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (!MoteMeshes || Motes.Num() <= 0 || DensityLevel <= 0)
	{
		return;
	}

	const UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	if (const APlayerController* PlayerController = World->GetFirstPlayerController())
	{
		FVector ViewLocation = FVector::ZeroVector;
		FRotator ViewRotation = FRotator::ZeroRotator;
		PlayerController->GetPlayerViewPoint(ViewLocation, ViewRotation);
		FieldCenter = ViewLocation;
	}

	const bool bLeaf = MoteStyle == EMemoryRoomMoteStyle::Leaf;

	// Thin the field out when the room is lit; motes are a night effect and would just be
	// visual noise against a bright sky. Leaves are an all-hours ambient effect, so this
	// thinning does not apply to them.
	int32 DesiredMoteCount = Motes.Num();
	if (bScaleDensityWithNight && !bLeaf)
	{
		float NightAmount = 0.0f;
		if (const AMemoryRoomSkyDomeActor* SkyDome = ResolveSkyDome())
		{
			// PhotoBrightness runs 1.0 in daylight up to ~2.6 at night.
			NightAmount = FMath::Clamp((SkyDome->GetPhotoBrightness() - 1.0f) / 1.6f, 0.0f, 1.0f);
		}
		const float DensityFraction = FMath::Lerp(FMath::Clamp(DaytimeDensityFraction, 0.0f, 1.0f), 1.0f, NightAmount);
		DesiredMoteCount = FMath::RoundToInt(Motes.Num() * DensityFraction);
	}

	// The settings-panel density level (Low/Medium/Full) scales the whole field on top.
	const float LevelScale = FMath::Clamp(DensityLevel, 0, 3) / 3.0f;
	DesiredMoteCount = FMath::RoundToInt(DesiredMoteCount * LevelScale);

	const float Time = World->GetTimeSeconds();
	const float Radius = FMath::Max(FieldRadiusCm, 100.0f);
	const float Drift = FMath::Max(DriftSpeedCmPerSecond, 0.0f);
	const int32 GroupCount = FMath::Clamp(UpdateGroupCount, 1, 8);
	UpdateGroupOffset = (UpdateGroupOffset + 1) % GroupCount;

	for (int32 Index = UpdateGroupOffset; Index < Motes.Num(); Index += GroupCount)
	{
		const FMote& Mote = Motes[Index];

		FVector Position = Mote.BasePosition;
		FQuat InstanceRotation = FQuat::Identity;
		if (bLeaf)
		{
			// Constant push along the wind direction (not a bounded oscillation like Dust
			// below) so leaves visibly stream across the field and wrap around, plus a
			// perpendicular flutter so they do not travel in a dead-straight line.
			const FVector2D WindDir = WindDirection.IsNearlyZero() ? FVector2D(1.0f, 0.0f) : WindDirection.GetSafeNormal();
			const FVector2D WindPerp(-WindDir.Y, WindDir.X);
			const float WindStrength = FMath::Max(WindStrengthCmPerSecond, 0.0f);
			const float Sway = FMath::Max(SwayAmountCm, 0.0f);
			const float SwayOffset = Sway * FMath::Sin(Time * Mote.Rate + Mote.Phase);
			Position.X += WindDir.X * WindStrength * Time + WindPerp.X * SwayOffset;
			Position.Y += WindDir.Y * WindStrength * Time + WindPerp.Y * SwayOffset;
			// Gentle vertical bob, like a leaf riding little eddies rather than falling straight.
			Position.Z += FMath::Sin(Time * Mote.Rate * 0.6f + Mote.Phase * 1.3f) * Sway * 0.3f;

			InstanceRotation = FQuat(Mote.DriftAxis, FMath::DegreesToRadians(Mote.TumbleRateDegPerSec) * Time + Mote.Phase);
		}
		else
		{
			// Drift along the mote's own axis, plus a slow bob so the field never looks like it
			// is sliding as one block.
			Position += Mote.DriftAxis * (FMath::Sin(Time * Mote.Rate * 0.35f + Mote.Phase) * Drift * 6.0f);
			Position.Z += FMath::Sin(Time * Mote.Rate * 0.5f + Mote.Phase * 1.3f) * Drift;
		}

		// Wrap inside the box so the field travels with the player without respawning.
		// FVector components are double in UE5, so the bounds have to match.
		const double HorizontalBound = static_cast<double>(Radius);
		const double LowerBound = -static_cast<double>(Radius) * 0.55;
		const double UpperBound = static_cast<double>(Radius) * 0.9;
		Position.X = FMath::Wrap(Position.X, -HorizontalBound, HorizontalBound);
		Position.Y = FMath::Wrap(Position.Y, -HorizontalBound, HorizontalBound);
		Position.Z = FMath::Wrap(Position.Z, LowerBound, UpperBound);

		const bool bActive = Index < DesiredMoteCount;
		// 1.0 +/- TwinkleAmount, on the mote's own decorrelated cycle - see the TwinklePhase
		// comment on FMote for why this does not just reuse Phase/Rate. Leaves do not twinkle.
		const float Twinkle = bLeaf ? 1.0f : (1.0f + TwinkleAmount * FMath::Sin(Time * Mote.TwinkleRate + Mote.TwinklePhase));
		const float SizeCm = bLeaf ? LeafSizeCm : MoteSizeCm;
		const float MoteRadiusCm = FMath::Max(SizeCm, 0.1f) * Mote.Scale * FMath::Max(Twinkle, 0.0f);
		// Culled motes are scaled to nothing rather than removed, so the instance buffer
		// never has to be rebuilt.
		const FVector InstanceScale = bActive ? FVector(MoteRadiusCm / 100.0f) : FVector::ZeroVector;

		MoteMeshes->UpdateInstanceTransform(Index, FTransform(InstanceRotation, FieldCenter + Position, InstanceScale), true, false, true);
		if (bLeaf)
		{
			// Back-to-back partner plane: same spot, flipped 180 degrees about its local X axis.
			const FQuat FlippedRotation = InstanceRotation * FQuat(FVector::XAxisVector, PI);
			MoteMeshes->UpdateInstanceTransform(Index + Motes.Num(), FTransform(FlippedRotation, FieldCenter + Position, InstanceScale), true, false, true);
		}
	}

	MoteMeshes->MarkRenderStateDirty();
}

AMemoryRoomDustActor* AMemoryRoomDustActor::FindOrSpawn(UWorld* World, TSubclassOf<AMemoryRoomDustActor> DustClass)
{
	if (!World)
	{
		return nullptr;
	}

	for (TActorIterator<AMemoryRoomDustActor> It(World); It;)
	{
		return *It;
	}

	UClass* ClassToSpawn = DustClass ? DustClass.Get() : AMemoryRoomDustActor::StaticClass();
	return World->SpawnActor<AMemoryRoomDustActor>(ClassToSpawn, FTransform::Identity);
}
