// Fill out your copyright notice in the Description page of Project Settings.


#include "AMemoryRoomManager.h"
#include "MemoryRoom.h"

#if PLATFORM_ANDROID
#include "AndroidMediaStorePhotoSource.h"
#endif

#include "Components/AudioComponent.h"
#include "Components/InputComponent.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "MemoryRoomMenuActor.h"
#include "MemoryRoomDustActor.h"
#include "MemoryRoomGroundActor.h"
#include "MemoryRoomSkyButtonActor.h"
#include "MemoryRoomUIGlass.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "MemoryRoomSkyDomeActor.h"
#include "MemoryRoomQualitySaveGame.h"
#include "MemoryRoomPlacedPhotosSaveGame.h"
#include "Sound/SoundBase.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PawnMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformFileManager.h"
#include "Misc/Paths.h"

namespace
{
FRotator MakeUprightLookAtRotation(const FVector& FromLocation, const FVector& LookAtLocation, const FRotator& FallbackRotation, const FRotator& RotationOffset)
{
	FVector DirectionToViewer = LookAtLocation - FromLocation;
	DirectionToViewer.Z = 0.0f;

	if (DirectionToViewer.IsNearlyZero())
	{
		return FallbackRotation + RotationOffset;
	}

	FRotator UprightRotation = DirectionToViewer.Rotation();
	UprightRotation.Pitch = 0.0f;
	UprightRotation.Roll = 0.0f;
	return UprightRotation + RotationOffset;
}
}

// Sets default values
AMemoryRoomManager::AMemoryRoomManager()
{
	PrimaryActorTick.bCanEverTick = true;

	NumberOfPhotos = 8;
	Radius = 500.0f;
	PhotoHeight = 0.0f;
	ScrollOffset = 0.0f;
	TargetScrollOffset = 0.0f;
	ScrollSpeed = 1.0f;
	ScrollSmoothing = 8.0f;
	ScrollAcceleration = 4.0f;
	ScrollFriction = 3.0f;
	MaximumScrollSpeed = 12.0f;
	bAutoBindThumbstickScrollInput = true;
	ScrollInputAxisName = TEXT("MemoryRoomScroll");
	bEnableHelixLayout = true;
	bCenterHelixOnUser = true;
	HelixRadius = 500.0f;
	DomeHeight = 2400.0f;
	BaseRadius = 650.0f;
	TopRadius = 250.0f;
	DomeCurveStrength = 1.5f;
	PhotoRotationOffset = FRotator::ZeroRotator;
	MinPhotoScale = 0.85f;
	MaxPhotoScale = 1.25f;
	SizeVariationSeed = 1337;
	// Path parameter only advances ~0.3 per photo (spacing is by real distance along the
	// arc), so 120 sent the spiral ~37m up for 100 photos and everything past the first
	// dozen ended up tiny and faded near the ceiling. Keep the tower short.
	VerticalSpacing = 45.0f;
	DegreesPerPhoto = 35.0f;
	PhotoWidthSpacingOffset = 140.0f;
	PhotoPathGap = 20.0f;
	PathPlacementStep = 0.025f;
	LayoutOverlapLookback = 12;
	PhotoOverlapSafetyGap = 10.0f;
	// Each placement iteration now resolves the deepest overlap in one move rather than
	// creeping forward by PathPlacementStep, so a small budget is plenty.
	MaximumPlacementIterations = 96;
	AntiClusterAngleDegrees = 32.0f;
	AntiClusterLookback = 8;
	StartHeight = 0.0f;
	// Keep a wide band of photos fully readable. The fade only exists to soften the two
	// ends of the spiral, not to hide most of the gallery.
	VisibleRangeBelow = 8.0f;
	VisibleRangeAbove = 60.0f;
	TopFadeStart = 44.0f;
	TopFadeEnd = 58.0f;
	TopMaxBlurAmount = 1.0f;
	TopThreadMinimumOpacity = 0.35f;
	TopMinimumScale = 0.85f;
	BottomFadeStart = -6.0f;
	BottomFadeEnd = -3.0f;
	bEnableDomeLayout = true;
	DomeLayoutPhotoThreshold = 12;
	DomeMinElevationDegrees = 0.0f;
	DomeMaxElevationDegrees = 65.0f;
	PhotoClass = nullptr;
	PhotoFolderPath = TEXT("");
	MaxPhotosToLoad = 0;
	bBuildGalleryOnBeginPlay = false;
	// A batch is now a header read rather than a full decode, so many per tick is cheap.
	PhotosToLoadPerBatch = 32;
	BatchDelaySeconds = 0.0f;
	MaximumRuntimeTextureDimension = 2048;
	bAndroidSingleImageDiagnostic = false;
	AndroidPhotoActorPoolSize = 20;
	AndroidThumbnailMaxDimension = 384;
	AndroidMaxSimultaneousThumbnailDecodes = 2;
	AndroidMaxThumbnailCacheEntries = 40;
	AndroidExpandedPhotoMaxDimension = 2048;
	AndroidExpandedCacheEntries = 2;
	AndroidMaxConcurrentExpandedDecodes = 1;
	LoadedPhotoCount = 0;
	FailedPhotoCount = 0;
	TotalPhotosToLoad = 0;
	bIsLoadingGallery = false;
	ScrollVelocity = 0.0f;
	ScrollInputAmount = 0.0f;
	HeldScrollInputAmount = 0.0f;
	LastAppliedScrollOffset = 0.0f;
	InitialScrollOffset = 0.0f;
	bGalleryScrollSuppressed = false;
}

FVector AMemoryRoomManager::GetHelixCenterLocation() const
{
	return CachedHelixCenterLocation;
}

void AMemoryRoomManager::RefreshCachedHelixCenterLocation()
{
	// Free locomotion means the player walks around a gallery that stays put, so the spiral
	// must NOT chase the head - lock it to this manager's placed location.
	if (bAllowFreeLocomotion || !bCenterHelixOnUser)
	{
		CachedHelixCenterLocation = GetActorLocation();
		return;
	}

	if (const UWorld* World = GetWorld())
	{
		if (const APlayerController* PlayerController = World->GetFirstPlayerController())
		{
			FVector ViewLocation = FVector::ZeroVector;
			FRotator ViewRotation = FRotator::ZeroRotator;
			PlayerController->GetPlayerViewPoint(ViewLocation, ViewRotation);
			CachedHelixCenterLocation = ViewLocation;
			return;
		}
	}

	CachedHelixCenterLocation = GetActorLocation();
}

float AMemoryRoomManager::GetDeterministicPhotoScale(int32 PhotoIndex) const
{
	const float SafeMinScale = FMath::Max(MinPhotoScale, 0.01f);
	const float SafeMaxScale = FMath::Max(MaxPhotoScale, SafeMinScale);
	if (FMath::IsNearlyEqual(SafeMinScale, SafeMaxScale))
	{
		return SafeMinScale;
	}

	uint32 Hash = static_cast<uint32>(SizeVariationSeed) ^ (static_cast<uint32>(PhotoIndex + 1) * 2654435761u);
	Hash ^= Hash >> 16;
	Hash *= 2246822519u;
	Hash ^= Hash >> 13;
	Hash *= 3266489917u;
	Hash ^= Hash >> 16;

	const float UnitValue = static_cast<float>(Hash & 0x00FFFFFFu) / static_cast<float>(0x00FFFFFFu);
	return FMath::Lerp(SafeMinScale, SafeMaxScale, UnitValue);
}

float AMemoryRoomManager::GetEstimatedPhotoWidth(float PhotoScale) const
{
	const AMemoryPhotoActor* PhotoDefaults = PhotoClass ? PhotoClass->GetDefaultObject<AMemoryPhotoActor>() : nullptr;
	const float Width = PhotoDefaults ? PhotoDefaults->GetMaxPhotoWidth() : 1.0f;
	return FMath::Max(Width, 0.01f) * FMath::Max(PhotoScale, 0.01f) * 100.0f;
}

float AMemoryRoomManager::GetEstimatedPhotoHeight(float PhotoScale) const
{
	const AMemoryPhotoActor* PhotoDefaults = PhotoClass ? PhotoClass->GetDefaultObject<AMemoryPhotoActor>() : nullptr;
	const float Height = PhotoDefaults ? PhotoDefaults->GetBasePhotoHeight() : 1.0f;
	return FMath::Max(Height, 0.01f) * FMath::Max(PhotoScale, 0.01f) * 100.0f;
}

float AMemoryRoomManager::GetPhotoLayoutWidth(int32 PhotoIndex, float PhotoScale) const
{
	const float SafePhotoScale = FMath::Max(PhotoScale, 0.01f);
	if (SpawnedPhotos.IsValidIndex(PhotoIndex))
	{
		if (const AMemoryPhotoActor* Photo = SpawnedPhotos[PhotoIndex].Get())
		{
			const float LoadedVisualWidth = Photo->GetPhotoVisualHalfWidthLocal() * 2.0f;
			if (LoadedVisualWidth > KINDA_SMALL_NUMBER)
			{
				return LoadedVisualWidth * SafePhotoScale;
			}
		}
	}

	return GetEstimatedPhotoWidth(SafePhotoScale);
}

float AMemoryRoomManager::GetPhotoCenterSpacingWidth(int32 LeftPhotoIndex, int32 RightPhotoIndex) const
{
	const float LeftScale = GetDeterministicPhotoScale(LeftPhotoIndex);
	const float RightScale = GetDeterministicPhotoScale(RightPhotoIndex);
	const float LeftWidth = GetPhotoLayoutWidth(LeftPhotoIndex, LeftScale);
	const float RightWidth = GetPhotoLayoutWidth(RightPhotoIndex, RightScale);
	return (LeftWidth + RightWidth) * 0.5f + FMath::Max(PhotoWidthSpacingOffset, 0.0f);
}

float AMemoryRoomManager::GetPhotoAngularPositionDegrees(int32 PhotoIndex, float PathIndex, float CurrentRadius) const
{
	TArray<float> PreviousAnglesDegrees;
	PreviousAnglesDegrees.Reserve(FMath::Max(PhotoIndex, 0));

	float AngleDegrees = 0.0f;
	for (int32 SegmentIndex = 0; SegmentIndex < PhotoIndex; ++SegmentIndex)
	{
		PreviousAnglesDegrees.Add(AngleDegrees);
		const float SegmentStepDegrees = GetMinAngularStepDegrees(GetPhotoCenterSpacingWidth(SegmentIndex, SegmentIndex + 1), CurrentRadius);
		const float ProposedAngleDegrees = AngleDegrees + FMath::Max(DegreesPerPhoto, SegmentStepDegrees);
		AngleDegrees = GetAntiClusterAdjustedAngleDegrees(SegmentIndex + 1, ProposedAngleDegrees, PreviousAnglesDegrees);
	}

	const float IntegerAngleDegrees = AngleDegrees;
	const float FractionalPathOffset = PathIndex - static_cast<float>(PhotoIndex);
	if (!FMath::IsNearlyZero(FractionalPathOffset))
	{
		const int32 NeighborIndex = FMath::Clamp(PhotoIndex + (FractionalPathOffset >= 0.0f ? 1 : -1), 0, FMath::Max(NumberOfPhotos - 1, 0));
		const float FractionalStepDegrees = GetMinAngularStepDegrees(GetPhotoCenterSpacingWidth(PhotoIndex, NeighborIndex), CurrentRadius);
		const float TargetAngleDegrees = IntegerAngleDegrees + FMath::Sign(FractionalPathOffset) * FMath::Max(DegreesPerPhoto, FractionalStepDegrees);
		AngleDegrees = FMath::Lerp(IntegerAngleDegrees, TargetAngleDegrees, FMath::Abs(FractionalPathOffset));
	}

	return AngleDegrees;
}

float AMemoryRoomManager::GetAntiClusterAdjustedAngleDegrees(int32 PhotoIndex, float ProposedAngleDegrees, const TArray<float>& PreviousAnglesDegrees) const
{
	const float SafeAvoidDegrees = FMath::Max(AntiClusterAngleDegrees, 0.0f);
	const int32 SafeLookback = FMath::Max(AntiClusterLookback, 0);
	const float PerPhotoMicroStaggerDegrees = static_cast<float>((PhotoIndex * 7) % 11) * 0.35f;
	if (SafeAvoidDegrees <= KINDA_SMALL_NUMBER || SafeLookback <= 0 || PreviousAnglesDegrees.Num() <= 0)
	{
		return ProposedAngleDegrees;
	}

	// A pure helix can accidentally create vertical columns when the step becomes a neat
	// divisor of 360 degrees. That reads as random clumps from the viewer. Nudge only
	// forward, and only against recent turns, so the thread stays continuous but avoids
	// repeatedly occupying the same angular slice.
	float AdjustedAngleDegrees = ProposedAngleDegrees;
	const int32 FirstPreviousIndex = FMath::Max(0, PreviousAnglesDegrees.Num() - SafeLookback);
	for (int32 PassIndex = 0; PassIndex < 2; ++PassIndex)
	{
		for (int32 PreviousIndex = FirstPreviousIndex; PreviousIndex < PreviousAnglesDegrees.Num(); ++PreviousIndex)
		{
			const float PreviousAngleDegrees = PreviousAnglesDegrees[PreviousIndex];
			const float DeltaDegrees = FMath::FindDeltaAngleDegrees(PreviousAngleDegrees, AdjustedAngleDegrees);
			const float AbsDeltaDegrees = FMath::Abs(DeltaDegrees);
			if (AbsDeltaDegrees >= SafeAvoidDegrees)
			{
				continue;
			}

			AdjustedAngleDegrees += SafeAvoidDegrees - AbsDeltaDegrees + 2.0f + PerPhotoMicroStaggerDegrees;
		}
	}

	return FMath::Max(AdjustedAngleDegrees, ProposedAngleDegrees);
}

float AMemoryRoomManager::GetMinAngularStepDegrees(float PhotoWidth, float CurrentRadius) const
{
	const float SafeRadius = FMath::Max(CurrentRadius, 1.0f);
	// Add a conservative safety multiplier because authored photo mesh axes/rotations can vary
	// between blueprints, and a pure chord calculation can still look edge-to-edge in VR.
	const float RequiredChord = FMath::Max(PhotoWidth, 0.01f) * 1.25f;
	const float HalfChordRatio = FMath::Clamp(RequiredChord / (2.0f * SafeRadius), 0.0f, 0.95f);
	return FMath::RadiansToDegrees(2.0f * FMath::Asin(HalfChordRatio)) + 5.0f;
}

FVector AMemoryRoomManager::EvaluateHelixPosition(float PathParameter) const
{
	const FVector CenterLocation = GetHelixCenterLocation();
	const float SafeDomeHeight = FMath::Max(DomeHeight, 1.0f);
	const float SafeDomeCurveStrength = FMath::Max(DomeCurveStrength, 0.01f);
	const float HeightOffset = StartHeight + PathParameter * VerticalSpacing;
	const float HeightAlpha = FMath::Clamp((HeightOffset - StartHeight) / SafeDomeHeight, 0.0f, 1.0f);
	const float CurvedAlpha = FMath::Pow(HeightAlpha, SafeDomeCurveStrength);
	const float CurrentRadius = FMath::Lerp(BaseRadius, TopRadius, CurvedAlpha);
	const float AngleDegrees = PathParameter * DegreesPerPhoto;
	const FVector HelixOffset = FVector(CurrentRadius, 0.0f, 0.0f).RotateAngleAxis(AngleDegrees, FVector::UpVector);
	return CenterLocation + FVector(HelixOffset.X, HelixOffset.Y, PhotoHeight + HeightOffset);
}

FVector AMemoryRoomManager::EvaluateHelixTangent(float PathParameter) const
{
	const float Epsilon = FMath::Max(PathPlacementStep, 0.001f);
	return (EvaluateHelixPosition(PathParameter + Epsilon) - EvaluateHelixPosition(PathParameter - Epsilon)).GetSafeNormal();
}

float AMemoryRoomManager::GetHelixArcSpeed(float PathParameter) const
{
	const float Epsilon = FMath::Max(PathPlacementStep, 0.001f);
	const float ChordLength = FVector::Distance(EvaluateHelixPosition(PathParameter - Epsilon), EvaluateHelixPosition(PathParameter + Epsilon));
	return FMath::Max(ChordLength / (2.0f * Epsilon), KINDA_SMALL_NUMBER);
}

float AMemoryRoomManager::AdvanceAlongHelixByDistance(float StartParameter, float TargetDistance) const
{
	if (TargetDistance <= KINDA_SMALL_NUMBER)
	{
		return StartParameter;
	}

	const FVector StartPosition = EvaluateHelixPosition(StartParameter);
	float Parameter = StartParameter + TargetDistance / GetHelixArcSpeed(StartParameter);

	// The radius tapers as the helix rises, so a first-order guess drifts. A few secant
	// corrections land within a fraction of a centimetre, versus the hundreds of fixed
	// steps the previous implementation needed to cover the same distance.
	const float ToleranceCm = 0.5f;
	for (int32 Refinement = 0; Refinement < 4; ++Refinement)
	{
		const float CurrentDistance = FVector::Distance(StartPosition, EvaluateHelixPosition(Parameter));
		const float DistanceError = TargetDistance - CurrentDistance;
		if (FMath::Abs(DistanceError) <= ToleranceCm)
		{
			break;
		}
		Parameter += DistanceError / GetHelixArcSpeed(Parameter);
	}

	// Never hand back a parameter at or behind the start; callers rely on forward progress.
	return FMath::Max(Parameter, StartParameter + FMath::Max(PathPlacementStep, 0.001f));
}

float AMemoryRoomManager::GetLayoutFootprintRadius(int32 PhotoIndex) const
{
	const float Scale = GetDeterministicPhotoScale(PhotoIndex);

	// Only trust a spawned actor's measured size when every photo owns an actor. Under
	// pooling, SpawnedPhotos[i] is a recycled slot showing some other photo, so reading
	// its display size here made the cached path depend on scroll position.
	const bool bHasDedicatedActorPerPhoto = NumberOfPhotos > 0 && SpawnedPhotos.Num() >= NumberOfPhotos;
	float DisplayWidth = GetEstimatedPhotoWidth(1.0f);
	float DisplayHeight = GetEstimatedPhotoHeight(1.0f);
	if (bHasDedicatedActorPerPhoto && SpawnedPhotos.IsValidIndex(PhotoIndex))
	{
		if (const AMemoryPhotoActor* Photo = SpawnedPhotos[PhotoIndex].Get())
		{
			DisplayWidth = Photo->GetDisplayWidth();
			DisplayHeight = Photo->GetDisplayHeight();
		}
	}

	return 0.5f * FMath::Sqrt(FMath::Square(DisplayWidth) + FMath::Square(DisplayHeight)) * Scale;
}

void AMemoryRoomManager::RebuildPhotoPathCache()
{
	RefreshCachedHelixCenterLocation();

	const int32 PhotoCount = FMath::Max(NumberOfPhotos, SpawnedPhotos.Num());
	if (PhotoCount < CachedPhotoPathParameters.Num())
	{
		CachedPhotoPathParameters.Reset();
		CachedFootprintRadii.Reset();
		CachedPhotoPositions.Reset();
	}
	CachedPhotoPathParameters.Reserve(PhotoCount);
	CachedFootprintRadii.Reserve(PhotoCount);
	CachedPhotoPositions.Reserve(PhotoCount);

	if (PhotoCount <= 0)
	{
		UE_LOG(LogMemoryRoom, Log, TEXT("%s rebuilt helix path cache: 0 photo(s)."), *GetName());
		return;
	}

	const double BuildStartSeconds = FPlatformTime::Seconds();
	const int32 FirstPhotoIndexToBuild = CachedPhotoPathParameters.Num();
	const float SafePathGap = FMath::Max(PhotoPathGap, 0.0f);
	const float SafeOverlapGap = FMath::Max(PhotoOverlapSafetyGap, 0.0f);
	const int32 SafeLookback = FMath::Max(LayoutOverlapLookback, 0);
	const int32 SafeMaxIterations = FMath::Max(MaximumPlacementIterations, 1);

	int32 MaxOverlapIterationsUsed = 0;
	bool bReachedSafetyLimit = false;

	for (int32 PhotoIndex = FirstPhotoIndexToBuild; PhotoIndex < PhotoCount; ++PhotoIndex)
	{
		const float FootprintRadius = GetLayoutFootprintRadius(PhotoIndex);
		CachedFootprintRadii.Add(FootprintRadius);

		float ChosenParameter = 0.0f;
		int32 OverlapIterations = 0;

		if (PhotoIndex > 0)
		{
			const float PreviousParameter = CachedPhotoPathParameters[PhotoIndex - 1];
			const float Scale = GetDeterministicPhotoScale(PhotoIndex);
			const float PreviousScale = GetDeterministicPhotoScale(PhotoIndex - 1);
			const bool bHasDedicatedActorPerPhoto = NumberOfPhotos > 0 && SpawnedPhotos.Num() >= NumberOfPhotos;
			const AMemoryPhotoActor* Photo = bHasDedicatedActorPerPhoto && SpawnedPhotos.IsValidIndex(PhotoIndex) ? SpawnedPhotos[PhotoIndex].Get() : nullptr;
			const AMemoryPhotoActor* PreviousPhoto = bHasDedicatedActorPerPhoto && SpawnedPhotos.IsValidIndex(PhotoIndex - 1) ? SpawnedPhotos[PhotoIndex - 1].Get() : nullptr;
			const float DisplayWidth = Photo ? Photo->GetDisplayWidth() : GetEstimatedPhotoWidth(1.0f);
			const float PreviousWidth = PreviousPhoto ? PreviousPhoto->GetDisplayWidth() : GetEstimatedPhotoWidth(1.0f);
			const float RequiredDistance = PreviousWidth * 0.5f * PreviousScale + DisplayWidth * 0.5f * Scale + SafePathGap;

			ChosenParameter = AdvanceAlongHelixByDistance(PreviousParameter, RequiredDistance);

			// Push forward until the candidate clears every recent neighbour. Each pass
			// converts the deepest overlap straight into a distance to travel, so this
			// converges in a handful of iterations instead of creeping by a fixed epsilon.
			const int32 FirstCheckedIndex = FMath::Max(0, PhotoIndex - SafeLookback);
			bool bClearsRecentPhotos = false;
			while (!bClearsRecentPhotos && OverlapIterations < SafeMaxIterations)
			{
				bClearsRecentPhotos = true;
				const FVector CandidatePosition = EvaluateHelixPosition(ChosenParameter);
				float DeepestPenetration = 0.0f;
				for (int32 PreviousIndex = FirstCheckedIndex; PreviousIndex < PhotoIndex; ++PreviousIndex)
				{
					const float RequiredCenterDistance = FootprintRadius + CachedFootprintRadii[PreviousIndex] + SafeOverlapGap;
					const float CenterDistance = FVector::Distance(CandidatePosition, CachedPhotoPositions[PreviousIndex]);
					if (CenterDistance < RequiredCenterDistance)
					{
						bClearsRecentPhotos = false;
						DeepestPenetration = FMath::Max(DeepestPenetration, RequiredCenterDistance - CenterDistance);
					}
				}

				if (!bClearsRecentPhotos)
				{
					ChosenParameter = AdvanceAlongHelixByDistance(ChosenParameter, FMath::Max(DeepestPenetration, 1.0f));
					++OverlapIterations;
				}
			}

			if (!bClearsRecentPhotos)
			{
				bReachedSafetyLimit = true;
			}
		}

		CachedPhotoPathParameters.Add(ChosenParameter);
		CachedPhotoPositions.Add(EvaluateHelixPosition(ChosenParameter));
		MaxOverlapIterationsUsed = FMath::Max(MaxOverlapIterationsUsed, OverlapIterations);
	}

	const double BuildMilliseconds = (FPlatformTime::Seconds() - BuildStartSeconds) * 1000.0;
	UE_LOG(LogMemoryRoom, Log, TEXT("%s rebuilt helix path cache: %d photo(s) (%d new) in %.2f ms, max overlap iterations %d, reached safety limit %s."),
		*GetName(), CachedPhotoPathParameters.Num(), PhotoCount - FirstPhotoIndexToBuild, BuildMilliseconds, MaxOverlapIterationsUsed, bReachedSafetyLimit ? TEXT("true") : TEXT("false"));
}

