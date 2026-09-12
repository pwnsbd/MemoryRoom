// Fill out your copyright notice in the Description page of Project Settings.

#include "MemoryRoomSkyDomeActor.h"
#include "MemoryRoom.h"

#include "Components/DirectionalLightComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/SceneComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "AMemoryRoomManager.h"
#include "Components/SkyAtmosphereComponent.h"
#include "Engine/DirectionalLight.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/ExponentialHeightFog.h"
#include "Engine/SkyLight.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "MemoryRoomQualitySaveGame.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
const TCHAR* const SkySaveSlotName = TEXT("MemoryRoomQualitySettings");

// The engine sphere is 100cm across, so this converts a desired radius into a scale.
constexpr float EngineSphereRadiusCm = 50.0f;

// The gradient only varies vertically, so a few columns are plenty. More than one column
// gives the dither pattern somewhere to vary horizontally.
constexpr int32 GradientTextureWidth = 8;

// Stars need real 2D placement, so they get their own texture. Generated once, so its
// size costs memory rather than frame time.
constexpr int32 StarTextureWidth = 1024;
constexpr int32 StarTextureHeight = 512;
}

AMemoryRoomSkyDomeActor::AMemoryRoomSkyDomeActor()
{
	PrimaryActorTick.bCanEverTick = true;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);

	DomeMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("DomeMesh"));
	DomeMesh->SetupAttachment(SceneRoot);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereMesh(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	if (SphereMesh.Succeeded())
	{
		DomeMesh->SetStaticMesh(SphereMesh.Object);
	}
	DomeMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	DomeMesh->SetCollisionProfileName(TEXT("NoCollision"));
	DomeMesh->SetCastShadow(false);
	DomeMesh->bCastDynamicShadow = false;
	DomeMesh->bAffectDistanceFieldLighting = false;
	DomeMesh->bReceivesDecals = false;
	// The dome is always around the camera, so let it skip distance and frustum culling
	// rather than paying for bounds tests it can never fail.
	DomeMesh->SetBoundsScale(8.0f);
	DomeMesh->SetRelativeScale3D(FVector(DomeRadius / EngineSphereRadiusCm));

	SunLight = CreateDefaultSubobject<UDirectionalLightComponent>(TEXT("SunLight"));
	SunLight->SetupAttachment(SceneRoot);
	SunLight->SetMobility(EComponentMobility::Movable);
	SunLight->SetCastShadows(false);

	SkyLight = CreateDefaultSubobject<USkyLightComponent>(TEXT("SkyLight"));
	SkyLight->SetupAttachment(SceneRoot);
	SkyLight->SetMobility(EComponentMobility::Movable);
	SkyLight->SourceType = ESkyLightSourceType::SLS_CapturedScene;
	SkyLight->bLowerHemisphereIsBlack = false;
	SkyLight->bRealTimeCapture = false;

	ApplyDefaultPresetValues();
}

