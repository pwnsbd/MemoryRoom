// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MemoryRoomGroundActor.generated.h"

struct FCollisionObjectQueryParams;
struct FCollisionQueryParams;

class UHierarchicalInstancedStaticMeshComponent;
class UInstancedStaticMeshComponent;
class UMaterialInterface;
class USceneComponent;
class UStaticMeshComponent;

/**
 * Dresses the level's OWN existing floor (found by tracing straight down, the same way
 * AMemoryRoomManager::SnapPlayerPawnToFloor finds it - Floor_0 in this level) rather than
 * spawning a competing ground mesh: applies the installed "Stylized Log Cabin" pack's grass
 * material (MI_Grass_A1) to it, and scatters SM_Grass_A1-4 clumps across its actual bounds.
 *
 * Deliberately does not create any mesh of its own or re-centre on the player - the floor it
 * is dressing is the room's real, fixed floor, not an open field, so there is nothing to keep
 * "underfoot" the way the (removed) standalone ground plane needed to.
 */
UCLASS(Blueprintable)
class MEMORYROOM_API AMemoryRoomGroundActor : public AActor
{
	GENERATED_BODY()

public:
	AMemoryRoomGroundActor();

	static AMemoryRoomGroundActor* FindOrSpawn(UWorld* World, TSubclassOf<AMemoryRoomGroundActor> GroundClass = nullptr);

protected:
	virtual void BeginPlay() override;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room|Ground|Grass", meta = (ClampMin = "0"))
	int32 GrassCount = 500;

	// Left clear right around the player's likely standing position (the floor's own centre)
	// so nothing pokes up where they are.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room|Ground|Grass", meta = (ClampMin = "0.0"))
	float GrassClearRadiusCm = 100.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room|Ground|Grass", meta = (ClampMin = "0.3", UIMax = "2.5"))
	float GrassMinScale = 0.8f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room|Ground|Grass", meta = (ClampMin = "0.3", UIMax = "2.5"))
	float GrassMaxScale = 1.6f;

	// --- Tree ring ------------------------------------------------------------------------
	// The hand-painted pine forest is kept and just compressed radially so the farthest tree
	// lands at this distance from the room centre. 0 leaves the painted forest where it is.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room|Ground|Trees", meta = (ClampMin = "0.0"))
	float PaintedTreeMaxRadiusCm = 2500.0f;

	// Code-scattered pines spread across the whole map (SM_Pine_A1/A2/A3), sized and spread
	// to match the painted forest that was measured at BeginPlay, but with a clear middle for
	// the user and photos and a different leaf colour. Meant to REPLACE the painted foliage.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room|Ground|Trees")
	bool bScatterTreeRing = true;

	// Total scattered trees. Higher = denser forest but more triangles + leaf overdraw, which
	// is the main forest cost on Quest.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room|Ground|Trees", meta = (ClampMin = "0"))
	int32 TreeCount = 200;

	// Nothing spawns closer than this to the room centre - the clear space for the user and
	// the photo spiral (which reaches ~620 cm).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room|Ground|Trees", meta = (ClampMin = "0.0"))
	float TreeRingInnerRadiusCm = 1400.0f;

	// Outer edge, used only when the painted forest could not be measured (it has been
	// deleted). 9000 matches the reach of the forest that was measured before deletion.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room|Ground|Trees", meta = (ClampMin = "1.0"))
	float TreeRingOuterRadiusCm = 9000.0f;

	// Leaf material for the scattered trees, so they read as a different colour from the
	// painted forest. Leave null to use a built-in alternate pine-leaf material.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room|Ground|Trees")
	TObjectPtr<UMaterialInterface> TreeLeafMaterialOverride;

	// Per-instance scale range, used only when the painted forest could not be measured.
	// 0.12-0.24 matches the painted foliage that was measured before it was deleted.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room|Ground|Trees", meta = (ClampMin = "0.02", UIMax = "4.0"))
	float TreeMinScale = 0.12f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room|Ground|Trees", meta = (ClampMin = "0.02", UIMax = "4.0"))
	float TreeMaxScale = 0.24f;

	// Off by default - a dense shadow-casting ring is one of the most expensive things you can
	// do on a Quest. Turn on only if a capture shows headroom.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room|Ground|Trees")
	bool bTreesCastShadow = false;

