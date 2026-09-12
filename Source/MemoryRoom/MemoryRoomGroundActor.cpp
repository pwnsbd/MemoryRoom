// Fill out your copyright notice in the Description page of Project Settings.

#include "MemoryRoomGroundActor.h"
#include "MemoryRoom.h"
#include "AMemoryRoomManager.h"

#include "CollisionQueryParams.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/HitResult.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "Materials/MaterialInterface.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
// Asset paths from the installed "Stylized Log Cabin" pack (Content/StylizedlogCabin_A1).
// Trees are hand-painted foliage in the map and some sit right on top of the photo
// spiral. On BeginPlay we delete any painted foliage instance whose horizontal distance
// from the room centre is under this, leaving a clean clearing with the trees ringed
// around the outside. The photo spiral reaches ~620 cm, so this clears well past it.
// Bump it up if a tree is still too close; drop it if the ring pulls back too far.
constexpr float CentreFoliageClearRadiusCm = 900.0f;

// The pine variants (Stylized Log Cabin pack) used for the scattered forest.
const TCHAR* PineMeshPaths[] = {
	TEXT("/Game/StylizedlogCabin_A1/Landscape/Vegetation/SM_Pine_A1.SM_Pine_A1"),
	TEXT("/Game/StylizedlogCabin_A1/Landscape/Vegetation/SM_Pine_A2.SM_Pine_A2"),
	TEXT("/Game/StylizedlogCabin_A1/Landscape/Vegetation/SM_Pine_A3.SM_Pine_A3"),
};

// Big rocks scaled way up to fake a distant mountain range.
const TCHAR* RockMeshPaths[] = {
	TEXT("/Game/StylizedlogCabin_A1/Landscape/Rocks/SM_Rock_A1.SM_Rock_A1"),
	TEXT("/Game/StylizedlogCabin_A1/Landscape/Rocks/SM_Rock_A2.SM_Rock_A2"),
	TEXT("/Game/StylizedlogCabin_A1/Landscape/Rocks/SM_Rock_A3.SM_Rock_A3"),
};

// Three leaf materials mixed across the scattered forest for colour variety.
const TCHAR* PineLeafMaterialPaths[] = {
	TEXT("/Game/StylizedlogCabin_A1/Landscape/Vegetation/Materials/MI_PineLeaf_A1.MI_PineLeaf_A1"),
	TEXT("/Game/StylizedlogCabin_A1/Landscape/Vegetation/Materials/MI_PineLeaf_A3.MI_PineLeaf_A3"),
	TEXT("/Game/StylizedlogCabin_A1/Landscape/Vegetation/Materials/MI_PineLeaf_A4.MI_PineLeaf_A4"),
};

// Reads scale range + how far the hand-painted pine foliage reaches from Centre, so the
// scattered forest can be built to match before the InstancedFoliageActor is deleted.
struct FPaintedPineStats
{
	bool bFound = false;
	float MinScale = 0.0f;
	float MaxScale = 0.0f;
	float MaxRadiusCm = 0.0f;
};

FPaintedPineStats MeasurePaintedPines(UWorld* World, const FVector& Centre)
{
	FPaintedPineStats Stats;
	if (!World)
	{
		return Stats;
	}
	float MinScale = TNumericLimits<float>::Max();
	float MaxScale = 0.0f;
	float MaxRadius = 0.0f;
	int32 Count = 0;

	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		if (!Actor || Actor->GetClass()->GetName() != TEXT("InstancedFoliageActor"))
		{
			continue;
		}
		TArray<UInstancedStaticMeshComponent*> Components;
		Actor->GetComponents(Components);
		for (UInstancedStaticMeshComponent* Component : Components)
		{
			const UStaticMesh* Mesh = Component ? Component->GetStaticMesh() : nullptr;
			if (!Mesh || !Mesh->GetName().Contains(TEXT("Pine")))
			{
				continue;
			}
			const int32 N = Component->GetInstanceCount();
			for (int32 i = 0; i < N; ++i)
			{
				FTransform T;
				if (!Component->GetInstanceTransform(i, T, /*bWorldSpace*/ true))
				{
					continue;
				}
				const float S = T.GetScale3D().GetAbsMax();
				MinScale = FMath::Min(MinScale, S);
				MaxScale = FMath::Max(MaxScale, S);
				const FVector O = T.GetLocation() - Centre;
				MaxRadius = FMath::Max(MaxRadius, FMath::Sqrt(O.X * O.X + O.Y * O.Y));
				++Count;
			}
		}
	}

	if (Count > 0)
	{
		Stats.bFound = true;
		Stats.MinScale = MinScale;
		Stats.MaxScale = MaxScale;
		Stats.MaxRadiusCm = MaxRadius;
	}
	UE_LOG(LogMemoryRoom, Warning, TEXT("MeasurePaintedPines: %d instance(s), scale %.2f-%.2f, reach %.0f cm."),
		Count, Count > 0 ? MinScale : 0.0f, MaxScale, MaxRadius);
	return Stats;
}

