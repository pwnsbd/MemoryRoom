// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MemoryRoomDustActor.generated.h"

class AMemoryRoomSkyDomeActor;
class UInstancedStaticMeshComponent;
class UMaterialInterface;
class UMaterialInstanceDynamic;
class USceneComponent;
class UStaticMesh;

// Dust = the original slow-drifting firefly motes. Leaf = flat tumbling cards blown sideways
// by a wind vector - same field/wrap mechanics, different mesh and motion in Tick.
UENUM(BlueprintType)
enum class EMemoryRoomMoteStyle : uint8
{
	Dust,
	Leaf
};

/**
 * A field of small instanced props filling the space around the player - either slow-drifting
 * dust motes or wind-blown leaves (MoteStyle). Motes carry no light of their own - they are lit
 * by whatever the photos are casting, so at night they pick up the colours of nearby memories.
 *
 * One instanced mesh component, so the whole field is a single draw call. Motes are kept
 * inside a box centred on the player and wrap around when they leave it, which means the
 * field is always where the player is without ever spawning or destroying anything.
 */
UCLASS(Blueprintable)
class MEMORYROOM_API AMemoryRoomDustActor : public AActor
{
	GENERATED_BODY()

public:
	AMemoryRoomDustActor();

	virtual void Tick(float DeltaSeconds) override;

	static AMemoryRoomDustActor* FindOrSpawn(UWorld* World, TSubclassOf<AMemoryRoomDustActor> DustClass = nullptr);

	// 0 Off, 1 Low, 2 Medium, 3 Full. Scales how many motes are active and disables tick
	// entirely at Off.
	UFUNCTION(BlueprintCallable, Category = "Memory Room|Dust")
	void SetDensityLevel(int32 Level);

	// Theme tint multiplied into the mote material. Stored and re-pushed if the field is
	// rebuilt.
	UFUNCTION(BlueprintCallable, Category = "Memory Room|Dust")
	void SetDustTint(FLinearColor NewTint);

protected:
	virtual void BeginPlay() override;

	// Dust (fireflies) or Leaf (wind-blown cards). Changing this at runtime rebuilds the field.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room|Dust")
	EMemoryRoomMoteStyle MoteStyle = EMemoryRoomMoteStyle::Dust;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room|Dust", meta = (ClampMin = "0", ClampMax = "2000"))
	int32 MoteCount = 260;

	// Half-extent of the box the motes live in, centred on the player.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room|Dust", meta = (ClampMin = "100.0"))
	float FieldRadiusCm = 420.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room|Dust", meta = (ClampMin = "0.1", UIMax = "4.0"))
	float MoteSizeCm = 1.1f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room|Dust", meta = (ClampMin = "0.0", UIMax = "30.0"))
	float DriftSpeedCmPerSecond = 4.5f;

	// Motes carry no light of their own - they only read as fireflies where a photo's glow
	// reaches them (see the class comment). A slow, per-mote size pulse sells that flicker even
	// where the light itself is static, the way a real speck catching light at a changing angle
	// would. 0 disables it and holds every mote at its built size.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room|Dust", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float TwinkleAmount = 0.35f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room|Dust", meta = (ClampMin = "0.05", UIMax = "3.0"))
	float TwinkleSpeedMin = 0.4f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room|Dust", meta = (ClampMin = "0.05", UIMax = "3.0"))
	float TwinkleSpeedMax = 1.3f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room|Dust")
	int32 DustSeed = 715517;

	// Motes are barely there in daylight and come out at night, following the same photo
	// brightness the sky dome uses to decide how dark the room is.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room|Dust")
	bool bScaleDensityWithNight = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room|Dust", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float DaytimeDensityFraction = 0.25f;

	// Rebuilding every transform each frame is wasted work on motes this slow; a third of
	// them per frame is indistinguishable and a third of the cost.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room|Dust", meta = (ClampMin = "1", ClampMax = "8"))
	int32 UpdateGroupCount = 3;

	// Optional override material for the motes. Left empty the engine sphere's own material
	// is used, which still takes the "Color" tint below.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room|Dust")
	TObjectPtr<UMaterialInterface> MoteMaterial;

	// --- Leaf style only. Ignored in Dust style. ---

	// Horizontal push direction (need not be normalised). Leaves drift this way continuously
	// on top of their own sway, the way a steady breeze carries falling leaves sideways.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room|Dust|Leaves")
	FVector2D WindDirection = FVector2D(1.0f, 0.3f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room|Dust|Leaves", meta = (ClampMin = "0.0", UIMax = "60.0"))
	float WindStrengthCmPerSecond = 22.0f;

	// Side-to-side sway perpendicular to the wind, the flutter real leaves have instead of
	// travelling in a straight line.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room|Dust|Leaves", meta = (ClampMin = "0.0", UIMax = "80.0"))
	float SwayAmountCm = 26.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room|Dust|Leaves", meta = (ClampMin = "0.0", UIMax = "400.0"))
	float TumbleSpeedDegPerSecMin = 40.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room|Dust|Leaves", meta = (ClampMin = "0.0", UIMax = "400.0"))
	float TumbleSpeedDegPerSecMax = 160.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room|Dust|Leaves", meta = (ClampMin = "0.5", UIMax = "20.0"))
	float LeafSizeCm = 4.0f;

	// Vector parameters SetDustTint tries, in order, on the mote material. The first the
	// material actually declares wins; unknown names are ignored.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room|Dust")
	TArray<FName> DustTintParameterNames = { TEXT("Color"), TEXT("Tint"), TEXT("BaseColor"), TEXT("EmissiveColor") };

private:
	void BuildMotes();
	void EnsureMoteMaterial();
	void ApplyDustTintToMaterial();
	AMemoryRoomSkyDomeActor* ResolveSkyDome();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Memory Room|Dust", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<USceneComponent> SceneRoot;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Memory Room|Dust", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UInstancedStaticMeshComponent> MoteMeshes;

	// Engine primitives cached in the constructor so BuildMotes can swap mesh by MoteStyle
	// without a ConstructorHelpers lookup outside of constructor context.
	UPROPERTY(Transient)
	TObjectPtr<UStaticMesh> DustSphereMesh;

	UPROPERTY(Transient)
	TObjectPtr<UStaticMesh> LeafPlaneMesh;

	struct FMote
	{
		FVector BasePosition = FVector::ZeroVector;
		FVector DriftAxis = FVector::UpVector;
		float Phase = 0.0f;
		float Rate = 1.0f;
		float Scale = 1.0f;
		// Deliberately decorrelated from Phase/Rate above (different random draws) so the
		// twinkle does not pulse in lockstep with the drift bob.
		float TwinklePhase = 0.0f;
		float TwinkleRate = 1.0f;
		// Leaf style: spin rate (deg/sec) and axis (reuses DriftAxis) for the tumble.
		float TumbleRateDegPerSec = 90.0f;
	};

	TArray<FMote> Motes;
	// 0..3; scales MoteCount / DaytimeDensityFraction. 0 hides the field and stops ticking.
	int32 DensityLevel = 3;
	FLinearColor DustTint = FLinearColor::White;
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> MoteMaterialInstance;
	TWeakObjectPtr<AMemoryRoomSkyDomeActor> CachedSkyDome;
	FVector FieldCenter = FVector::ZeroVector;
	int32 UpdateGroupOffset = 0;
	int32 ActiveMoteCount = 0;
};