void AMemoryRoomSkyDomeActor::ApplyDefaultPresetValues()
{
	// Cold sky, warm low sun coming up. Bright enough to read photos comfortably.
	DawnPreset.ZenithColor = FLinearColor(0.10f, 0.20f, 0.44f);
	DawnPreset.HorizonColor = FLinearColor(1.00f, 0.76f, 0.52f);
	DawnPreset.MidColor = FLinearColor(0.70f, 0.60f, 0.78f);
	DawnPreset.MidElevation = 0.20f;
	DawnPreset.GroundColor = FLinearColor(0.05f, 0.06f, 0.10f);
	DawnPreset.HorizonFalloff = 1.8f;
	DawnPreset.StarBrightness = 0.0f;
	DawnPreset.SunColor = FLinearColor(1.00f, 0.88f, 0.72f);
	DawnPreset.SunIntensity = 3.0f;
	DawnPreset.SunPitchDegrees = -6.0f;
	DawnPreset.SunYawDegrees = 20.0f;
	DawnPreset.SkyLightColor = FLinearColor(0.58f, 0.68f, 0.92f);
	DawnPreset.SkyLightIntensity = 1.0f;
	DawnPreset.PhotoBrightness = 1.0f;
	DawnPreset.FogInscatteringColor = FLinearColor(0.42f, 0.48f, 0.62f);
	DawnPreset.FogDensity = 0.018f;

	// Deep indigo overhead falling into a hot band right on the horizon line.
	SunsetPreset.ZenithColor = FLinearColor(0.045f, 0.065f, 0.24f);
	SunsetPreset.HorizonColor = FLinearColor(1.00f, 0.34f, 0.10f);
	SunsetPreset.MidColor = FLinearColor(0.66f, 0.20f, 0.34f);
	SunsetPreset.MidElevation = 0.20f;
	SunsetPreset.GroundColor = FLinearColor(0.04f, 0.03f, 0.06f);
	SunsetPreset.HorizonFalloff = 1.6f;
	// No stars while the sky is still lit; they only belong to night here.
	SunsetPreset.StarBrightness = 0.0f;
	SunsetPreset.SunColor = FLinearColor(1.00f, 0.55f, 0.26f);
	SunsetPreset.SunIntensity = 2.6f;
	SunsetPreset.SunPitchDegrees = -2.0f;
	SunsetPreset.SunYawDegrees = 200.0f;
	SunsetPreset.SkyLightColor = FLinearColor(0.78f, 0.52f, 0.46f);
	SunsetPreset.SkyLightIntensity = 0.85f;
	SunsetPreset.PhotoBrightness = 1.0f;
	SunsetPreset.FogInscatteringColor = FLinearColor(0.52f, 0.28f, 0.16f);
	SunsetPreset.FogDensity = 0.020f;

	// Deliberately almost black. The photos become the only real light source, so the
	// gallery reads as luminous memories hanging in the dark rather than a lit room at
	// night. Keep the sun and sky light barely above zero: enough that frames and hands
	// are not pure silhouette, low enough that the stars and the photos carry the scene.
	NightPreset.ZenithColor = FLinearColor(0.0006f, 0.0009f, 0.0020f);
	NightPreset.HorizonColor = FLinearColor(0.007f, 0.009f, 0.016f);
	NightPreset.MidColor = FLinearColor(0.0026f, 0.0034f, 0.0068f);
	NightPreset.MidElevation = 0.15f;
	NightPreset.GroundColor = FLinearColor(0.005f, 0.005f, 0.007f);
	NightPreset.HorizonFalloff = 1.4f;
	NightPreset.StarBrightness = 1.0f;
	NightPreset.SunColor = FLinearColor(0.74f, 0.80f, 1.00f);
	// Dim moonlight + ambient starlight fill so the floor and hands are not pure black.
	NightPreset.SunIntensity = 0.09f;
	NightPreset.SunPitchDegrees = -16.0f;
	NightPreset.SunYawDegrees = 120.0f;
	NightPreset.SkyLightColor = FLinearColor(0.20f, 0.22f, 0.30f);
	NightPreset.SkyLightIntensity = 0.20f;
	// Pushed above 1 so the photos read as the light source in the room rather than just
	// surviving the dark. M_Photo is unlit, so this is a straight multiply on the image.
	NightPreset.PhotoBrightness = 2.6f;
	// A faint cool haze that catches the starlight and lifts the blacks a little.
	NightPreset.FogInscatteringColor = FLinearColor(0.020f, 0.024f, 0.035f);
	NightPreset.FogDensity = 0.006f;
}

