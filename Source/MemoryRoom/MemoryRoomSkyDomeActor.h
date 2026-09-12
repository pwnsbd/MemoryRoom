// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MemoryRoomSkyDomeActor.generated.h"

class UDirectionalLightComponent;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class USceneComponent;
class USkyLightComponent;
class UStaticMeshComponent;
class UTexture2D;

UENUM(BlueprintType)
enum class EMemoryRoomTimeOfDay : uint8
{
	Dawn,
	Sunset,
	Night
};

/**
 * One look for the room: the colours painted onto the dome plus the lighting that
 * matches them. Everything is data so the whole set can be retuned without code, and
 * so two presets can be blended into each other for the transition.
 */
USTRUCT(BlueprintType)
struct FMemoryRoomSkyPreset
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sky")
	FLinearColor ZenithColor = FLinearColor(0.05f, 0.10f, 0.28f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sky")
	FLinearColor HorizonColor = FLinearColor(1.0f, 0.55f, 0.25f);

	// The band between the horizon and the zenith. A real sunset does not run straight from
	// orange to blue - it passes through pink, and a two-colour ramp cannot express that.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sky")
	FLinearColor MidColor = FLinearColor(0.72f, 0.24f, 0.36f);

	// How far up the sky MidColor peaks. 0 is the horizon, 1 is straight overhead.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sky", meta = (ClampMin = "0.02", ClampMax = "0.95"))
	float MidElevation = 0.18f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sky")
	FLinearColor GroundColor = FLinearColor(0.03f, 0.03f, 0.05f);

	// Shapes the horizon-to-mid band only. Higher values pull the horizon colour tighter
	// to the horizon line.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sky", meta = (ClampMin = "0.1", UIMin = "0.1", UIMax = "8.0"))
	float HorizonFalloff = 2.2f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sky", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float StarBrightness = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sun")
	FLinearColor SunColor = FLinearColor(1.0f, 0.62f, 0.32f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sun", meta = (ClampMin = "0.0"))
	float SunIntensity = 2.5f;

	// Negative pitch points the light downward, so a small negative value sits the sun
	// just above the horizon and a large one drops it well below (night).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sun", meta = (ClampMin = "-90.0", ClampMax = "90.0"))
	float SunPitchDegrees = -3.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sun", meta = (ClampMin = "-360.0", ClampMax = "360.0"))
	float SunYawDegrees = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ambient")
	FLinearColor SkyLightColor = FLinearColor(0.7f, 0.5f, 0.45f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ambient", meta = (ClampMin = "0.0"))
	float SkyLightIntensity = 0.9f;

	// Multiplier on the photo image. M_Photo is unlit, so the photos ignore the sun
	// entirely; 1.0 leaves them exactly as captured and above 1.0 makes them glow, which
	// is what carries the room once night drops the sky to almost black.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ambient", meta = (ClampMin = "0.0", UIMax = "4.0"))
	float PhotoBrightness = 1.0f;

	// The level's height fog is unaffected by any of the above, so at night it becomes the
	// brightest thing in the scene and washes everything with its own tint. Driving it from
	// the preset keeps it agreeing with the sky instead of fighting it.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fog")
	FLinearColor FogInscatteringColor = FLinearColor(0.45f, 0.30f, 0.22f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fog", meta = (ClampMin = "0.0", UIMax = "0.2"))
	float FogDensity = 0.02f;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FMemoryRoomTimeOfDayChangedSignature, EMemoryRoomTimeOfDay, TimeOfDay);

/**
 * A large unlit dome that surrounds the gallery, plus the sun and ambient light that
 * belong with it. The dome is far enough out that the player can never reach it and is
 * re-centred on the player each frame so it always reads as sky rather than a wall.
 *
 * Deliberately not a SkyAtmosphere: this renders as a single unlit draw call, which is
 * what keeps it affordable on the Quest's tile GPU.
 */
UCLASS(Blueprintable)
class MEMORYROOM_API AMemoryRoomSkyDomeActor : public AActor
{
	GENERATED_BODY()

public:
	AMemoryRoomSkyDomeActor();

	virtual void Tick(float DeltaSeconds) override;

	UFUNCTION(BlueprintCallable, Category = "Memory Room|Sky")
	void SetTimeOfDay(EMemoryRoomTimeOfDay NewTimeOfDay, bool bImmediate = false);

	// Dawn -> Sunset -> Night -> Dawn.
	UFUNCTION(BlueprintCallable, Category = "Memory Room|Sky")
	void CycleTimeOfDay();

	// Replaces the three presets wholesale (used by the theme system) and re-applies the
	// current time of day so the change is visible immediately. Snaps by default because
	// this runs as the room opens.
	UFUNCTION(BlueprintCallable, Category = "Memory Room|Sky")
	void ApplyThemePresets(const FMemoryRoomSkyPreset& InDawnPreset, const FMemoryRoomSkyPreset& InSunsetPreset, const FMemoryRoomSkyPreset& InNightPreset, bool bImmediate = true);

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Memory Room|Sky")
	EMemoryRoomTimeOfDay GetTimeOfDay() const { return TargetTimeOfDay; }

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Memory Room|Sky")
	FText GetTimeOfDayDisplayName() const;

	static FText GetDisplayNameForTimeOfDay(EMemoryRoomTimeOfDay TimeOfDay);

	// The live sky gradient. Its contents are rewritten in place as the time of day
	// changes, so anything holding this pointer keeps showing the current sky for free.
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Memory Room|Sky")
	UTexture2D* GetGradientTexture() const { return GradientTexture; }

	UMaterialInterface* GetSkyDomeMaterial() const { return SkyDomeMaterial; }
	FName GetGradientTextureParameterName() const { return GradientTextureParameterName; }

	// The photo brightness multiplier for the current moment. Blends across a transition,
	// so the gallery fades up into the dark rather than snapping on.
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Memory Room|Sky")
	float GetPhotoBrightness() const { return CurrentPhotoBrightness; }

	// Pulls the whole sky down while a photo is open, so the room recedes and the photo
	// becomes the only thing in it. 0 is the normal sky, 1 is fully dimmed.
	UFUNCTION(BlueprintCallable, Category = "Memory Room|Sky")
	void SetFocusDim(float NewFocusDim);

	// Finds the sky dome in the level, spawning one if the level has none.
	static AMemoryRoomSkyDomeActor* FindOrSpawn(UWorld* World, TSubclassOf<AMemoryRoomSkyDomeActor> SkyDomeClass = nullptr);

	UPROPERTY(BlueprintAssignable, Category = "Memory Room|Sky")
	FMemoryRoomTimeOfDayChangedSignature OnTimeOfDayChanged;

protected:
	virtual void BeginPlay() override;

	/**
	 * Unlit, two-sided material for the dome. Expected parameters (all optional - any the
	 * material does not declare are simply skipped):
	 *   Vector: ZenithColor, HorizonColor, GroundColor
	 *   Scalar: HorizonFalloff, StarBrightness
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room|Sky")
	TObjectPtr<UMaterialInterface> SkyDomeMaterial;

	// The sphere is one cheap unlit draw call, so making it big costs nothing - go large
	// enough that the mountains sit comfortably inside it, behind the trees.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room|Sky", meta = (ClampMin = "1000.0", UIMin = "1000.0"))
	float DomeRadius = 30000.0f;

	// Fit the level's hand-placed SM_MountainLayer meshes (if any remain) to reach this
	// distance from the room centre. OFF by default now - the ground actor scatters big
	// rocks as "mountains" instead.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room|Sky|Scenery")
	bool bPullInDistantScenery = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room|Sky|Scenery", meta = (ClampMin = "0.0", EditCondition = "bPullInDistantScenery"))
	float SceneryRingRadiusCm = 22000.0f;

	// Keeps the dome centred on the player so it never gets nearer or further away.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room|Sky")
	bool bCenterOnPlayer = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room|Sky", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float TransitionDurationSeconds = 1.6f;

	// How far the sky falls when a photo is opened. 0.75 leaves a quarter of it.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room|Sky|Focus", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float FocusSkyDimAmount = 0.75f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room|Sky")
	EMemoryRoomTimeOfDay DefaultTimeOfDay = EMemoryRoomTimeOfDay::Sunset;

	// Restores whatever the player last chose instead of DefaultTimeOfDay.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room|Sky")
	bool bRestoreSavedTimeOfDay = true;

	// The sun casts a single tight cascaded shadow - enough to ground the hands, the player and
	// the photos near you without paying for a full-scene shadow map. The scattered trees,
	// rocks and grass are all set NOT to cast, so the forest costs nothing here.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room|Sky|Shadows")
	bool bSunCastsShadows = true;

	// How far from the player the sun shadow reaches, cm. Kept short on purpose - only the
	// gallery around you casts.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room|Sky|Shadows", meta = (ClampMin = "200.0", EditCondition = "bSunCastsShadows"))
	float SunShadowDistanceCm = 1600.0f;

	// 1 is the mobile-friendly default; 2 sharpens the near ground at some cost.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room|Sky|Shadows", meta = (ClampMin = "1", ClampMax = "3", EditCondition = "bSunCastsShadows"))
	int32 SunShadowCascades = 1;

	// Lower = cheaper + softer shadows. 0.7 is a good Quest starting point.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room|Sky|Shadows", meta = (ClampMin = "0.25", ClampMax = "2.0", EditCondition = "bSunCastsShadows"))
	float SunShadowResolutionScale = 0.7f;

	// Takes over the level's ExponentialHeightFog, if it has one, so the fog tracks the
	// time of day. Turn off to leave whatever the level author set.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room|Sky")
	bool bDriveExponentialHeightFog = true;

	// On BeginPlay, destroy any SkyAtmosphere / standalone DirectionalLight / SkyLight actor
	// in the level - this dome brings its own sun and ambient, so those only cost frame time
	// (a SkyAtmosphere is ~2-4 ms on Quest). Turn off if you want to hand-place lighting.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room|Sky")
	bool bRemoveRedundantLevelLighting = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room|Sky|Presets")
	FMemoryRoomSkyPreset DawnPreset;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room|Sky|Presets")
	FMemoryRoomSkyPreset SunsetPreset;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room|Sky|Presets")
	FMemoryRoomSkyPreset NightPreset;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room|Sky|Parameters")
	FName ZenithColorParameterName = TEXT("ZenithColor");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room|Sky|Parameters")
	FName HorizonColorParameterName = TEXT("HorizonColor");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room|Sky|Parameters")
	FName GroundColorParameterName = TEXT("GroundColor");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room|Sky|Parameters")
	FName HorizonFalloffParameterName = TEXT("HorizonFalloff");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room|Sky|Parameters")
	FName StarBrightnessParameterName = TEXT("StarBrightness");

	// The texture parameter the generated sky gradient is fed into. This is all the
	// material needs: sample it with the mesh UVs and plug it into Emissive.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room|Sky|Parameters")
	FName GradientTextureParameterName = TEXT("SkyGradient");

	// The star field. Generated once and never rewritten, so it costs nothing per frame;
	// the preset's StarBrightness is what fades it in and out.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room|Sky|Parameters")
	FName StarTextureParameterName = TEXT("StarField");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room|Sky|Stars", meta = (ClampMin = "0", ClampMax = "20000"))
	int32 StarCount = 1600;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room|Sky|Stars")
	int32 StarSeed = 20260903;

	// Set this if the sky comes out upside down, i.e. the sphere's V axis runs the other
	// way than assumed (top of the texture = zenith).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room|Sky")
	bool bFlipGradient = false;

	// Rows in the generated gradient. More rows means a smoother ramp; 512 is already
	// well past what an 8-bit gradient can resolve, and the whole texture is only ~16KB.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room|Sky", meta = (ClampMin = "32", ClampMax = "1024"))
	int32 GradientResolution = 512;

private:
	const FMemoryRoomSkyPreset& GetPreset(EMemoryRoomTimeOfDay TimeOfDay) const;
	static FMemoryRoomSkyPreset BlendPresets(const FMemoryRoomSkyPreset& From, const FMemoryRoomSkyPreset& To, float Alpha);
	// The sky gradient itself, evaluated for one point on the dome. Elevation is the
	// sine of the angle above the horizon: +1 zenith, 0 horizon, -1 straight down.
	static FLinearColor EvaluateSkyColor(const FMemoryRoomSkyPreset& Preset, float Elevation);
	void EnsureGradientTexture();
	void UpdateGradientTexture(const FMemoryRoomSkyPreset& Preset);
	// Built once at BeginPlay. Star positions never move, so only the brightness scalar
	// needs to change as the sky does.
	void EnsureStarTexture();
	void ApplyPreset(const FMemoryRoomSkyPreset& Preset);
	// Pushes a preset to the scene with the current focus dim folded in. Kept separate from
	// ApplyPreset so focus changes can re-apply without disturbing a running transition.
	void ApplyPresetToScene(const FMemoryRoomSkyPreset& Preset);
	void ApplyDefaultPresetValues();
	void SaveTimeOfDay() const;
	void LoadTimeOfDay();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Memory Room|Sky|Components", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<USceneComponent> SceneRoot;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Memory Room|Sky|Components", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UStaticMeshComponent> DomeMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Memory Room|Sky|Components", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UDirectionalLightComponent> SunLight;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Memory Room|Sky|Components", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<USkyLightComponent> SkyLight;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> DomeMaterialInstance;

	UPROPERTY(Transient)
	TObjectPtr<UTexture2D> GradientTexture;

	UPROPERTY(Transient)
	TObjectPtr<UTexture2D> StarTexture;

	TWeakObjectPtr<class AExponentialHeightFog> CachedHeightFog;

	EMemoryRoomTimeOfDay TargetTimeOfDay = EMemoryRoomTimeOfDay::Sunset;
	// The look the current transition started from. Held by value so interrupting a
	// transition part-way blends out of what is actually on screen, not a preset corner.
	FMemoryRoomSkyPreset TransitionStartPreset;
	float TransitionElapsedSeconds = 0.0f;
	float CurrentPhotoBrightness = 1.0f;
	// The un-dimmed look currently in effect, so focus can be re-applied on top of it.
	FMemoryRoomSkyPreset CurrentPreset;
	float FocusDim = 0.0f;
	bool bTransitioning = false;
};