	// --- Rock "mountains" ----------------------------------------------------------------
	// Big scaled-up rocks (SM_Rock_A1/A2/A3) scattered in a far ring to read as a mountain
	// range behind the trees. Cheap: instanced, no collision, no shadow, half-buried, and
	// mostly lost to fog + distance so detail does not matter.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room|Ground|Mountains")
	bool bScatterRockMountains = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room|Ground|Mountains", meta = (ClampMin = "0"))
	int32 RockMountainCount = 26;

	// Distance of the range from the room centre. Sits well past the tree spread (~90 m).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room|Ground|Mountains", meta = (ClampMin = "1.0"))
	float RockMountainRadiusCm = 22000.0f;

	// How far each rock's distance wanders in/out, so the range is not a perfect circle.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room|Ground|Mountains", meta = (ClampMin = "0.0"))
	float RockMountainRadiusJitterCm = 4000.0f;

	// Target WORLD height of a mountain, in cm (the rock mesh is scaled to hit this, whatever
	// its authored size). 3000 = 30 m, 7000 = 70 m. At ~220 m distance that reads as a hill
	// range, not a wall in your face.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room|Ground|Mountains", meta = (ClampMin = "100.0"))
	float RockMountainHeightMinCm = 3000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room|Ground|Mountains", meta = (ClampMin = "100.0"))
	float RockMountainHeightMaxCm = 7000.0f;

	// Sinks each rock this fraction of its height so its base is below the horizon line.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room|Ground|Mountains", meta = (ClampMin = "0.0", ClampMax = "0.9"))
	float RockMountainSinkFraction = 0.30f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room|Ground")
	int32 ScatterSeed = 20260904;

	// How far below the trace start (the player's view) to look for the floor.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room|Ground", meta = (ClampMin = "100.0"))
	float FloorTraceDistanceCm = 100000.0f;

private:
	// Finds the floor the same way AMemoryRoomManager::SnapPlayerPawnToFloor does (a downward
	// trace from the player), applies the grass material to it, and scatters grass clumps
	// across its bounds. Logs a warning and does nothing else if no floor is found.
	void DressExistingFloor();

	// Scatters TreeCount pines from TreeRingInnerRadiusCm out to OuterRadiusCm around Centre,
	// tracing each onto whatever surface is there. MinScale/MaxScale/OuterRadiusCm come from
	// the measured painted forest when it still exists (pass <= 0 to use the UPROPERTYs).
	void ScatterTreeRing(UWorld* World, const FVector& Centre, const FCollisionObjectQueryParams& ObjectParams, const FCollisionQueryParams& QueryParams,
		float OuterRadiusCm, float MinScale, float MaxScale);

	// Scatters RockMountainCount big rocks in a jittered far ring to fake a mountain range.
	void ScatterRockMountains(UWorld* World, const FVector& Centre, const FCollisionObjectQueryParams& ObjectParams, const FCollisionQueryParams& QueryParams);

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Memory Room|Ground|Components", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<USceneComponent> SceneRoot;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Memory Room|Ground|Components", meta = (AllowPrivateAccess = "true"))
	TArray<TObjectPtr<UInstancedStaticMeshComponent>> GrassMeshes;

	// One HISM per pine variant, so the half of the ring behind the player is frustum-culled
	// and the meshes' LODs kick in with range.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Memory Room|Ground|Components", meta = (AllowPrivateAccess = "true"))
	TArray<TObjectPtr<UHierarchicalInstancedStaticMeshComponent>> TreeMeshes;

	// One HISM per rock variant (SM_Rock_A1/A2/A3) for the distant fake mountains.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Memory Room|Ground|Components", meta = (AllowPrivateAccess = "true"))
	TArray<TObjectPtr<UHierarchicalInstancedStaticMeshComponent>> RockMeshes;

	// Fetched via ConstructorHelpers in the constructor rather than a runtime LoadObject - the
	// latter is only reliable in the editor; in a packaged/cooked build it can come back null
	// if nothing else hard-references the asset.
	UPROPERTY()
	TObjectPtr<UMaterialInterface> GrassFloorMaterial;

	// One leaf material per scattered-tree variant (MI_PineLeaf_A1 / A3 / A4), loaded in the
	// constructor, so the forest mixes three leaf colours. TreeLeafMaterialOverride replaces
	// all of them when set.
	UPROPERTY()
	TArray<TObjectPtr<UMaterialInterface>> TreeLeafMaterials;
};