void AMemoryRoomSkyDomeActor::BeginPlay()
{
	Super::BeginPlay();

	DomeMesh->SetRelativeScale3D(FVector(DomeRadius / EngineSphereRadiusCm));
	SunLight->SetCastShadows(bSunCastsShadows);
	if (bSunCastsShadows)
	{
		// One tight cascade around the player: grounds the hands/photos/body without paying
		// for a forest-wide shadow map (the trees/rocks/grass are all set non-casting).
		SunLight->SetDynamicShadowDistanceMovableLight(FMath::Max(SunShadowDistanceCm, 200.0f));
		SunLight->SetDynamicShadowCascades(FMath::Clamp(SunShadowCascades, 1, 3));
		SunLight->SetCascadeDistributionExponent(3.0f);
		SunLight->SetShadowBias(0.6f);
		SunLight->SetShadowSlopeBias(0.6f);
		SunLight->ShadowResolutionScale = FMath::Clamp(SunShadowResolutionScale, 0.25f, 2.0f);
		SunLight->ShadowSharpen = 0.35f;
		SunLight->MarkRenderStateDirty();
	}
	EnsureStarTexture();

	if (bDriveExponentialHeightFog)
	{
		if (UWorld* World = GetWorld())
		{
			for (TActorIterator<AExponentialHeightFog> It(World); It;)
			{
				CachedHeightFog = *It;
				UE_LOG(LogMemoryRoom, Log, TEXT("%s is driving height fog actor %s."), *GetName(), *GetNameSafe(*It));
				break;
			}
		}
	}

	// This dome carries its own sun (SunLight) and ambient (SkyLight), so a SkyAtmosphere or
	// standalone light actor left in the level is pure redundant cost - a SkyAtmosphere alone
	// is 2-4 ms/frame on the Quest tile GPU. Destroy them so the packaged build never pays for
	// them. Turn off to keep hand-placed lighting.
	if (bRemoveRedundantLevelLighting)
	{
		if (UWorld* World = GetWorld())
		{
			int32 Removed = 0;
			for (TActorIterator<ASkyAtmosphere> It(World); It; ++It) { It->Destroy(); ++Removed; }
			for (TActorIterator<ADirectionalLight> It(World); It; ++It) { It->Destroy(); ++Removed; }
			for (TActorIterator<ASkyLight> It(World); It; ++It) { It->Destroy(); ++Removed; }
			if (Removed > 0)
			{
				UE_LOG(LogMemoryRoom, Warning, TEXT("%s removed %d redundant level lighting actor(s) (SkyAtmosphere / DirectionalLight / SkyLight); the dome supplies its own."),
					*GetName(), Removed);
			}
		}
	}

	// Bring the hand-placed mountain backdrop in to a clean middle band between the trees and
	// the dome. These are big meshes, so scaling the actor about the room centre (position AND
	// scale) is what actually controls where the visible silhouette sits - just moving the
	// pivot leaves the geometry sprawling through the tree zone.
	if (bPullInDistantScenery && SceneryRingRadiusCm > 1.0f)
	{
		if (UWorld* World = GetWorld())
		{
			FVector Centre = GetActorLocation();
			for (TActorIterator<AMemoryRoomManager> It(World); It;)
			{
				Centre = It->GetActorLocation();
				break;
			}

			int32 Moved = 0;
			for (TActorIterator<AActor> It(World); It; ++It)
			{
				AActor* Actor = *It;
				if (!Actor || Actor == this)
				{
					continue;
				}

				bool bIsMountain = Actor->GetName().Contains(TEXT("Mountain")) || Actor->GetActorNameOrLabel().Contains(TEXT("Mountain"));
				if (!bIsMountain)
				{
					TArray<UStaticMeshComponent*> MeshComps;
					Actor->GetComponents(MeshComps);
					for (const UStaticMeshComponent* MeshComp : MeshComps)
					{
						const UStaticMesh* Mesh = MeshComp ? MeshComp->GetStaticMesh() : nullptr;
						if (Mesh && Mesh->GetName().Contains(TEXT("Mountain")))
						{
							bIsMountain = true;
							break;
						}
					}
				}
				if (!bIsMountain)
				{
					continue;
				}

				if (USceneComponent* Root = Actor->GetRootComponent())
				{
					if (Root->Mobility != EComponentMobility::Movable)
					{
						Root->SetMobility(EComponentMobility::Movable);
					}
				}

				// How far does this mountain's geometry currently reach, horizontally, from the
				// room centre? Scale the whole actor about the centre so that reaches exactly
				// SceneryRingRadiusCm.
				const FBox Bounds = Actor->GetComponentsBoundingBox(/*bNonColliding*/ true);
				const float FarX = FMath::Max(FMath::Abs(Bounds.Max.X - Centre.X), FMath::Abs(Bounds.Min.X - Centre.X));
				const float FarY = FMath::Max(FMath::Abs(Bounds.Max.Y - Centre.Y), FMath::Abs(Bounds.Min.Y - Centre.Y));
				const float CurrentReach = FMath::Max(FarX, FarY);
				if (CurrentReach < 1.0f)
				{
					continue;
				}

				const float Factor = FMath::Clamp(SceneryRingRadiusCm / CurrentReach, 0.02f, 4.0f);
				const FVector OldLoc = Actor->GetActorLocation();
				const FVector NewLoc(Centre.X + (OldLoc.X - Centre.X) * Factor,
									 Centre.Y + (OldLoc.Y - Centre.Y) * Factor,
									 OldLoc.Z);
				Actor->SetActorLocation(NewLoc);
				Actor->SetActorScale3D(Actor->GetActorScale3D() * Factor);
				++Moved;
			}
			if (Moved > 0)
			{
				UE_LOG(LogMemoryRoom, Warning, TEXT("%s fitted %d mountain actor(s) to reach ~%.0f cm from the room centre."),
					*GetName(), Moved, SceneryRingRadiusCm);
			}
			else
			{
				UE_LOG(LogMemoryRoom, Warning, TEXT("%s found no mountain actor to fit (looked for name/mesh containing 'Mountain')."), *GetName());
			}
		}
	}

	if (SkyDomeMaterial)
	{
		DomeMaterialInstance = UMaterialInstanceDynamic::Create(SkyDomeMaterial, this);
		if (DomeMaterialInstance)
		{
			DomeMesh->SetMaterial(0, DomeMaterialInstance);
		}
	}
	else
	{
		// Without a material the sphere would fall back to the engine's grid material and
		// wrap the room in a grey checkerboard. Hiding it is a far better default: the
		// lighting still changes with the time of day, you just do not get a tinted sky.
		DomeMesh->SetVisibility(false);
		UE_LOG(LogMemoryRoom, Warning, TEXT("%s has no SkyDomeMaterial assigned, so the dome mesh is hidden. Lighting still changes with the time of day. Assign M_SkyDome to see the sky itself."), *GetName());
	}

	TargetTimeOfDay = DefaultTimeOfDay;
	if (bRestoreSavedTimeOfDay)
	{
		LoadTimeOfDay();
	}

	// Snap rather than fade on the first frame; the player should never watch the room
	// wipe from one preset to another as the level opens.
	SetTimeOfDay(TargetTimeOfDay, true);

	if (SkyLight)
	{
		SkyLight->RecaptureSky();
	}
}

void AMemoryRoomSkyDomeActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (bCenterOnPlayer && DomeMesh)
	{
		if (const UWorld* World = GetWorld())
		{
			if (const APlayerController* PlayerController = World->GetFirstPlayerController())
			{
				FVector ViewLocation = FVector::ZeroVector;
				FRotator ViewRotation = FRotator::ZeroRotator;
				PlayerController->GetPlayerViewPoint(ViewLocation, ViewRotation);
				DomeMesh->SetWorldLocation(ViewLocation);
			}
		}
	}

	if (!bTransitioning)
	{
		return;
	}

	TransitionElapsedSeconds += FMath::Max(DeltaSeconds, 0.0f);
	const float Duration = FMath::Max(TransitionDurationSeconds, KINDA_SMALL_NUMBER);
	const float Alpha = FMath::Clamp(TransitionElapsedSeconds / Duration, 0.0f, 1.0f);
	const float SmoothAlpha = Alpha * Alpha * (3.0f - 2.0f * Alpha);

	ApplyPreset(BlendPresets(TransitionStartPreset, GetPreset(TargetTimeOfDay), SmoothAlpha));

	if (Alpha >= 1.0f)
	{
		bTransitioning = false;
		ApplyPreset(GetPreset(TargetTimeOfDay));
	}
}

void AMemoryRoomSkyDomeActor::SetTimeOfDay(EMemoryRoomTimeOfDay NewTimeOfDay, bool bImmediate)
{
	const bool bChanged = TargetTimeOfDay != NewTimeOfDay;

	// Start the blend from whatever is currently on screen. Mid-transition that is a
	// partly blended look, so rapid presses never snap.
	TransitionStartPreset = bTransitioning
		? BlendPresets(TransitionStartPreset, GetPreset(TargetTimeOfDay), FMath::Clamp(TransitionElapsedSeconds / FMath::Max(TransitionDurationSeconds, KINDA_SMALL_NUMBER), 0.0f, 1.0f))
		: GetPreset(TargetTimeOfDay);

	TargetTimeOfDay = NewTimeOfDay;
	TransitionElapsedSeconds = 0.0f;
	bTransitioning = !bImmediate && TransitionDurationSeconds > KINDA_SMALL_NUMBER;

	if (!bTransitioning)
	{
		ApplyPreset(GetPreset(TargetTimeOfDay));
	}

	if (bChanged)
	{
		SaveTimeOfDay();
		OnTimeOfDayChanged.Broadcast(TargetTimeOfDay);
		UE_LOG(LogMemoryRoom, Log, TEXT("%s time of day set to %s."), *GetName(), *GetTimeOfDayDisplayName().ToString());
	}
}

void AMemoryRoomSkyDomeActor::ApplyThemePresets(const FMemoryRoomSkyPreset& InDawnPreset, const FMemoryRoomSkyPreset& InSunsetPreset, const FMemoryRoomSkyPreset& InNightPreset, bool bImmediate)
{
	DawnPreset = InDawnPreset;
	SunsetPreset = InSunsetPreset;
	NightPreset = InNightPreset;

	// Any running transition is now aiming at stale colours; restart from what is on screen.
	TransitionStartPreset = CurrentPreset;
	TransitionElapsedSeconds = 0.0f;
	bTransitioning = !bImmediate && TransitionDurationSeconds > KINDA_SMALL_NUMBER;

	if (!bTransitioning)
	{
		ApplyPreset(GetPreset(TargetTimeOfDay));
	}
}

void AMemoryRoomSkyDomeActor::CycleTimeOfDay()
{
	switch (TargetTimeOfDay)
	{
	case EMemoryRoomTimeOfDay::Dawn:
		SetTimeOfDay(EMemoryRoomTimeOfDay::Sunset);
		break;
	case EMemoryRoomTimeOfDay::Sunset:
		SetTimeOfDay(EMemoryRoomTimeOfDay::Night);
		break;
	case EMemoryRoomTimeOfDay::Night:
	default:
		SetTimeOfDay(EMemoryRoomTimeOfDay::Dawn);
		break;
	}
}