int32 AMemoryRoomManager::GetRequiredPhotoPoolSize() const
{
	const float LowerBound = -FMath::Max(VisibleRangeBelow, 0.0f);
	const float UpperBound = FMath::Max(VisibleRangeAbove, 0.0f);
	int32 PoolSize = FMath::Clamp(FMath::CeilToInt(UpperBound - LowerBound) + 2, 1, FMath::Max(NumberOfPhotos, 1));
#if PLATFORM_ANDROID
	if (MediaStorePhotos.Num() > 0 && !bAndroidSingleImageDiagnostic)
	{
		PoolSize = FMath::Min(PoolSize, FMath::Clamp(AndroidPhotoActorPoolSize, 1, FMath::Max(NumberOfPhotos, 1)));
	}
#endif
	return PoolSize;
}

AMemoryRoomManager::FPhotoViewingState AMemoryRoomManager::BuildPhotoViewingState(float PathIndex) const
{
	const float LowerBound = -FMath::Max(VisibleRangeBelow, 0.0f);
	const float UpperBound = FMath::Max(VisibleRangeAbove, 0.0f);

	FPhotoViewingState State;
	State.bVisible = PathIndex >= LowerBound && PathIndex <= UpperBound;
	if (!State.bVisible)
	{
		return State;
	}

	float Opacity = 1.0f;
	float ScaleMultiplier = 1.0f;
	float BlurAmount = 0.0f;
	const float SafeMinimumScale = FMath::Clamp(TopMinimumScale, 0.01f, 1.0f);

	const float SafeTopStart = FMath::Min(TopFadeStart, TopFadeEnd);
	const float SafeTopEnd = FMath::Max(TopFadeStart, TopFadeEnd);
	if (SafeTopEnd > SafeTopStart && PathIndex >= SafeTopStart)
	{
		const float TopAlpha = FMath::Clamp((PathIndex - SafeTopStart) / (SafeTopEnd - SafeTopStart), 0.0f, 1.0f);
		Opacity *= FMath::Lerp(1.0f, FMath::Clamp(TopThreadMinimumOpacity, 0.0f, 1.0f), TopAlpha);
		ScaleMultiplier = FMath::Min(ScaleMultiplier, FMath::Lerp(1.0f, SafeMinimumScale, TopAlpha));
		BlurAmount = FMath::Lerp(0.0f, FMath::Max(TopMaxBlurAmount, 0.0f), TopAlpha);
	}

	const float SafeBottomStart = FMath::Min(BottomFadeStart, BottomFadeEnd);
	const float SafeBottomEnd = FMath::Max(BottomFadeStart, BottomFadeEnd);
	if (SafeBottomEnd > SafeBottomStart && PathIndex <= SafeBottomEnd)
	{
		const float BottomAlpha = FMath::Clamp((PathIndex - SafeBottomStart) / (SafeBottomEnd - SafeBottomStart), 0.0f, 1.0f);
		Opacity *= BottomAlpha;
		ScaleMultiplier = FMath::Min(ScaleMultiplier, FMath::Lerp(SafeMinimumScale, 1.0f, BottomAlpha));
	}

	State.Opacity = FMath::Clamp(Opacity, 0.0f, 1.0f);
	State.ScaleMultiplier = ScaleMultiplier;
	State.BlurAmount = FMath::Max(BlurAmount, 0.0f);
	State.bVisible = State.Opacity > KINDA_SMALL_NUMBER;
	State.bInteractive = State.bVisible && State.Opacity > 0.15f && PathIndex < SafeTopStart;
	return State;
}

void AMemoryRoomManager::ToggleMemoryRoomMenu()
{
	if (!GetWorld())
	{
		return;
	}

	AMemoryRoomMenuActor* MenuActor = nullptr;
	for (TActorIterator<AMemoryRoomMenuActor> It(GetWorld()); It;)
	{
		MenuActor = *It;
		break;
	}

	if (!MenuActor)
	{
		UClass* MenuActorClass = MemoryRoomMenuActorClass ? MemoryRoomMenuActorClass.Get() : AMemoryRoomMenuActor::StaticClass();
		const FTransform MenuSpawnTransform(GetActorRotation(), GetActorLocation(), FVector::OneVector);
		MenuActor = GetWorld()->SpawnActor<AMemoryRoomMenuActor>(MenuActorClass, MenuSpawnTransform);
	}

	if (MenuActor)
	{
		MenuActor->ToggleMenu();
	}
}

// Called when the game starts or when spawned
void AMemoryRoomManager::BeginPlay()
{
	Super::BeginPlay();
	LoadQualitySettings();
	ApplyQualityDimensionsFromEnums();
	InitialScrollOffset = ScrollOffset;
	TargetScrollOffset = ScrollOffset;
	LastAppliedScrollOffset = ScrollOffset;
	BindScrollInput();
	if (bAllowFreeLocomotion)
	{
		BindLocomotionInput();
	}
	if (bAutoSpawnMemoryRoomMenu && GetWorld())
	{
		bool bHasMenu = false;
		for (TActorIterator<AMemoryRoomMenuActor> It(GetWorld()); It;)
		{
			bHasMenu = true;
			break;
		}
		if (!bHasMenu)
		{
			UClass* MenuActorClass = MemoryRoomMenuActorClass ? MemoryRoomMenuActorClass.Get() : AMemoryRoomMenuActor::StaticClass();
			const FTransform MenuSpawnTransform(GetActorRotation(), GetActorLocation(), FVector::OneVector);
			GetWorld()->SpawnActor<AMemoryRoomMenuActor>(MenuActorClass, MenuSpawnTransform);
		}
	}

	if (bAutoSpawnSkyDome)
	{
		AMemoryRoomSkyDomeActor::FindOrSpawn(GetWorld(), SkyDomeActorClass);
	}
	if (bAutoSpawnSkyButton)
	{
		AMemoryRoomSkyButtonActor::FindOrSpawn(GetWorld(), EMemoryRoomWristButtonKind::TimeOfDay, FVector::ZeroVector, SkyButtonActorClass);
	}
	if (bAutoSpawnSettingsButton)
	{
		AMemoryRoomSkyButtonActor::FindOrSpawn(GetWorld(), EMemoryRoomWristButtonKind::Settings, SettingsButtonExtraOffset, SkyButtonActorClass);
	}
	if (bAutoSpawnResetPlacementsButton)
	{
		AMemoryRoomSkyButtonActor::FindOrSpawn(GetWorld(), EMemoryRoomWristButtonKind::ResetPlacements, ResetPlacementsButtonExtraOffset, SkyButtonActorClass);
	}
	if (bAutoSpawnDust)
	{
		AMemoryRoomDustActor::FindOrSpawn(GetWorld(), DustActorClass);
	}
	// Redone: dresses the level's own existing floor (found by tracing down, same as
	// SnapPlayerPawnToFloor) instead of spawning a competing ground mesh at a guessed height.
	if (bAutoSpawnGround)
	{
		AMemoryRoomGroundActor::FindOrSpawn(GetWorld(), GroundActorClass);
	}

	// Anchor the player to the room centre BEFORE anything lays out around them - the photo
	// spiral centres on the player, so the player has to be in the right place first.
	SnapPlayerPawnToFloor();

	// The room opens on the theme. There is no separate loading screen in this project, so
	// this is the "loading transition" hook: it runs once, right after the world is built and
	// before the player's saved tweaks are layered on top.
	ApplyTheme();

	// Restore the player's saved settings-panel choices and push them into the world.
	ApplyUserSettings();

	// Independent of the gallery scan below - a placed photo is addressed by its own saved
	// absolute path, not a logical index into the discovered library.
	LoadPlacedPhotos();

	// Legacy GameMode Blueprints scan and load only one diagnostic image after
	// spawning this manager. Finish their startup once those Blueprint calls end.
#if !PLATFORM_ANDROID
	GetWorldTimerManager().SetTimerForNextTick(FTimerDelegate::CreateWeakLambda(this, [this]()
	{
		if (GalleryPhotoFiles.IsEmpty() && DiscoveredPhotoFiles.Num() > 1)
		{
			UE_LOG(LogMemoryRoom, Log, TEXT("Completing legacy single-photo startup with %d discovered photos."), DiscoveredPhotoFiles.Num());
			BuildGalleryFromDiscoveredPhotos();
		}
	}));
#else
	// On Android, if nothing else has kicked off a load by next tick, load from the folder.
	if (bAndroidUsePhotoFolder)
	{
		GetWorldTimerManager().SetTimerForNextTick(FTimerDelegate::CreateWeakLambda(this, [this]()
		{
			if (!bIsLoadingGallery && GalleryPhotoFiles.IsEmpty())
			{
				TryStartAndroidFolderLoad();
			}
		}));
	}
#endif
	if (bBuildGalleryOnBeginPlay)
	{
#if PLATFORM_ANDROID
		if (!StartAndroidMediaStoreDiagnosticLoad())
		{
			UE_LOG(LogMemoryRoom, Warning, TEXT("%s Android MediaStore diagnostic load did not start."), *GetName());
		}
#else
		ScanPhotoFolder();
		BuildGalleryFromDiscoveredPhotos();
#endif
	}
}

void AMemoryRoomManager::SnapPlayerPawnToFloor()
{
	if (!bSnapPlayerToFloorOnBeginPlay)
	{
		return;
	}

	UWorld* World = GetWorld();
	APlayerController* PlayerController = World ? World->GetFirstPlayerController() : nullptr;
	APawn* Pawn = PlayerController ? PlayerController->GetPawn() : nullptr;
	if (!World || !PlayerController || !Pawn)
	{
		return;
	}

	// Only the (X, Y) matters here - the trace starts high and goes straight down regardless
	// of the pawn's current (possibly wrong) height, which is exactly the case this exists for:
	// diagnosed via TEMP-DIAG logging, the pawn was spawning ~4m above the level's actual floor
	// (Floor_0 at world Z=~0) with nothing to correct it, which is what read as "floating".
	FVector ViewLocation = FVector::ZeroVector;
	FRotator ViewRotation = FRotator::ZeroRotator;
	PlayerController->GetPlayerViewPoint(ViewLocation, ViewRotation);

	// Anchor the player to this manager's placed location, not wherever the pawn happened to
	// spawn. In PIE the spawn point drifts with the editor camera, and since the photo spiral
	// centres on the player (bCenterHelixOnUser), that drift dragged the whole layout around
	// and pushed the static painted trees inside it. Pinning (X, Y) here makes every session
	// start in the same spot relative to the trees and the floor.
	const FVector RoomAnchor = GetActorLocation();

	FCollisionObjectQueryParams ObjectParams;
	ObjectParams.AddObjectTypesToQuery(ECC_WorldStatic);
	ObjectParams.AddObjectTypesToQuery(ECC_WorldDynamic);
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(MemoryRoomFloorSnap), /*bTraceComplex*/ true);
	QueryParams.AddIgnoredActor(Pawn);

	FHitResult Hit;
	const float TraceDistance = FMath::Max(FloorTraceDistanceCm, 100.0f);
	const FVector TraceStart(RoomAnchor.X, RoomAnchor.Y, ViewLocation.Z);
	const FVector TraceEnd = TraceStart - FVector(0.0f, 0.0f, TraceDistance);
	if (World->LineTraceSingleByObjectType(Hit, TraceStart, TraceEnd, ObjectParams, QueryParams))
	{
		// VR pawns under floor-level tracking already put the headset's real-world height above
		// the pawn's own root automatically, so lining the root up with the floor here is enough
		// - there is no need to know or preserve whatever (wrong) height gap it spawned with.
		const FVector PawnLocation = Pawn->GetActorLocation();
		Pawn->SetActorLocation(FVector(RoomAnchor.X, RoomAnchor.Y, Hit.Location.Z));
		LocomotionFloorZ = Hit.Location.Z;
		bLocomotionFloorCaptured = true;
		UE_LOG(LogMemoryRoom, Log, TEXT("%s anchored %s to the room centre (%.0f, %.0f) and onto the floor (%s at Z=%.1f); it was at (%.0f, %.0f, %.1f)."),
			*GetName(), *GetNameSafe(Pawn), RoomAnchor.X, RoomAnchor.Y, *GetNameSafe(Hit.GetActor()), Hit.Location.Z,
			PawnLocation.X, PawnLocation.Y, PawnLocation.Z);
	}
	else
	{
		const FVector PawnLocation = Pawn->GetActorLocation();
		Pawn->SetActorLocation(FVector(RoomAnchor.X, RoomAnchor.Y, PawnLocation.Z));
		LocomotionFloorZ = PawnLocation.Z;
		bLocomotionFloorCaptured = true;
		UE_LOG(LogMemoryRoom, Warning, TEXT("%s found no floor within %.0f cm below the room centre - anchored %s horizontally only."),
			*GetName(), TraceDistance, *GetNameSafe(Pawn));
	}

	// Rebuild the cached spiral centre from the pawn's new position straight away so the first
	// gallery layout uses the anchored location, not the pre-move one.
	RefreshCachedHelixCenterLocation();
}

void AMemoryRoomManager::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	TickTurn(DeltaTime);
	TickLocomotion(DeltaTime);
	RefreshCachedHelixCenterLocation();

	if (!FMath::IsNearlyEqual(ScrollOffset, LastAppliedScrollOffset))
	{
		TargetScrollOffset = ScrollOffset;
		ScrollVelocity = 0.0f;
	}

	// Turning takes priority over scrolling: a diagonal or imperfect sideways push on the same
	// stick would otherwise rotate the room and move the gallery at once, which is a reliable
	// way to make someone motion-sick in VR. Any real turn deflection zeroes scroll for the
	// frame rather than blending the two.
	const bool bTurningNow = FMath::Abs(TurnInputX) > 0.15f;
	const float CombinedScrollInput = bTurningNow ? 0.0f : (ScrollInputAmount + HeldScrollInputAmount);
	const bool bHasScrollInput = !FMath::IsNearlyZero(CombinedScrollInput);
	if (bHasScrollInput)
	{
		const float ScrollUnitsPerSecond = FMath::Max(MaximumScrollSpeed, 0.0f) * ScrollSpeed;
		TargetScrollOffset += CombinedScrollInput * ScrollUnitsPerSecond * DeltaTime;

		// Theme scroll cue: one-shot on the rest -> moving edge, re-armed once the stick
		// returns to centre below.
		if (!bScrollWhooshActive)
		{
			bScrollWhooshActive = true;
			PlayThemeCue(ActiveAudio.ScrollWhoosh, ActiveAudio.ScrollWhooshVolume, FVector::ZeroVector);
		}
	}
	else
	{
		// Stop where the player released the stick/wheel instead of coasting toward an old target.
		TargetScrollOffset = ScrollOffset;
		bScrollWhooshActive = false;
	}

	// Keep the scroll inside the range where photos actually exist, plus a little slack so
	// the newest and oldest can still be brought fully into the readable band. Without this
	// the player can scroll off into empty space and the gallery looks like it ran out.
	if (UseVirtualizedGallery() && GalleryPathOffsets.Num() > 0)
	{
		// Photo i sits at path offset GalleryPathOffsets[i] + ScrollOffset. Allow scrolling
		// from photo 0 at the bottom of the band to the last photo just past it.
		const float MinScroll = -GalleryPathOffsets.Last() - 3.0f;
		const float MaxScroll = static_cast<float>(FMath::Clamp(GalleryBottomMargin, 0, 20)) + 3.0f;
		TargetScrollOffset = FMath::Clamp(TargetScrollOffset, MinScroll, MaxScroll);
	}
	else if (CachedPhotoPathParameters.Num() > 0)
	{
		const float LastPhotoPath = CachedPhotoPathParameters.Last();
		const float Slack = FMath::Max(VisibleRangeAbove, 1.0f) * 0.5f;
		const float MinScroll = -(LastPhotoPath) + FMath::Max(VisibleRangeBelow, 0.0f);
		const float MaxScroll = Slack;
		TargetScrollOffset = FMath::Clamp(TargetScrollOffset, FMath::Min(MinScroll, MaxScroll), MaxScroll);
	}

	ScrollVelocity = 0.0f;
	ScrollOffset = ScrollSmoothing > 0.0f ? FMath::FInterpTo(ScrollOffset, TargetScrollOffset, DeltaTime, ScrollSmoothing) : TargetScrollOffset;

	// Treat AddScrollInput as frame input. Axis bindings will refresh it every tick, while one-shot
	// Enhanced Input trigger events will no longer leave the photos scrolling forever.
	ScrollInputAmount = 0.0f;

	const bool bScrollOffsetUnchanged = FMath::IsNearlyEqual(ScrollOffset, LastAppliedScrollOffset);
	// Focus needs this every frame, including while scrolling, or opening a photo and then
	// moving the stick would fade the room back in underneath you.
	bool bHasExpandedPhoto = false;
	for (const TObjectPtr<AMemoryPhotoActor>& Photo : SpawnedPhotos)
	{
		if (Photo && Photo->IsExpanded())
		{
			bHasExpandedPhoto = true;
			break;
		}
	}

	// Ease focus toward whatever is open. Kept running while it settles so closing a photo
	// fades the room back in instead of freezing part-way.
	const float FocusTarget = (bEnableFocusMode && bHasExpandedPhoto) ? 1.0f : 0.0f;
	const float FocusStep = DeltaTime / FMath::Max(FocusFadeSeconds, 0.05f);
	FocusBlend = FMath::FInterpConstantTo(FocusBlend, FocusTarget, 1.0f, FocusStep);
	const bool bFocusSettling = !FMath::IsNearlyEqual(FocusBlend, FocusTarget, 0.002f);

	if (AMemoryRoomSkyDomeActor* SkyDome = CachedSkyDome.Get())
	{
		SkyDome->SetFocusDim(FocusBlend);
	}

	if (!bScrollOffsetUnchanged || bHasExpandedPhoto || bIsLoadingGallery || bFocusSettling || FocusBlend > 0.002f || bHasPendingPhotoTextures)
	{
		LayoutSpawnedPhotos();
	}

	ProcessAndroidExpandedSelection();
	UpdatePlacedPhotoQuality(DeltaTime);
	UpdatePhotoBrightnessFromSky();

	LastAppliedScrollOffset = ScrollOffset;
}

void AMemoryRoomManager::UpdatePhotoBrightnessFromSky()
{
	AMemoryRoomSkyDomeActor* SkyDome = CachedSkyDome.Get();
	if (!SkyDome)
	{
		// Resolve without spawning: if there is no dome, photos simply never glow.
		if (UWorld* World = GetWorld())
		{
			for (TActorIterator<AMemoryRoomSkyDomeActor> It(World); It;)
			{
				SkyDome = *It;
				CachedSkyDome = SkyDome;
				break;
			}
		}
		if (!SkyDome)
		{
			return;
		}
	}

	const float DesiredBrightness = SkyDome->GetPhotoBrightness();

	// The lights track which photos are nearest, so they need refreshing as the gallery
	// scrolls, not only when the time of day changes.
	UpdatePhotoGlowLights(DesiredBrightness);

	if (FMath::IsNearlyEqual(DesiredBrightness, LastAppliedPhotoBrightness, 0.001f))
	{
		return;
	}

	LastAppliedPhotoBrightness = DesiredBrightness;
	for (const TObjectPtr<AMemoryPhotoActor>& PhotoPtr : SpawnedPhotos)
	{
		if (AMemoryPhotoActor* Photo = PhotoPtr.Get())
		{
			Photo->SetPhotoBrightness(DesiredBrightness);
		}
	}
}

void AMemoryRoomManager::UpdatePhotoGlowLights(float PhotoBrightness)
{
	const int32 MaxLights = FMath::Clamp(MaxPhotoGlowLights, 0, 16);

	// Wall photos only glow to help light the room once it is actually dark (PhotoBrightness
	// above 1.0 is the sky pushing past captured brightness, the night look). A placed photo is
	// staged deliberately rather than just passed on the wall, so it keeps a small presence
	// through the day too - this pass now also runs whenever there is at least one, purely for
	// those, even at PhotoBrightness 1.0.
	const bool bWantWallGlow = PhotoBrightness > 1.05f;
	const bool bHavePlacedPhotos = PlacedPhotoActors.Num() > 0;
	if (MaxLights <= 0 || (!bWantWallGlow && !bHavePlacedPhotos))
	{
		for (const TObjectPtr<AMemoryPhotoActor>& PhotoPtr : SpawnedPhotos)
		{
			if (AMemoryPhotoActor* Photo = PhotoPtr.Get())
			{
				Photo->SetGlowLightEnabled(false, 0.0f);
			}
		}
		return;
	}

	const FVector ViewLocation = GetHelixCenterLocation();

	// Placed photos get first claim on the shared light budget - they were put there on
	// purpose - and wall photos only compete for whatever is left, and only once bWantWallGlow.
	TArray<TPair<float, AMemoryPhotoActor*>> PlacedCandidates;
	TArray<TPair<float, AMemoryPhotoActor*>> WallCandidates;
	PlacedCandidates.Reserve(PlacedPhotoActors.Num());
	WallCandidates.Reserve(SpawnedPhotos.Num());
	for (const TObjectPtr<AMemoryPhotoActor>& PhotoPtr : SpawnedPhotos)
	{
		AMemoryPhotoActor* Photo = PhotoPtr.Get();
		if (!Photo || Photo->IsHidden() || !Photo->HasLoadedTexture())
		{
			if (Photo)
			{
				Photo->SetGlowLightEnabled(false, 0.0f);
			}
			continue;
		}

		const float DistSq = FVector::DistSquared(Photo->GetActorLocation(), ViewLocation);
		if (Photo->IsPlacedInWorld())
		{
			PlacedCandidates.Emplace(DistSq, Photo);
		}
		else if (bWantWallGlow)
		{
			WallCandidates.Emplace(DistSq, Photo);
		}
		else
		{
			Photo->SetGlowLightEnabled(false, 0.0f);
		}
	}

	auto SortByDistance = [](const TPair<float, AMemoryPhotoActor*>& Left, const TPair<float, AMemoryPhotoActor*>& Right)
	{
		return Left.Key < Right.Key;
	};
	PlacedCandidates.Sort(SortByDistance);
	WallCandidates.Sort(SortByDistance);

	const float PlacedIntensity = FMath::Max(PlacedPhotoGlowLightIntensity, 0.0f);
	const float WallIntensity = FMath::Max(PhotoGlowLightIntensity, 0.0f) * PhotoBrightness;

	int32 LightsUsed = 0;
	for (const TPair<float, AMemoryPhotoActor*>& Candidate : PlacedCandidates)
	{
		Candidate.Value->SetGlowLightEnabled(LightsUsed < MaxLights, PlacedIntensity);
		++LightsUsed;
	}
	for (const TPair<float, AMemoryPhotoActor*>& Candidate : WallCandidates)
	{
		Candidate.Value->SetGlowLightEnabled(LightsUsed < MaxLights, WallIntensity);
		++LightsUsed;
	}
}

void AMemoryRoomManager::AddScrollInput(float Amount)
{
	if (bGalleryScrollSuppressed)
	{
		StopScrollInput();
		return;
	}
	const float DeadZone = 0.2f;
	ScrollInputAmount = FMath::Abs(Amount) > DeadZone ? Amount : 0.0f;
}

void AMemoryRoomManager::AddForwardScrollInput(float Amount)
{
	if (bGalleryScrollSuppressed)
	{
		StopScrollInput();
		return;
	}
	const float DeadZone = 0.2f;
	HeldScrollInputAmount = FMath::Abs(Amount) > DeadZone ? FMath::Abs(Amount) : 0.0f;
}