const TCHAR* GrassMaterialPath = TEXT("/Game/StylizedlogCabin_A1/Landscape/Materials/MI_Grass_A1.MI_Grass_A1");
const TCHAR* GrassMeshPaths[] = {
	TEXT("/Game/StylizedlogCabin_A1/Landscape/Vegetation/SM_Grass_A1.SM_Grass_A1"),
	TEXT("/Game/StylizedlogCabin_A1/Landscape/Vegetation/SM_Grass_A2.SM_Grass_A2"),
	TEXT("/Game/StylizedlogCabin_A1/Landscape/Vegetation/SM_Grass_A3.SM_Grass_A3"),
	TEXT("/Game/StylizedlogCabin_A1/Landscape/Vegetation/SM_Grass_A4.SM_Grass_A4"),
};

// Reshapes the hand-painted PINE foliage around Centre WITHOUT deleting the forest:
//  - compresses every pine radially so the farthest one sits at MaxRadiusCm (keeps the
//    forest's shape and clumping, just pulls it in to fit inside the dome),
//  - then removes only the handful that land within CentreClearCm of the room centre so
//    none stand on the photo spiral.
// Only touches foliage whose mesh name contains "Pine"; grass/bushes are left alone.
// Finds the level's InstancedFoliageActor by class name so this file needs no Foliage module.
void ReshapePaintedPines(UWorld* World, const FVector& Centre, float CentreClearCm, float MaxRadiusCm)
{
	if (!World)
	{
		return;
	}

	TArray<UInstancedStaticMeshComponent*> PineComponents;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		if (!Actor || Actor->GetClass()->GetName() != TEXT("InstancedFoliageActor"))
		{
			continue;
		}
		TArray<UInstancedStaticMeshComponent*> Components;
		Actor->GetComponents(Components);
		for (UInstancedStaticMeshComponent* Component : Components)
		{
			const UStaticMesh* Mesh = Component ? Component->GetStaticMesh() : nullptr;
			if (Mesh && Mesh->GetName().Contains(TEXT("Pine")))
			{
				PineComponents.Add(Component);
			}
		}
	}

	// Pass 1: how far out does the painted forest currently reach?
	float MaxRadius = 0.0f;
	for (UInstancedStaticMeshComponent* Component : PineComponents)
	{
		const int32 Count = Component->GetInstanceCount();
		for (int32 Index = 0; Index < Count; ++Index)
		{
			FTransform T;
			if (Component->GetInstanceTransform(Index, T, /*bWorldSpace*/ true))
			{
				const FVector O = T.GetLocation() - Centre;
				MaxRadius = FMath::Max(MaxRadius, FMath::Sqrt(O.X * O.X + O.Y * O.Y));
			}
		}
	}

	const float Compression = (MaxRadiusCm > 0.0f && MaxRadius > MaxRadiusCm) ? (MaxRadiusCm / MaxRadius) : 1.0f;
	const float ClearSq = CentreClearCm * CentreClearCm;
	int32 Moved = 0;
	int32 Removed = 0;

	// Pass 2: compress each pine inward, drop the ones that end up on the photos.
	for (UInstancedStaticMeshComponent* Component : PineComponents)
	{
		TArray<int32> ToRemove;
		const int32 Count = Component->GetInstanceCount();
		for (int32 Index = 0; Index < Count; ++Index)
		{
			FTransform T;
			if (!Component->GetInstanceTransform(Index, T, /*bWorldSpace*/ true))
			{
				continue;
			}
			const FVector Loc = T.GetLocation();
			FVector Offset = Loc - Centre;
			const FVector2D Flat(Offset.X * Compression, Offset.Y * Compression);
			if (Flat.SizeSquared() < ClearSq)
			{
				ToRemove.Add(Index);
				continue;
			}
			if (Compression < 1.0f)
			{
				T.SetLocation(FVector(Centre.X + Flat.X, Centre.Y + Flat.Y, Loc.Z));
				Component->UpdateInstanceTransform(Index, T, /*bWorldSpace*/ true, /*bMarkRenderStateDirty*/ false, /*bTeleport*/ true);
				++Moved;
			}
		}
		if (ToRemove.Num() > 0)
		{
			Component->RemoveInstances(ToRemove);
			Removed += ToRemove.Num();
		}
		Component->MarkRenderStateDirty();
	}

	UE_LOG(LogMemoryRoom, Warning, TEXT("ReshapePaintedPines: forest reached %.0f cm, compressed x%.2f (moved %d), removed %d from the centre."),
		MaxRadius, Compression, Moved, Removed);
}

}