const FMemoryRoomSkyPreset& AMemoryRoomSkyDomeActor::GetPreset(EMemoryRoomTimeOfDay TimeOfDay) const
{
	switch (TimeOfDay)
	{
	case EMemoryRoomTimeOfDay::Dawn:
		return DawnPreset;
	case EMemoryRoomTimeOfDay::Night:
		return NightPreset;
	case EMemoryRoomTimeOfDay::Sunset:
	default:
		return SunsetPreset;
	}
}

FMemoryRoomSkyPreset AMemoryRoomSkyDomeActor::BlendPresets(const FMemoryRoomSkyPreset& From, const FMemoryRoomSkyPreset& To, float Alpha)
{
	const float ClampedAlpha = FMath::Clamp(Alpha, 0.0f, 1.0f);

	FMemoryRoomSkyPreset Result;
	Result.ZenithColor = FMath::Lerp(From.ZenithColor, To.ZenithColor, ClampedAlpha);
	Result.HorizonColor = FMath::Lerp(From.HorizonColor, To.HorizonColor, ClampedAlpha);
	Result.MidColor = FMath::Lerp(From.MidColor, To.MidColor, ClampedAlpha);
	Result.MidElevation = FMath::Lerp(From.MidElevation, To.MidElevation, ClampedAlpha);
	Result.GroundColor = FMath::Lerp(From.GroundColor, To.GroundColor, ClampedAlpha);
	Result.HorizonFalloff = FMath::Lerp(From.HorizonFalloff, To.HorizonFalloff, ClampedAlpha);
	Result.StarBrightness = FMath::Lerp(From.StarBrightness, To.StarBrightness, ClampedAlpha);
	Result.SunColor = FMath::Lerp(From.SunColor, To.SunColor, ClampedAlpha);
	Result.SunIntensity = FMath::Lerp(From.SunIntensity, To.SunIntensity, ClampedAlpha);
	Result.SunPitchDegrees = FMath::Lerp(From.SunPitchDegrees, To.SunPitchDegrees, ClampedAlpha);
	// Take the short way round so a 350 -> 10 degree move does not swing the sun all the
	// way back across the sky.
	Result.SunYawDegrees = From.SunYawDegrees + FMath::FindDeltaAngleDegrees(From.SunYawDegrees, To.SunYawDegrees) * ClampedAlpha;
	Result.SkyLightColor = FMath::Lerp(From.SkyLightColor, To.SkyLightColor, ClampedAlpha);
	Result.SkyLightIntensity = FMath::Lerp(From.SkyLightIntensity, To.SkyLightIntensity, ClampedAlpha);
	Result.PhotoBrightness = FMath::Lerp(From.PhotoBrightness, To.PhotoBrightness, ClampedAlpha);
	Result.FogInscatteringColor = FMath::Lerp(From.FogInscatteringColor, To.FogInscatteringColor, ClampedAlpha);
	Result.FogDensity = FMath::Lerp(From.FogDensity, To.FogDensity, ClampedAlpha);
	return Result;
}

FLinearColor AMemoryRoomSkyDomeActor::EvaluateSkyColor(const FMemoryRoomSkyPreset& Preset, float Elevation)
{
	// Above the horizon the sky runs through three bands, not two: the horizon glow, a mid
	// band, then the zenith. The mid band is what makes a sunset read as a sunset instead
	// of an orange line under a blue dome.
	if (Elevation >= 0.0f)
	{
		const float SafeMidElevation = FMath::Clamp(Preset.MidElevation, 0.02f, 0.95f);
		if (Elevation <= SafeMidElevation)
		{
			const float SafeFalloff = FMath::Max(Preset.HorizonFalloff, 0.1f);
			const float BandAlpha = FMath::Pow(FMath::Clamp(Elevation / SafeMidElevation, 0.0f, 1.0f), 1.0f / SafeFalloff);
			return FMath::Lerp(Preset.HorizonColor, Preset.MidColor, BandAlpha);
		}

		// Smoothstep the upper band so the join at MidElevation has no visible kink; both
		// branches are already equal to MidColor there.
		const float RawAlpha = FMath::Clamp((Elevation - SafeMidElevation) / (1.0f - SafeMidElevation), 0.0f, 1.0f);
		const float BandAlpha = RawAlpha * RawAlpha * (3.0f - 2.0f * RawAlpha);
		return FMath::Lerp(Preset.MidColor, Preset.ZenithColor, BandAlpha);
	}

	// Below it, fall off into the ground colour fairly quickly. Both branches meet at
	// HorizonColor when Elevation is 0, so there is no seam at the join.
	const float GroundAlpha = FMath::Clamp(-Elevation * 3.0f, 0.0f, 1.0f);
	return FMath::Lerp(Preset.HorizonColor, Preset.GroundColor, GroundAlpha);
}