void AMemoryRoomManager::AddBackwardScrollInput(float Amount)
{
	if (bGalleryScrollSuppressed)
	{
		StopScrollInput();
		return;
	}
	const float DeadZone = 0.2f;
	HeldScrollInputAmount = FMath::Abs(Amount) > DeadZone ? -FMath::Abs(Amount) : 0.0f;
}

void AMemoryRoomManager::StopScrollInput()
{
	HeldScrollInputAmount = 0.0f;
	ScrollInputAmount = 0.0f;
}

void AMemoryRoomManager::SetGalleryScrollSuppressed(bool bSuppressed)
{
	bGalleryScrollSuppressed = bSuppressed;
	if (bGalleryScrollSuppressed)
	{
		StopScrollInput();
		ScrollVelocity = 0.0f;
		TargetScrollOffset = ScrollOffset;
	}
}

void AMemoryRoomManager::ResetGalleryScroll()
{
	ScrollOffset = InitialScrollOffset;
	TargetScrollOffset = InitialScrollOffset;
	ScrollVelocity = 0.0f;
	ScrollInputAmount = 0.0f;
	HeldScrollInputAmount = 0.0f;
	LastAppliedScrollOffset = ScrollOffset;
	LayoutSpawnedPhotos();
}

void AMemoryRoomManager::JumpToNewestPhoto()
{
	ResetGalleryScroll();
}

void AMemoryRoomManager::ReloadPhotoLibrary()
{
	OnPhotoLibraryReloadStarted.Broadcast();
#if PLATFORM_ANDROID
	if (TryStartAndroidFolderLoad())
	{
		OnPhotoLibraryReloadCompleted.Broadcast(NumberOfPhotos, true);
		return;
	}
	CancelGalleryLoad();
	++PhotoLibraryGeneration;
	++PhotoAssignmentGeneration;
	++AndroidExpandedSelectionGeneration;
	PendingAndroidThumbnailQueue.Reset();
	QueuedAndroidThumbnailIds.Reset();
	PendingAndroidExpandedQueue.Reset();
	PendingAndroidTextureLoads.Reset();
	ActiveExpandedPhotoActor.Reset();
	ActiveExpandedPhotoIndex = INDEX_NONE;
	ActiveExpandedStableMediaId.Reset();
	ActiveExpandedContentUri.Reset();
	for (const TObjectPtr<AMemoryPhotoActor>& PhotoPtr : SpawnedPhotos)
	{
		if (AMemoryPhotoActor* Photo = PhotoPtr.Get())
		{
			Photo->Collapse();
			Photo->UnloadRuntimeTexture();
			Photo->ApplyViewingWindowState(0.0f, 1.0f, false);
			Photo->AssignAndroidMediaStoreRecord(INDEX_NONE, TEXT(""), TEXT(""), 1, 1);
		}
	}
	if (!PhotoSource)
	{
		PhotoSource = MakeUnique<FAndroidMediaStorePhotoSource>();
	}
	if (!PhotoClass || !PhotoSource)
	{
		OnPhotoLibraryReloadCompleted.Broadcast(NumberOfPhotos, false);
		return;
	}
	bIsLoadingGallery = true;
	const int32 RequestGeneration = PhotoLibraryGeneration;
	TWeakObjectPtr<AMemoryRoomManager> WeakThis(this);
	PhotoSource->RequestPhotoPermissionAsync(FMemoryPhotoPermissionCallback::CreateLambda([WeakThis, RequestGeneration](bool bGranted, const FString& Message)
	{
		AMemoryRoomManager* Manager = WeakThis.Get();
		if (!Manager || Manager->PhotoLibraryGeneration != RequestGeneration) return;
		if (!bGranted)
		{
			Manager->bIsLoadingGallery = false;
			UE_LOG(LogMemoryRoom, Warning, TEXT("%s Android photo permission denied during reload: %s"), *Manager->GetName(), *Message);
			Manager->OnPhotoLibraryReloadCompleted.Broadcast(Manager->NumberOfPhotos, false);
			return;
		}
		const int32 QueryLimit = FMath::Max(Manager->MaxPhotosToLoad > 0 ? Manager->MaxPhotosToLoad : 1000, 1);
		Manager->PhotoSource->QueryPhotosAsync(0, QueryLimit, FMemoryPhotoMetadataPageCallback::CreateLambda([WeakThis, RequestGeneration](bool bSuccess, const TArray<FMemoryPhotoMetadata>& Photos, int32 TotalCount)
		{
			AMemoryRoomManager* M = WeakThis.Get();
			if (!M || M->PhotoLibraryGeneration != RequestGeneration) return;
			M->bIsLoadingGallery = false;
			if (!bSuccess)
			{
				M->OnPhotoLibraryReloadCompleted.Broadcast(M->NumberOfPhotos, false);
				return;
			}
			M->MediaStorePhotos.Reset();
			TSet<FString> ValidIds;
			for (const FMemoryPhotoMetadata& Photo : Photos)
			{
				if (!Photo.StableMediaId.IsEmpty() && !Photo.ContentUri.IsEmpty() && Photo.Width > 0 && Photo.Height > 0)
				{
					M->MediaStorePhotos.Add(Photo);
					ValidIds.Add(Photo.StableMediaId);
				}
			}
			if (M->MaxPhotosToLoad > 0 && M->MediaStorePhotos.Num() > M->MaxPhotosToLoad)
			{
				M->MediaStorePhotos.SetNum(M->MaxPhotosToLoad);
				ValidIds.Reset();
				for (const FMemoryPhotoMetadata& Photo : M->MediaStorePhotos) ValidIds.Add(Photo.StableMediaId);
			}
			for (auto It = M->AndroidThumbnailCache.CreateIterator(); It; ++It) if (!ValidIds.Contains(It.Key())) It.RemoveCurrent();
			M->AndroidThumbnailCacheOrder.RemoveAll([&ValidIds](const FString& Id){ return !ValidIds.Contains(Id); });
			for (auto It = M->AndroidExpandedCache.CreateIterator(); It; ++It) if (!ValidIds.Contains(It.Key())) It.RemoveCurrent();
			M->AndroidExpandedCacheOrder.RemoveAll([&ValidIds](const FString& Id){ return !ValidIds.Contains(Id); });
			M->TotalPhotosToLoad = M->bAndroidSingleImageDiagnostic ? FMath::Min(M->MediaStorePhotos.Num(), 1) : M->MediaStorePhotos.Num();
			M->NumberOfPhotos = M->TotalPhotosToLoad;
			M->LoadedPhotoCount = 0;
			M->FailedPhotoCount = 0;
			M->CachedPhotoPathParameters.Reset();
			M->CachedFootprintRadii.Reset();
			M->CachedPhotoPositions.Reset();

			M->GalleryPhotoFiles.Reset();
			M->PhotoAspectRatios.Reset();
			M->GalleryPhotoFiles.Reserve(M->MediaStorePhotos.Num());
			M->PhotoAspectRatios.Reserve(M->MediaStorePhotos.Num());
			for (const FMemoryPhotoMetadata& Photo : M->MediaStorePhotos)
			{
				int32 CorrectedWidth = 0;
				int32 CorrectedHeight = 0;
				GetCorrectedMediaStoreDimensions(Photo, CorrectedWidth, CorrectedHeight);
				M->GalleryPhotoFiles.Add(Photo.ContentUri);
				M->PhotoAspectRatios.Add(CorrectedHeight > 0 ? static_cast<float>(CorrectedWidth) / static_cast<float>(CorrectedHeight) : 1.0f);
			}

			M->ResetGalleryScroll();
			M->EnsurePhotoPoolSize();
			M->EnsureGalleryPool();
			M->RebuildGalleryPathOffsets();
			M->LayoutSpawnedPhotos();
			M->OnPhotoCountChanged.Broadcast(M->NumberOfPhotos);
			M->OnPhotoLibraryReloadCompleted.Broadcast(M->NumberOfPhotos, true);
			M->OnGalleryLoadProgress.Broadcast(0, 0, M->TotalPhotosToLoad);
			UE_LOG(LogMemoryRoom, Log, TEXT("%s Android MediaStore reload complete: total %d, used %d."), *M->GetName(), TotalCount, M->NumberOfPhotos);
		}));
	}));
#else
	ScanPhotoFolder();
	BuildGalleryFromDiscoveredPhotos();
	ResetGalleryScroll();
	OnPhotoCountChanged.Broadcast(NumberOfPhotos);
	OnPhotoLibraryReloadCompleted.Broadcast(NumberOfPhotos, true);
#endif
}

int32 AMemoryRoomManager::ThumbnailDimensionForQuality(EMemoryRoomThumbnailQuality Quality)
{
	switch (Quality)
	{
	case EMemoryRoomThumbnailQuality::Low: return 256;
	case EMemoryRoomThumbnailQuality::High: return 512;
	case EMemoryRoomThumbnailQuality::Medium:
	default: return 384;
	}
}

int32 AMemoryRoomManager::ExpandedDimensionForQuality(EMemoryRoomExpandedPhotoQuality Quality)
{
	switch (Quality)
	{
	case EMemoryRoomExpandedPhotoQuality::Standard: return 1024;
	case EMemoryRoomExpandedPhotoQuality::High: return 1536;
	case EMemoryRoomExpandedPhotoQuality::Ultra:
	default: return 2048;
	}
}

void AMemoryRoomManager::ApplyQualityDimensionsFromEnums()
{
	AndroidThumbnailMaxDimension = ThumbnailDimensionForQuality(ThumbnailQuality);
	AndroidExpandedPhotoMaxDimension = ExpandedDimensionForQuality(ExpandedPhotoQuality);
}

namespace
{
const TCHAR* const MemoryRoomSaveSlot = TEXT("MemoryRoomQualitySettings");

// The save slot is shared with the sky dome. Always load-then-merge so writing one group
// of settings never wipes another.
UMemoryRoomQualitySaveGame* LoadOrCreateMemoryRoomSave()
{
	if (UMemoryRoomQualitySaveGame* Existing = Cast<UMemoryRoomQualitySaveGame>(UGameplayStatics::LoadGameFromSlot(MemoryRoomSaveSlot, 0)))
	{
		return Existing;
	}
	return Cast<UMemoryRoomQualitySaveGame>(UGameplayStatics::CreateSaveGameObject(UMemoryRoomQualitySaveGame::StaticClass()));
}
}

void AMemoryRoomManager::LoadQualitySettings()
{
	if (const UMemoryRoomQualitySaveGame* Save = Cast<UMemoryRoomQualitySaveGame>(UGameplayStatics::LoadGameFromSlot(MemoryRoomSaveSlot, 0)))
	{
		ThumbnailQuality = Save->ThumbnailQuality;
		ExpandedPhotoQuality = Save->ExpandedPhotoQuality;
	}
}

void AMemoryRoomManager::SaveQualitySettings() const
{
	UMemoryRoomQualitySaveGame* Save = LoadOrCreateMemoryRoomSave();
	if (!Save) return;
	Save->ThumbnailQuality = ThumbnailQuality;
	Save->ExpandedPhotoQuality = ExpandedPhotoQuality;
	UGameplayStatics::SaveGameToSlot(Save, MemoryRoomSaveSlot, 0);
}

// ---------------------------------------------------------------------------------------
// Free-placed photos
// ---------------------------------------------------------------------------------------

void AMemoryRoomManager::LoadPlacedPhotos()
{
	if (!PhotoClass)
	{
		return;
	}
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	const UMemoryRoomPlacedPhotosSaveGame* Save = Cast<UMemoryRoomPlacedPhotosSaveGame>(UGameplayStatics::LoadGameFromSlot(PlacedPhotosSaveSlotName, 0));
	if (!Save)
	{
		return;
	}

	for (const TPair<FString, FMemoryRoomPlacedPhotoEntry>& Entry : Save->PlacedPhotos)
	{
		if (!FPaths::FileExists(Entry.Key))
		{
			// The source file moved or was deleted since this was placed - nothing sane to
			// show, and re-saving would just carry the stale entry forward again.
			UE_LOG(LogMemoryRoom, Warning, TEXT("%s skipped a placed photo whose file no longer exists: %s"), *GetName(), *Entry.Key);
			continue;
		}

		// Only a one-time cost at startup, not a per-interaction one - a live grab-and-place
		// during play reuses the gallery's own actor for that photo instead (see
		// PlacePhotoInWorld), but on load there is no "actor already showing it" to reuse yet,
		// so this is the one place that still spawns fresh.
		AMemoryPhotoActor* PlacedActor = World->SpawnActor<AMemoryPhotoActor>(PhotoClass, GetActorTransform());
		if (!PlacedActor)
		{
			continue;
		}
		PlacedActor->AssignPhotoIndex(INDEX_NONE);
		ApplyThemeToPhoto(PlacedActor);
		if (PlacedActor->PrepareFromFile(Entry.Key))
		{
			PlacedActor->LoadImageFromFileAsync(Entry.Key, FMath::Max(MaximumRuntimeTextureDimension, 512));
		}
		PlacedActor->SetPlacedInWorld(true);
		// Brand new actor, never expanded/hovered - SetLayoutTransform snaps it straight
		// there instead of animating, which is right for something that was never held.
		PlacedActor->SetLayoutTransform(Entry.Value.WorldTransform, 0.0f);
		PlacedPhotoActors.Add(Entry.Key, PlacedActor);
		// Must join the pool SpawnedPhotos, not just PlacedPhotoActors - LayoutVirtualizedGallery
		// only ever looks at SpawnedPhotos, so an actor missing from it can never be handed a
		// normal wall slot again once unplaced (e.g. via Reset): it would just sit there orphaned
		// forever while a different pooled actor backfills its gallery index as a duplicate.
		SpawnedPhotos.Add(PlacedActor);
	}
}

void AMemoryRoomManager::SavePlacedPhotos() const
{
	UMemoryRoomPlacedPhotosSaveGame* Save = Cast<UMemoryRoomPlacedPhotosSaveGame>(UGameplayStatics::CreateSaveGameObject(UMemoryRoomPlacedPhotosSaveGame::StaticClass()));
	if (!Save)
	{
		return;
	}

	for (const TPair<FString, TObjectPtr<AMemoryPhotoActor>>& Entry : PlacedPhotoActors)
	{
		if (const AMemoryPhotoActor* Actor = Entry.Value.Get())
		{
			FMemoryRoomPlacedPhotoEntry SaveEntry;
			SaveEntry.WorldTransform = Actor->GetActorTransform();
			Save->PlacedPhotos.Add(Entry.Key, SaveEntry);
		}
	}

	UGameplayStatics::SaveGameToSlot(Save, PlacedPhotosSaveSlotName, 0);
}

void AMemoryRoomManager::PlacePhotoInWorld(AMemoryPhotoActor* Actor, const FTransform& WorldTransform)
{
	if (!Actor)
	{
		return;
	}
	const FString FilePath = Actor->GetLoadedFilePath();
	if (FilePath.IsEmpty())
	{
		return;
	}

	// No new actor spawned here at all - Actor is whichever one the gallery pool (or a
	// previous placement) already had showing this photo, and it keeps that job permanently
	// now. LayoutVirtualizedGallery's own IsPlacedInWorld() checks are what leave its old wall
	// slot as an empty gap instead of a different actor backfilling it with a duplicate.
	const bool bWasAlreadyPlaced = Actor->IsPlacedInWorld();
	if (!bWasAlreadyPlaced)
	{
		Actor->SetPlacedInWorld(true);
		PlacedPhotoActors.Add(FilePath, Actor);
	}

	// WorldTransform's scale is already correct as handed in: UMemoryRoomSelectionComponent
	// only ever multiplies GrabScaleMultiplier against a freshly-grabbed wall photo's own true
	// scale, and keeps an already-placed photo at its current size on a re-grab rather than
	// re-applying that shrink - so, unlike an earlier version of this function, nothing here
	// overrides it. Moving an existing placement is presumably still held (its own grab just
	// re-selected it), so this only updates the target and Collapse() eases it there; a
	// first-time placement is neither, so SetLayoutTransform snaps it in immediately instead.
	Actor->SetLayoutTransform(WorldTransform, 0.0f);
	Actor->Collapse();

	SavePlacedPhotos();
}

void AMemoryRoomManager::ReturnPlacedPhotoToGallery(const FString& FilePath)
{
	TObjectPtr<AMemoryPhotoActor> Existing;
	if (!PlacedPhotoActors.RemoveAndCopyValue(FilePath, Existing))
	{
		return;
	}

	// Just hands the actor back to the pool's own layout pass - the very next
	// LayoutVirtualizedGallery call assigns it (or a fresh one, if this one's old logical index
	// has scrolled out of range by now) a normal wall slot again, same as any other pooled
	// actor. Nothing to destroy: this actor was never a separate, extra spawn to begin with.
	if (AMemoryPhotoActor* Actor = Existing.Get())
	{
		Actor->SetPlacedInWorld(false);
		OnPlacedPhotoUnplaced.Broadcast(Actor);
	}

	SavePlacedPhotos();
	// Clearing the flag alone is invisible: the actor just sits at its placed transform until
	// something else happens to call LayoutVirtualizedGallery (a scroll, a day change...). Force
	// that pass now so the reclaimed slot snaps back into formation immediately.
	LayoutSpawnedPhotos();
}

void AMemoryRoomManager::ResetAllPlacedPhotos()
{
	for (const TPair<FString, TObjectPtr<AMemoryPhotoActor>>& Entry : PlacedPhotoActors)
	{
		if (AMemoryPhotoActor* Actor = Entry.Value.Get())
		{
			Actor->SetPlacedInWorld(false);
			OnPlacedPhotoUnplaced.Broadcast(Actor);
		}
	}
	PlacedPhotoActors.Empty();
	SavePlacedPhotos();
	// Same reasoning as ReturnPlacedPhotoToGallery - nothing else was going to re-run the
	// layout pass on its own, so every unplaced actor above would otherwise stay frozen in
	// place until an unrelated scroll/day-change happened to trigger one.
	LayoutSpawnedPhotos();
}

bool AMemoryRoomManager::IsPlacedPhotoActor(const AMemoryPhotoActor* Actor) const
{
	return Actor && Actor->IsPlacedInWorld();
}

void AMemoryRoomManager::UpdatePlacedPhotoQuality(float DeltaTime)
{
	if (PlacedPhotoActors.Num() == 0)
	{
		return;
	}

	PlacedPhotoQualityCheckAccumulator += DeltaTime;
	if (PlacedPhotoQualityCheckAccumulator < FMath::Max(PlacedPhotoQualityCheckInterval, 0.05f))
	{
		return;
	}
	PlacedPhotoQualityCheckAccumulator = 0.0f;

	const UWorld* World = GetWorld();
	const APlayerController* PlayerController = World ? World->GetFirstPlayerController() : nullptr;
	if (!PlayerController)
	{
		return;
	}
	FVector ViewLocation = FVector::ZeroVector;
	FRotator ViewRotation = FRotator::ZeroRotator;
	PlayerController->GetPlayerViewPoint(ViewLocation, ViewRotation);
	const FVector ViewForward = ViewRotation.Vector();
	const float GazeCosThreshold = FMath::Cos(FMath::DegreesToRadians(FMath::Clamp(PlacedPhotoGazeConeDegrees, 1.0f, 89.0f)));
	const float MaxDistance = FMath::Max(PlacedPhotoGazeMaxDistance, 1.0f);
	const int32 ThumbnailDimension = FMath::Max(GalleryThumbnailMaxDimension, 64);

	for (const TPair<FString, TObjectPtr<AMemoryPhotoActor>>& Entry : PlacedPhotoActors)
	{
		AMemoryPhotoActor* Actor = Entry.Value.Get();
		// A held photo already gets full-res through the normal expanded-selection path (it is
		// bExpanded while grabbed) - leave it alone here so the two paths never fight over it.
		if (!Actor || Actor->IsExpanded())
		{
			continue;
		}

		const FVector ToActor = Actor->GetActorLocation() - ViewLocation;
		const float Distance = ToActor.Size();
		const bool bInGaze = Distance > KINDA_SMALL_NUMBER && Distance <= MaxDistance
			&& FVector::DotProduct(ViewForward, ToActor / Distance) >= GazeCosThreshold;

		const int32 LoadedLongEdge = FMath::Max(Actor->GetLoadedImageWidth(), Actor->GetLoadedImageHeight());
		const bool bCurrentlyFullRes = Actor->HasLoadedTexture() && LoadedLongEdge > ThumbnailDimension;

		if (bInGaze && !bCurrentlyFullRes && !Actor->IsTextureLoadInFlight())
		{
			RequestPlacedPhotoFullResolution(Actor);
		}
		else if (!bInGaze && bCurrentlyFullRes && !Actor->IsTextureLoadInFlight())
		{
			RequestPlacedPhotoThumbnail(Actor);
		}
	}
}

void AMemoryRoomManager::RequestPlacedPhotoFullResolution(AMemoryPhotoActor* Actor)
{
	if (!Actor)
	{
		return;
	}
#if PLATFORM_ANDROID
	// One decode in flight at a time, same budget the wall's own expanded photo uses - a second
	// gazed-at photo just waits for the next quality check once this one finishes.
	if (PlacedPhotoUpgradeInFlight.IsValid() || !PhotoSource)
	{
		return;
	}
	const FString ContentUri = Actor->GetAssignedContentUri();
	if (ContentUri.IsEmpty())
	{
		return;
	}

	PlacedPhotoUpgradeInFlight = Actor;
	const int32 Generation = PhotoLibraryGeneration;
	const int32 AssignmentGeneration = Actor->GetAssignmentGeneration();
	const FString StableMediaId = Actor->GetAssignedStableMediaId();
	TWeakObjectPtr<AMemoryRoomManager> WeakManager(this);
	TWeakObjectPtr<AMemoryPhotoActor> WeakActor(Actor);
	PhotoSource->LoadPhotoPixelsAsync(ContentUri, FMath::Max(AndroidExpandedPhotoMaxDimension, 1), Generation,
		FMemoryPhotoPixelsCallback::CreateLambda([WeakManager, WeakActor, ContentUri, Generation, AssignmentGeneration, StableMediaId](bool bSuccess, TArray<FColor> Pixels, int32 Width, int32 Height) mutable
	{
		AMemoryRoomManager* Manager = WeakManager.Get();
		if (!Manager)
		{
			return;
		}
		if (Manager->PlacedPhotoUpgradeInFlight == WeakActor)
		{
			Manager->PlacedPhotoUpgradeInFlight = nullptr;
		}
		AMemoryPhotoActor* FreshActor = WeakActor.Get();
		// Verified against this actor's own identity, not a shared "active" slot - it may have
		// been reset, re-grabbed, or reassigned to a different photo while this was in flight.
		const bool bFresh = FreshActor
			&& Manager->PhotoLibraryGeneration == Generation
			&& FreshActor->GetAssignmentGeneration() == AssignmentGeneration
			&& FreshActor->GetAssignedStableMediaId() == StableMediaId
			&& FreshActor->IsPlacedInWorld()
			&& !FreshActor->IsExpanded();
		if (bFresh && bSuccess && FreshActor->ApplyDecodedRuntimeImage(ContentUri, MoveTemp(Pixels), Width, Height))
		{
			Manager->CacheAndroidExpandedTexture(StableMediaId, FreshActor->GetRuntimeTexture(), Width, Height);
		}
	}));
#else
	Actor->LoadImageFromFileAsync(Actor->GetLoadedFilePath(), FMath::Max(MaximumRuntimeTextureDimension, 1));
#endif
}

void AMemoryRoomManager::RequestPlacedPhotoThumbnail(AMemoryPhotoActor* Actor)
{
	if (!Actor)
	{
		return;
	}
#if PLATFORM_ANDROID
	RestoreAndroidThumbnailToActor(Actor);
#else
	Actor->UnloadRuntimeTexture();
	Actor->LoadImageFromFileAsync(Actor->GetLoadedFilePath(), FMath::Max(GalleryThumbnailMaxDimension, 64));
#endif
}