AMemoryRoomGroundActor::AMemoryRoomGroundActor()
{
	PrimaryActorTick.bCanEverTick = false;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);

	static ConstructorHelpers::FObjectFinder<UMaterialInterface> GrassMaterialFinder(GrassMaterialPath);
	if (GrassMaterialFinder.Succeeded())
	{
		GrassFloorMaterial = GrassMaterialFinder.Object;
	}
	else
	{
		UE_LOG(LogMemoryRoom, Warning, TEXT("AMemoryRoomGroundActor could not find %s - the floor keeps its own material."), GrassMaterialPath);
	}

	// One separately-named static ConstructorHelpers::FObjectFinder per asset - a `static`
	// declared inside a loop only ever runs its lookup once, on the first iteration, then keeps
	// returning that same cached result for every later index, so each asset needs its own.
	static ConstructorHelpers::FObjectFinder<UStaticMesh> GrassFinder0(GrassMeshPaths[0]);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> GrassFinder1(GrassMeshPaths[1]);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> GrassFinder2(GrassMeshPaths[2]);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> GrassFinder3(GrassMeshPaths[3]);
	UStaticMesh* GrassResolvedMeshes[] = {
		GrassFinder0.Succeeded() ? GrassFinder0.Object : nullptr,
		GrassFinder1.Succeeded() ? GrassFinder1.Object : nullptr,
		GrassFinder2.Succeeded() ? GrassFinder2.Object : nullptr,
		GrassFinder3.Succeeded() ? GrassFinder3.Object : nullptr,
	};

	for (int32 Index = 0; Index < UE_ARRAY_COUNT(GrassResolvedMeshes); ++Index)
	{
		if (!GrassResolvedMeshes[Index])
		{
			UE_LOG(LogMemoryRoom, Warning, TEXT("AMemoryRoomGroundActor could not find %s - that grass variant will be skipped."), GrassMeshPaths[Index]);
			continue;
		}
		UInstancedStaticMeshComponent* Component = CreateDefaultSubobject<UInstancedStaticMeshComponent>(*FString::Printf(TEXT("GrassMeshes_%d"), Index));
		Component->SetupAttachment(SceneRoot);
		Component->SetStaticMesh(GrassResolvedMeshes[Index]);
		Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Component->SetCastShadow(false);
		Component->bCastDynamicShadow = false;
		// Cull distant grass - clumps 25 m+ away are barely visible but still cost triangles
		// and overdraw every frame. Fades in the last 6 m rather than popping.
		Component->SetCullDistances(1900, 2500);
		GrassMeshes.Add(Component);
	}

	// Tree meshes. ConstructorHelpers (not runtime LoadObject) so the pines survive a cook
	// even though nothing else in the level hard-references the Stylized pack directly.
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Pine0(PineMeshPaths[0]);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Pine1(PineMeshPaths[1]);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Pine2(PineMeshPaths[2]);
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> Leaf0(PineLeafMaterialPaths[0]);
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> Leaf1(PineLeafMaterialPaths[1]);
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> Leaf2(PineLeafMaterialPaths[2]);
	TreeLeafMaterials.Add(Leaf0.Succeeded() ? Leaf0.Object : nullptr);
	TreeLeafMaterials.Add(Leaf1.Succeeded() ? Leaf1.Object : nullptr);
	TreeLeafMaterials.Add(Leaf2.Succeeded() ? Leaf2.Object : nullptr);

	UStaticMesh* PineResolved[] = {
		Pine0.Succeeded() ? Pine0.Object : nullptr,
		Pine1.Succeeded() ? Pine1.Object : nullptr,
		Pine2.Succeeded() ? Pine2.Object : nullptr,
	};
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(PineResolved); ++Index)
	{
		if (!PineResolved[Index])
		{
			UE_LOG(LogMemoryRoom, Warning, TEXT("AMemoryRoomGroundActor could not find %s - that pine variant will be skipped."), PineMeshPaths[Index]);
			continue;
		}
		UHierarchicalInstancedStaticMeshComponent* Component = CreateDefaultSubobject<UHierarchicalInstancedStaticMeshComponent>(*FString::Printf(TEXT("TreeMeshes_%d"), Index));
		Component->SetupAttachment(SceneRoot);
		Component->SetStaticMesh(PineResolved[Index]);
		Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Component->SetCastShadow(false);
		Component->bCastDynamicShadow = false;
		// Past 30 m, freeze any leaf-wind vertex animation - imperceptible at range, and it
		// otherwise blocks occlusion culling and costs vertex work for the whole forest.
		Component->SetWorldPositionOffsetDisableDistance(3000);

		// Give each pine variant its own leaf material (A1 / A3 / A4) so the scattered forest
		// mixes three leaf colours. Replaces any slot whose current material name has "Leaf".
		if (TreeLeafMaterials.IsValidIndex(Index) && TreeLeafMaterials[Index])
		{
			const int32 SlotCount = Component->GetNumMaterials();
			for (int32 Slot = 0; Slot < SlotCount; ++Slot)
			{
				const UMaterialInterface* Existing = Component->GetMaterial(Slot);
				if (Existing && Existing->GetName().Contains(TEXT("Leaf")))
				{
					Component->SetMaterial(Slot, TreeLeafMaterials[Index]);
				}
			}
		}
		TreeMeshes.Add(Component);
	}

	// Rock "mountain" meshes.
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Rock0(RockMeshPaths[0]);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Rock1(RockMeshPaths[1]);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Rock2(RockMeshPaths[2]);
	UStaticMesh* RockResolved[] = {
		Rock0.Succeeded() ? Rock0.Object : nullptr,
		Rock1.Succeeded() ? Rock1.Object : nullptr,
		Rock2.Succeeded() ? Rock2.Object : nullptr,
	};
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(RockResolved); ++Index)
	{
		if (!RockResolved[Index])
		{
			UE_LOG(LogMemoryRoom, Warning, TEXT("AMemoryRoomGroundActor could not find %s - that rock variant will be skipped."), RockMeshPaths[Index]);
			continue;
		}
		UHierarchicalInstancedStaticMeshComponent* Component = CreateDefaultSubobject<UHierarchicalInstancedStaticMeshComponent>(*FString::Printf(TEXT("RockMeshes_%d"), Index));
		Component->SetupAttachment(SceneRoot);
		Component->SetStaticMesh(RockResolved[Index]);
		Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Component->SetCastShadow(false);
		Component->bCastDynamicShadow = false;
		Component->SetBoundsScale(2.0f);
		RockMeshes.Add(Component);
	}
}