void AMemoryRoomSkyDomeActor::EnsureGradientTexture()
{
	if (GradientTexture)
	{
		return;
	}

	const int32 Height = FMath::Clamp(GradientResolution, 32, 1024);
	GradientTexture = UTexture2D::CreateTransient(GradientTextureWidth, Height, PF_B8G8R8A8);
	if (!GradientTexture)
	{
		UE_LOG(LogMemoryRoom, Warning, TEXT("%s failed to create its sky gradient texture."), *GetName());
		return;
	}

	GradientTexture->SRGB = true;
	GradientTexture->NeverStream = true;
	GradientTexture->Filter = TF_Bilinear;
	// Clamp so the topmost and bottommost rows hold rather than wrapping the sky around.
	GradientTexture->AddressX = TA_Clamp;
	GradientTexture->AddressY = TA_Clamp;
	GradientTexture->LODGroup = TEXTUREGROUP_UI;

	if (FTexturePlatformData* PlatformData = GradientTexture->GetPlatformData())
	{
		if (PlatformData->Mips.Num() > 0)
		{
			FTexture2DMipMap& Mip = PlatformData->Mips[0];
			if (void* Data = Mip.BulkData.Lock(LOCK_READ_WRITE))
			{
				FMemory::Memzero(Data, static_cast<SIZE_T>(GradientTextureWidth) * Height * sizeof(FColor));
			}
			Mip.BulkData.Unlock();
		}
	}
	GradientTexture->UpdateResource();
}

void AMemoryRoomSkyDomeActor::EnsureStarTexture()
{
	if (StarTexture)
	{
		return;
	}

	const int32 Width = StarTextureWidth;
	const int32 Height = StarTextureHeight;
	StarTexture = UTexture2D::CreateTransient(Width, Height, PF_B8G8R8A8);
	if (!StarTexture)
	{
		UE_LOG(LogMemoryRoom, Warning, TEXT("%s failed to create its star texture."), *GetName());
		return;
	}

	StarTexture->SRGB = false;
	StarTexture->NeverStream = true;
	StarTexture->Filter = TF_Bilinear;
	StarTexture->AddressX = TA_Wrap;
	StarTexture->AddressY = TA_Clamp;
	StarTexture->LODGroup = TEXTUREGROUP_UI;

	FTexturePlatformData* PlatformData = StarTexture->GetPlatformData();
	if (!PlatformData || PlatformData->Mips.Num() == 0)
	{
		return;
	}

	FTexture2DMipMap& Mip = PlatformData->Mips[0];
	FColor* Pixels = static_cast<FColor*>(Mip.BulkData.Lock(LOCK_READ_WRITE));
	if (!Pixels)
	{
		Mip.BulkData.Unlock();
		return;
	}

	FMemory::Memzero(Pixels, static_cast<SIZE_T>(Width) * Height * sizeof(FColor));

	// Seeded so the constellations are the same every run. A sky whose stars rearrange
	// themselves between sessions would quietly undermine the whole idea of a memory room.
	FRandomStream Random(StarSeed);
	const int32 SafeStarCount = FMath::Clamp(StarCount, 0, 20000);
	for (int32 StarIndex = 0; StarIndex < SafeStarCount; ++StarIndex)
	{
		const int32 X = Random.RandRange(0, Width - 1);
		// Row 0 is the zenith and the halfway row is the horizon, matching the gradient.
		// Stars live above the horizon only.
		const int32 Y = Random.RandRange(0, Height / 2 - 1);

		// Most stars faint, a few bright. A flat distribution reads as noise rather than sky.
		float Brightness = FMath::Pow(Random.FRand(), 4.0f);

		// Fade out approaching the horizon, where haze would wash them out anyway.
		const float HorizonProximity = static_cast<float>(Y) / static_cast<float>(FMath::Max(Height / 2 - 1, 1));
		Brightness *= FMath::Clamp(1.0f - FMath::Pow(HorizonProximity, 3.0f), 0.0f, 1.0f);

		const uint8 Core = static_cast<uint8>(FMath::Clamp(Brightness * 255.0f, 0.0f, 255.0f));
		if (Core == 0)
		{
			continue;
		}

		// Bleed a quarter of the brightness into the four neighbours so bilinear filtering
		// on a dome this large does not thin the stars away to nothing.
		const uint8 Halo = static_cast<uint8>(Core / 4);
		auto WritePixel = [Pixels, Width, Height](int32 PixelX, int32 PixelY, uint8 Value)
		{
			if (PixelY < 0 || PixelY >= Height)
			{
				return;
			}
			const int32 WrappedX = ((PixelX % Width) + Width) % Width;
			FColor& Target = Pixels[PixelY * Width + WrappedX];
			// Stars can land on each other; keep the brighter one rather than summing to white.
			const uint8 Existing = Target.R;
			if (Value > Existing)
			{
				Target = FColor(Value, Value, Value, 255);
			}
		};

		WritePixel(X, Y, Core);
		WritePixel(X + 1, Y, Halo);
		WritePixel(X - 1, Y, Halo);
		WritePixel(X, Y + 1, Halo);
		WritePixel(X, Y - 1, Halo);
	}

	Mip.BulkData.Unlock();
	StarTexture->UpdateResource();
	UE_LOG(LogMemoryRoom, Log, TEXT("%s generated a %dx%d star field with %d stars."), *GetName(), Width, Height, SafeStarCount);
}