// ---------------------------------------------------------------------------------------
//  In-headset user settings
// ---------------------------------------------------------------------------------------

AMemoryRoomSkyDomeActor* AMemoryRoomManager::FindSkyDome() const
{
	if (UWorld* World = GetWorld())
	{
		for (TActorIterator<AMemoryRoomSkyDomeActor> It(World); It;)
		{
			return *It;
		}
	}
	return nullptr;
}

AMemoryRoomDustActor* AMemoryRoomManager::FindDustActor() const
{
	if (UWorld* World = GetWorld())
	{
		for (TActorIterator<AMemoryRoomDustActor> It(World); It;)
		{
			return *It;
		}
	}
	return nullptr;
}

// ---------------------------------------------------------------------------------------
// Theme
// ---------------------------------------------------------------------------------------

UMemoryRoomThemeAsset* AMemoryRoomManager::ResolveTheme()
{
	if (CurrentTheme)
	{
		ActiveTheme = CurrentTheme;
	}
	else
	{
		if (!ActiveTheme)
		{
			ActiveTheme = UMemoryRoomThemeAsset::MakeBuiltInTheme(FallbackThemeId, this);
		}
	}
	return ActiveTheme;
}

void AMemoryRoomManager::ApplyThemeToPhoto(AMemoryPhotoActor* PhotoActor) const
{
	if (PhotoActor && ActiveTheme)
	{
		PhotoActor->SetFrameStyle(ActiveTheme->PhotoFrame.FrameTintColor, ActiveTheme->PhotoFrame.FrameEdgeWidth);
		PhotoActor->ApplyThemeUI(ActiveTheme->WristUI, ThemeHoverFrameMaterial, ThemeGlassMaterial);
	}
}

void AMemoryRoomManager::ApplyAmbientAudio()
{
	// Tear down whatever was playing, then start the new bed. Spawned rather than a
	// constructor subobject so the manager needs no scene root of its own.
	if (AmbientAudioComponent)
	{
		AmbientAudioComponent->Stop();
		AmbientAudioComponent = nullptr;
	}

	if (USoundBase* Loop = ActiveAudio.AmbientLoop)
	{
		AmbientAudioComponent = UGameplayStatics::SpawnSound2D(
			this, Loop, FMath::Max(ActiveAudio.AmbientVolume, 0.0f),
			1.0f, 0.0f, nullptr, /*bPersistAcrossLevelTransition*/ false, /*bAutoDestroy*/ false);
	}
}

void AMemoryRoomManager::PlayThemeCue(USoundBase* Cue, float VolumeScale, const FVector& Location) const
{
	if (!Cue)
	{
		return;
	}

	const float Volume = FMath::Max(VolumeScale, 0.0f);
	if (Location.IsNearlyZero())
	{
		UGameplayStatics::PlaySound2D(this, Cue, Volume);
	}
	else
	{
		UGameplayStatics::PlaySoundAtLocation(this, Cue, Location, Volume);
	}
}

void AMemoryRoomManager::ApplyTheme()
{
	UMemoryRoomThemeAsset* Theme = ResolveTheme();
	if (!Theme)
	{
		return;
	}

	// Fold the manager's per-cue overrides over the theme's audio (asset stays untouched).
	ActiveAudio = Theme->Audio;
	if (AmbientLoopOverride)  { ActiveAudio.AmbientLoop  = AmbientLoopOverride; }
	if (ScrollWhooshOverride) { ActiveAudio.ScrollWhoosh = ScrollWhooshOverride; }
	if (HoverChimeOverride)   { ActiveAudio.HoverChime   = HoverChimeOverride; }
	if (GrabToneOverride)     { ActiveAudio.GrabTone     = GrabToneOverride; }

	if (AMemoryRoomSkyDomeActor* Sky = FindSkyDome())
	{
		Sky->ApplyThemePresets(Theme->DawnPreset, Theme->SunsetPreset, Theme->NightPreset, /*bImmediate*/ true);
	}

	if (AMemoryRoomDustActor* Dust = FindDustActor())
	{
		Dust->SetMoteStyle(Theme->MoteStyle);
		Dust->SetDustTint(Theme->DustTint);
	}

	// One gold halo and one glass slab, shared by every photo in the pool (they are all the
	// same colour, so there is nothing per-photo to vary).
	UMaterialInterface* GlassBase = MemoryRoomUI::LoadGlassBaseMaterial();
	ThemeHoverFrameMaterial = MemoryRoomUI::MakeSolidMaterial(GlassBase, Theme->WristUI.HighlightColor, this);
	ThemeGlassMaterial = MemoryRoomUI::MakeGlassMaterial(GlassBase, Theme->WristUI, this);

	for (const TObjectPtr<AMemoryPhotoActor>& PhotoPtr : SpawnedPhotos)
	{
		ApplyThemeToPhoto(PhotoPtr.Get());
	}

	if (UWorld* World = GetWorld())
	{
		for (TActorIterator<AMemoryRoomSkyButtonActor> It(World); It; ++It)
		{
			It->ApplyUIStyle(Theme->WristUI);
		}
		for (TActorIterator<AMemoryRoomMenuActor> It(World); It; ++It)
		{
			It->ApplyUIStyle(Theme->WristUI);
		}
	}

	ApplyAmbientAudio();

	UE_LOG(LogMemoryRoom, Log, TEXT("%s applied theme '%s'."), *GetName(), *Theme->ThemeName.ToString());
}

void AMemoryRoomManager::NotifyPhotoHovered(const FVector& WorldLocation)
{
	PlayThemeCue(ActiveAudio.HoverChime, ActiveAudio.HoverChimeVolume, WorldLocation);
}

void AMemoryRoomManager::NotifyPhotoGrabbed(const FVector& WorldLocation)
{
	PlayThemeCue(ActiveAudio.GrabTone, ActiveAudio.GrabToneVolume, WorldLocation);
}

FText AMemoryRoomManager::GetUserSettingText(EMemoryRoomUserSetting Setting) const
{
	FText Name, Value;
	GetUserSettingParts(Setting, Name, Value);
	if (Name.IsEmpty())
	{
		return FText::GetEmpty();
	}
	return FText::FromString(FString::Printf(TEXT("%s   %s"), *Name.ToString(), *Value.ToString()));
}

void AMemoryRoomManager::GetUserSettingParts(EMemoryRoomUserSetting Setting, FText& OutName, FText& OutValue) const
{
	OutName = FText::GetEmpty();
	OutValue = FText::GetEmpty();

	auto Row = [&OutName, &OutValue](const TCHAR* Label, const FString& Value)
	{
		OutName = FText::FromString(Label);
		OutValue = FText::FromString(Value);
	};

	switch (Setting)
	{
	case EMemoryRoomUserSetting::TimeOfDay:
	{
		const AMemoryRoomSkyDomeActor* Sky = FindSkyDome();
		return Row(TEXT("Time of Day"), Sky ? Sky->GetTimeOfDayDisplayName().ToString() : TEXT("Sunset"));
	}
	case EMemoryRoomUserSetting::SpiralTwist:
		return Row(TEXT("Spiral Twist"), FString::Printf(TEXT("%.0f"), GallerySpiralDegreesPerUnit));
	case EMemoryRoomUserSetting::PhotoSpacing:
		return Row(TEXT("Photo Spacing"), FString::Printf(TEXT("%.0f"), GalleryPhotoEdgeGap));
	case EMemoryRoomUserSetting::PhotosInView:
		return Row(TEXT("Photos In View"), FString::Printf(TEXT("%d"), GalleryWindowCount));
	case EMemoryRoomUserSetting::ScrollSpeed:
		return Row(TEXT("Scroll Speed"), FString::Printf(TEXT("%.0f"), MaximumScrollSpeed));
	case EMemoryRoomUserSetting::NightPhotoGlow:
		return Row(TEXT("Night Photo Glow"), FString::Printf(TEXT("%.0f"), PhotoGlowLightIntensity));
	case EMemoryRoomUserSetting::DustAmount:
	{
		static const TCHAR* Levels[] = { TEXT("Off"), TEXT("Low"), TEXT("Medium"), TEXT("Full") };
		return Row(TEXT("Dust"), Levels[FMath::Clamp(DustAmountLevel, 0, 3)]);
	}
	case EMemoryRoomUserSetting::ThumbnailQuality:
	{
		const TCHAR* Q = ThumbnailQuality == EMemoryRoomThumbnailQuality::Low ? TEXT("Low")
			: (ThumbnailQuality == EMemoryRoomThumbnailQuality::High ? TEXT("High") : TEXT("Medium"));
		return Row(TEXT("Thumbnail Quality"), Q);
	}
	case EMemoryRoomUserSetting::Theme:
		return Row(TEXT("Theme"), FallbackThemeId == EMemoryRoomThemeId::Void ? TEXT("Void") : TEXT("Hearth"));
	case EMemoryRoomUserSetting::Float:
		return Row(TEXT("Float"), bFloatEnabled ? TEXT("On") : TEXT("Off"));
	default:
		break;
	}
}

void AMemoryRoomManager::AdjustUserSetting(EMemoryRoomUserSetting Setting, int32 Direction)
{
	const float Step = static_cast<float>(FMath::Clamp(Direction, -1, 1));
	bool bNeedsRelayout = false;

	switch (Setting)
	{
	case EMemoryRoomUserSetting::TimeOfDay:
		if (AMemoryRoomSkyDomeActor* Sky = FindSkyDome())
		{
			// One direction cycles forward; the other cycles back twice = forward once less.
			Sky->CycleTimeOfDay();
			if (Direction < 0) { Sky->CycleTimeOfDay(); Sky->CycleTimeOfDay(); }
		}
		break;
	case EMemoryRoomUserSetting::SpiralTwist:
		GallerySpiralDegreesPerUnit = FMath::Clamp(GallerySpiralDegreesPerUnit + Step * 2.0f, 6.0f, 40.0f);
		bNeedsRelayout = true;
		break;
	case EMemoryRoomUserSetting::PhotoSpacing:
		GalleryPhotoEdgeGap = FMath::Clamp(GalleryPhotoEdgeGap + Step * 10.0f, 0.0f, 200.0f);
		bNeedsRelayout = true;
		break;
	case EMemoryRoomUserSetting::PhotosInView:
		GalleryWindowCount = FMath::Clamp(GalleryWindowCount + FMath::RoundToInt(Step) * 4, 16, 120);
		break;
	case EMemoryRoomUserSetting::ScrollSpeed:
		MaximumScrollSpeed = FMath::Clamp(MaximumScrollSpeed + Step * 2.0f, 2.0f, 30.0f);
		break;
	case EMemoryRoomUserSetting::NightPhotoGlow:
		PhotoGlowLightIntensity = FMath::Clamp(PhotoGlowLightIntensity + Step * 1.0f, 0.0f, 14.0f);
		break;
	case EMemoryRoomUserSetting::DustAmount:
		DustAmountLevel = FMath::Clamp(DustAmountLevel + FMath::RoundToInt(Step), 0, 3);
		break;
	case EMemoryRoomUserSetting::ThumbnailQuality:
		CycleThumbnailQuality();
		break;
	case EMemoryRoomUserSetting::Theme:
		// Only two built-in themes exist right now, so either arrow just swaps between them -
		// same "ignore Direction's sign" idea ThumbnailQuality's cycle uses above.
		FallbackThemeId = (FallbackThemeId == EMemoryRoomThemeId::Void) ? EMemoryRoomThemeId::Hearth : EMemoryRoomThemeId::Void;
		break;
	case EMemoryRoomUserSetting::Float:
		bFloatEnabled = !bFloatEnabled;
		break;
	default:
		break;
	}

	bool bUnused = false;
	PushUserSettingToWorld(Setting, bUnused);

	if (bNeedsRelayout && UseVirtualizedGallery())
	{
		RebuildGalleryPathOffsets();
		LayoutSpawnedPhotos();
	}

	SaveUserSettings();
	OnUserSettingsChanged.Broadcast();
}

void AMemoryRoomManager::PushUserSettingToWorld(EMemoryRoomUserSetting Setting, bool& bOutNeedsRelayout)
{
	bOutNeedsRelayout = false;
	switch (Setting)
	{
	case EMemoryRoomUserSetting::SpiralTwist:
	case EMemoryRoomUserSetting::PhotoSpacing:
		bOutNeedsRelayout = true;
		break;
	case EMemoryRoomUserSetting::NightPhotoGlow:
		// Applied on the next frame by UpdatePhotoGlowLights.
		LastAppliedPhotoBrightness = -1.0f;
		break;
	case EMemoryRoomUserSetting::DustAmount:
		if (AMemoryRoomDustActor* Dust = FindDustActor())
		{
			Dust->SetDensityLevel(DustAmountLevel);
		}
		break;
	case EMemoryRoomUserSetting::Theme:
		// ResolveTheme prefers CurrentTheme unconditionally (see ApplyTheme), so this has no
		// visible effect once a real theme asset is ever assigned there - same as today, just
		// now reachable live instead of only at BeginPlay. Clearing the cached ActiveTheme forces
		// ResolveTheme to re-synthesise off the new FallbackThemeId instead of reusing the old one.
		ActiveTheme = nullptr;
		ApplyTheme();
		break;
	default:
		break;
	}
}

void AMemoryRoomManager::ApplyUserSettings()
{
	if (const UMemoryRoomQualitySaveGame* Save = Cast<UMemoryRoomQualitySaveGame>(UGameplayStatics::LoadGameFromSlot(MemoryRoomSaveSlot, 0)))
	{
		if (Save->bHasSavedUserSettings)
		{
			GallerySpiralDegreesPerUnit = FMath::Clamp(Save->SpiralTwist, 6.0f, 40.0f);
			GalleryPhotoEdgeGap = FMath::Clamp(Save->PhotoSpacing, 0.0f, 200.0f);
			GalleryWindowCount = FMath::Clamp(Save->PhotosInView, 16, 120);
			MaximumScrollSpeed = FMath::Clamp(Save->ScrollSpeed, 2.0f, 30.0f);
			PhotoGlowLightIntensity = FMath::Clamp(Save->NightPhotoGlow, 0.0f, 14.0f);
			DustAmountLevel = FMath::Clamp(Save->DustAmountLevel, 0, 3);
			// BeginPlay calls ApplyTheme() before this (see the order there), so it has already
			// synthesised and cached ActiveTheme off the default FallbackThemeId - if the saved
			// choice differs, clear that cache and re-resolve now so the room opens on whichever
			// theme was actually chosen last, not always Hearth.
			if (FallbackThemeId != Save->ThemeId)
			{
				FallbackThemeId = Save->ThemeId;
				ActiveTheme = nullptr;
				ApplyTheme();
			}
		}
	}

	if (AMemoryRoomDustActor* Dust = FindDustActor())
	{
		Dust->SetDensityLevel(DustAmountLevel);
	}
	LastAppliedPhotoBrightness = -1.0f;
}

void AMemoryRoomManager::SaveUserSettings() const
{
	UMemoryRoomQualitySaveGame* Save = LoadOrCreateMemoryRoomSave();
	if (!Save) return;
	Save->bHasSavedUserSettings = true;
	Save->SpiralTwist = GallerySpiralDegreesPerUnit;
	Save->PhotoSpacing = GalleryPhotoEdgeGap;
	Save->PhotosInView = GalleryWindowCount;
	Save->ScrollSpeed = MaximumScrollSpeed;
	Save->NightPhotoGlow = PhotoGlowLightIntensity;
	Save->DustAmountLevel = DustAmountLevel;
	Save->ThemeId = FallbackThemeId;
	UGameplayStatics::SaveGameToSlot(Save, MemoryRoomSaveSlot, 0);
}

void AMemoryRoomManager::ClearAndroidThumbnailCacheAndActiveTextures()
{
	++ThumbnailQualityGeneration;
	PendingAndroidThumbnailQueue.Reset();
	QueuedAndroidThumbnailIds.Reset();
	PendingAndroidTextureLoads.Reset();
	AndroidThumbnailCache.Reset();
	AndroidThumbnailCacheOrder.Reset();
	LoadedPhotoCount = 0;
	FailedPhotoCount = 0;
	for (const TObjectPtr<AMemoryPhotoActor>& PhotoPtr : SpawnedPhotos)
	{
		if (AMemoryPhotoActor* Photo = PhotoPtr.Get())
		{
			if (!Photo->IsExpanded())
			{
				Photo->UnloadRuntimeTexture();
			}
		}
	}
	LayoutSpawnedPhotos();
}

void AMemoryRoomManager::ClearAndroidExpandedCache()
{
	++AndroidExpandedSelectionGeneration;
	PendingAndroidExpandedQueue.Reset();
	AndroidExpandedCache.Reset();
	AndroidExpandedCacheOrder.Reset();
	if (AMemoryPhotoActor* Actor = ActiveExpandedPhotoActor.Get())
	{
		RestoreAndroidThumbnailToActor(Actor);
	}
}

void AMemoryRoomManager::SetThumbnailQuality(EMemoryRoomThumbnailQuality NewQuality)
{
	if (ThumbnailQuality == NewQuality) return;
	ThumbnailQuality = NewQuality;
	AndroidThumbnailMaxDimension = ThumbnailDimensionForQuality(ThumbnailQuality);
	ClearAndroidThumbnailCacheAndActiveTextures();
	SaveQualitySettings();
	OnThumbnailQualityChanged.Broadcast(ThumbnailQuality);
}

void AMemoryRoomManager::SetExpandedPhotoQuality(EMemoryRoomExpandedPhotoQuality NewQuality)
{
	if (ExpandedPhotoQuality == NewQuality) return;
	ExpandedPhotoQuality = NewQuality;
	AndroidExpandedPhotoMaxDimension = ExpandedDimensionForQuality(ExpandedPhotoQuality);
	ClearAndroidExpandedCache();
	SaveQualitySettings();
	OnExpandedQualityChanged.Broadcast(ExpandedPhotoQuality);
}

void AMemoryRoomManager::CycleThumbnailQuality()
{
	SetThumbnailQuality(ThumbnailQuality == EMemoryRoomThumbnailQuality::Low ? EMemoryRoomThumbnailQuality::Medium : (ThumbnailQuality == EMemoryRoomThumbnailQuality::Medium ? EMemoryRoomThumbnailQuality::High : EMemoryRoomThumbnailQuality::Low));
}

void AMemoryRoomManager::CycleExpandedPhotoQuality()
{
	SetExpandedPhotoQuality(ExpandedPhotoQuality == EMemoryRoomExpandedPhotoQuality::Standard ? EMemoryRoomExpandedPhotoQuality::High : (ExpandedPhotoQuality == EMemoryRoomExpandedPhotoQuality::High ? EMemoryRoomExpandedPhotoQuality::Ultra : EMemoryRoomExpandedPhotoQuality::Standard));
}

void AMemoryRoomManager::ScanPhotoFolder()
{
	DiscoveredPhotoFiles.Empty();

	FString FolderToScan = PhotoFolderPath;
	FolderToScan.TrimStartAndEndInline();

	if (FolderToScan.IsEmpty())
	{
		UE_LOG(LogMemoryRoom, Warning, TEXT("%s cannot scan photos because PhotoFolderPath is empty or invalid."), *GetName());
		return;
	}

	FPaths::NormalizeDirectoryName(FolderToScan);

	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
	if (!PlatformFile.DirectoryExists(*FolderToScan))
	{
		UE_LOG(LogMemoryRoom, Warning, TEXT("%s cannot scan photos because folder does not exist: %s"), *GetName(), *FolderToScan);
		return;
	}

	UE_LOG(LogMemoryRoom, Log, TEXT("%s scanning photo folder: %s"), *GetName(), *FolderToScan);

	TArray<FString> AllFiles;
	const FString SearchPattern = FPaths::Combine(FolderToScan, TEXT("*.*"));
	IFileManager::Get().FindFiles(AllFiles, *SearchPattern, true, false);

	for (const FString& FileName : AllFiles)
	{
		const FString Extension = FPaths::GetExtension(FileName, false).ToLower();
		const bool bSupportedImage = Extension == TEXT("jpg") || Extension == TEXT("jpeg") || Extension == TEXT("png") || Extension == TEXT("cr2");
		if (!bSupportedImage)
		{
			UE_LOG(LogMemoryRoom, Warning, TEXT("%s skipped unsupported photo file in %s: %s"), *GetName(), *FolderToScan, *FileName);
			continue;
		}

		DiscoveredPhotoFiles.Add(FPaths::ConvertRelativePathToFull(FPaths::Combine(FolderToScan, FileName)));
	}

	DiscoveredPhotoFiles.Sort([](const FString& Left, const FString& Right)
	{
		const FString LeftFileName = FPaths::GetCleanFilename(Left);
		const FString RightFileName = FPaths::GetCleanFilename(Right);
		const int32 NameCompare = LeftFileName.Compare(RightFileName, ESearchCase::IgnoreCase);
		if (NameCompare != 0)
		{
			return NameCompare < 0;
		}
		return Left < Right;
	});

	UE_LOG(LogMemoryRoom, Log, TEXT("%s discovered %d supported image file(s) in: %s"), *GetName(), DiscoveredPhotoFiles.Num(), *FolderToScan);
}

bool AMemoryRoomManager::BuildGalleryFromDiscoveredPhotos()
{
#if PLATFORM_ANDROID
	if (TryStartAndroidFolderLoad())
	{
		return true;
	}
	return StartAndroidMediaStoreDiagnosticLoad();
#else
	return StartIncrementalGalleryLoad();
#endif
}

FString AMemoryRoomManager::ResolveAndroidPhotoFolderPath() const
{
	FString Folder = PhotoFolderPath;
	Folder.TrimStartAndEndInline();
	if (!Folder.IsEmpty())
	{
		return Folder;
	}
	// App-private external folder - readable with no runtime permission, and adb push works:
	//   <PersistentDownloadDir>/Photos
	return FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectPersistentDownloadDir(), TEXT("Photos")));
}

bool AMemoryRoomManager::TryStartAndroidFolderLoad()
{
#if PLATFORM_ANDROID
	if (!bAndroidUsePhotoFolder)
	{
		return false;
	}

	const FString Folder = ResolveAndroidPhotoFolderPath();
	if (Folder.IsEmpty())
	{
		return false;
	}
	PhotoFolderPath = Folder;
	IFileManager::Get().MakeDirectory(*Folder, /*Tree*/ true);
	UE_LOG(LogMemoryRoom, Warning, TEXT("%s Android photo folder: %s"), *GetName(), *Folder);

	if (!PhotoSource)
	{
		PhotoSource = MakeUnique<FAndroidMediaStorePhotoSource>();
	}

	bIsLoadingGallery = true;
	TWeakObjectPtr<AMemoryRoomManager> WeakThis(this);
	// Ask for the media permission first: harmless (auto-granted) for the app-private folder,
	// and required if the user pointed PhotoFolderPath at shared storage.
	PhotoSource->RequestPhotoPermissionAsync(FMemoryPhotoPermissionCallback::CreateLambda([WeakThis](bool bGranted, const FString& Message)
	{
		AMemoryRoomManager* M = WeakThis.Get();
		if (!M)
		{
			return;
		}
		M->bIsLoadingGallery = false;
		M->ScanPhotoFolder();
		if (M->DiscoveredPhotoFiles.Num() > 0)
		{
			UE_LOG(LogMemoryRoom, Warning, TEXT("%s loading %d photo(s) from folder: %s"), *M->GetName(), M->DiscoveredPhotoFiles.Num(), *M->PhotoFolderPath);
			M->StartIncrementalGalleryLoad();
		}
		else
		{
			UE_LOG(LogMemoryRoom, Warning, TEXT("%s folder '%s' had no readable images (permission granted=%d); falling back to the system photo library."),
				*M->GetName(), *M->PhotoFolderPath, bGranted ? 1 : 0);
			M->StartAndroidMediaStoreDiagnosticLoad();
		}
	}));
	return true;
#else
	return false;
#endif
}