void AMemoryRoomGroundActor::BeginPlay()
{
	Super::BeginPlay();
	DressExistingFloor();
}

void AMemoryRoomGroundActor::DressExistingFloor()
{
	UE_LOG(LogMemoryRoom, Warning, TEXT("AMemoryRoomGroundActor::DressExistingFloor running (build with centre-foliage clearing)."));

	UWorld* World = GetWorld();
	const APlayerController* PlayerController = World ? World->GetFirstPlayerController() : nullptr;
	if (!World || !PlayerController)
	{
		return;
	}

	FVector ViewLocation = FVector::ZeroVector;
	FRotator ViewRotation = FRotator::ZeroRotator;
	PlayerController->GetPlayerViewPoint(ViewLocation, ViewRotation);

	FCollisionObjectQueryParams ObjectParams;
	ObjectParams.AddObjectTypesToQuery(ECC_WorldStatic);
	ObjectParams.AddObjectTypesToQuery(ECC_WorldDynamic);
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(MemoryRoomFloorDress), /*bTraceComplex*/ true);
	if (const APawn* Pawn = PlayerController->GetPawn())
	{
		QueryParams.AddIgnoredActor(Pawn);
	}

	const float TraceDistance = FMath::Max(FloorTraceDistanceCm, 100.0f);
	FHitResult FloorHit;
	const bool bHasFloor = World->LineTraceSingleByObjectType(FloorHit, ViewLocation, ViewLocation - FVector(0.0f, 0.0f, TraceDistance), ObjectParams, QueryParams);
	UStaticMeshComponent* FloorMesh = bHasFloor ? Cast<UStaticMeshComponent>(FloorHit.GetComponent()) : nullptr;

	// The room centre = the MemoryRoomManager's placed location (where the player is anchored),
	// with the floor height from the trace if we got one, otherwise the player's own feet.
	FVector RoomCentre(ViewLocation.X, ViewLocation.Y, bHasFloor ? FloorHit.Location.Z : ViewLocation.Z - 150.0f);
	if (const APawn* Pawn = PlayerController->GetPawn())
	{
		RoomCentre.Z = bHasFloor ? FloorHit.Location.Z : Pawn->GetActorLocation().Z;
	}
	for (TActorIterator<AMemoryRoomManager> It(World); It;)
	{
		RoomCentre.X = It->GetActorLocation().X;
		RoomCentre.Y = It->GetActorLocation().Y;
		break;
	}

	// Trees FIRST, independent of whether the floor mesh was found.
	// 1) Measure the painted forest BEFORE anything moves it, so the scattered forest can
	//    match its size and spread (and still work once the painted foliage is deleted).
	const FPaintedPineStats PaintedStats = MeasurePaintedPines(World, RoomCentre);

	// 2) Keep the painted forest for now, compressed inward to clear the photos.
	ReshapePaintedPines(World, RoomCentre, CentreFoliageClearRadiusCm, PaintedTreeMaxRadiusCm);

	// 3) Scatter the code forest across the map, sized/spread to the measured painted one,
	//    with a clear middle for the user + photos.
	if (bScatterTreeRing && TreeMeshes.Num() > 0 && TreeCount > 0)
	{
		const float OuterCm = PaintedStats.bFound && PaintedStats.MaxRadiusCm > TreeRingInnerRadiusCm ? PaintedStats.MaxRadiusCm : TreeRingOuterRadiusCm;
		const float MinS = PaintedStats.bFound ? PaintedStats.MinScale : -1.0f;
		const float MaxS = PaintedStats.bFound ? PaintedStats.MaxScale : -1.0f;
		ScatterTreeRing(World, RoomCentre, ObjectParams, QueryParams, OuterCm, MinS, MaxS);
	}

	// 4) Fake mountain range from big rocks, far out behind the trees.
	if (bScatterRockMountains && RockMeshes.Num() > 0 && RockMountainCount > 0)
	{
		ScatterRockMountains(World, RoomCentre, ObjectParams, QueryParams);
	}

	// Everything below needs the actual floor mesh (material swap + grass scatter across its
	// bounds). If the trace missed, we are done - the trees are the important part.
	if (!FloorMesh)
	{
		if (!bHasFloor)
		{
			UE_LOG(LogMemoryRoom, Warning, TEXT("%s found no floor below the player - scattered trees only, skipped grass."), *GetName());
		}
		return;
	}

	// Authored meadow levels supply their own correctly tiled floor material.
	// Keep the runtime fallback for existing levels that still need dressing.
	if (GrassFloorMaterial && !FloorMesh->ComponentHasTag(TEXT("MemoryRoomAuthoredGround")))
	{
		FloorMesh->SetMaterial(FloorHit.ElementIndex, GrassFloorMaterial);
	}

	const FBoxSphereBounds FloorBounds = FloorMesh->Bounds;

	if (GrassMeshes.Num() == 0 || GrassCount <= 0)
	{
		return;
	}

	// A small margin so grass does not overhang the floor's actual edge.
	const float ScatterRadius = FMath::Max(FMath::Min(FloorBounds.BoxExtent.X, FloorBounds.BoxExtent.Y) - 30.0f, 10.0f);
	const float SafeClear = FMath::Clamp(GrassClearRadiusCm, 0.0f, ScatterRadius);
	const float SafeMinScale = FMath::Max(GrassMinScale, 0.1f);
	const float SafeMaxScale = FMath::Max(GrassMaxScale, SafeMinScale);

	TArray<TArray<FTransform>> InstanceTransformsPerVariant;
	InstanceTransformsPerVariant.SetNum(GrassMeshes.Num());

	FRandomStream Random(ScatterSeed);
	for (int32 Index = 0; Index < GrassCount; ++Index)
	{
		const float Angle = Random.FRandRange(0.0f, 2.0f * PI);
		// Sampling distance^2 uniformly (not distance itself) keeps density even across the
		// disc instead of bunching up toward the clear-radius edge in the middle.
		const float Dist = FMath::Sqrt(Random.FRandRange(SafeClear * SafeClear, ScatterRadius * ScatterRadius));
		const float WorldX = FloorBounds.Origin.X + Dist * FMath::Cos(Angle);
		const float WorldY = FloorBounds.Origin.Y + Dist * FMath::Sin(Angle);

		// Traced individually rather than assumed flat - a per-instance height still costs
		// nothing worth worrying about for a few hundred one-time traces at BeginPlay, and
		// keeps this correct even if the floor turns out not to be a perfectly flat slab.
		FHitResult GrassHit;
		const FVector TraceStart(WorldX, WorldY, FloorBounds.Origin.Z + FloorBounds.BoxExtent.Z + 200.0f);
		const FVector TraceEnd(WorldX, WorldY, FloorBounds.Origin.Z - FloorBounds.BoxExtent.Z - 200.0f);
		const float WorldZ = World->LineTraceSingleByObjectType(GrassHit, TraceStart, TraceEnd, ObjectParams, QueryParams)
			? GrassHit.Location.Z
			: FloorBounds.Origin.Z + FloorBounds.BoxExtent.Z;

		const FVector Location(WorldX, WorldY, WorldZ);
		const FRotator Rotation(0.0f, Random.FRandRange(0.0f, 360.0f), 0.0f);
		const float Scale = Random.FRandRange(SafeMinScale, SafeMaxScale);

		const int32 VariantIndex = Random.RandRange(0, GrassMeshes.Num() - 1);
		// AddInstances below takes world-space transforms directly (bWorldSpace=true) - the
		// grass components are attached to this actor's own SceneRoot, but the floor being
		// dressed is a separate, independently-placed actor, so component-relative coordinates
		// would not line up with it.
		InstanceTransformsPerVariant[VariantIndex].Emplace(Rotation, Location, FVector(Scale));
	}

	for (int32 VariantIndex = 0; VariantIndex < GrassMeshes.Num(); ++VariantIndex)
	{
		if (UInstancedStaticMeshComponent* Component = GrassMeshes[VariantIndex])
		{
			Component->AddInstances(InstanceTransformsPerVariant[VariantIndex], false, /*bWorldSpace*/ true);
		}
	}
}