void AMemoryRoomSkyDomeActor::UpdateGradientTexture(const FMemoryRoomSkyPreset& Preset)
{
	EnsureGradientTexture();
	if (!GradientTexture)
	{
		return;
	}

	const int32 Width = GradientTextureWidth;
	const int32 Height = FMath::Clamp(GradientResolution, 32, 1024);

	// Owned by the render command; released in the cleanup callback below.
	uint8* RawPixels = new uint8[static_cast<SIZE_T>(Width) * Height * sizeof(FColor)];
	FColor* Pixels = reinterpret_cast<FColor*>(RawPixels);

	for (int32 Y = 0; Y < Height; ++Y)
	{
		const float V = static_cast<float>(Y) / static_cast<float>(Height - 1);
		const float SampleV = bFlipGradient ? 1.0f - V : V;

		// A UV sphere's V runs linearly in latitude, so cosine converts it to the sine of
		// the elevation angle. That makes the horizon land exactly halfway up the sphere
		// and matches how a view-direction gradient would distribute the colours.
		const float Elevation = FMath::Cos(PI * SampleV);
		const FLinearColor Color = EvaluateSkyColor(Preset, Elevation);

		for (int32 X = 0; X < Width; ++X)
		{
			// Eight bits per channel bands visibly across a sky this large, so offset each
			// pixel by well under one step. The eye averages the noise; the bands go away.
			const float Dither = ((((X * 7) + (Y * 3)) % 8) / 8.0f - 0.5f) / 255.0f;
			const FLinearColor Dithered(Color.R + Dither, Color.G + Dither, Color.B + Dither, 1.0f);
			Pixels[Y * Width + X] = Dithered.ToFColor(true);
		}
	}

	FUpdateTextureRegion2D* Region = new FUpdateTextureRegion2D(0, 0, 0, 0, Width, Height);
	GradientTexture->UpdateTextureRegions(0, 1, Region, Width * sizeof(FColor), sizeof(FColor), RawPixels,
		[](uint8* SourceData, const FUpdateTextureRegion2D* Regions)
		{
			delete[] SourceData;
			delete Regions;
		});
}

void AMemoryRoomSkyDomeActor::SetFocusDim(float NewFocusDim)
{
	const float Clamped = FMath::Clamp(NewFocusDim, 0.0f, 1.0f);
	if (FMath::IsNearlyEqual(Clamped, FocusDim, 0.002f))
	{
		return;
	}

	FocusDim = Clamped;
	ApplyPresetToScene(CurrentPreset);
}

void AMemoryRoomSkyDomeActor::ApplyPreset(const FMemoryRoomSkyPreset& Preset)
{
	// Remember the undimmed look so focus can be re-applied on top of it later without
	// compounding, and without needing to know where we are in a transition.
	CurrentPreset = Preset;
	ApplyPresetToScene(Preset);
}