bool AMemoryRoomManager::LoadGalleryFromFolder(const FString& InFolderPath)
{
	FString Trimmed = InFolderPath;
	Trimmed.TrimStartAndEndInline();
	if (!Trimmed.IsEmpty())
	{
		PhotoFolderPath = Trimmed;
	}
#if PLATFORM_ANDROID
	bAndroidUsePhotoFolder = true;
	return TryStartAndroidFolderLoad();
#else
	ScanPhotoFolder();
	return StartIncrementalGalleryLoad();
#endif
}

bool AMemoryRoomManager::StartAndroidMediaStoreDiagnosticLoad()
{
#if PLATFORM_ANDROID
	CancelGalleryLoad();
	ClearSpawnedPhotoActors();
	LoadedPhotoCount = 0;
	FailedPhotoCount = 0;
	TotalPhotosToLoad = 0;
	NumberOfPhotos = 0;
	MediaStorePhotos.Reset();
	PendingAndroidTextureLoads.Reset();
	PendingAndroidThumbnailQueue.Reset();
	QueuedAndroidThumbnailIds.Reset();
	AndroidThumbnailCache.Reset();
	AndroidThumbnailCacheOrder.Reset();
	PendingAndroidExpandedQueue.Reset();
	AndroidExpandedCache.Reset();
	AndroidExpandedCacheOrder.Reset();
	ActiveAndroidThumbnailDecodes = 0;
	ActiveAndroidExpandedDecodes = 0;
	AndroidThumbnailSuccessCount = 0;
	AndroidThumbnailFailureCount = 0;
	AndroidStaleThumbnailDiscardCount = 0;
	++PhotoAssignmentGeneration;

	if (!PhotoClass)
	{
		UE_LOG(LogMemoryRoom, Warning, TEXT("%s cannot start Android MediaStore load because PhotoClass is not set."), *GetName());
		return false;
	}
	if (!PhotoSource)
	{
		PhotoSource = MakeUnique<FAndroidMediaStorePhotoSource>();
	}

	bIsLoadingGallery = true;
	TWeakObjectPtr<AMemoryRoomManager> WeakThis(this);
	PhotoSource->RequestPhotoPermissionAsync(FMemoryPhotoPermissionCallback::CreateLambda([WeakThis](bool bGranted, const FString& Message)
	{
		AMemoryRoomManager* Manager = WeakThis.Get();
		if (!Manager) return;
		if (!bGranted)
		{
			Manager->bIsLoadingGallery = false;
			UE_LOG(LogMemoryRoom, Warning, TEXT("%s Android photo permission denied: %s"), *Manager->GetName(), *Message);
			Manager->OnGalleryLoadCompleted.Broadcast(0, 0);
			return;
		}

		const int32 QueryLimit = FMath::Max(Manager->MaxPhotosToLoad > 0 ? Manager->MaxPhotosToLoad : 1000, 1);
		Manager->PhotoSource->QueryPhotosAsync(0, QueryLimit, FMemoryPhotoMetadataPageCallback::CreateLambda([WeakThis](bool bSuccess, const TArray<FMemoryPhotoMetadata>& Photos, int32 TotalCount)
		{
			AMemoryRoomManager* M = WeakThis.Get();
			if (!M) return;
			M->bIsLoadingGallery = false;
			if (!bSuccess)
			{
				UE_LOG(LogMemoryRoom, Warning, TEXT("%s Android MediaStore query failed."), *M->GetName());
				M->OnGalleryLoadCompleted.Broadcast(0, 1);
				return;
			}

			M->MediaStorePhotos.Reset();
			for (const FMemoryPhotoMetadata& Photo : Photos)
			{
				if (!Photo.StableMediaId.IsEmpty() && !Photo.ContentUri.IsEmpty() && Photo.Width > 0 && Photo.Height > 0)
				{
					M->MediaStorePhotos.Add(Photo);
				}
			}
			if (M->MaxPhotosToLoad > 0 && M->MediaStorePhotos.Num() > M->MaxPhotosToLoad)
			{
				M->MediaStorePhotos.SetNum(M->MaxPhotosToLoad);
			}

			M->TotalPhotosToLoad = M->bAndroidSingleImageDiagnostic ? FMath::Min(M->MediaStorePhotos.Num(), 1) : M->MediaStorePhotos.Num();
			M->NumberOfPhotos = M->TotalPhotosToLoad;
			M->OnPhotoCountChanged.Broadcast(M->NumberOfPhotos);
			UE_LOG(LogMemoryRoom, Log, TEXT("%s Android MediaStore total records: %d; records used after limit: %d."), *M->GetName(), TotalCount, M->MediaStorePhotos.Num());
			if (M->NumberOfPhotos <= 0)
			{
				UE_LOG(LogMemoryRoom, Warning, TEXT("%s Android gallery is empty or inaccessible. Add images or grant photo permission."), *M->GetName());
				M->OnGalleryLoadCompleted.Broadcast(0, 0);
				return;
			}

			// Feed the shared virtualized gallery: content URIs as the "files", and the
			// MediaStore's own pixel dimensions as the aspect ratios so spacing is
			// width-aware without decoding anything.
			M->GalleryPhotoFiles.Reset();
			M->PhotoAspectRatios.Reset();
			M->GalleryPhotoFiles.Reserve(M->MediaStorePhotos.Num());
			M->PhotoAspectRatios.Reserve(M->MediaStorePhotos.Num());
			for (const FMemoryPhotoMetadata& Photo : M->MediaStorePhotos)
			{
				int32 CorrectedWidth = 0;
				int32 CorrectedHeight = 0;
				GetCorrectedMediaStoreDimensions(Photo, CorrectedWidth, CorrectedHeight);
				M->GalleryPhotoFiles.Add(Photo.ContentUri);
				M->PhotoAspectRatios.Add(CorrectedHeight > 0 ? static_cast<float>(CorrectedWidth) / static_cast<float>(CorrectedHeight) : 1.0f);
			}
			M->ClearGalleryTextureCaches();

			M->ResetGalleryScroll();
			M->EnsurePhotoPoolSize();
			M->EnsureGalleryPool();
			UE_LOG(LogMemoryRoom, Log, TEXT("%s Android actor pool size: %d."), *M->GetName(), M->SpawnedPhotos.Num());
			M->RebuildGalleryPathOffsets();
			M->LayoutSpawnedPhotos();

			if (M->bAndroidSingleImageDiagnostic && M->SpawnedPhotos.Num() > 0)
			{
				M->PendingAndroidThumbnailQueue.Reset();
			M->QueuedAndroidThumbnailIds.Reset();
				M->AssignAndroidPhotoToActor(0, M->SpawnedPhotos[0].Get());
				M->QueueAndroidThumbnailRequest(0, M->SpawnedPhotos[0].Get());
				UE_LOG(LogMemoryRoom, Log, TEXT("%s Android diagnostic metadata query complete; queued one image decode only."), *M->GetName());
			}
			else
			{
				UE_LOG(LogMemoryRoom, Log, TEXT("%s Android pooled MediaStore gallery initialized: pool %d, thumbnails %d px, max decodes %d, cache entries %d."), *M->GetName(), FMath::Max(M->AndroidPhotoActorPoolSize, 1), FMath::Max(M->AndroidThumbnailMaxDimension, 1), FMath::Max(M->AndroidMaxSimultaneousThumbnailDecodes, 1), FMath::Max(M->AndroidMaxThumbnailCacheEntries, 1));
			}
			M->OnGalleryLoadProgress.Broadcast(0, 0, M->TotalPhotosToLoad);
		}));
	}));
	return true;
#else
	return false;
#endif
}

void AMemoryRoomManager::GetCorrectedMediaStoreDimensions(const FMemoryPhotoMetadata& Photo, int32& OutWidth, int32& OutHeight)
{
	const bool bSwap = Photo.Orientation == 90 || Photo.Orientation == 270;
	OutWidth = FMath::Max(bSwap ? Photo.Height : Photo.Width, 1);
	OutHeight = FMath::Max(bSwap ? Photo.Width : Photo.Height, 1);
}

void AMemoryRoomManager::AssignAndroidPhotoToActor(int32 PhotoIndex, AMemoryPhotoActor* PhotoActor)
{
#if PLATFORM_ANDROID
	if (!PhotoActor || !MediaStorePhotos.IsValidIndex(PhotoIndex)) return;
	const FMemoryPhotoMetadata& Photo = MediaStorePhotos[PhotoIndex];
	int32 CorrectedWidth = 0;
	int32 CorrectedHeight = 0;
	GetCorrectedMediaStoreDimensions(Photo, CorrectedWidth, CorrectedHeight);
	PhotoActor->AssignAndroidMediaStoreRecord(PhotoIndex, Photo.StableMediaId, Photo.ContentUri, CorrectedWidth, CorrectedHeight);
#endif
}

void AMemoryRoomManager::QueueAndroidThumbnailRequest(int32 PhotoIndex, AMemoryPhotoActor* PhotoActor)
{
#if PLATFORM_ANDROID
	if (!PhotoActor || !PhotoSource || !MediaStorePhotos.IsValidIndex(PhotoIndex) || PhotoActor->HasLoadedTexture()) return;
	const FMemoryPhotoMetadata& Photo = MediaStorePhotos[PhotoIndex];
	if (FAndroidThumbnailCacheEntry* Cached = AndroidThumbnailCache.Find(Photo.StableMediaId))
	{
		if (Cached->Texture.IsValid())
		{
			if (PhotoActor->GetRuntimeTexture() != Cached->Texture.Get())
			{
				PhotoActor->ApplyRuntimeTexture(Photo.ContentUri, Cached->Texture.Get(), Cached->Width, Cached->Height);
			}
			AndroidThumbnailCacheOrder.Remove(Photo.StableMediaId);
			AndroidThumbnailCacheOrder.Add(Photo.StableMediaId);
			return;
		}
		AndroidThumbnailCache.Remove(Photo.StableMediaId);
		AndroidThumbnailCacheOrder.Remove(Photo.StableMediaId);
	}
	if (PendingAndroidTextureLoads.Contains(Photo.StableMediaId)) return;
	if (QueuedAndroidThumbnailIds.Contains(Photo.StableMediaId)) return;

	FAndroidThumbnailRequest Request;
	Request.PhotoActor = PhotoActor;
	Request.PhotoIndex = PhotoIndex;
	Request.StableMediaId = Photo.StableMediaId;
	Request.ContentUri = Photo.ContentUri;
	Request.AssignmentGeneration = PhotoActor->GetAssignmentGeneration();
	Request.LibraryGeneration = PhotoLibraryGeneration;
	Request.ThumbnailQualityGeneration = ThumbnailQualityGeneration;
	PendingAndroidThumbnailQueue.Add(Request);
	QueuedAndroidThumbnailIds.Add(Request.StableMediaId);
	UE_LOG(LogMemoryRoom, Verbose, TEXT("%s Android thumbnail queue size: %d; active decodes: %d; cache size: %d."), *GetName(), PendingAndroidThumbnailQueue.Num(), ActiveAndroidThumbnailDecodes, AndroidThumbnailCache.Num());
	PumpAndroidThumbnailQueue();
#endif
}

void AMemoryRoomManager::PumpAndroidThumbnailQueue()
{
#if PLATFORM_ANDROID
	while (PhotoSource && ActiveAndroidExpandedDecodes <= 0 && PendingAndroidExpandedQueue.Num() <= 0 && ActiveAndroidThumbnailDecodes < FMath::Max(AndroidMaxSimultaneousThumbnailDecodes, 1) && PendingAndroidThumbnailQueue.Num() > 0)
	{
		FAndroidThumbnailRequest Request = PendingAndroidThumbnailQueue[0];
		PendingAndroidThumbnailQueue.RemoveAt(0);
		QueuedAndroidThumbnailIds.Remove(Request.StableMediaId);
		PendingAndroidTextureLoads.Add(Request.StableMediaId);
		++ActiveAndroidThumbnailDecodes;
		UE_LOG(LogMemoryRoom, Verbose, TEXT("%s Android thumbnail pump: queue=%d active=%d cache=%d."), *GetName(), PendingAndroidThumbnailQueue.Num(), ActiveAndroidThumbnailDecodes, AndroidThumbnailCache.Num());

		TWeakObjectPtr<AMemoryRoomManager> WeakManager(this);
		PhotoSource->LoadPhotoPixelsAsync(Request.ContentUri, FMath::Max(AndroidThumbnailMaxDimension, 1), Request.AssignmentGeneration, FMemoryPhotoPixelsCallback::CreateLambda([WeakManager, Request](bool bSuccess, TArray<FColor> Pixels, int32 Width, int32 Height) mutable
		{
			AMemoryRoomManager* Manager = WeakManager.Get();
			if (!Manager) return;
			Manager->PendingAndroidTextureLoads.Remove(Request.StableMediaId);
			Manager->ActiveAndroidThumbnailDecodes = FMath::Max(Manager->ActiveAndroidThumbnailDecodes - 1, 0);

			AMemoryPhotoActor* Actor = Request.PhotoActor.Get();
			const bool bFresh = Manager->PhotoLibraryGeneration == Request.LibraryGeneration
				&& Manager->ThumbnailQualityGeneration == Request.ThumbnailQualityGeneration
				&& Actor
				&& Actor->GetAssignedPhotoIndex() == Request.PhotoIndex
				&& Actor->GetAssignedStableMediaId() == Request.StableMediaId
				&& Actor->GetAssignmentGeneration() == Request.AssignmentGeneration;
			if (!bFresh)
			{
				++Manager->AndroidStaleThumbnailDiscardCount;
				UE_LOG(LogMemoryRoom, Verbose, TEXT("%s discarded stale Android thumbnail result (%d total)."), *Manager->GetName(), Manager->AndroidStaleThumbnailDiscardCount);
				Manager->PumpAndroidThumbnailQueue();
				return;
			}

			if (bSuccess && Actor->ApplyDecodedRuntimeImage(Request.ContentUri, MoveTemp(Pixels), Width, Height))
			{
				Manager->CacheAndroidThumbnail(Request.StableMediaId, Actor->GetRuntimeTexture(), Width, Height);
				++Manager->LoadedPhotoCount;
				++Manager->AndroidThumbnailSuccessCount;
				UE_LOG(LogMemoryRoom, Verbose, TEXT("%s loaded Android thumbnail %s (%dx%d). success=%d queue=%d active=%d cache=%d"), *Manager->GetName(), *Request.StableMediaId, Width, Height, Manager->AndroidThumbnailSuccessCount, Manager->PendingAndroidThumbnailQueue.Num(), Manager->ActiveAndroidThumbnailDecodes, Manager->AndroidThumbnailCache.Num());
			}
			else
			{
				++Manager->FailedPhotoCount;
				++Manager->AndroidThumbnailFailureCount;
				UE_LOG(LogMemoryRoom, Warning, TEXT("%s failed Android thumbnail %s. failed=%d queue=%d active=%d cache=%d"), *Manager->GetName(), *Request.StableMediaId, Manager->AndroidThumbnailFailureCount, Manager->PendingAndroidThumbnailQueue.Num(), Manager->ActiveAndroidThumbnailDecodes, Manager->AndroidThumbnailCache.Num());
			}
			Manager->OnGalleryLoadProgress.Broadcast(Manager->LoadedPhotoCount, Manager->FailedPhotoCount, Manager->TotalPhotosToLoad);
			Manager->PumpAndroidThumbnailQueue();
		}));
	}
#endif
}

void AMemoryRoomManager::CacheAndroidThumbnail(const FString& StableMediaId, UTexture2D* Texture, int32 Width, int32 Height)
{
#if PLATFORM_ANDROID
	if (StableMediaId.IsEmpty() || !Texture) return;
	FAndroidThumbnailCacheEntry Entry;
	Entry.Texture = TStrongObjectPtr<UTexture2D>(Texture);
	Entry.Width = Width;
	Entry.Height = Height;
	AndroidThumbnailCache.Add(StableMediaId, MoveTemp(Entry));
	AndroidThumbnailCacheOrder.Remove(StableMediaId);
	AndroidThumbnailCacheOrder.Add(StableMediaId);
	while (AndroidThumbnailCacheOrder.Num() > FMath::Max(AndroidMaxThumbnailCacheEntries, 1))
	{
		const FString EvictedId = AndroidThumbnailCacheOrder[0];
		AndroidThumbnailCacheOrder.RemoveAt(0);
		AndroidThumbnailCache.Remove(EvictedId);
	}
#endif
}

void AMemoryRoomManager::RestoreAndroidThumbnailToActor(AMemoryPhotoActor* PhotoActor)
{
#if PLATFORM_ANDROID
	if (!PhotoActor) return;
	const FString StableMediaId = PhotoActor->GetAssignedStableMediaId();
	if (const FAndroidThumbnailCacheEntry* Cached = AndroidThumbnailCache.Find(StableMediaId); Cached && Cached->Texture.IsValid())
	{
		PhotoActor->ApplyRuntimeTexture(PhotoActor->GetAssignedContentUri(), Cached->Texture.Get(), Cached->Width, Cached->Height);
	}
	else
	{
		PhotoActor->UnloadRuntimeTexture();
		QueueAndroidThumbnailRequest(PhotoActor->GetAssignedPhotoIndex(), PhotoActor);
	}
#endif
}

void AMemoryRoomManager::ProcessAndroidExpandedSelection()
{
#if PLATFORM_ANDROID
	AMemoryPhotoActor* ExpandedActor = nullptr;
	for (const TObjectPtr<AMemoryPhotoActor>& PhotoPtr : SpawnedPhotos)
	{
		AMemoryPhotoActor* Photo = PhotoPtr.Get();
		if (Photo && Photo->IsExpanded() && !Photo->GetAssignedStableMediaId().IsEmpty())
		{
			ExpandedActor = Photo;
			break;
		}
	}

	if (!ExpandedActor)
	{
		if (AMemoryPhotoActor* Previous = ActiveExpandedPhotoActor.Get())
		{
			RestoreAndroidThumbnailToActor(Previous);
		}
		ActiveExpandedPhotoActor.Reset();
		ActiveExpandedPhotoIndex = INDEX_NONE;
		ActiveExpandedStableMediaId.Reset();
		ActiveExpandedContentUri.Reset();
		PendingAndroidExpandedQueue.Reset();
		++AndroidExpandedSelectionGeneration;
		PumpAndroidThumbnailQueue();
		return;
	}

	const bool bNewSelection = ActiveExpandedPhotoActor.Get() != ExpandedActor
		|| ActiveExpandedPhotoIndex != ExpandedActor->GetAssignedPhotoIndex()
		|| ActiveExpandedStableMediaId != ExpandedActor->GetAssignedStableMediaId()
		|| ActiveExpandedContentUri != ExpandedActor->GetAssignedContentUri();
	if (bNewSelection)
	{
		if (AMemoryPhotoActor* Previous = ActiveExpandedPhotoActor.Get())
		{
			if (Previous != ExpandedActor)
			{
				RestoreAndroidThumbnailToActor(Previous);
			}
		}
		++AndroidExpandedSelectionGeneration;
		ActiveExpandedPhotoActor = ExpandedActor;
		ActiveExpandedPhotoIndex = ExpandedActor->GetAssignedPhotoIndex();
		ActiveExpandedStableMediaId = ExpandedActor->GetAssignedStableMediaId();
		ActiveExpandedContentUri = ExpandedActor->GetAssignedContentUri();
		PendingAndroidExpandedQueue.Reset();
	}

	QueueAndroidExpandedRequest(ExpandedActor->GetAssignedPhotoIndex(), ExpandedActor);
#endif
}

void AMemoryRoomManager::QueueAndroidExpandedRequest(int32 PhotoIndex, AMemoryPhotoActor* PhotoActor)
{
#if PLATFORM_ANDROID
	if (!PhotoActor || !PhotoSource || !MediaStorePhotos.IsValidIndex(PhotoIndex)) return;
	const FMemoryPhotoMetadata& Photo = MediaStorePhotos[PhotoIndex];
	if (FAndroidExpandedCacheEntry* Cached = AndroidExpandedCache.Find(Photo.StableMediaId))
	{
		if (Cached->Texture.IsValid())
		{
			if (PhotoActor->GetRuntimeTexture() != Cached->Texture.Get())
			{
				PhotoActor->ApplyRuntimeTexture(Photo.ContentUri, Cached->Texture.Get(), Cached->Width, Cached->Height);
			}
			AndroidExpandedCacheOrder.Remove(Photo.StableMediaId);
			AndroidExpandedCacheOrder.Add(Photo.StableMediaId);
		}
		return;
	}
	if (ActiveAndroidExpandedDecodes >= FMath::Max(AndroidMaxConcurrentExpandedDecodes, 1)) return;
	for (const FAndroidExpandedRequest& Request : PendingAndroidExpandedQueue)
	{
		if (Request.StableMediaId == Photo.StableMediaId && Request.SelectionGeneration == AndroidExpandedSelectionGeneration) return;
	}
	FAndroidExpandedRequest Request;
	Request.PhotoActor = PhotoActor;
	Request.PhotoIndex = PhotoIndex;
	Request.StableMediaId = Photo.StableMediaId;
	Request.ContentUri = Photo.ContentUri;
	Request.AssignmentGeneration = PhotoActor->GetAssignmentGeneration();
	Request.SelectionGeneration = AndroidExpandedSelectionGeneration;
	Request.LibraryGeneration = PhotoLibraryGeneration;
	PendingAndroidExpandedQueue.Insert(Request, 0);
	PumpAndroidExpandedQueue();
#endif
}

void AMemoryRoomManager::PumpAndroidExpandedQueue()
{
#if PLATFORM_ANDROID
	while (PhotoSource && ActiveAndroidExpandedDecodes < FMath::Max(AndroidMaxConcurrentExpandedDecodes, 1) && PendingAndroidExpandedQueue.Num() > 0)
	{
		FAndroidExpandedRequest Request = PendingAndroidExpandedQueue[0];
		PendingAndroidExpandedQueue.RemoveAt(0);
		++ActiveAndroidExpandedDecodes;
		TWeakObjectPtr<AMemoryRoomManager> WeakManager(this);
		const AMemoryPhotoActor* RequestedPhoto = Request.PhotoActor.Get();
		const int32 DecodeDimension = RequestedPhoto && RequestedPhoto->IsPanoramaCandidate() ? 4096 : FMath::Max(AndroidExpandedPhotoMaxDimension, 1);
		PhotoSource->LoadPhotoPixelsAsync(Request.ContentUri, DecodeDimension, Request.SelectionGeneration, FMemoryPhotoPixelsCallback::CreateLambda([WeakManager, Request](bool bSuccess, TArray<FColor> Pixels, int32 Width, int32 Height) mutable
		{
			AMemoryRoomManager* Manager = WeakManager.Get();
			if (!Manager) return;
			Manager->ActiveAndroidExpandedDecodes = FMath::Max(Manager->ActiveAndroidExpandedDecodes - 1, 0);
			AMemoryPhotoActor* Actor = Request.PhotoActor.Get();
			const bool bFresh = Manager->PhotoLibraryGeneration == Request.LibraryGeneration
				&& Actor && Actor->IsExpanded()
				&& Manager->ActiveExpandedPhotoActor.Get() == Actor
				&& Manager->ActiveExpandedPhotoIndex == Request.PhotoIndex
				&& Manager->ActiveExpandedStableMediaId == Request.StableMediaId
				&& Manager->ActiveExpandedContentUri == Request.ContentUri
				&& Manager->AndroidExpandedSelectionGeneration == Request.SelectionGeneration
				&& Actor->GetAssignedPhotoIndex() == Request.PhotoIndex
				&& Actor->GetAssignedStableMediaId() == Request.StableMediaId
				&& Actor->GetAssignmentGeneration() == Request.AssignmentGeneration;
			if (bFresh && bSuccess && Actor->ApplyDecodedRuntimeImage(Request.ContentUri, TArray<FColor>(Pixels), Width, Height))
			{
				Manager->CacheAndroidExpandedTexture(Request.StableMediaId, Actor->GetRuntimeTexture(), Width, Height);
				UE_LOG(LogMemoryRoom, Verbose, TEXT("%s loaded Android expanded photo %s (%dx%d max %d)."), *Manager->GetName(), *Request.StableMediaId, Width, Height, FMath::Max(Manager->AndroidExpandedPhotoMaxDimension, 1));
			}
			else if (!bFresh)
			{
				UE_LOG(LogMemoryRoom, Verbose, TEXT("%s discarded stale Android expanded photo result for %s."), *Manager->GetName(), *Request.StableMediaId);
			}
			else
			{
				UE_LOG(LogMemoryRoom, Warning, TEXT("%s failed Android expanded photo %s."), *Manager->GetName(), *Request.StableMediaId);
			}
			Manager->PumpAndroidExpandedQueue();
			Manager->PumpAndroidThumbnailQueue();
		}));
	}
#endif
}