void AMemoryRoomGroundActor::ScatterTreeRing(UWorld* World, const FVector& Centre, const FCollisionObjectQueryParams& ObjectParams, const FCollisionQueryParams& QueryParams,
	float OuterRadiusCm, float MinScale, float MaxScale)
{
	if (!World || TreeMeshes.Num() == 0)
	{
		return;
	}

	const float InnerRadius = FMath::Max(TreeRingInnerRadiusCm, 0.0f);
	const float OuterRadius = FMath::Max(OuterRadiusCm > 0.0f ? OuterRadiusCm : TreeRingOuterRadiusCm, InnerRadius + 1.0f);
	const float SafeMinScale = FMath::Max(MinScale > 0.0f ? MinScale : TreeMinScale, 0.05f);
	const float SafeMaxScale = FMath::Max(MaxScale > 0.0f ? MaxScale : TreeMaxScale, SafeMinScale);

	// Honour a user-set leaf material override (the constructor only sees the class default).
	if (TreeLeafMaterialOverride)
	{
		for (UHierarchicalInstancedStaticMeshComponent* Component : TreeMeshes)
		{
			if (!Component)
			{
				continue;
			}
			const int32 SlotCount = Component->GetNumMaterials();
			for (int32 Slot = 0; Slot < SlotCount; ++Slot)
			{
				const UMaterialInterface* Existing = Component->GetMaterial(Slot);
				if (Existing && Existing->GetName().Contains(TEXT("Leaf")))
				{
					Component->SetMaterial(Slot, TreeLeafMaterialOverride);
				}
			}
		}
	}

	TArray<TArray<FTransform>> PineTransforms;
	PineTransforms.SetNum(TreeMeshes.Num());

	FRandomStream Random(ScatterSeed ^ 0x7EE5);
	for (int32 Index = 0; Index < TreeCount; ++Index)
	{
		const float Angle = Random.FRandRange(0.0f, 2.0f * PI);
		// distance^2 uniform keeps the density even across the annulus.
		const float Dist = FMath::Sqrt(Random.FRandRange(InnerRadius * InnerRadius, OuterRadius * OuterRadius));
		const float WorldX = Centre.X + Dist * FMath::Cos(Angle);
		const float WorldY = Centre.Y + Dist * FMath::Sin(Angle);

		// Trace onto whatever surface is out there (floor, terrain, prop); fall back to the
		// room floor height so a tree never floats or vanishes if nothing is hit.
		FHitResult GroundHit;
		const FVector TraceStart(WorldX, WorldY, Centre.Z + 6000.0f);
		const FVector TraceEnd(WorldX, WorldY, Centre.Z - 6000.0f);
		const float WorldZ = World->LineTraceSingleByObjectType(GroundHit, TraceStart, TraceEnd, ObjectParams, QueryParams)
			? GroundHit.Location.Z
			: Centre.Z;

		const FRotator Rotation(0.0f, Random.FRandRange(0.0f, 360.0f), 0.0f);
		const float Scale = Random.FRandRange(SafeMinScale, SafeMaxScale);

		PineTransforms[Random.RandRange(0, TreeMeshes.Num() - 1)].Emplace(Rotation, FVector(WorldX, WorldY, WorldZ), FVector(Scale));
	}

	int32 Placed = 0;
	for (int32 VariantIndex = 0; VariantIndex < TreeMeshes.Num(); ++VariantIndex)
	{
		UHierarchicalInstancedStaticMeshComponent* Component = TreeMeshes[VariantIndex];
		if (Component && PineTransforms[VariantIndex].Num() > 0)
		{
			Component->SetCastShadow(bTreesCastShadow);
			Component->AddInstances(PineTransforms[VariantIndex], false, /*bWorldSpace*/ true);
			Placed += PineTransforms[VariantIndex].Num();
		}
	}

	UE_LOG(LogMemoryRoom, Warning, TEXT("ScatterTreeRing: placed %d pine(s) in a %.0f-%.0f cm ring around (%.0f, %.0f)."),
		Placed, InnerRadius, OuterRadius, Centre.X, Centre.Y);
}