void AMemoryRoomSkyDomeActor::ApplyPresetToScene(const FMemoryRoomSkyPreset& Preset)
{
	const float DimScale = FMath::Lerp(1.0f, 1.0f - FMath::Clamp(FocusSkyDimAmount, 0.0f, 1.0f), FocusDim);

	FMemoryRoomSkyPreset Dimmed = Preset;
	Dimmed.ZenithColor *= DimScale;
	Dimmed.HorizonColor *= DimScale;
	Dimmed.MidColor *= DimScale;
	Dimmed.GroundColor *= DimScale;
	Dimmed.StarBrightness *= DimScale;
	Dimmed.SunIntensity *= DimScale;
	Dimmed.SkyLightIntensity *= DimScale;
	Dimmed.FogInscatteringColor *= DimScale;

	UpdateGradientTexture(Dimmed);
	// The open photo keeps its own brightness; dimming it would defeat the point.
	CurrentPhotoBrightness = Preset.PhotoBrightness;

	const FMemoryRoomSkyPreset& Applied = Dimmed;

	if (DomeMaterialInstance)
	{
		if (!GradientTextureParameterName.IsNone() && GradientTexture)
		{
			DomeMaterialInstance->SetTextureParameterValue(GradientTextureParameterName, GradientTexture);
		}
		if (!StarTextureParameterName.IsNone() && StarTexture)
		{
			DomeMaterialInstance->SetTextureParameterValue(StarTextureParameterName, StarTexture);
		}
		if (!ZenithColorParameterName.IsNone())
		{
			DomeMaterialInstance->SetVectorParameterValue(ZenithColorParameterName, Applied.ZenithColor);
		}
		if (!HorizonColorParameterName.IsNone())
		{
			DomeMaterialInstance->SetVectorParameterValue(HorizonColorParameterName, Applied.HorizonColor);
		}
		if (!GroundColorParameterName.IsNone())
		{
			DomeMaterialInstance->SetVectorParameterValue(GroundColorParameterName, Applied.GroundColor);
		}
		if (!HorizonFalloffParameterName.IsNone())
		{
			DomeMaterialInstance->SetScalarParameterValue(HorizonFalloffParameterName, Applied.HorizonFalloff);
		}
		if (!StarBrightnessParameterName.IsNone())
		{
			DomeMaterialInstance->SetScalarParameterValue(StarBrightnessParameterName, Applied.StarBrightness);
		}
	}

	if (SunLight)
	{
		SunLight->SetWorldRotation(FRotator(Applied.SunPitchDegrees, Applied.SunYawDegrees, 0.0f));
		SunLight->SetLightColor(Applied.SunColor);
		SunLight->SetIntensity(Applied.SunIntensity);
	}

	if (SkyLight)
	{
		SkyLight->SetLightColor(Applied.SkyLightColor);
		SkyLight->SetIntensity(Applied.SkyLightIntensity);
	}

	if (bDriveExponentialHeightFog)
	{
		if (AExponentialHeightFog* Fog = CachedHeightFog.Get())
		{
			if (UExponentialHeightFogComponent* FogComponent = Fog->GetComponent())
			{
				FogComponent->SetFogInscatteringColor(Applied.FogInscatteringColor);
				FogComponent->SetFogDensity(Applied.FogDensity);
			}
		}
	}
}

FText AMemoryRoomSkyDomeActor::GetDisplayNameForTimeOfDay(EMemoryRoomTimeOfDay TimeOfDay)
{
	switch (TimeOfDay)
	{
	case EMemoryRoomTimeOfDay::Dawn:
		return NSLOCTEXT("MemoryRoom", "TimeOfDayDawn", "Dawn");
	case EMemoryRoomTimeOfDay::Night:
		return NSLOCTEXT("MemoryRoom", "TimeOfDayNight", "Night");
	case EMemoryRoomTimeOfDay::Sunset:
	default:
		return NSLOCTEXT("MemoryRoom", "TimeOfDaySunset", "Sunset");
	}
}

FText AMemoryRoomSkyDomeActor::GetTimeOfDayDisplayName() const
{
	return GetDisplayNameForTimeOfDay(TargetTimeOfDay);
}

AMemoryRoomSkyDomeActor* AMemoryRoomSkyDomeActor::FindOrSpawn(UWorld* World, TSubclassOf<AMemoryRoomSkyDomeActor> SkyDomeClass)
{
	if (!World)
	{
		return nullptr;
	}

	for (TActorIterator<AMemoryRoomSkyDomeActor> It(World); It;)
	{
		return *It;
	}

	UClass* ClassToSpawn = SkyDomeClass ? SkyDomeClass.Get() : AMemoryRoomSkyDomeActor::StaticClass();
	return World->SpawnActor<AMemoryRoomSkyDomeActor>(ClassToSpawn, FTransform::Identity);
}

void AMemoryRoomSkyDomeActor::SaveTimeOfDay() const
{
	// Shares the quality slot so the room restores as one set of preferences. Read the
	// existing save first so writing the sky choice does not wipe the quality settings.
	UMemoryRoomQualitySaveGame* Save = Cast<UMemoryRoomQualitySaveGame>(UGameplayStatics::LoadGameFromSlot(SkySaveSlotName, 0));
	if (!Save)
	{
		Save = Cast<UMemoryRoomQualitySaveGame>(UGameplayStatics::CreateSaveGameObject(UMemoryRoomQualitySaveGame::StaticClass()));
	}
	if (!Save)
	{
		return;
	}

	Save->TimeOfDay = TargetTimeOfDay;
	Save->bHasSavedTimeOfDay = true;
	UGameplayStatics::SaveGameToSlot(Save, SkySaveSlotName, 0);
}

void AMemoryRoomSkyDomeActor::LoadTimeOfDay()
{
	if (const UMemoryRoomQualitySaveGame* Save = Cast<UMemoryRoomQualitySaveGame>(UGameplayStatics::LoadGameFromSlot(SkySaveSlotName, 0)))
	{
		if (Save->bHasSavedTimeOfDay)
		{
			TargetTimeOfDay = Save->TimeOfDay;
		}
	}
}