void AMemoryRoomManager::CacheAndroidExpandedTexture(const FString& StableMediaId, UTexture2D* Texture, int32 Width, int32 Height)
{
#if PLATFORM_ANDROID
	if (StableMediaId.IsEmpty() || !Texture) return;
	FAndroidExpandedCacheEntry Entry;
	Entry.Texture = TStrongObjectPtr<UTexture2D>(Texture);
	Entry.Width = Width;
	Entry.Height = Height;
	AndroidExpandedCache.Add(StableMediaId, MoveTemp(Entry));
	AndroidExpandedCacheOrder.Remove(StableMediaId);
	AndroidExpandedCacheOrder.Add(StableMediaId);
	while (AndroidExpandedCacheOrder.Num() > FMath::Max(AndroidExpandedCacheEntries, 1))
	{
		const FString EvictedId = AndroidExpandedCacheOrder[0];
		AndroidExpandedCacheOrder.RemoveAt(0);
		AndroidExpandedCache.Remove(EvictedId);
	}
#endif
}

bool AMemoryRoomManager::StartIncrementalGalleryLoad()
{
	if (bIsLoadingGallery)
	{
		UE_LOG(LogMemoryRoom, Warning, TEXT("%s restarting active incremental gallery load."), *GetName());
	}

	CancelGalleryLoad();
	ClearSpawnedPhotoActors();
	ClearGalleryTextureCaches();
	GalleryPhotoFiles.Reset();
	PhotoAspectRatios.Reset();
	GalleryPathOffsets.Reset();
	LoadedPhotoCount = 0;
	FailedPhotoCount = 0;
	TotalPhotosToLoad = 0;
	NumberOfPhotos = 0;

	if (!PhotoClass)
	{
		UE_LOG(LogMemoryRoom, Warning, TEXT("%s cannot start gallery load because PhotoClass is not set."), *GetName());
		return false;
	}

	if (DiscoveredPhotoFiles.Num() <= 0)
	{
		UE_LOG(LogMemoryRoom, Warning, TEXT("%s cannot start gallery load because DiscoveredPhotoFiles is empty. Run ScanPhotoFolder first."), *GetName());
		return false;
	}

	if (!GetWorld())
	{
		UE_LOG(LogMemoryRoom, Warning, TEXT("%s cannot start gallery load because World is unavailable."), *GetName());
		return false;
	}

	if (bUseVirtualizedGallery)
	{
		GalleryPhotoFiles = DiscoveredPhotoFiles;
		for (FString& Path : GalleryPhotoFiles)
		{
			FPaths::NormalizeFilename(Path);
		}
		if (MaxPhotosToLoad > 0 && GalleryPhotoFiles.Num() > MaxPhotosToLoad)
		{
			GalleryPhotoFiles.SetNum(MaxPhotosToLoad);
		}

		LoadPhotoAspectRatiosFromHeaders();

		NumberOfPhotos = GalleryPhotoFiles.Num();
		TotalPhotosToLoad = NumberOfPhotos;
		LoadedPhotoCount = NumberOfPhotos;
		CachedPhotoPathParameters.Reset();
		CachedFootprintRadii.Reset();
		CachedPhotoPositions.Reset();
		RebuildGalleryPathOffsets();
		ResetGalleryScroll();
		EnsureGalleryPool();
		LayoutVirtualizedGallery();
		OnPhotoCountChanged.Broadcast(NumberOfPhotos);
		OnGalleryLoadProgress.Broadcast(NumberOfPhotos, 0, NumberOfPhotos);
		OnGalleryLoadCompleted.Broadcast(NumberOfPhotos, 0);
		UE_LOG(LogMemoryRoom, Log, TEXT("%s virtualized gallery ready: %d photo(s), pool %d, thumbnails %d px."),
			*GetName(), NumberOfPhotos, SpawnedPhotos.Num(), FMath::Max(GalleryThumbnailMaxDimension, 64));
		return true;
	}

	PendingPhotoLoadQueue = DiscoveredPhotoFiles;
	PendingPhotoLoadQueue.Sort([](const FString& Left, const FString& Right)
	{
		const FString LeftFileName = FPaths::GetCleanFilename(Left);
		const FString RightFileName = FPaths::GetCleanFilename(Right);
		const int32 NameCompare = LeftFileName.Compare(RightFileName, ESearchCase::IgnoreCase);
		if (NameCompare != 0)
		{
			return NameCompare < 0;
		}
		return Left < Right;
	});

	if (MaxPhotosToLoad > 0 && PendingPhotoLoadQueue.Num() > MaxPhotosToLoad)
	{
		PendingPhotoLoadQueue.SetNum(MaxPhotosToLoad);
	}

	TotalPhotosToLoad = PendingPhotoLoadQueue.Num();
	if (TotalPhotosToLoad <= 0)
	{
		UE_LOG(LogMemoryRoom, Warning, TEXT("%s has no photos queued for gallery load."), *GetName());
		return false;
	}

	bIsLoadingGallery = true;
	UE_LOG(LogMemoryRoom, Log, TEXT("%s started incremental gallery load: %d photo(s), %d per batch, %.3fs delay, max texture dimension %d."), *GetName(), TotalPhotosToLoad, FMath::Max(PhotosToLoadPerBatch, 1), FMath::Max(BatchDelaySeconds, 0.0f), FMath::Max(MaximumRuntimeTextureDimension, 1));
	OnGalleryLoadProgress.Broadcast(LoadedPhotoCount, FailedPhotoCount, TotalPhotosToLoad);
	ProcessGalleryLoadBatch();
	return true;
}

void AMemoryRoomManager::CancelGalleryLoad()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(GalleryLoadTimerHandle);
	}
	PendingPhotoLoadQueue.Reset();
	PendingAndroidThumbnailQueue.Reset();
	QueuedAndroidThumbnailIds.Reset();
	PendingAndroidExpandedQueue.Reset();
	PendingAndroidTextureLoads.Reset();
	ActiveAndroidThumbnailDecodes = 0;
	ActiveAndroidExpandedDecodes = 0;
	ActiveExpandedPhotoActor.Reset();
	ActiveExpandedPhotoIndex = INDEX_NONE;
	ActiveExpandedStableMediaId.Reset();
	ActiveExpandedContentUri.Reset();
	if (bIsLoadingGallery)
	{
		UE_LOG(LogMemoryRoom, Log, TEXT("%s cancelled gallery load at %d loaded, %d failed of %d."), *GetName(), LoadedPhotoCount, FailedPhotoCount, TotalPhotosToLoad);
	}
	bIsLoadingGallery = false;
}

void AMemoryRoomManager::ClearGeneratedGallery()
{
	CancelGalleryLoad();
	ClearSpawnedPhotoActors();
	ClearGalleryTextureCaches();
	GalleryPhotoFiles.Reset();
	PhotoAspectRatios.Reset();
	GalleryPathOffsets.Reset();
	LoadedPhotoCount = 0;
	FailedPhotoCount = 0;
	TotalPhotosToLoad = 0;
	NumberOfPhotos = 0;
	MediaStorePhotos.Reset();
	PendingAndroidThumbnailQueue.Reset();
	QueuedAndroidThumbnailIds.Reset();
	PendingAndroidTextureLoads.Reset();
	AndroidThumbnailCache.Reset();
	AndroidThumbnailCacheOrder.Reset();
	PendingAndroidExpandedQueue.Reset();
	AndroidExpandedCache.Reset();
	AndroidExpandedCacheOrder.Reset();
	ActiveAndroidThumbnailDecodes = 0;
	ActiveAndroidExpandedDecodes = 0;
	ActiveExpandedPhotoActor.Reset();
	ActiveExpandedPhotoIndex = INDEX_NONE;
	ActiveExpandedStableMediaId.Reset();
	ActiveExpandedContentUri.Reset();
}

void AMemoryRoomManager::ProcessGalleryLoadBatch()
{
	if (!bIsLoadingGallery)
	{
		return;
	}

	UWorld* World = GetWorld();
	if (!World || !PhotoClass)
	{
		UE_LOG(LogMemoryRoom, Warning, TEXT("%s stopping gallery load because World or PhotoClass became unavailable."), *GetName());
		FinishGalleryLoad();
		return;
	}

	World->GetTimerManager().ClearTimer(GalleryLoadTimerHandle);

	FActorSpawnParameters SpawnParameters;
	SpawnParameters.Owner = this;
	const int32 BatchCount = FMath::Max(PhotosToLoadPerBatch, 1);

	for (int32 BatchIndex = 0; BatchIndex < BatchCount && PendingPhotoLoadQueue.Num() > 0; ++BatchIndex)
	{
		const FString PhotoFilePath = PendingPhotoLoadQueue[0];
		PendingPhotoLoadQueue.RemoveAt(0);

		AMemoryPhotoActor* SpawnedPhoto = World->SpawnActor<AMemoryPhotoActor>(PhotoClass, BuildPhotoTransform(SpawnedPhotos.Num()), SpawnParameters);
		if (!SpawnedPhoto)
		{
			++FailedPhotoCount;
			UE_LOG(LogMemoryRoom, Warning, TEXT("%s failed to spawn photo actor for: %s"), *GetName(), *PhotoFilePath);
			continue;
		}

		SpawnedPhoto->SetActorHiddenInGame(true);
		// Header read only. Decoding here blocked the game thread for ~180ms per photo;
		// LayoutSpawnedPhotos async-loads the pixels for photos that become visible.
		if (!SpawnedPhoto->PrepareFromFile(PhotoFilePath))
		{
			++FailedPhotoCount;
			UE_LOG(LogMemoryRoom, Warning, TEXT("%s failed file path: %s"), *GetName(), *PhotoFilePath);
			SpawnedPhoto->Destroy();
			continue;
		}

		SpawnedPhotos.Add(SpawnedPhoto);
		++LoadedPhotoCount;
	}

	NumberOfPhotos = SpawnedPhotos.Num();
	RebuildPhotoPathCache();
	LayoutSpawnedPhotos();
	UE_LOG(LogMemoryRoom, Verbose, TEXT("%s gallery load progress: %d loaded, %d failed of %d."), *GetName(), LoadedPhotoCount, FailedPhotoCount, TotalPhotosToLoad);
	OnGalleryLoadProgress.Broadcast(LoadedPhotoCount, FailedPhotoCount, TotalPhotosToLoad);

	if (PendingPhotoLoadQueue.Num() <= 0)
	{
		FinishGalleryLoad();
		return;
	}

	// FTimerManager::SetTimer clears the timer when the rate is <= 0, so a zero
	// BatchDelaySeconds used to stall the load after the first batch. Route that case
	// through the next-tick queue instead so the remaining photos still spawn.
	const float SafeBatchDelay = FMath::Max(BatchDelaySeconds, 0.0f);
	if (SafeBatchDelay <= KINDA_SMALL_NUMBER)
	{
		World->GetTimerManager().SetTimerForNextTick(this, &AMemoryRoomManager::ProcessGalleryLoadBatch);
	}
	else
	{
		World->GetTimerManager().SetTimer(GalleryLoadTimerHandle, this, &AMemoryRoomManager::ProcessGalleryLoadBatch, SafeBatchDelay, false);
	}
}

void AMemoryRoomManager::FinishGalleryLoad()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(GalleryLoadTimerHandle);
	}
	PendingPhotoLoadQueue.Reset();
	bIsLoadingGallery = false;

	// Any photo actors spawned during this load pick up the theme frame style here (spawn
	// sites also style individually; this covers a reload that reused an existing pool).
	for (const TObjectPtr<AMemoryPhotoActor>& PhotoPtr : SpawnedPhotos)
	{
		ApplyThemeToPhoto(PhotoPtr.Get());
	}

	UE_LOG(LogMemoryRoom, Log, TEXT("%s completed gallery load: %d loaded, %d failed of %d."), *GetName(), LoadedPhotoCount, FailedPhotoCount, TotalPhotosToLoad);
	OnGalleryLoadCompleted.Broadcast(LoadedPhotoCount, FailedPhotoCount);
}

bool AMemoryRoomManager::LoadFirstDiscoveredPhoto()
{
	RefreshCachedHelixCenterLocation();

	if (DiscoveredPhotoFiles.Num() <= 0)
	{
		UE_LOG(LogMemoryRoom, Warning, TEXT("%s cannot load first discovered photo because DiscoveredPhotoFiles is empty. Run ScanPhotoFolder first."), *GetName());
		return false;
	}

	const FString& FirstPhotoPath = DiscoveredPhotoFiles[0];
	if (FirstPhotoPath.IsEmpty() || !FPaths::FileExists(FirstPhotoPath))
	{
		UE_LOG(LogMemoryRoom, Warning, TEXT("%s cannot load first discovered photo because the file is missing: %s"), *GetName(), *FirstPhotoPath);
		return false;
	}

	AMemoryPhotoActor* PhotoToLoad = nullptr;
	for (const TObjectPtr<AMemoryPhotoActor>& PhotoPtr : SpawnedPhotos)
	{
		if (AMemoryPhotoActor* ExistingPhoto = PhotoPtr.Get())
		{
			PhotoToLoad = ExistingPhoto;
			break;
		}
	}

	if (!PhotoToLoad)
	{
		if (UWorld* World = GetWorld())
		{
			for (TActorIterator<AMemoryPhotoActor> It(World); It;)
			{
				PhotoToLoad = *It;
				break;
			}
		}
	}

	if (!PhotoToLoad)
	{
		if (!PhotoClass)
		{
			UE_LOG(LogMemoryRoom, Warning, TEXT("%s cannot load first discovered photo because no existing photo was found and PhotoClass is not set."), *GetName());
			return false;
		}

		UWorld* World = GetWorld();
		if (!World)
		{
			UE_LOG(LogMemoryRoom, Warning, TEXT("%s cannot load first discovered photo because World is unavailable."), *GetName());
			return false;
		}

		PhotoToLoad = World->SpawnActor<AMemoryPhotoActor>(PhotoClass, BuildPhotoTransform(0));
		if (!PhotoToLoad)
		{
			UE_LOG(LogMemoryRoom, Warning, TEXT("%s failed to spawn one photo actor for runtime image test."), *GetName());
			return false;
		}
		SpawnedPhotos.Add(PhotoToLoad);
	}

	PhotoToLoad->SetActorHiddenInGame(false);
	PhotoToLoad->AssignPhotoIndex(0);
	const bool bLoaded = PhotoToLoad->LoadImageFromFile(FirstPhotoPath);
	if (bLoaded)
	{
		NumberOfPhotos = FMath::Max(NumberOfPhotos, SpawnedPhotos.Num());
		RebuildPhotoPathCache();
		LayoutSpawnedPhotos();
		UE_LOG(LogMemoryRoom, Log, TEXT("%s LoadFirstDiscoveredPhoto succeeded for: %s"), *GetName(), *FirstPhotoPath);
	}
	else
	{
		UE_LOG(LogMemoryRoom, Warning, TEXT("%s LoadFirstDiscoveredPhoto failed for: %s"), *GetName(), *FirstPhotoPath);
	}
	return bLoaded;
}

void AMemoryRoomManager::BindScrollInput()
{
	if (!bAutoBindThumbstickScrollInput || ScrollInputAxisName.IsNone())
	{
		return;
	}

	if (UWorld* World = GetWorld())
	{
		if (APlayerController* PlayerController = World->GetFirstPlayerController())
		{
			EnableInput(PlayerController);
			if (InputComponent)
			{
				InputComponent->BindAxis(ScrollInputAxisName, this, &AMemoryRoomManager::AddScrollInput);
			}
		}
	}
}

void AMemoryRoomManager::ToggleFloat()
{
	bFloatEnabled = !bFloatEnabled;
	OnUserSettingsChanged.Broadcast();

	// A Character-style pawn's movement component fights an upward SetActorLocation with
	// gravity + floor-snapping, so switch it to flying while floating.
	if (const UWorld* World = GetWorld())
	{
		if (const APlayerController* PC = World->GetFirstPlayerController())
		{
			if (APawn* Pawn = PC->GetPawn())
			{
				if (UCharacterMovementComponent* CMC = Pawn->FindComponentByClass<UCharacterMovementComponent>())
				{
					CMC->SetMovementMode(bFloatEnabled ? MOVE_Flying : MOVE_Walking);
					CMC->StopMovementImmediately();
				}
				else if (UPawnMovementComponent* PMC = Pawn->FindComponentByClass<UPawnMovementComponent>())
				{
					PMC->StopMovementImmediately();
				}
			}
		}
	}

	UE_LOG(LogMemoryRoom, Warning, TEXT("%s float %s."), *GetName(), bFloatEnabled ? TEXT("ON") : TEXT("OFF"));
}

void AMemoryRoomManager::BindLocomotionInput()
{
	UWorld* World = GetWorld();
	APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
	if (!PC)
	{
		return;
	}
	EnableInput(PC);
	if (!InputComponent)
	{
		return;
	}
	// Same mechanism as BindScrollInput. Bound delegates DO fire for OpenXR controllers even
	// though polling the raw key state (GetInputAnalogKeyState/IsInputKeyDown) does not.
	InputComponent->BindAxis(TEXT("MemoryRoomMoveX"), this, &AMemoryRoomManager::AddMoveXInput);
	InputComponent->BindAxis(TEXT("MemoryRoomMoveY"), this, &AMemoryRoomManager::AddMoveYInput);
	InputComponent->BindAction(TEXT("MemoryRoomToggleFloat"), IE_Pressed, this, &AMemoryRoomManager::ToggleFloat);
	UE_LOG(LogMemoryRoom, Warning, TEXT("%s locomotion input bound."), *GetName());
}

void AMemoryRoomManager::AddMoveXInput(float Value) { LocoInputX = Value; }
void AMemoryRoomManager::AddMoveYInput(float Value) { LocoInputY = Value; }

void AMemoryRoomManager::SetLocomotionMoveInput(FVector2D Value)
{
	LocoInputX = Value.X;
	LocoInputY = Value.Y;
}

void AMemoryRoomManager::StopLocomotionMoveInput()
{
	LocoInputX = 0.0f;
	LocoInputY = 0.0f;
}

void AMemoryRoomManager::SetTurnInput(float Value)
{
	TurnInputX = Value;
}

void AMemoryRoomManager::StopTurnInput()
{
	TurnInputX = 0.0f;
}

void AMemoryRoomManager::TickTurn(float DeltaSeconds)
{
	UWorld* World = GetWorld();
	APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
	APawn* Pawn = PC ? PC->GetPawn() : nullptr;

	// Throttled diagnostic: shows whether SetTurnInput is even reaching this class, same as
	// the "Loco diag" line above - fires regardless of pawn/deadzone so a silent Blueprint or
	// Input Mapping wiring gap (raw TurnInputX stuck at 0) is visible without a debugger.
	static float TurnDiagAcc = 0.0f;
	TurnDiagAcc += DeltaSeconds;
	if (TurnDiagAcc > 1.0f)
	{
		TurnDiagAcc = 0.0f;
		UE_LOG(LogMemoryRoom, Warning, TEXT("Turn diag: pawn=%s turnInput=%.3f speed=%.1f"),
			*GetNameSafe(Pawn), TurnInputX, TurnSpeedDegPerSec);
	}

	if (!Pawn || DeltaSeconds <= 0.0f)
	{
		return;
	}

	const float Stick = FMath::Abs(TurnInputX) > 0.15f ? TurnInputX : 0.0f;
	if (FMath::IsNearlyZero(Stick))
	{
		return;
	}

	const float YawDelta = Stick * TurnSpeedDegPerSec * DeltaSeconds;

	FVector HeadLocation = FVector::ZeroVector;
	FRotator HeadRotation = FRotator::ZeroRotator;
	PC->GetPlayerViewPoint(HeadLocation, HeadRotation);

	// Rotate the pawn about the player's actual head position, not the pawn's own capsule
	// origin - roomscale tracking means the head can be anywhere inside the play space, so
	// pivoting on the capsule would swing the room around you instead of spinning it in place.
	const FVector HeadToPawn = Pawn->GetActorLocation() - HeadLocation;
	const FVector RotatedOffset = HeadToPawn.RotateAngleAxis(YawDelta, FVector::UpVector);
	Pawn->SetActorLocation(HeadLocation + RotatedOffset, /*bSweep*/ false);
	Pawn->AddActorWorldRotation(FRotator(0.0f, YawDelta, 0.0f));
}