void AMemoryRoomGroundActor::ScatterRockMountains(UWorld* World, const FVector& Centre, const FCollisionObjectQueryParams& ObjectParams, const FCollisionQueryParams& QueryParams)
{
	if (!World || RockMeshes.Num() == 0 || RockMountainCount <= 0)
	{
		return;
	}

	const float Radius = FMath::Max(RockMountainRadiusCm, 1.0f);
	const float Jitter = FMath::Clamp(RockMountainRadiusJitterCm, 0.0f, Radius * 0.5f);
	const float HeightMin = FMath::Max(RockMountainHeightMinCm, 100.0f);
	const float HeightMax = FMath::Max(RockMountainHeightMaxCm, HeightMin);

	// Rock mesh heights (full, cm) so we can scale each to an absolute target height.
	float MeshFullHeight[3] = { 200.0f, 200.0f, 200.0f };
	for (int32 V = 0; V < RockMeshes.Num() && V < 3; ++V)
	{
		if (const UStaticMesh* Mesh = RockMeshes[V] ? RockMeshes[V]->GetStaticMesh() : nullptr)
		{
			MeshFullHeight[V] = FMath::Max(Mesh->GetBounds().BoxExtent.Z * 2.0f, 1.0f);
		}
	}

	TArray<TArray<FTransform>> Transforms;
	Transforms.SetNum(RockMeshes.Num());

	FRandomStream Random(ScatterSeed ^ 0x30CC);
	for (int32 Index = 0; Index < RockMountainCount; ++Index)
	{
		// Even-ish angular spacing plus a nudge, so the range wraps the gallery with no gap.
		const float Angle = (static_cast<float>(Index) + Random.FRandRange(-0.4f, 0.4f)) / static_cast<float>(RockMountainCount) * 2.0f * PI;
		const float R = Radius + Random.FRandRange(-Jitter, Jitter);
		const float WorldX = Centre.X + R * FMath::Cos(Angle);
		const float WorldY = Centre.Y + R * FMath::Sin(Angle);

		FHitResult GroundHit;
		const float GroundZ = World->LineTraceSingleByObjectType(GroundHit, FVector(WorldX, WorldY, Centre.Z + 60000.0f), FVector(WorldX, WorldY, Centre.Z - 60000.0f), ObjectParams, QueryParams)
			? GroundHit.Location.Z
			: Centre.Z;

		const int32 Variant = Random.RandRange(0, RockMeshes.Num() - 1);
		const float TargetHeight = Random.FRandRange(HeightMin, HeightMax);
		const float ScaleZ = TargetHeight / MeshFullHeight[Variant];
		// Roughly as wide as tall, give or take, so it reads as a peak not a spike or a slab.
		const float ScaleXY = ScaleZ * Random.FRandRange(0.8f, 1.5f);
		const FVector Scale(ScaleXY, ScaleXY * Random.FRandRange(0.75f, 1.25f), ScaleZ);
		const FRotator Rotation(Random.FRandRange(-8.0f, 8.0f), Random.FRandRange(0.0f, 360.0f), Random.FRandRange(-8.0f, 8.0f));

		const float SunkZ = GroundZ - RockMountainSinkFraction * TargetHeight;
		Transforms[Variant].Emplace(Rotation, FVector(WorldX, WorldY, SunkZ), Scale);
	}

	int32 Placed = 0;
	for (int32 Variant = 0; Variant < RockMeshes.Num(); ++Variant)
	{
		if (RockMeshes[Variant] && Transforms[Variant].Num() > 0)
		{
			RockMeshes[Variant]->AddInstances(Transforms[Variant], false, /*bWorldSpace*/ true);
			Placed += Transforms[Variant].Num();
		}
	}
	UE_LOG(LogMemoryRoom, Warning, TEXT("ScatterRockMountains: placed %d rock(s) at %.0f+-%.0f cm, target height %.0f-%.0f cm (rock mesh heights %.0f/%.0f/%.0f)."),
		Placed, Radius, Jitter, HeightMin, HeightMax, MeshFullHeight[0], MeshFullHeight[1], MeshFullHeight[2]);
}

AMemoryRoomGroundActor* AMemoryRoomGroundActor::FindOrSpawn(UWorld* World, TSubclassOf<AMemoryRoomGroundActor> GroundClass)
{
	if (!World)
	{
		return nullptr;
	}

	for (TActorIterator<AMemoryRoomGroundActor> It(World); It;)
	{
		return *It;
	}

	UClass* ClassToSpawn = GroundClass ? GroundClass.Get() : AMemoryRoomGroundActor::StaticClass();
	return World->SpawnActor<AMemoryRoomGroundActor>(ClassToSpawn, FTransform::Identity);
}
