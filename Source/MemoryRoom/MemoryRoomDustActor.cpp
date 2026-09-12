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
		MoteMeshes->SetStaticMesh(SphereMesh.Object);
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

	const int32 SafeCount = FMath::Clamp(MoteCount, 0, 2000);
	if (SafeCount <= 0)
	{
		return;
	}

	Motes.Reserve(SafeCount);
	FRandomStream Random(DustSeed);
	const float Radius = FMath::Max(FieldRadiusCm, 100.0f);

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
		Motes.Add(Mote);

		const float MoteRadiusCm = FMath::Max(MoteSizeCm, 0.1f) * Mote.Scale;
		// The engine sphere is 100cm across, hence the /100.
		InstanceTransforms.Emplace(FQuat::Identity, Mote.BasePosition, FVector(MoteRadiusCm / 100.0f));
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
			MoteMaterialInstance->SetVectorParameterValue(ParameterName, DustTint);
		}
	}
}

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

	// Thin the field out when the room is lit; motes are a night effect and would just be
	// visual noise against a bright sky.
	int32 DesiredMoteCount = Motes.Num();
	if (bScaleDensityWithNight)
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
		// Drift along the mote's own axis, plus a slow bob so the field never looks like it
		// is sliding as one block.
		Position += Mote.DriftAxis * (FMath::Sin(Time * Mote.Rate * 0.35f + Mote.Phase) * Drift * 6.0f);
		Position.Z += FMath::Sin(Time * Mote.Rate * 0.5f + Mote.Phase * 1.3f) * Drift;

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
		// comment on FMote for why this does not just reuse Phase/Rate.
		const float Twinkle = 1.0f + TwinkleAmount * FMath::Sin(Time * Mote.TwinkleRate + Mote.TwinklePhase);
		const float MoteRadiusCm = FMath::Max(MoteSizeCm, 0.1f) * Mote.Scale * FMath::Max(Twinkle, 0.0f);
		// Culled motes are scaled to nothing rather than removed, so the instance buffer
		// never has to be rebuilt.
		const FVector InstanceScale = bActive ? FVector(MoteRadiusCm / 100.0f) : FVector::ZeroVector;

		MoteMeshes->UpdateInstanceTransform(Index, FTransform(FQuat::Identity, FieldCenter + Position, InstanceScale), true, false, true);
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