void AMemoryRoomManager::TickLocomotion(float DeltaSeconds)
{
	UWorld* World = GetWorld();
	APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
	APawn* Pawn = PC ? PC->GetPawn() : nullptr;

	// Throttled diagnostic: shows the bound-axis values reaching us.
	static float DiagAcc2 = 0.0f;
	DiagAcc2 += DeltaSeconds;
	if (DiagAcc2 > 1.0f)
	{
		DiagAcc2 = 0.0f;
		UE_LOG(LogMemoryRoom, Warning, TEXT("Loco diag: allow=%d pawn=%s move=(%.2f,%.2f) float=%d"),
			bAllowFreeLocomotion ? 1 : 0, *GetNameSafe(Pawn), LocoInputX, LocoInputY, bFloatEnabled ? 1 : 0);
	}

	if (!bAllowFreeLocomotion || DeltaSeconds <= 0.0f || !Pawn)
	{
		return;
	}

	const float StickX = FMath::Abs(LocoInputX) > 0.15f ? LocoInputX : 0.0f;
	const float StickY = FMath::Abs(LocoInputY) > 0.15f ? LocoInputY : 0.0f;

	FVector ViewLocation = FVector::ZeroVector;
	FRotator ViewRotation = FRotator::ZeroRotator;
	PC->GetPlayerViewPoint(ViewLocation, ViewRotation);

	FVector Forward;
	FVector Right;
	if (bFloatEnabled)
	{
		// Fly where you look - full view direction, pitch included.
		Forward = ViewRotation.Vector();
		Right = FRotationMatrix(ViewRotation).GetScaledAxis(EAxis::Y);
	}
	else
	{
		// Walk - view direction flattened to the floor.
		Forward = FVector(ViewRotation.Vector().X, ViewRotation.Vector().Y, 0.0f);
		if (!Forward.Normalize()) { Forward = FVector::ForwardVector; }
		Right = FVector::CrossProduct(FVector::UpVector, Forward);
	}

	const float Speed = bFloatEnabled ? FloatSpeedCmPerSec : WalkSpeedCmPerSec;
	const FVector Delta = (Forward * StickY + Right * StickX) * Speed * DeltaSeconds;
	const bool bMoving = !Delta.IsNearlyZero();

	const FVector Current = Pawn->GetActorLocation();
	FVector Target = Current + Delta;

	// Vertical clamp: floor at the bottom, MaxFloatTurns of the spiral at the top.
	const float FloorZ = bLocomotionFloorCaptured ? LocomotionFloorZ : GetActorLocation().Z;
	const float TurnUnits = GallerySpiralDegreesPerUnit > KINDA_SMALL_NUMBER ? (360.0f / GallerySpiralDegreesPerUnit) : 15.0f;
	const float CeilingZ = FloorZ + FMath::Max(MaxFloatTurns, 0.0f) * TurnUnits * GallerySpiralRisePerUnit;
	if (!bFloatEnabled)
	{
		// Ease back down to the floor when not floating.
		Target.Z = FMath::FInterpTo(Current.Z, FloorZ, DeltaSeconds, 3.0f);
	}
	Target.Z = FMath::Clamp(Target.Z, FloorZ, CeilingZ);

	// Horizontal containment: never let the player drift past the photo ring - it tapers with
	// height exactly like EvaluateGallerySpiralPosition, so the boundary tapers along with it.
	{
		const FVector CenterLocation = GetHelixCenterLocation();
		const float HeightAboveBase = FMath::Max(Target.Z - CenterLocation.Z - PhotoHeight, 0.0f);
		const float RingTaperAlpha = FMath::Clamp(HeightAboveBase / FMath::Max(GallerySpiralTaperHeight, 1.0f), 0.0f, 1.0f);
		const float RingRadius = FMath::Lerp(GallerySpiralBaseRadius, GallerySpiralTopRadius, RingTaperAlpha);
		const float MaxRadius = FMath::Max(RingRadius - SpiralBoundaryMarginCm, 10.0f);

		FVector2D Offset2D(Target.X - CenterLocation.X, Target.Y - CenterLocation.Y);
		const float DistFromCenter = Offset2D.Size();
		if (DistFromCenter > MaxRadius && DistFromCenter > KINDA_SMALL_NUMBER)
		{
			const FVector2D Clamped = Offset2D * (MaxRadius / DistFromCenter);
			Target.X = CenterLocation.X + Clamped.X;
			Target.Y = CenterLocation.Y + Clamped.Y;
		}
	}

	// Keep-out ONLY while actively driving into a photo - never shove the player away from a
	// photo they are holding (a grabbed photo pulled in close would otherwise push them off).
	const float KeepOut = bFloatEnabled ? FloatKeepOutRadiusCm : PhotoKeepOutRadiusCm;
	if (bMoving && KeepOut > 1.0f)
	{
		for (const TObjectPtr<AMemoryPhotoActor>& PhotoPtr : SpawnedPhotos)
		{
			const AMemoryPhotoActor* Photo = PhotoPtr.Get();
			if (!Photo || Photo->IsHidden())
			{
				continue;
			}
			const FVector PhotoLoc = Photo->GetActorLocation();
			const float NewDistSq = FVector::DistSquared(Target, PhotoLoc);
			// Only correct if this move brought us inside the ring AND closer than we were.
			if (NewDistSq < KeepOut * KeepOut && NewDistSq < FVector::DistSquared(Current, PhotoLoc))
			{
				FVector Away = Target - PhotoLoc;
				if (Away.Normalize())
				{
					Target = PhotoLoc + Away * KeepOut;
				}
			}
		}
	}

	if (!Target.Equals(Current, 0.01f))
	{
		Pawn->SetActorLocation(Target, /*bSweep*/ false);
		// Kill any residual velocity so a movement component does not drag the pawn back.
		if (UPawnMovementComponent* PMC = Pawn->FindComponentByClass<UPawnMovementComponent>())
		{
			PMC->Velocity = FVector::ZeroVector;
		}
	}

	// Diagnostic: throttled, only while floating + moving.
	static float DiagAcc = 0.0f;
	DiagAcc += DeltaSeconds;
	if (bFloatEnabled && bMoving && DiagAcc > 0.4f)
	{
		DiagAcc = 0.0f;
		UE_LOG(LogMemoryRoom, Warning, TEXT("float move: stick(%.2f,%.2f) fwd.Z=%.2f cur.Z=%.0f tgt.Z=%.0f floor=%.0f ceil=%.0f"),
			StickX, StickY, Forward.Z, Current.Z, Target.Z, FloorZ, CeilingZ);
	}
}

void AMemoryRoomManager::SpawnPhotos()
{
	RefreshCachedHelixCenterLocation();

	if (!PhotoClass)
	{
		UE_LOG(LogMemoryRoom, Warning, TEXT("%s cannot spawn photos because PhotoClass is not set."), *GetName());
		return;
	}

	if (NumberOfPhotos <= 0)
	{
		UE_LOG(LogMemoryRoom, Warning, TEXT("%s cannot spawn photos because NumberOfPhotos must be greater than zero."), *GetName());
		return;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	ClearSpawnedPhotoActors();
	EnsurePhotoPoolSize();
	RebuildPhotoPathCache();
	LayoutSpawnedPhotos();
}

void AMemoryRoomManager::ClearSpawnedPhotoActors()
{
	for (const TObjectPtr<AMemoryPhotoActor>& PhotoPtr : SpawnedPhotos)
	{
		if (AMemoryPhotoActor* Photo = PhotoPtr.Get())
		{
			Photo->Destroy();
		}
	}
	SpawnedPhotos.Reset();
	CachedPhotoPathParameters.Reset();
	CachedFootprintRadii.Reset();
	CachedPhotoPositions.Reset();
}

void AMemoryRoomManager::EnsurePhotoPoolSize()
{
	if (!PhotoClass || NumberOfPhotos <= 0)
	{
		return;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	const int32 RequiredPoolSize = GetRequiredPhotoPoolSize();
	for (int32 PoolIndex = SpawnedPhotos.Num(); PoolIndex < RequiredPoolSize; ++PoolIndex)
	{
		if (AMemoryPhotoActor* SpawnedPhoto = World->SpawnActor<AMemoryPhotoActor>(PhotoClass, GetActorTransform()))
		{
			SpawnedPhoto->SetActorHiddenInGame(true);
			SpawnedPhoto->ApplyViewingWindowState(0.0f, 1.0f, false);
			ApplyThemeToPhoto(SpawnedPhoto);
			SpawnedPhotos.Add(SpawnedPhoto);
		}
	}
}

void AMemoryRoomManager::LayoutSpawnedPhotos()
{
	if (UseVirtualizedGallery())
	{
		LayoutVirtualizedGallery();
		return;
	}

	EnsurePhotoPoolSize();
	if (bEnableHelixLayout && CachedPhotoPathParameters.Num() < NumberOfPhotos)
	{
		RebuildPhotoPathCache();
	}

	const float LowerBound = -FMath::Max(VisibleRangeBelow, 0.0f);
	const float UpperBound = FMath::Max(VisibleRangeAbove, 0.0f);
	const int32 FirstVisiblePhotoIndex = FMath::Max(0, FMath::FloorToInt(LowerBound - ScrollOffset));
	const int32 LastVisiblePhotoIndex = FMath::Min(NumberOfPhotos - 1, FMath::CeilToInt(UpperBound - ScrollOffset));
	const bool bHasDedicatedActorPerPhoto = NumberOfPhotos > 0 && SpawnedPhotos.Num() >= NumberOfPhotos;

	if (bHasDedicatedActorPerPhoto)
	{
		for (int32 PhotoIndex = 0; PhotoIndex < SpawnedPhotos.Num(); ++PhotoIndex)
		{
			AMemoryPhotoActor* Photo = SpawnedPhotos[PhotoIndex].Get();
			if (!Photo)
			{
				continue;
			}

			const float BasePathParameter = CachedPhotoPathParameters.IsValidIndex(PhotoIndex) ? CachedPhotoPathParameters[PhotoIndex] : static_cast<float>(PhotoIndex);
			const float PathIndex = BasePathParameter + ScrollOffset;
			const FPhotoViewingState ViewingState = BuildPhotoViewingState(PathIndex);
			const float BaseScale = GetDeterministicPhotoScale(PhotoIndex);
			FTransform PhotoTransform = BuildPhotoTransform(PhotoIndex);
			PhotoTransform.SetScale3D(FVector(BaseScale * ViewingState.ScaleMultiplier));
			Photo->AssignPhotoIndex(PhotoIndex);
			Photo->SetLayoutTransform(PhotoTransform, ScrollOffset);
			if (Photo->IsExpanded())
			{
				continue;
			}

			if (!ViewingState.bVisible)
			{
				if (!Photo->IsHidden())
				{
					Photo->ApplyViewingWindowState(0.0f, 1.0f, false);
				}
#if PLATFORM_ANDROID
				Photo->AssignAndroidMediaStoreRecord(INDEX_NONE, TEXT(""), TEXT(""), 1, 1);
#endif
				if (Photo->HasLoadedTexture())
				{
					Photo->UnloadRuntimeTexture();
				}
				continue;
			}

#if PLATFORM_ANDROID
			if (MediaStorePhotos.IsValidIndex(PhotoIndex))
			{
				AssignAndroidPhotoToActor(PhotoIndex, Photo);
				QueueAndroidThumbnailRequest(PhotoIndex, Photo);
			}
#endif

			const float FocusOpacityScale = FMath::Lerp(1.0f, FMath::Clamp(FocusOtherPhotoOpacity, 0.0f, 1.0f), FocusBlend);
			Photo->ApplyViewingWindowState(ViewingState.Opacity * FocusOpacityScale, ViewingState.ScaleMultiplier, ViewingState.bInteractive, ViewingState.BlurAmount);
		}
		bHasPendingPhotoTextures = RequestVisiblePhotoTextures();
		LogLayoutDiagnostics();
		return;
	}

	int32 LogicalPhotoIndex = FirstVisiblePhotoIndex;
	for (int32 PoolIndex = 0; PoolIndex < SpawnedPhotos.Num(); ++PoolIndex)
	{
		AMemoryPhotoActor* Photo = SpawnedPhotos[PoolIndex].Get();
		if (!Photo)
		{
			continue;
		}

		// Do not re-apply or recycle a photo while it is expanded. The expanded photo
		// animates itself toward the viewer; forcing the helix transform every manager
		// tick pins it in place while only its metadata appears.
		if (Photo->IsExpanded())
		{
			const int32 ExpandedLogicalPhotoIndex = Photo->GetAssignedPhotoIndex();
			if (ExpandedLogicalPhotoIndex >= 0 && ExpandedLogicalPhotoIndex < NumberOfPhotos)
			{
				const float ExpandedBasePathParameter = CachedPhotoPathParameters.IsValidIndex(ExpandedLogicalPhotoIndex) ? CachedPhotoPathParameters[ExpandedLogicalPhotoIndex] : static_cast<float>(ExpandedLogicalPhotoIndex);
				const float ExpandedPathIndex = ExpandedBasePathParameter + ScrollOffset;
				const FPhotoViewingState ExpandedViewingState = BuildPhotoViewingState(ExpandedPathIndex);
				const float ExpandedBaseScale = GetDeterministicPhotoScale(ExpandedLogicalPhotoIndex);
				FTransform ExpandedLayoutTransform = BuildPhotoTransform(ExpandedLogicalPhotoIndex);
				ExpandedLayoutTransform.SetScale3D(FVector(ExpandedBaseScale * ExpandedViewingState.ScaleMultiplier));
				Photo->SetLayoutTransform(ExpandedLayoutTransform, ScrollOffset);
			}
			continue;
		}

		if (LogicalPhotoIndex > LastVisiblePhotoIndex)
		{
			if (!Photo->IsHidden())
			{
				Photo->ApplyViewingWindowState(0.0f, 1.0f, false);
			}
#if PLATFORM_ANDROID
			Photo->AssignAndroidMediaStoreRecord(INDEX_NONE, TEXT(""), TEXT(""), 1, 1);
#endif
			if (Photo->HasLoadedTexture())
			{
				Photo->UnloadRuntimeTexture();
			}
			continue;
		}

		const float BasePathParameter = CachedPhotoPathParameters.IsValidIndex(LogicalPhotoIndex) ? CachedPhotoPathParameters[LogicalPhotoIndex] : static_cast<float>(LogicalPhotoIndex);
		const float PathIndex = BasePathParameter + ScrollOffset;
		const FPhotoViewingState ViewingState = BuildPhotoViewingState(PathIndex);
		const float BaseScale = GetDeterministicPhotoScale(LogicalPhotoIndex);
		FTransform PhotoTransform = BuildPhotoTransform(LogicalPhotoIndex);
		PhotoTransform.SetScale3D(FVector(BaseScale * ViewingState.ScaleMultiplier));

		Photo->AssignPhotoIndex(LogicalPhotoIndex);
		Photo->SetLayoutTransform(PhotoTransform, ScrollOffset);
		const float FocusOpacityScale = FMath::Lerp(1.0f, FMath::Clamp(FocusOtherPhotoOpacity, 0.0f, 1.0f), FocusBlend);
#if PLATFORM_ANDROID
		if (MediaStorePhotos.IsValidIndex(LogicalPhotoIndex))
		{
			AssignAndroidPhotoToActor(LogicalPhotoIndex, Photo);
			QueueAndroidThumbnailRequest(LogicalPhotoIndex, Photo);
		}
#endif
		Photo->ApplyViewingWindowState(ViewingState.Opacity * FocusOpacityScale, ViewingState.ScaleMultiplier, ViewingState.bInteractive, ViewingState.BlurAmount);
		++LogicalPhotoIndex;
	}

	bHasPendingPhotoTextures = RequestVisiblePhotoTextures();
	LogLayoutDiagnostics();
}

void AMemoryRoomManager::LogLayoutDiagnostics()
{
	// Roughly once a second, so it is safe to leave on while tuning the spiral.
	const UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	const double Now = World->GetTimeSeconds();
	if (Now - LastLayoutDiagnosticSeconds < 1.0)
	{
		return;
	}
	LastLayoutDiagnosticSeconds = Now;

	int32 Visible = 0;
	int32 Textured = 0;
	float MinPath = FLT_MAX;
	float MaxPath = -FLT_MAX;
	for (int32 Index = 0; Index < SpawnedPhotos.Num(); ++Index)
	{
		const AMemoryPhotoActor* Photo = SpawnedPhotos[Index].Get();
		if (!Photo)
		{
			continue;
		}
		if (!Photo->IsHidden())
		{
			++Visible;
		}
		if (Photo->HasLoadedTexture())
		{
			++Textured;
		}
		if (CachedPhotoPathParameters.IsValidIndex(Index))
		{
			const float PathIndex = CachedPhotoPathParameters[Index] + ScrollOffset;
			MinPath = FMath::Min(MinPath, PathIndex);
			MaxPath = FMath::Max(MaxPath, PathIndex);
		}
	}

	UE_LOG(LogMemoryRoom, Log, TEXT("%s layout: %d photos, %d visible, %d textured, scroll %.1f, path range [%.1f..%.1f], window [%.1f..%.1f]."),
		*GetName(), SpawnedPhotos.Num(), Visible, Textured, ScrollOffset,
		MinPath == FLT_MAX ? 0.0f : MinPath, MaxPath == -FLT_MAX ? 0.0f : MaxPath,
		-FMath::Max(VisibleRangeBelow, 0.0f), FMath::Max(VisibleRangeAbove, 0.0f));
}

bool AMemoryRoomManager::RequestVisiblePhotoTextures()
{
#if PLATFORM_ANDROID
	// Android streams through its own MediaStore thumbnail queue.
	return false;
#else
	const int32 MaxConcurrent = FMath::Clamp(MaxSimultaneousPhotoDecodes, 1, 16);

	TArray<TPair<float, AMemoryPhotoActor*>> Waiting;
	int32 InFlight = 0;
	for (const TObjectPtr<AMemoryPhotoActor>& PhotoPtr : SpawnedPhotos)
	{
		AMemoryPhotoActor* Photo = PhotoPtr.Get();
		if (!Photo)
		{
			continue;
		}
		if (Photo->IsTextureLoadInFlight())
		{
			++InFlight;
			continue;
		}
		if (Photo->IsHidden() || Photo->HasLoadedTexture() || Photo->GetLoadedFilePath().IsEmpty())
		{
			continue;
		}
		Waiting.Emplace(static_cast<float>(FVector::DistSquared(Photo->GetActorLocation(), CachedHelixCenterLocation)), Photo);
	}

	if (Waiting.Num() <= 0)
	{
		return InFlight > 0;
	}

	// Nearest first, so what the player is looking at fills in before the far end of the helix.
	Waiting.Sort([](const TPair<float, AMemoryPhotoActor*>& Left, const TPair<float, AMemoryPhotoActor*>& Right)
	{
		return Left.Key < Right.Key;
	});

	const int32 ThumbnailDimension = FMath::Max(GalleryThumbnailMaxDimension, 64);
	for (int32 Index = 0; Index < Waiting.Num() && InFlight < MaxConcurrent; ++Index)
	{
		Waiting[Index].Value->LoadImageFromFileAsync(Waiting[Index].Value->GetLoadedFilePath(), ThumbnailDimension);
		++InFlight;
	}

	return true;
#endif
}

// ---------------------------------------------------------------------------------------
//  Virtualized desktop gallery
//
//  A fixed pool of photo actors is recycled as the spiral scrolls, the way a phone photo
//  grid reuses a handful of cell views. The manager owns an LRU texture cache keyed by
//  file path, so scrolling a photo out and back in is instant. Thumbnails stream in for
//  what is near the view; the open photo gets a full-resolution decode.
// ---------------------------------------------------------------------------------------

bool AMemoryRoomManager::UseVirtualizedGallery() const
{
	// Same width-aware spiral on both platforms now. On Android the "files" are MediaStore
	// content URIs and texture streaming still routes through the Android thumbnail queue,
	// but layout, spacing and shape are shared.
	return bUseVirtualizedGallery && GalleryPhotoFiles.Num() > 0;
}

void AMemoryRoomManager::LoadPhotoAspectRatiosFromHeaders()
{
	const double StartSeconds = FPlatformTime::Seconds();
	PhotoAspectRatios.SetNumZeroed(GalleryPhotoFiles.Num());
	for (int32 Index = 0; Index < GalleryPhotoFiles.Num(); ++Index)
	{
		int32 Width = 0;
		int32 Height = 0;
		if (AMemoryPhotoActor::ReadImageDimensions(GalleryPhotoFiles[Index], Width, Height) && Width > 0 && Height > 0)
		{
			PhotoAspectRatios[Index] = static_cast<float>(Width) / static_cast<float>(Height);
		}
		else
		{
			// A photo whose header would not parse still gets a slot; it just spaces as square.
			PhotoAspectRatios[Index] = 1.0f;
		}
	}
	UE_LOG(LogMemoryRoom, Log, TEXT("%s read %d photo headers in %.1f ms."),
		*GetName(), GalleryPhotoFiles.Num(), (FPlatformTime::Seconds() - StartSeconds) * 1000.0);
}

float AMemoryRoomManager::GetGalleryPhotoWidth(int32 LogicalPhotoIndex) const
{
	const float Aspect = PhotoAspectRatios.IsValidIndex(LogicalPhotoIndex) ? PhotoAspectRatios[LogicalPhotoIndex] : 1.0f;
	const float Scale = GetDeterministicPhotoScale(LogicalPhotoIndex);

	if (const AMemoryPhotoActor* Defaults = PhotoClass ? PhotoClass->GetDefaultObject<AMemoryPhotoActor>() : nullptr)
	{
		float Width = 1.0f;
		float Height = 1.0f;
		Defaults->GetDisplaySizeForAspect(Aspect, Width, Height);
		// GetDisplaySizeForAspect already returns world centimetres, the same units
		// EvaluateGallerySpiralPosition works in - no extra scaling.
		return FMath::Max(Width, 1.0f) * Scale;
	}

	return GetEstimatedPhotoWidth(Scale);
}

FVector AMemoryRoomManager::EvaluateGallerySpiralPosition(float PathOffset) const
{
	const FVector CenterLocation = GetHelixCenterLocation();
	const float TaperHeight = FMath::Max(GallerySpiralTaperHeight, 1.0f);
	const float RisePerUnit = FMath::Max(GallerySpiralRisePerUnit, 0.0f);

	const float HeightOffset = PathOffset * RisePerUnit;
	const float TaperAlpha = FMath::Clamp(HeightOffset / TaperHeight, 0.0f, 1.0f);
	const float CurrentRadius = FMath::Lerp(GallerySpiralBaseRadius, GallerySpiralTopRadius, TaperAlpha);
	const float AngleDegrees = PathOffset * GallerySpiralDegreesPerUnit;

	const FVector RingOffset = FVector(CurrentRadius, 0.0f, 0.0f).RotateAngleAxis(AngleDegrees, FVector::UpVector);
	return CenterLocation + FVector(RingOffset.X, RingOffset.Y, PhotoHeight + HeightOffset);
}

void AMemoryRoomManager::RebuildGalleryPathOffsets()
{
	const double StartSeconds = FPlatformTime::Seconds();
	const int32 Count = GalleryPhotoFiles.Num();
	GalleryPathOffsets.SetNumUninitialized(Count);
	if (Count <= 0)
	{
		return;
	}

	const float EdgeGap = FMath::Max(GalleryPhotoEdgeGap, 0.0f);
	const int32 Lookback = FMath::Clamp(GalleryOverlapLookback, 0, 24);

	// Convert a world-space centre-to-centre distance into path units at this point on the
	// spiral. Arc length per unit is roughly constant here (rise + circumference), so one
	// division is enough - no iterative solve.
	auto PathUnitsForDistance = [this](float PathOffset, float WorldDistance) -> float
	{
		const float Epsilon = 0.25f;
		const float ArcPerUnit = FMath::Max(
			FVector::Distance(EvaluateGallerySpiralPosition(PathOffset), EvaluateGallerySpiralPosition(PathOffset + Epsilon)) / Epsilon,
			1.0f);
		return WorldDistance / ArcPerUnit;
	};

	GalleryPathOffsets[0] = 0.0f;
	float MaxNudge = 0.0f;
	for (int32 Index = 1; Index < Count; ++Index)
	{
		const float PrevOffset = GalleryPathOffsets[Index - 1];
		const float RequiredCentreDistance = (GetGalleryPhotoWidth(Index - 1) + GetGalleryPhotoWidth(Index)) * 0.5f + EdgeGap;
		float Offset = PrevOffset + FMath::Max(PathUnitsForDistance(PrevOffset, RequiredCentreDistance), 0.05f);

		// Nudge forward if the new photo's centre lands too close to any recent one - this
		// is what stops a wide photo clipping into one a turn below it.
		const int32 First = FMath::Max(0, Index - Lookback);
		for (int32 Pass = 0; Pass < 4; ++Pass)
		{
			const FVector Candidate = EvaluateGallerySpiralPosition(Offset);
			float DeepestOverlap = 0.0f;
			for (int32 Prev = First; Prev < Index; ++Prev)
			{
				const float MinCentre = (GetGalleryPhotoWidth(Prev) + GetGalleryPhotoWidth(Index)) * 0.5f * 0.9f;
				const float Actual = FVector::Distance(Candidate, EvaluateGallerySpiralPosition(GalleryPathOffsets[Prev]));
				DeepestOverlap = FMath::Max(DeepestOverlap, MinCentre - Actual);
			}
			if (DeepestOverlap <= 1.0f)
			{
				break;
			}
			Offset += PathUnitsForDistance(Offset, DeepestOverlap);
			MaxNudge = FMath::Max(MaxNudge, DeepestOverlap);
		}

		GalleryPathOffsets[Index] = Offset;
	}

	UE_LOG(LogMemoryRoom, Log, TEXT("%s built gallery path offsets: %d photo(s), span %.1f units, max nudge %.0f cm, in %.1f ms."),
		*GetName(), Count, GalleryPathOffsets.Last(), MaxNudge, (FPlatformTime::Seconds() - StartSeconds) * 1000.0);
}

void AMemoryRoomManager::EnsureGalleryPool()
{
	if (!PhotoClass)
	{
		return;
	}
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	int32 Target = FMath::Clamp(GalleryPoolSize, 8, 256);
#if PLATFORM_ANDROID
	// Mobile memory is tighter; the Android pool size wins if it is smaller.
	Target = FMath::Min(Target, FMath::Max(AndroidPhotoActorPoolSize, 8));
#endif
	const int32 Want = FMath::Min(Target, FMath::Max(NumberOfPhotos, 1));
	for (int32 Index = SpawnedPhotos.Num(); Index < Want; ++Index)
	{
		if (AMemoryPhotoActor* SpawnedPhoto = World->SpawnActor<AMemoryPhotoActor>(PhotoClass, GetActorTransform()))
		{
			SpawnedPhoto->SetActorHiddenInGame(true);
			SpawnedPhoto->ApplyViewingWindowState(0.0f, 1.0f, false);
			SpawnedPhoto->AssignPhotoIndex(INDEX_NONE);
			ApplyThemeToPhoto(SpawnedPhoto);
			SpawnedPhotos.Add(SpawnedPhoto);
		}
	}
}

AMemoryRoomManager::FPhotoViewingState AMemoryRoomManager::BuildVirtualizedViewingState(float PathIndex) const
{
	const float BottomEdge = -FMath::Max(static_cast<float>(GalleryBottomMargin), 0.0f);
	const float TopEdge = FMath::Max(static_cast<float>(GalleryWindowCount), 1.0f);

	FPhotoViewingState State;
	if (PathIndex < BottomEdge - 1.5f || PathIndex > TopEdge + 0.5f)
	{
		return State;
	}

	float Opacity = 1.0f;
	float ScaleMultiplier = 1.0f;
	float BlurAmount = 0.0f;

	// Ease in over the bottom couple of slots so photos do not pop at the lower edge.
	if (PathIndex < BottomEdge + 2.0f)
	{
		const float Alpha = FMath::Clamp((PathIndex - BottomEdge) / 2.0f, 0.0f, 1.0f);
		Opacity *= Alpha;
		ScaleMultiplier = FMath::Lerp(0.85f, 1.0f, Alpha);
	}

	// The last GalleryFadeTopCount slots taper and blur into the thread.
	const float FadeStart = TopEdge - FMath::Max(static_cast<float>(GalleryFadeTopCount), 1.0f);
	if (PathIndex > FadeStart)
	{
		const float Alpha = FMath::Clamp((PathIndex - FadeStart) / FMath::Max(TopEdge - FadeStart, 1.0f), 0.0f, 1.0f);
		Opacity *= FMath::Lerp(1.0f, 0.12f, Alpha);
		ScaleMultiplier = FMath::Min(ScaleMultiplier, FMath::Lerp(1.0f, 0.68f, Alpha));
		BlurAmount = FMath::Lerp(0.0f, FMath::Max(TopMaxBlurAmount, 1.0f), Alpha);
	}

	State.Opacity = FMath::Clamp(Opacity, 0.0f, 1.0f);
	State.ScaleMultiplier = ScaleMultiplier;
	State.BlurAmount = FMath::Max(BlurAmount, 0.0f);
	State.bVisible = State.Opacity > KINDA_SMALL_NUMBER;
	State.bInteractive = State.bVisible && State.Opacity > 0.2f && PathIndex < FadeStart;
	return State;
}

FTransform AMemoryRoomManager::BuildVirtualizedPhotoTransform(int32 LogicalPhotoIndex, float PathOffset) const
{
	const FVector CenterLocation = GetHelixCenterLocation();
	const FVector LookAtLocation = CenterLocation + FVector(0.0f, 0.0f, PhotoHeight);
	const float PhotoScale = GetDeterministicPhotoScale(LogicalPhotoIndex);
	const FVector SpawnLocation = EvaluateGallerySpiralPosition(PathOffset);
	const FRotator SpawnRotation = MakeUprightLookAtRotation(SpawnLocation, LookAtLocation, GetActorRotation(), PhotoRotationOffset);
	return FTransform(SpawnRotation, SpawnLocation, FVector(PhotoScale));
}

void AMemoryRoomManager::CacheGalleryTexture(const FString& FilePath, UTexture2D* Texture, int32 Width, int32 Height)
{
	if (FilePath.IsEmpty() || !Texture)
	{
		return;
	}

	FGalleryTextureEntry Entry;
	Entry.Texture = TStrongObjectPtr<UTexture2D>(Texture);
	Entry.Width = Width;
	Entry.Height = Height;
	GalleryThumbnailCache.Add(FilePath, MoveTemp(Entry));
	GalleryThumbnailCacheOrder.Remove(FilePath);
	GalleryThumbnailCacheOrder.Add(FilePath);

	const int32 Cap = FMath::Clamp(GalleryThumbnailCacheEntries, 8, 512);
	while (GalleryThumbnailCacheOrder.Num() > Cap)
	{
		const FString Evicted = GalleryThumbnailCacheOrder[0];
		GalleryThumbnailCacheOrder.RemoveAt(0);
		GalleryThumbnailCache.Remove(Evicted);
	}
}

void AMemoryRoomManager::ClearGalleryTextureCaches()
{
	GalleryThumbnailCache.Reset();
	GalleryThumbnailCacheOrder.Reset();
	GalleryExpandedCache.Reset();
	GalleryExpandedCacheOrder.Reset();
	ActiveGalleryExpandedPath.Reset();
	ActiveGalleryExpandedActor.Reset();
}

void AMemoryRoomManager::HarvestGalleryTextures()
{
#if PLATFORM_ANDROID
	// The Android thumbnail/expanded caches already own their textures; nothing to harvest.
	return;
#else
	for (const TObjectPtr<AMemoryPhotoActor>& PhotoPtr : SpawnedPhotos)
	{
		AMemoryPhotoActor* Photo = PhotoPtr.Get();
		if (!Photo || !Photo->HasLoadedTexture())
		{
			continue;
		}
		const FString Path = Photo->GetLoadedFilePath();
		if (Path.IsEmpty())
		{
			continue;
		}
		UTexture2D* Texture = Photo->GetRuntimeTexture();
		if (!Texture)
		{
			continue;
		}

		const int32 LoadedLongEdge = FMath::Max(Photo->GetLoadedImageWidth(), Photo->GetLoadedImageHeight());
		const int32 ThumbnailLongEdge = FMath::Max(GalleryThumbnailMaxDimension, 64);
		// Only treat it as the full-res version once its long edge actually exceeds the
		// thumbnail cap. Right after Expand the actor is still showing the thumbnail, and
		// caching that as "expanded" would stop the real decode from ever running.
		const bool bIsExpandedFullRes = Photo->IsExpanded()
			&& Path == ActiveGalleryExpandedPath
			&& LoadedLongEdge > ThumbnailLongEdge;
		if (bIsExpandedFullRes)
		{
			if (!GalleryExpandedCache.Contains(Path))
			{
				FGalleryTextureEntry Entry;
				Entry.Texture = TStrongObjectPtr<UTexture2D>(Texture);
				Entry.Width = Photo->GetLoadedImageWidth();
				Entry.Height = Photo->GetLoadedImageHeight();
				GalleryExpandedCache.Add(Path, MoveTemp(Entry));
				GalleryExpandedCacheOrder.Remove(Path);
				GalleryExpandedCacheOrder.Add(Path);
				while (GalleryExpandedCacheOrder.Num() > 2)
				{
					const FString Evicted = GalleryExpandedCacheOrder[0];
					GalleryExpandedCacheOrder.RemoveAt(0);
					GalleryExpandedCache.Remove(Evicted);
				}
			}
			continue;
		}

		if (!Photo->IsExpanded() && !GalleryThumbnailCache.Contains(Path))
		{
			CacheGalleryTexture(Path, Texture, Photo->GetLoadedImageWidth(), Photo->GetLoadedImageHeight());
		}
	}
#endif
}

void AMemoryRoomManager::ApplyOrQueueGalleryThumbnail(AMemoryPhotoActor* PhotoActor, int32 LogicalPhotoIndex, int32& InOutActiveDecodes)
{
	if (!PhotoActor || !GalleryPhotoFiles.IsValidIndex(LogicalPhotoIndex))
	{
		return;
	}
	if (PhotoActor->IsExpanded())
	{
		return;
	}

	const FString& Path = GalleryPhotoFiles[LogicalPhotoIndex];

	if (PhotoActor->HasLoadedTexture() && PhotoActor->GetLoadedFilePath() == Path)
	{
		return;
	}

#if PLATFORM_ANDROID
	// On Android the pixels come from the MediaStore thumbnail pipeline, which owns its own
	// decode queue, concurrency cap and LRU cache.
	if (MediaStorePhotos.IsValidIndex(LogicalPhotoIndex))
	{
		AssignAndroidPhotoToActor(LogicalPhotoIndex, PhotoActor);
		QueueAndroidThumbnailRequest(LogicalPhotoIndex, PhotoActor);
		if (!PhotoActor->HasLoadedTexture())
		{
			++InOutActiveDecodes;
		}
	}
#else
	if (FGalleryTextureEntry* Cached = GalleryThumbnailCache.Find(Path))
	{
		if (Cached->Texture.IsValid())
		{
			PhotoActor->ApplyRuntimeTexture(Path, Cached->Texture.Get(), Cached->Width, Cached->Height);
			GalleryThumbnailCacheOrder.Remove(Path);
			GalleryThumbnailCacheOrder.Add(Path);
			return;
		}
		GalleryThumbnailCache.Remove(Path);
		GalleryThumbnailCacheOrder.Remove(Path);
	}

	if (PhotoActor->IsTextureLoadInFlight())
	{
		++InOutActiveDecodes;
		return;
	}

	const int32 MaxConcurrent = FMath::Clamp(MaxSimultaneousPhotoDecodes, 1, 16);
	if (InOutActiveDecodes >= MaxConcurrent)
	{
		return;
	}

	PhotoActor->LoadImageFromFileAsync(Path, FMath::Max(GalleryThumbnailMaxDimension, 64));
	++InOutActiveDecodes;
#endif
}

void AMemoryRoomManager::ProcessVirtualizedExpandedSelection()
{
	AMemoryPhotoActor* ExpandedActor = nullptr;
	for (const TObjectPtr<AMemoryPhotoActor>& PhotoPtr : SpawnedPhotos)
	{
		AMemoryPhotoActor* Photo = PhotoPtr.Get();
		if (Photo && Photo->IsExpanded())
		{
			ExpandedActor = Photo;
			break;
		}
	}

	if (!ExpandedActor)
	{
		if (AMemoryPhotoActor* Previous = ActiveGalleryExpandedActor.Get())
		{
			// Drop the (large) full-res texture and let the next layout put a thumbnail back.
			const int32 PreviousIndex = Previous->GetAssignedPhotoIndex();
			if (GalleryPhotoFiles.IsValidIndex(PreviousIndex))
			{
				Previous->UnloadRuntimeTexture();
				int32 Unused = 0;
				ApplyOrQueueGalleryThumbnail(Previous, PreviousIndex, Unused);
			}
		}
		ActiveGalleryExpandedActor.Reset();
		ActiveGalleryExpandedPath.Reset();
		return;
	}

	const int32 Index = ExpandedActor->GetAssignedPhotoIndex();
	if (!GalleryPhotoFiles.IsValidIndex(Index))
	{
		return;
	}
	const FString& Path = GalleryPhotoFiles[Index];
	ActiveGalleryExpandedActor = ExpandedActor;
	ActiveGalleryExpandedPath = Path;

#if !PLATFORM_ANDROID
	if (FGalleryTextureEntry* Cached = GalleryExpandedCache.Find(Path))
	{
		if (Cached->Texture.IsValid())
		{
			if (ExpandedActor->GetRuntimeTexture() != Cached->Texture.Get())
			{
				ExpandedActor->ApplyRuntimeTexture(Path, Cached->Texture.Get(), Cached->Width, Cached->Height);
			}
			GalleryExpandedCacheOrder.Remove(Path);
			GalleryExpandedCacheOrder.Add(Path);
			return;
		}
	}

	// One full-resolution decode for the open photo. The thumbnail keeps showing until it
	// arrives, then HarvestGalleryTextures moves it into the expanded cache.
	const int32 LoadedLongEdge = FMath::Max(ExpandedActor->GetLoadedImageWidth(), ExpandedActor->GetLoadedImageHeight());
	const bool bAlreadyFullRes = ExpandedActor->GetLoadedFilePath() == Path
		&& LoadedLongEdge > FMath::Max(GalleryThumbnailMaxDimension, 64);
	if (!bAlreadyFullRes && !ExpandedActor->IsTextureLoadInFlight())
	{
		const int32 FullResDimension = ExpandedActor->IsPanoramaCandidate() ? 4096 : FMath::Max(MaximumRuntimeTextureDimension, 1);
		UE_LOG(LogMemoryRoom, Log, TEXT("%s requesting full-res (%d px) for expanded photo %d: %s"),
			*GetName(), FullResDimension, Index, *FPaths::GetCleanFilename(Path));
		ExpandedActor->LoadImageFromFileAsync(Path, FullResDimension);
	}
	// On Android the higher-res decode is driven from Tick by ProcessAndroidExpandedSelection.
#endif
}

void AMemoryRoomManager::LayoutVirtualizedGallery()
{
	EnsureGalleryPool();

	const int32 TotalPhotos = GalleryPhotoFiles.Num();
	if (TotalPhotos <= 0 || SpawnedPhotos.Num() <= 0)
	{
		return;
	}

	if (GalleryPathOffsets.Num() != TotalPhotos)
	{
		RebuildGalleryPathOffsets();
	}

	const float BottomEdge = -FMath::Max(static_cast<float>(GalleryBottomMargin), 0.0f);
	const float TopEdge = FMath::Max(static_cast<float>(GalleryWindowCount), 4.0f);

	// Photo i sits at path offset (GalleryPathOffsets[i] + ScrollOffset). The offsets are
	// monotonically increasing, so binary-search the two ends of the live band.
	auto FirstOffsetAtLeast = [this](float Target) -> int32
	{
		int32 Low = 0;
		int32 High = GalleryPathOffsets.Num();
		while (Low < High)
		{
			const int32 Mid = (Low + High) / 2;
			if (GalleryPathOffsets[Mid] < Target) { Low = Mid + 1; } else { High = Mid; }
		}
		return Low;
	};

	int32 FirstVisible = FirstOffsetAtLeast(BottomEdge - ScrollOffset);
	int32 LastVisible = FirstOffsetAtLeast(TopEdge - ScrollOffset) - 1;
	FirstVisible = FMath::Clamp(FirstVisible, 0, TotalPhotos - 1);
	LastVisible = FMath::Clamp(LastVisible, 0, TotalPhotos - 1);

	// Never ask for more distinct photos than the pool can hold.
	if (LastVisible - FirstVisible + 1 > SpawnedPhotos.Num())
	{
		LastVisible = FirstVisible + SpawnedPhotos.Num() - 1;
	}
	const int32 NeededCount = FMath::Max(LastVisible - FirstVisible + 1, 0);

	// Pass 1: which needed indices are already on an actor, and which actors are free.
	TBitArray<> Covered(false, FMath::Max(NeededCount, 1));
	TArray<AMemoryPhotoActor*> FreeActors;
	FreeActors.Reserve(SpawnedPhotos.Num());
	for (const TObjectPtr<AMemoryPhotoActor>& PhotoPtr : SpawnedPhotos)
	{
		AMemoryPhotoActor* Photo = PhotoPtr.Get();
		if (!Photo)
		{
			continue;
		}
		if (Photo->IsExpanded() || Photo->IsPlacedInWorld())
		{
			continue;
		}
		const int32 Index = Photo->GetAssignedPhotoIndex();
		if (Index >= FirstVisible && Index <= LastVisible)
		{
			Covered[Index - FirstVisible] = true;
		}
		else
		{
			FreeActors.Add(Photo);
		}
	}

	// Pass 2: hand free actors to uncovered indices, nearest to the viewer first. An index
	// whose photo is currently placed elsewhere in the room is left uncovered on purpose - see
	// PlacedPhotoActors - rather than backfilled with a different actor showing the same
	// picture a second time.
	int32 FreeCursor = 0;
	for (int32 Offset = 0; Offset < NeededCount && FreeCursor < FreeActors.Num(); ++Offset)
	{
		if (Covered[Offset])
		{
			continue;
		}
		const int32 LogicalIndex = FirstVisible + Offset;
		if (GalleryPhotoFiles.IsValidIndex(LogicalIndex) && PlacedPhotoActors.Contains(GalleryPhotoFiles[LogicalIndex]))
		{
			continue;
		}
		AMemoryPhotoActor* Photo = FreeActors[FreeCursor++];
		Photo->AssignPhotoIndex(LogicalIndex);
		Photo->ApplyAspectRatioHint(PhotoAspectRatios.IsValidIndex(LogicalIndex) ? PhotoAspectRatios[LogicalIndex] : 1.0f);
	}

	// Pass 3: place and dress every actor; hide the ones with nothing to show.
	int32 ActiveDecodes = 0;
	int32 VisibleCount = 0;
	int32 TexturedCount = 0;
	const float FocusOpacityScale = FMath::Lerp(1.0f, FMath::Clamp(FocusOtherPhotoOpacity, 0.0f, 1.0f), FocusBlend);
	for (const TObjectPtr<AMemoryPhotoActor>& PhotoPtr : SpawnedPhotos)
	{
		AMemoryPhotoActor* Photo = PhotoPtr.Get();
		if (!Photo || Photo->IsExpanded() || Photo->IsPlacedInWorld())
		{
			continue;
		}

		const int32 Index = Photo->GetAssignedPhotoIndex();
		if (Index < FirstVisible || Index > LastVisible || !GalleryPhotoFiles.IsValidIndex(Index))
		{
			if (!Photo->IsHidden())
			{
				Photo->ApplyViewingWindowState(0.0f, 1.0f, false);
			}
			if (Photo->HasLoadedTexture())
			{
				Photo->UnloadRuntimeTexture();
			}
			continue;
		}

		const float PathOffset = GalleryPathOffsets.IsValidIndex(Index) ? GalleryPathOffsets[Index] : static_cast<float>(Index);
		const float PathIndex = PathOffset + ScrollOffset;
		const FPhotoViewingState ViewingState = BuildVirtualizedViewingState(PathIndex);
		const float BaseScale = GetDeterministicPhotoScale(Index);
		FTransform PhotoTransform = BuildVirtualizedPhotoTransform(Index, PathOffset + ScrollOffset);
		PhotoTransform.SetScale3D(FVector(BaseScale * ViewingState.ScaleMultiplier));
		Photo->SetLayoutTransform(PhotoTransform, ScrollOffset);

		ApplyOrQueueGalleryThumbnail(Photo, Index, ActiveDecodes);
		Photo->ApplyViewingWindowState(ViewingState.Opacity * FocusOpacityScale, ViewingState.ScaleMultiplier, ViewingState.bInteractive, ViewingState.BlurAmount);

		if (ViewingState.bVisible)
		{
			++VisibleCount;
		}
		if (Photo->HasLoadedTexture())
		{
			++TexturedCount;
		}
	}

	HarvestGalleryTextures();
	ProcessVirtualizedExpandedSelection();

	bHasPendingPhotoTextures = ActiveDecodes > 0 || TexturedCount < VisibleCount;

	const UWorld* World = GetWorld();
	if (World && World->GetTimeSeconds() - LastLayoutDiagnosticSeconds >= 1.0)
	{
		LastLayoutDiagnosticSeconds = World->GetTimeSeconds();
		UE_LOG(LogMemoryRoom, Log, TEXT("%s virtual gallery: %d photos, window [%d..%d], %d visible, %d textured, %d decoding, scroll %.1f, cache %d."),
			*GetName(), TotalPhotos, FirstVisible, LastVisible, VisibleCount, TexturedCount, ActiveDecodes, ScrollOffset, GalleryThumbnailCache.Num());
	}
}

FTransform AMemoryRoomManager::BuildPhotoTransform(int32 PhotoIndex) const
{
	const FVector CenterLocation = GetHelixCenterLocation();
	const FVector LookAtLocation = CenterLocation + FVector(0.0f, 0.0f, PhotoHeight);
	const float BasePathParameter = CachedPhotoPathParameters.IsValidIndex(PhotoIndex) ? CachedPhotoPathParameters[PhotoIndex] : static_cast<float>(PhotoIndex);
	const float PathIndex = BasePathParameter + ScrollOffset;

	if (bEnableHelixLayout)
	{
		const float PhotoScale = GetDeterministicPhotoScale(PhotoIndex);
		const FVector SpawnLocation = EvaluateHelixPosition(PathIndex);
		const FRotator SpawnRotation = MakeUprightLookAtRotation(SpawnLocation, LookAtLocation, GetActorRotation(), PhotoRotationOffset);
		return FTransform(SpawnRotation, SpawnLocation, FVector(PhotoScale));
	}

	const bool bUseDomeLayout = NumberOfPhotos >= DomeLayoutPhotoThreshold && (bEnableDomeLayout || NumberOfPhotos >= 50);
	if (bUseDomeLayout)
	{
		const float GoldenAngleDegrees = 137.507764f;
		const float Denominator = FMath::Max(static_cast<float>(NumberOfPhotos - 1), 1.0f);
		const float Alpha = FMath::Clamp(PathIndex / Denominator, 0.0f, 1.0f);
		const float MinSinElevation = FMath::Sin(FMath::DegreesToRadians(DomeMinElevationDegrees));
		const float MaxSinElevation = FMath::Sin(FMath::DegreesToRadians(FMath::Max(DomeMaxElevationDegrees, DomeMinElevationDegrees)));
		const float SinElevation = FMath::Lerp(MinSinElevation, MaxSinElevation, Alpha);
		const float ElevationRadians = FMath::Asin(FMath::Clamp(SinElevation, -1.0f, 1.0f));
		const float HorizontalRadius = Radius * FMath::Cos(ElevationRadians);
		const float HeightOffset = Radius * FMath::Sin(ElevationRadians);
		const float AngleDegrees = GoldenAngleDegrees * PathIndex;
		const FVector HorizontalOffset = FVector(HorizontalRadius, 0.0f, 0.0f).RotateAngleAxis(AngleDegrees, FVector::UpVector);
		const FVector SpawnLocation = CenterLocation + FVector(HorizontalOffset.X, HorizontalOffset.Y, PhotoHeight + HeightOffset);
		const FRotator SpawnRotation = MakeUprightLookAtRotation(SpawnLocation, LookAtLocation, GetActorRotation(), PhotoRotationOffset);
		return FTransform(SpawnRotation, SpawnLocation, FVector(GetDeterministicPhotoScale(PhotoIndex)));
	}

	const float PhotoScale = GetDeterministicPhotoScale(PhotoIndex);
	const float BaseAngleStepDegrees = 360.0f / static_cast<float>(FMath::Max(NumberOfPhotos, 1));
	const float LayoutAngleDegrees = GetPhotoAngularPositionDegrees(PhotoIndex, PathIndex, Radius);
	const float EvenAngleDegrees = PathIndex * BaseAngleStepDegrees;
	const float AngleDegrees = FMath::Abs(LayoutAngleDegrees) > FMath::Abs(EvenAngleDegrees) ? LayoutAngleDegrees : EvenAngleDegrees;
	const FVector CircleOffset = FVector(Radius, 0.0f, 0.0f).RotateAngleAxis(AngleDegrees, FVector::UpVector);
	const FVector SpawnLocation = CenterLocation + FVector(CircleOffset.X, CircleOffset.Y, PhotoHeight);
	const FRotator SpawnRotation = MakeUprightLookAtRotation(SpawnLocation, CenterLocation, GetActorRotation(), PhotoRotationOffset);
	return FTransform(SpawnRotation, SpawnLocation, FVector(PhotoScale));
}

