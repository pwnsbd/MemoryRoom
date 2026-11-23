// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MemoryPhotoActor.h"
#include "MemoryPhotoSource.h"
#include "MemoryRoomThemeAsset.h"
#include "TimerManager.h"
#include "UObject/ObjectPtr.h"
#include "UObject/StrongObjectPtr.h"
#include "AMemoryRoomManager.generated.h"

class UMaterialInstanceDynamic;

UENUM(BlueprintType)
enum class EMemoryRoomThumbnailQuality : uint8
{
	Low,
	Medium,
	High
};

UENUM(BlueprintType)
enum class EMemoryRoomExpandedPhotoQuality : uint8
{
	Standard,
	High,
	Ultra
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FGalleryLoadProgressSignature, int32, Loaded, int32, Failed, int32, Total);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FGalleryLoadCompletedSignature, int32, Loaded, int32, Failed);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FMemoryRoomSimpleEventSignature);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FMemoryRoomPhotoCountChangedSignature, int32, PhotoCount);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FMemoryRoomPhotoLibraryReloadCompletedSignature, int32, PhotoCount, bool, bSuccess);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FMemoryRoomThumbnailQualityChangedSignature, EMemoryRoomThumbnailQuality, Quality);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FMemoryRoomExpandedQualityChangedSignature, EMemoryRoomExpandedPhotoQuality, Quality);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FMemoryRoomUserSettingsChangedSignature);
// Fired whenever ReturnPlacedPhotoToGallery/ResetAllPlacedPhotos clears an actor's placed
// state - including while it happens to be re-grabbed at that moment. UMemoryRoomSelectionComponent
// listens so it can drop its own GrabbedPhoto/ExpandedPhoto reference to that actor too; without
// this, simply releasing an object that reset just un-placed would silently re-place it right
// back (PlacePhotoInWorld sees IsPlacedInWorld() false and treats the release as a first-time
// placement again), which is what "reset does not reset the regrabbed object" turned out to be.
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FMemoryRoomPlacedPhotoUnplacedSignature, AMemoryPhotoActor*, Actor);

// Everything the in-headset settings panel can adjust. Order here is the order shown.
UENUM(BlueprintType)
enum class EMemoryRoomUserSetting : uint8
{
	TimeOfDay,
	SpiralTwist,
	PhotoSpacing,
	PhotosInView,
	ScrollSpeed,
	NightPhotoGlow,
	DustAmount,
	ThumbnailQuality,
	Theme,
	Float,
	PhotoAlbum,
	Count UMETA(Hidden)
};

// One entry in the Photos row's album list. "@all" identifies All photos.
struct FMemoryRoomAlbum
{
	FString Id;
	FString Name;
	FString ParentId;
	int32 Count = 0;
};

class AMemoryRoomMenuActor;
class AMemoryRoomDustActor;
class AMemoryRoomGroundActor;
class AMemoryRoomSkyButtonActor;
class AMemoryRoomSkyDomeActor;
class UAudioComponent;
class USoundBase;

UCLASS()
class MEMORYROOM_API AMemoryRoomManager : public AActor
{
	GENERATED_BODY()

public:
	// Sets default values for this actor's properties
	AMemoryRoomManager();

protected:
	// Called when the game starts or when spawned
	virtual void BeginPlay() override;

public:
	virtual void Tick(float DeltaTime) override;

	UFUNCTION(BlueprintCallable, Category = "Memory Room|Scrolling")
	void AddScrollInput(float Amount);

	UFUNCTION(BlueprintCallable, Category = "Memory Room|Scrolling")
	void AddForwardScrollInput(float Amount = 1.0f);

	UFUNCTION(BlueprintCallable, Category = "Memory Room|Scrolling")
	void AddBackwardScrollInput(float Amount = 1.0f);

	UFUNCTION(BlueprintCallable, Category = "Memory Room|Scrolling")
	void StopScrollInput();

	// Left-stick locomotion, driven from BP_VRPawn's Enhanced Input graph (same path as
	// scroll - legacy axis mappings never reach OpenXR here). Feed the Axis2D action value
	// on Triggered, and call the Stop variant on Completed/Canceled.
	UFUNCTION(BlueprintCallable, Category = "Memory Room|Locomotion")
	void SetLocomotionMoveInput(FVector2D Value);

	UFUNCTION(BlueprintCallable, Category = "Memory Room|Locomotion")
	void StopLocomotionMoveInput();

	// Right-stick smooth turn (X axis only - Y is still gallery scroll). Rotates the pawn
	// about the player's actual head position each tick, not the pawn's own origin, so the
	// room spins around you instead of swinging you around the capsule. Feed the Axis1D
	// action value on Triggered, and call the Stop variant on Completed/Canceled.
	UFUNCTION(BlueprintCallable, Category = "Memory Room|Locomotion")
	void SetTurnInput(float Value);

	UFUNCTION(BlueprintCallable, Category = "Memory Room|Locomotion")
	void StopTurnInput();

	// Flip walk <-> float. Bind to a digital action (left X click) on Triggered.
	UFUNCTION(BlueprintCallable, Category = "Memory Room|Locomotion")
	void ToggleFloat();

	UFUNCTION(BlueprintCallable, Category = "Memory Room|Navigation")
	void ResetGalleryScroll();

	UFUNCTION(BlueprintCallable, Category = "Memory Room|Navigation")
	void JumpToNewestPhoto();

	// Traces straight down from the player and moves their pawn's root onto whatever it hits.
	// Called once from BeginPlay - the level's pawn was found to spawn well above the actual
	// floor with nothing correcting it (that is what "the player is floating" turned out to be),
	// and floor-level VR tracking already puts the headset at the real height above wherever the
	// pawn's root ends up, so this alone is enough to fix it.
	UFUNCTION(BlueprintCallable, Category = "Memory Room|Player")
	void SnapPlayerPawnToFloor();

	UFUNCTION(BlueprintCallable, Category = "Memory Room|Scrolling")
	void SetGalleryScrollSuppressed(bool bSuppressed);

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Memory Room|Scrolling")
	bool IsGalleryScrollSuppressed() const { return bGalleryScrollSuppressed; }

	UFUNCTION(BlueprintCallable, Category = "Memory Room|Photo Library")
	void ReloadPhotoLibrary();

	UFUNCTION(BlueprintCallable, Category = "Memory Room|Quality")
	void CycleThumbnailQuality();

	UFUNCTION(BlueprintCallable, Category = "Memory Room|Quality")
	void CycleExpandedPhotoQuality();

	UFUNCTION(BlueprintCallable, Category = "Memory Room|Quality")
	void SetThumbnailQuality(EMemoryRoomThumbnailQuality NewQuality);

	UFUNCTION(BlueprintCallable, Category = "Memory Room|Quality")
	void SetExpandedPhotoQuality(EMemoryRoomExpandedPhotoQuality NewQuality);

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Memory Room|Quality")
	EMemoryRoomThumbnailQuality GetThumbnailQuality() const { return ThumbnailQuality; }

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Memory Room|Quality")
	EMemoryRoomExpandedPhotoQuality GetExpandedPhotoQuality() const { return ExpandedPhotoQuality; }

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Memory Room|Photo Library")
	int32 GetPhotoCount() const { return NumberOfPhotos; }

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Memory Room|Photo Library")
	int32 GetThumbnailCacheCount() const { return AndroidThumbnailCache.Num(); }

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Memory Room|Photo Library")
	int32 GetThumbnailCacheCapacity() const { return AndroidMaxThumbnailCacheEntries; }

	UFUNCTION(BlueprintCallable, Category = "Memory Room|Photo Folder")
	void ScanPhotoFolder();

	UFUNCTION(BlueprintCallable, Category = "Memory Room|Photo Folder")
	bool BuildGalleryFromDiscoveredPhotos();

	UFUNCTION(BlueprintCallable, Category = "Memory Room|Photo Folder")
	bool StartIncrementalGalleryLoad();

	UFUNCTION(BlueprintCallable, Category = "Memory Room|Photo Folder")
	void CancelGalleryLoad();

	UFUNCTION(BlueprintCallable, Category = "Memory Room|Photo Folder")
	void ClearGeneratedGallery();

	UFUNCTION(BlueprintCallable, Category = "Memory Room|Helix Path")
	void RebuildPhotoPathCache();

	UFUNCTION(BlueprintCallable, Category = "Memory Room|Menu")
	void ToggleMemoryRoomMenu();

	// --- Free-placed photos ---
	// Placing a photo does not spawn or duplicate anything: the same AMemoryPhotoActor that was
	// already showing it (grabbed straight off the scrolling wall, or already placed and being
	// moved) just keeps that job permanently, marked via AMemoryPhotoActor::IsPlacedInWorld().
	// LayoutVirtualizedGallery skips any actor with that flag set, the same way it already
	// skips a currently-grabbed one, so the photo's old wall slot sits empty rather than being
	// backfilled with a different actor showing the same picture again - "no duplicate,
	// original slot goes empty" rather than "a separate standalone copy alongside the gallery
	// original" (an earlier version of this API worked the other way; PlacePhotoInWorld's own
	// scale handling changed with it - see UMemoryRoomSelectionComponent::GrabHoveredPhoto for
	// where a first-time grab vs. a re-grab-of-a-placed-photo now diverge on GrabScaleMultiplier
	// specifically to avoid the repeated-shrink bug that caused). Each placement still means one
	// less actor available to the scrolling pool for as long as it stays placed - with no cap on
	// placements, that pool can run low if a great many photos are placed at once.
	// Persisted to disk keyed by the photo's own absolute file path (PlacedPhotosSaveSlotName),
	// so an arrangement survives a restart - restoring a placement on load is the one remaining
	// case that spawns an actor, since there is nothing already showing it yet to reuse.

	// Places (or moves, if this photo is already placed) Actor - whichever actor is already
	// showing it - to WorldTransform, and saves. Called by UMemoryRoomSelectionComponent on a
	// normal grip release.
	UFUNCTION(BlueprintCallable, Category = "Memory Room|Placement")
	void PlacePhotoInWorld(AMemoryPhotoActor* Actor, const FTransform& WorldTransform);

	// Un-places FilePath if it was placed (clears IsPlacedInWorld() on its actor and removes the
	// saved entry; the very next gallery layout pass then hands that actor a normal wall slot
	// again like any other pooled actor). No-op if it was never placed.
	UFUNCTION(BlueprintCallable, Category = "Memory Room|Placement")
	void ReturnPlacedPhotoToGallery(const FString& FilePath);

	// Un-places every currently-placed photo. Bound to the wrist reset puck.
	UFUNCTION(BlueprintCallable, Category = "Memory Room|Placement")
	void ResetAllPlacedPhotos();

	// Equivalent to Actor->IsPlacedInWorld() - kept as a manager-level call so callers that
	// only otherwise need the manager do not also need to include MemoryPhotoActor.h.
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Memory Room|Placement")
	bool IsPlacedPhotoActor(const AMemoryPhotoActor* Actor) const;

	UPROPERTY(BlueprintAssignable, Category = "Memory Room|Placement")
	FMemoryRoomPlacedPhotoUnplacedSignature OnPlacedPhotoUnplaced;

	// --- In-headset user settings (driven by the settings panel) ---

	// "Label   value" for one setting row.
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Memory Room|Settings")
	FText GetUserSettingText(EMemoryRoomUserSetting Setting) const;

	// The same row split in two, for panels that set the name and the value separately.
	UFUNCTION(BlueprintCallable, Category = "Memory Room|Settings")
	void GetUserSettingParts(EMemoryRoomUserSetting Setting, FText& OutName, FText& OutValue) const;

	// Steps a setting up (+1) or down (-1); clamps, applies live, and saves.
	UFUNCTION(BlueprintCallable, Category = "Memory Room|Settings")
	void AdjustUserSetting(EMemoryRoomUserSetting Setting, int32 Direction);

	// Source selection state for the in-headset onboarding menu.
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Memory Room|Settings")
	bool HasPhotoSourceChoice() const { return bHasPhotoSourceChoice; }

	// Commits the album currently previewed by the Photos row. Browsing never loads or saves.
	UFUNCTION(BlueprintCallable, Category = "Memory Room|Settings")
	bool CommitPhotoAlbumPreview();
	const TArray<FMemoryRoomAlbum>& GetPhotoAlbums() const { return Albums; }
	const FString& GetSelectedPhotoAlbumId() const { return SelectedAlbumId; }
	bool SelectPhotoAlbumById(const FString& Id);
	void RefreshPhotoFolders() { RefreshAlbumList(); }

	// Loads saved settings and pushes them into the world. Called at BeginPlay.
	UFUNCTION(BlueprintCallable, Category = "Memory Room|Settings")
	void ApplyUserSettings();

	UPROPERTY(BlueprintAssignable, Category = "Memory Room|Settings")
	FMemoryRoomUserSettingsChangedSignature OnUserSettingsChanged;

	// --- Theme ---

	// Pushes CurrentTheme (or, if empty, the built-in FallbackThemeId) into the sky dome,
	// the photos, the dust, the wrist UI and the ambient audio. Called once as the room
	// opens, and again after a photo-library reload so fresh photo actors are styled.
	UFUNCTION(BlueprintCallable, Category = "Memory Room|Theme")
	void ApplyTheme();

	// Assign an authored theme asset here, or leave it empty to use FallbackThemeId. Resolved
	// into a live asset by ApplyTheme (never null afterward).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room|Theme")
	TObjectPtr<UMemoryRoomThemeAsset> CurrentTheme;

	// Which built-in theme ApplyTheme synthesises when CurrentTheme is empty.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room|Theme")
	EMemoryRoomThemeId FallbackThemeId = EMemoryRoomThemeId::Hearth;

	// Drop sounds straight onto the manager here - anything set wins over the theme's cue,
	// so you can add audio without authoring a full theme asset. Volumes still come from
	// the theme (built-in Hearth: 1.0 / 0.6 / 0.5 / 0.8).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room|Theme|Audio Override")
	TObjectPtr<USoundBase> AmbientLoopOverride;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room|Theme|Audio Override")
	TObjectPtr<USoundBase> ScrollWhooshOverride;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room|Theme|Audio Override")
	TObjectPtr<USoundBase> HoverChimeOverride;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room|Theme|Audio Override")
	TObjectPtr<USoundBase> GrabToneOverride;

	// Called by the selection component so theme audio can fire on the real interaction
	// events. WorldLocation is where the one-shot is spawned.
	void NotifyPhotoHovered(const FVector& WorldLocation);
	void NotifyPhotoGrabbed(const FVector& WorldLocation);

	// TEST HELPER ONLY: loads the first discovered image into one photo actor for diagnostics.
	// Not used by the full gallery build path.
	UFUNCTION(BlueprintCallable, Category = "Memory Room|Photo Folder|Diagnostics")
	bool LoadFirstDiscoveredPhoto();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room|Photo Folder")
	FString PhotoFolderPath;

	// On Android, load photos from a folder on the headset instead of the system photo
	// library. Leave PhotoFolderPath blank to use the app's own folder (no permission
	// needed): adb push images to
	//   /sdcard/Android/data/<package>/files/UnrealGame/MemoryRoom/MemoryRoom/Photos
	// or set PhotoFolderPath to any absolute path you can read (e.g. /sdcard/Download/Mem).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room|Photo Folder")
	bool bAndroidUsePhotoFolder = true;

	// Point the gallery at a folder and (re)load it now. Works on desktop and Android.
	// Empty string keeps the current PhotoFolderPath / the Android default.
	UFUNCTION(BlueprintCallable, Category = "Memory Room|Photo Folder")
	bool LoadGalleryFromFolder(const FString& InFolderPath);

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Memory Room|Photo Folder")
	TArray<FString> DiscoveredPhotoFiles;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room|Photo Folder", meta = (ClampMin = "0", UIMin = "0"))
	int32 MaxPhotosToLoad;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room|Photo Folder")
	bool bBuildGalleryOnBeginPlay;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room|Photo Folder", meta = (ClampMin = "1", UIMin = "1"))
	int32 PhotosToLoadPerBatch = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room|Photo Folder", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float BatchDelaySeconds = 0.05f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room|Photo Folder", meta = (ClampMin = "1", UIMin = "1"))
	int32 MaximumRuntimeTextureDimension = 2048;

	// Resolution the gallery streams at. The helix shows photos small; decoding them at
	// MaximumRuntimeTextureDimension cost ~8MB and a long game-thread upload each, for
	// detail no one can see from the spiral.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room|Photo Folder", meta = (ClampMin = "64", UIMin = "64"))
	int32 GalleryThumbnailMaxDimension = 512;

	// Decodes finish on the game thread, so letting every visible photo start at once just
	// queues them all behind each other. Android has always throttled; this is the same.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room|Photo Folder", meta = (ClampMin = "1", ClampMax = "16"))
	int32 MaxSimultaneousPhotoDecodes = 3;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Android Photo Library|Debug")
	bool bAndroidSingleImageDiagnostic = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Android Photo Library", meta = (ClampMin = "1", UIMin = "1"))
	int32 AndroidPhotoActorPoolSize = 20;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Android Photo Library", meta = (ClampMin = "1", UIMin = "1"))
	int32 AndroidThumbnailMaxDimension = 384;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Android Photo Library", meta = (ClampMin = "1", UIMin = "1"))
	int32 AndroidMaxSimultaneousThumbnailDecodes = 2;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Android Photo Library", meta = (ClampMin = "1", UIMin = "1"))
	int32 AndroidMaxThumbnailCacheEntries = 40;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Android Photo Library|Quality", meta = (ClampMin = "1", UIMin = "1"))
	int32 AndroidExpandedPhotoMaxDimension = 2048;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Android Photo Library|Quality", meta = (ClampMin = "1", UIMin = "1"))
	int32 AndroidExpandedCacheEntries = 2;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Android Photo Library|Quality", meta = (ClampMin = "1", UIMin = "1"))
	int32 AndroidMaxConcurrentExpandedDecodes = 1;

	UPROPERTY(BlueprintReadOnly, Category = "Memory Room|Photo Folder")
	int32 LoadedPhotoCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Memory Room|Photo Folder")
	int32 FailedPhotoCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Memory Room|Photo Folder")
	int32 TotalPhotosToLoad = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Memory Room|Photo Folder")
	bool bIsLoadingGallery = false;

	UPROPERTY(BlueprintAssignable, Category = "Memory Room|Photo Folder")
	FGalleryLoadProgressSignature OnGalleryLoadProgress;

	UPROPERTY(BlueprintAssignable, Category = "Memory Room|Photo Folder")
	FGalleryLoadCompletedSignature OnGalleryLoadCompleted;

	UPROPERTY(BlueprintAssignable, Category = "Memory Room|Photo Library")
	FMemoryRoomSimpleEventSignature OnPhotoLibraryReloadStarted;

	UPROPERTY(BlueprintAssignable, Category = "Memory Room|Photo Library")
	FMemoryRoomPhotoLibraryReloadCompletedSignature OnPhotoLibraryReloadCompleted;

	UPROPERTY(BlueprintAssignable, Category = "Memory Room|Photo Library")
	FMemoryRoomPhotoCountChangedSignature OnPhotoCountChanged;

	UPROPERTY(BlueprintAssignable, Category = "Memory Room|Quality")
	FMemoryRoomThumbnailQualityChangedSignature OnThumbnailQualityChanged;

	UPROPERTY(BlueprintAssignable, Category = "Memory Room|Quality")
	FMemoryRoomExpandedQualityChangedSignature OnExpandedQualityChanged;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room", meta = (ClampMin = "1", UIMin = "1"))
	int32 NumberOfPhotos;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float Radius;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room")
	float PhotoHeight;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room")
	float ScrollOffset;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room|Scrolling")
	float TargetScrollOffset;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room|Scrolling", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float ScrollSpeed;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room|Scrolling", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float ScrollSmoothing;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room|Scrolling", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float ScrollAcceleration;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room|Scrolling", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float ScrollFriction;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room|Scrolling", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float MaximumScrollSpeed;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room|Scrolling")
	bool bAutoBindThumbstickScrollInput;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room|Scrolling")
	FName ScrollInputAxisName;

	// --- Free locomotion ---
	// Left thumbstick moves the player through the room; the gallery stays put (its centre
	// locks to this manager's location). The Float setting / the left X button switch to
	// fly-where-you-look so you can rise up the spiral to reach photos in the air.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room|Locomotion")
	bool bAllowFreeLocomotion = true;

	// Runtime state, also the "Float" settings row. On = fly (stick follows head look, incl.
	// pitch). Off = walk on the ground.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room|Locomotion")
	bool bFloatEnabled = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room|Locomotion", meta = (ClampMin = "0.0"))
	float WalkSpeedCmPerSec = 170.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room|Locomotion", meta = (ClampMin = "0.0"))
	float FloatSpeedCmPerSec = 260.0f;

	// Do not let the player get closer than this to a photo while walking.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room|Locomotion", meta = (ClampMin = "0.0"))
	float PhotoKeepOutRadiusCm = 130.0f;

	// A smaller keep-out while floating, so you can move in to pick a photo out of the air.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room|Locomotion", meta = (ClampMin = "0.0"))
	float FloatKeepOutRadiusCm = 45.0f;

	// How far up you can float, measured in full turns of the photo spiral (~5.1 m each).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room|Locomotion", meta = (ClampMin = "0.0"))
	float MaxFloatTurns = 3.0f;

	// Horizontal containment: the player can never get farther from the helix centre than the
	// photo ring itself, minus this margin. The ring tapers with height (GallerySpiralBaseRadius
	// -> GallerySpiralTopRadius over GallerySpiralTaperHeight, same curve as
	// EvaluateGallerySpiralPosition), so this boundary tapers with you as you float up.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room|Locomotion", meta = (ClampMin = "0.0"))
	float SpiralBoundaryMarginCm = 60.0f;

	// Smooth-turn speed at full stick deflection.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room|Locomotion", meta = (ClampMin = "0.0"))
	float TurnSpeedDegPerSec = 90.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room", meta = (DisplayName = "Enable Helix Layout"))
	bool bEnableHelixLayout;

	// Keeps the helix centered on the player's current view/user location instead of the manager actor's placed location.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room", meta = (DisplayName = "Center Helix On User"))
	bool bCenterHelixOnUser;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room", meta = (DisplayName = "Helix Radius", ClampMin = "0.0", UIMin = "0.0", AdvancedDisplay))
	float HelixRadius;

	// Height range used to taper the helix radius from BaseRadius toward TopRadius.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room", meta = (DisplayName = "Dome Height", ClampMin = "1.0", UIMin = "1.0"))
	float DomeHeight;

	// Radius used near the viewing area / bottom of the spiral.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room", meta = (DisplayName = "Base Radius", ClampMin = "0.0", UIMin = "0.0"))
	float BaseRadius;

	// Radius used near the top of the spiral.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room", meta = (DisplayName = "Top Radius", ClampMin = "0.0", UIMin = "0.0"))
	float TopRadius;

	// Controls how quickly the helix narrows as it rises. Higher values keep it wider longer.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room", meta = (DisplayName = "Dome Curve Strength", ClampMin = "0.01", UIMin = "0.01"))
	float DomeCurveStrength;

	// Applied after the upright yaw-only look-at rotation to correct the photo plane's authored forward axis.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room", meta = (DisplayName = "Photo Rotation Offset"))
	FRotator PhotoRotationOffset;

	// Deterministic per-photo size range. Keeps variation stable between room starts.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room|Size Variation", meta = (ClampMin = "0.01", UIMin = "0.01"))
	float MinPhotoScale;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room|Size Variation", meta = (ClampMin = "0.01", UIMin = "0.01"))
	float MaxPhotoScale;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room|Size Variation")
	int32 SizeVariationSeed;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room", meta = (DisplayName = "Vertical Spacing"))
	float VerticalSpacing;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room", meta = (DisplayName = "Degrees Per Photo"))
	float DegreesPerPhoto;

	// Extra world-space gap added to each loaded photo width when calculating angular spacing.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room", meta = (DisplayName = "Photo Width Spacing Offset", ClampMin = "0.0", UIMin = "0.0"))
	float PhotoWidthSpacingOffset;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room|Helix Path", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float PhotoPathGap;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room|Helix Path", meta = (ClampMin = "0.001", UIMin = "0.001"))
	float PathPlacementStep;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room|Helix Path", meta = (ClampMin = "0", UIMin = "0"))
	int32 LayoutOverlapLookback;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room|Helix Path", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float PhotoOverlapSafetyGap;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room|Helix Path", meta = (ClampMin = "1", UIMin = "1"))
	int32 MaximumPlacementIterations;

	// Prevents upper/lower turns of the helix from stacking in the same view sector.
	// This keeps the spiral readable without simply pushing every photo farther apart.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room", meta = (DisplayName = "Anti Cluster Angle Degrees", ClampMin = "0.0", UIMin = "0.0"))
	float AntiClusterAngleDegrees;

	// Number of previous photos checked when avoiding repeated angular sectors.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room", meta = (DisplayName = "Anti Cluster Lookback", ClampMin = "0", UIMin = "0"))
	int32 AntiClusterLookback;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room", meta = (DisplayName = "Start Height"))
	float StartHeight;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room|Viewing Window", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float VisibleRangeBelow;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room|Viewing Window", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float VisibleRangeAbove;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room|Viewing Window")
	float TopFadeStart;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room|Viewing Window")
	float TopFadeEnd;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room|Viewing Window", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float TopMaxBlurAmount;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room|Viewing Window", meta = (ClampMin = "0.0", ClampMax = "1.0", UIMin = "0.0", UIMax = "1.0"))
	float TopThreadMinimumOpacity;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room|Viewing Window", meta = (ClampMin = "0.01", UIMin = "0.01"))
	float TopMinimumScale;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room|Viewing Window")
	float BottomFadeStart;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room|Viewing Window")
	float BottomFadeEnd;

	// When many photos are present, spread them across a spherical dome instead of one flat ring.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room", meta = (DisplayName = "Enable Dome Layout"))
	bool bEnableDomeLayout;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room", meta = (DisplayName = "Dome Layout Photo Threshold", ClampMin = "1", UIMin = "1"))
	int32 DomeLayoutPhotoThreshold;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room", meta = (DisplayName = "Dome Min Elevation Degrees", ClampMin = "0.0", ClampMax = "89.0", UIMin = "0.0", UIMax = "89.0"))
	float DomeMinElevationDegrees;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room", meta = (DisplayName = "Dome Max Elevation Degrees", ClampMin = "1.0", ClampMax = "89.0", UIMin = "1.0", UIMax = "89.0"))
	float DomeMaxElevationDegrees;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room")
	TSubclassOf<AMemoryPhotoActor> PhotoClass;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room|Menu")
	bool bAutoSpawnMemoryRoomMenu = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room|Menu")
	TSubclassOf<AMemoryRoomMenuActor> MemoryRoomMenuActorClass;

	// Spawns the surrounding sky dome if the level does not already contain one.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room|Sky")
	bool bAutoSpawnSkyDome = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room|Sky")
	TSubclassOf<AMemoryRoomSkyDomeActor> SkyDomeActorClass;

	// Spawns the wrist-mounted time-of-day button if the level does not already contain one.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room|Sky")
	bool bAutoSpawnSkyButton = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room|Sky")
	TSubclassOf<AMemoryRoomSkyButtonActor> SkyButtonActorClass;

	// Spawns a second wrist puck next to the time-of-day one that opens the settings board.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room|Settings")
	bool bAutoSpawnSettingsButton = true;

	// The settings puck's offset from the time-of-day puck, along the wrist (the controller's
	// own local Y). Each puck's outer rim is 8.2 cm across (CaseDiameter in
	// MemoryRoomSkyButtonActor.cpp), so anything under that overlaps; 11 clears the rims with a
	// gap either way. The sign was only ever chosen for that spacing, never for which visual
	// side it lands on - negated here to put Settings on the requested (left of time-of-day)
	// side, but unverified in headset; flip back to +11 if it lands on the other side instead.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room|Settings")
	FVector SettingsButtonExtraOffset = FVector(0.0f, -11.0f, 0.0f);

	// Spawns a third wrist puck, further along the same direction as the settings puck, that
	// un-places every placed photo at once.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room|Settings")
	bool bAutoSpawnResetPlacementsButton = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room|Settings")
	FVector ResetPlacementsButtonExtraOffset = FVector(0.0f, -22.0f, 0.0f);

	// Slot name for the placed-photos save (see the "Free-placed photos" API above).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room|Placement")
	FString PlacedPhotosSaveSlotName = TEXT("MemoryRoomPlacedPhotos");

	// A placed photo only carries its held/hand-held resolution while it is being grabbed - once
	// let go it is skipped by the normal wall layout pass entirely (it is not on the wall), so
	// nothing else would ever load it back up again. Instead, each placed photo is checked
	// against where the player is currently looking: inside this half-angle cone (in front of
	// the headset) and closer than PlacedPhotoGazeMaxDistance, it gets upgraded to full
	// resolution; outside it, it drops back to a thumbnail to bound memory (there is no cap on
	// how many photos can be placed at once).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room|Placement")
	float PlacedPhotoGazeConeDegrees = 35.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room|Placement")
	float PlacedPhotoGazeMaxDistance = 600.0f;

	// How often the gaze check above re-scans PlacedPhotoActors, in seconds.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room|Placement")
	float PlacedPhotoQualityCheckInterval = 0.25f;

	// Drifting motes that catch the light the photos cast.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room|Dust")
	bool bAutoSpawnDust = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room|Dust")
	TSubclassOf<AMemoryRoomDustActor> DustActorClass;

	// Dresses the level's own existing floor with the installed grass pack (material + scattered
	// clumps) rather than spawning a competing ground mesh - see AMemoryRoomGroundActor.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room|Ground")
	bool bAutoSpawnGround = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room|Ground")
	TSubclassOf<AMemoryRoomGroundActor> GroundActorClass;

	// See SnapPlayerPawnToFloor.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room|Player")
	bool bSnapPlayerToFloorOnBeginPlay = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room|Player", meta = (ClampMin = "100.0"))
	float FloorTraceDistanceCm = 100000.0f;

	// How many photos may cast real light at once. Dynamic lights are the expensive part
	// of the night look on mobile, so this is a hard cap rather than a per-photo choice.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room|Sky", meta = (ClampMin = "0", ClampMax = "16"))
	int32 MaxPhotoGlowLights = 5;

	// Light intensity at PhotoBrightness 1.0; scaled up as the room darkens.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room|Sky", meta = (ClampMin = "0.0"))
	float PhotoGlowLightIntensity = 6.0f;

	// A placed photo is staged deliberately, not just passed on the wall, so it keeps a gentle
	// glow through the day as well as at night - unlike wall photos, which only ever light up
	// once PhotoBrightness says the room has gone dark (see UpdatePhotoGlowLights). It still
	// draws from the same MaxPhotoGlowLights budget, with first claim on it over wall photos.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room|Placement", meta = (ClampMin = "0.0"))
	float PlacedPhotoGlowLightIntensity = 3.0f;

	// How far the rest of the gallery fades while one photo is open. 0.22 leaves the room
	// legible without competing with the photo you opened.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room|Focus", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float FocusOtherPhotoOpacity = 0.22f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room|Focus", meta = (ClampMin = "0.05", UIMax = "3.0"))
	float FocusFadeSeconds = 0.55f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room|Focus")
	bool bEnableFocusMode = true;

	// Virtualized gallery: a small pool of photo actors recycled as the spiral scrolls,
	// like a phone's photo grid, instead of one actor per photo. Off falls back to the
	// old spawn-everything path (fine for a few dozen photos, not hundreds).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room|Gallery")
	bool bUseVirtualizedGallery = true;

	// Photos in the readable band above the viewer. The spiral holds far more than this;
	// scrolling flows the rest through.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room|Gallery", meta = (ClampMin = "4", ClampMax = "200"))
	int32 GalleryWindowCount = 44;

	// Photos kept live just below the viewer, so looking down does not hit a hard edge.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room|Gallery", meta = (ClampMin = "0", ClampMax = "20"))
	int32 GalleryBottomMargin = 3;

	// The last photos before the top of the band fade and shrink into the "thread".
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room|Gallery", meta = (ClampMin = "0", ClampMax = "60"))
	int32 GalleryFadeTopCount = 12;

	// Actor pool size. Must exceed GalleryWindowCount + GalleryBottomMargin with headroom
	// for the ones mid-recycle.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room|Gallery", meta = (ClampMin = "8", ClampMax = "256"))
	int32 GalleryPoolSize = 56;

	// Decoded thumbnails kept in memory, keyed by file path. Recycling an actor back onto
	// a photo still in this cache is instant.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room|Gallery", meta = (ClampMin = "8", ClampMax = "512"))
	int32 GalleryThumbnailCacheEntries = 96;

	// --- Virtualized spiral shape. Independent of the old helix params so a Blueprint that
	// overrode DegreesPerPhoto / VerticalSpacing / radii does not fight these. ---

	// Degrees the spiral turns per unit of path. Photos are spaced ~1 path unit apart on
	// average, so this is roughly the turn between neighbours. Lower = gentler spiral.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room|Gallery|Shape", meta = (ClampMin = "1.0", ClampMax = "90.0"))
	float GallerySpiralDegreesPerUnit = 24.0f;

	// Centimetres the spiral rises per unit of path.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room|Gallery|Shape", meta = (ClampMin = "0.0"))
	float GallerySpiralRisePerUnit = 34.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room|Gallery|Shape", meta = (ClampMin = "50.0"))
	float GallerySpiralBaseRadius = 620.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room|Gallery|Shape", meta = (ClampMin = "50.0"))
	float GallerySpiralTopRadius = 380.0f;

	// Height over which the radius tapers from base to top.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room|Gallery|Shape", meta = (ClampMin = "1.0"))
	float GallerySpiralTaperHeight = 1400.0f;

	// Extra world-space gap between the edges of neighbouring photos.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room|Gallery|Shape", meta = (ClampMin = "0.0"))
	float GalleryPhotoEdgeGap = 40.0f;

	// How many earlier photos each new one is nudged clear of, so a wide photo does not
	// clip into one placed a turn below it.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Room|Gallery|Shape", meta = (ClampMin = "0", ClampMax = "24"))
	int32 GalleryOverlapLookback = 10;

private:
	struct FPhotoViewingState
	{
		float Opacity = 0.0f;
		float ScaleMultiplier = 1.0f;
		float BlurAmount = 0.0f;
		bool bVisible = false;
		bool bInteractive = false;
	};

	FVector GetHelixCenterLocation() const;
	void RefreshCachedHelixCenterLocation();
	float GetDeterministicPhotoScale(int32 PhotoIndex) const;
	float GetEstimatedPhotoWidth(float PhotoScale) const;
	float GetEstimatedPhotoHeight(float PhotoScale) const;
	float GetPhotoLayoutWidth(int32 PhotoIndex, float PhotoScale) const;
	float GetPhotoCenterSpacingWidth(int32 LeftPhotoIndex, int32 RightPhotoIndex) const;
	float GetPhotoAngularPositionDegrees(int32 PhotoIndex, float PathIndex, float CurrentRadius) const;
	float GetAntiClusterAdjustedAngleDegrees(int32 PhotoIndex, float ProposedAngleDegrees, const TArray<float>& PreviousAnglesDegrees) const;
	float GetMinAngularStepDegrees(float PhotoWidth, float CurrentRadius) const;
	FVector EvaluateHelixPosition(float PathParameter) const;
	FVector EvaluateHelixTangent(float PathParameter) const;
	// World centimetres travelled per unit of path parameter at this point on the helix.
	float GetHelixArcSpeed(float PathParameter) const;
	// Solves for the path parameter that sits TargetDistance further along the curve.
	// Replaces stepping forward by a fixed epsilon, which cost hundreds of helix
	// evaluations per photo and dominated gallery startup.
	float AdvanceAlongHelixByDistance(float StartParameter, float TargetDistance) const;
	float GetLayoutFootprintRadius(int32 PhotoIndex) const;
	int32 GetRequiredPhotoPoolSize() const;
	void SpawnPhotos();
	void ClearSpawnedPhotoActors();
	void EnsurePhotoPoolSize();
	void LayoutSpawnedPhotos();

	// --- Virtualized desktop gallery ---
	bool UseVirtualizedGallery() const;
	void EnsureGalleryPool();
	void LayoutVirtualizedGallery();
	// Cumulative path offset per photo, spaced by each photo's real width. Built once at load.
	void RebuildGalleryPathOffsets();
	float GetGalleryPhotoWidth(int32 LogicalPhotoIndex) const;
	// The virtualized spiral's own shape, decoupled from the Blueprint-tuned helix params.
	FVector EvaluateGallerySpiralPosition(float PathOffset) const;
	// Position + facing for a virtualized photo at the given path offset.
	FTransform BuildVirtualizedPhotoTransform(int32 LogicalPhotoIndex, float PathOffset) const;
	FPhotoViewingState BuildVirtualizedViewingState(float PathIndex) const;
	// Pulls a decoded thumbnail from the cache onto the actor, or queues a decode.
	void ApplyOrQueueGalleryThumbnail(AMemoryPhotoActor* PhotoActor, int32 LogicalPhotoIndex, int32& InOutActiveDecodes);
	// Full-resolution image for the currently expanded photo.
	void ProcessVirtualizedExpandedSelection();
	// Copies any freshly decoded pool textures into the LRU cache.
	void HarvestGalleryTextures();
	void CacheGalleryTexture(const FString& FilePath, UTexture2D* Texture, int32 Width, int32 Height);
	void ClearGalleryTextureCaches();

	void LoadPhotoAspectRatiosFromHeaders();
	FTransform BuildPhotoTransform(int32 PhotoIndex) const;
	void BindScrollInput();
	void BindLocomotionInput();
	void AddMoveXInput(float Value);
	void AddMoveYInput(float Value);
	void TickLocomotion(float DeltaSeconds);
	void TickTurn(float DeltaSeconds);
	void ProcessGalleryLoadBatch();
	void FinishGalleryLoad();
	bool StartAndroidMediaStoreDiagnosticLoad();
	// Android folder-based photo loading (alternative to MediaStore).
	FString ResolveAndroidPhotoFolderPath() const;
	bool TryStartAndroidFolderLoad();
	// Photo albums (the settings panel's Photos row). The list is cached; RefreshAlbumList
	// rebuilds it off the game thread at startup, on reload, and when the row is used.
	void RefreshAlbumList();
	void FinishAlbumList(TArray<FMemoryRoomAlbum>&& Found, bool bComplete);
	void ScheduleAlbumReload();
	// The MediaStore bucket to filter by, or empty when the selection is not a MediaStore album.
	FString GetActiveBucketId() const;
	void ApplyAlbumFilterToPhotoSource();
	void AssignAndroidPhotoToActor(int32 PhotoIndex, AMemoryPhotoActor* PhotoActor);
	void QueueAndroidThumbnailRequest(int32 PhotoIndex, AMemoryPhotoActor* PhotoActor);
	void PumpAndroidThumbnailQueue();
	void CacheAndroidThumbnail(const FString& StableMediaId, UTexture2D* Texture, int32 Width, int32 Height);
	void ClearAndroidThumbnailCacheAndActiveTextures();
	void ClearAndroidExpandedCache();
	// Reads the sky dome's current glow strength and pushes it to the photos when it moves.
	void UpdatePhotoBrightnessFromSky();
	// Lights the nearest few visible photos so the gallery illuminates the room at night.
	void UpdatePhotoGlowLights(float PhotoBrightness);
	// Starts decodes for visible photos that still lack a texture, nearest first, within
	// the concurrency budget. Returns true while any photo is still waiting.
	bool RequestVisiblePhotoTextures();
	// Throttled log of visible / textured counts and the path window, for tuning the spiral.
	void LogLayoutDiagnostics();
	void SaveQualitySettings() const;
	void LoadQualitySettings();
	// Applies one setting's current value to the running world (called from AdjustUserSetting
	// and ApplyUserSettings). bRelayout requests a gallery path rebuild afterward.
	void PushUserSettingToWorld(EMemoryRoomUserSetting Setting, bool& bOutNeedsRelayout);
	void SaveUserSettings() const;

	TArray<FMemoryRoomAlbum> Albums;
	FString SelectedAlbumId;
	bool bHasPhotoSourceChoice = false;
	int32 PreviewAlbumIndex = 0;
	bool bPhotoAlbumPreviewActive = false;
	int32 AlbumEnumerationGeneration = 0;
	double LastAlbumRefreshSeconds = -1000.0;
	FTimerHandle AlbumReloadTimerHandle;
	AMemoryRoomSkyDomeActor* FindSkyDome() const;
	AMemoryRoomDustActor* FindDustActor() const;

	// --- Theme ---
	// Ensures CurrentTheme points at a usable asset (authored or built-in).
	UMemoryRoomThemeAsset* ResolveTheme();
	// Applies the current theme's frame style to one photo actor. Called from ApplyTheme and
	// from every spawn site so pooled/late actors match.
	void ApplyThemeToPhoto(AMemoryPhotoActor* PhotoActor) const;
	// Starts / restarts the ambient loop from the current theme.
	void ApplyAmbientAudio();
	// Fires a one-shot cue at a world location (2D when Location is ZeroVector).
	void PlayThemeCue(USoundBase* Cue, float VolumeScale, const FVector& Location) const;
	void ApplyQualityDimensionsFromEnums();
	static int32 ThumbnailDimensionForQuality(EMemoryRoomThumbnailQuality Quality);
	static int32 ExpandedDimensionForQuality(EMemoryRoomExpandedPhotoQuality Quality);
	void ProcessAndroidExpandedSelection();
	void QueueAndroidExpandedRequest(int32 PhotoIndex, AMemoryPhotoActor* PhotoActor);
	void PumpAndroidExpandedQueue();
	void CacheAndroidExpandedTexture(const FString& StableMediaId, UTexture2D* Texture, int32 Width, int32 Height);
	void RestoreAndroidThumbnailToActor(AMemoryPhotoActor* PhotoActor);
	static void GetCorrectedMediaStoreDimensions(const FMemoryPhotoMetadata& Photo, int32& OutWidth, int32& OutHeight);

	// --- Free-placed photos ---
	// Reads the save slot and spawns a standalone actor for every entry it finds. Called once
	// from BeginPlay - independent of the gallery scan/discovery, since a placed photo is
	// addressed by its own saved absolute path rather than a logical index into it.
	void LoadPlacedPhotos();
	void SavePlacedPhotos() const;

	// Gaze-driven quality for placed photos (see the property comments above). Independent of
	// the single-slot ActiveExpandedPhotoActor/ProcessAndroidExpandedSelection machinery above -
	// that machinery only ever tracks the one currently-held-or-wall-opened photo, whereas any
	// number of placed photos can be sitting in the room at once, so each is requested and
	// verified against its own identity rather than a shared "active" slot.
	void UpdatePlacedPhotoQuality(float DeltaTime);
	void RequestPlacedPhotoFullResolution(AMemoryPhotoActor* Actor);
	void RequestPlacedPhotoThumbnail(AMemoryPhotoActor* Actor);

	// Left-stick input, sampled by the bound axis handlers each frame (polling the raw key
	// state does not work for OpenXR controllers - only bound axis/action delegates fire).
	float LocoInputX = 0.0f;
	float LocoInputY = 0.0f;
	// Right-stick X only - Y stays on gallery scroll.
	float TurnInputX = 0.0f;
	// Floor height captured once by SnapPlayerPawnToFloor - the bottom of the float range.
	float LocomotionFloorZ = 0.0f;
	bool bLocomotionFloorCaptured = false;

	float PlacedPhotoQualityCheckAccumulator = 0.0f;
	// Android decodes one placed-photo upgrade at a time, same budget as the wall's own expanded
	// photo (AndroidMaxConcurrentExpandedDecodes is 1 by default) - this is that one slot's owner.
	TWeakObjectPtr<AMemoryPhotoActor> PlacedPhotoUpgradeInFlight;

	UPROPERTY(Transient)
	TMap<FString, TObjectPtr<AMemoryPhotoActor>> PlacedPhotoActors;

	UPROPERTY(Transient)
	TArray<TObjectPtr<AMemoryPhotoActor>> SpawnedPhotos;

	TArray<float> CachedPhotoPathParameters;
	TArray<float> CachedFootprintRadii;
	// Helix position for each cached path parameter. The overlap pass compares every
	// candidate against its recent neighbours; without this it re-evaluated the same
	// neighbour positions on every iteration.
	TArray<FVector> CachedPhotoPositions;
	FVector CachedHelixCenterLocation = FVector::ZeroVector;

	TArray<FString> PendingPhotoLoadQueue;

	// --- Virtualized desktop gallery ---
	// All discovered photo paths (mirrors DiscoveredPhotoFiles once the gallery is built),
	// with a header-read aspect ratio each so spacing works without a spawned actor.
	TArray<FString> GalleryPhotoFiles;
	TArray<float> PhotoAspectRatios;
	// Where each photo sits along the spiral, in path units. Monotonically increasing.
	TArray<float> GalleryPathOffsets;

	struct FGalleryTextureEntry
	{
		TStrongObjectPtr<UTexture2D> Texture;
		int32 Width = 0;
		int32 Height = 0;
	};
	TMap<FString, FGalleryTextureEntry> GalleryThumbnailCache;
	TArray<FString> GalleryThumbnailCacheOrder;
	TMap<FString, FGalleryTextureEntry> GalleryExpandedCache;
	TArray<FString> GalleryExpandedCacheOrder;
	FString ActiveGalleryExpandedPath;
	TWeakObjectPtr<AMemoryPhotoActor> ActiveGalleryExpandedActor;

	TArray<FMemoryPhotoMetadata> MediaStorePhotos;
	TSet<FString> PendingAndroidTextureLoads;
	TUniquePtr<IMemoryPhotoSource> PhotoSource;
	int32 PhotoAssignmentGeneration = 0;
	FTimerHandle GalleryLoadTimerHandle;

	struct FAndroidThumbnailRequest
	{
		TWeakObjectPtr<AMemoryPhotoActor> PhotoActor;
		int32 PhotoIndex = INDEX_NONE;
		FString StableMediaId;
		FString ContentUri;
		int32 AssignmentGeneration = 0;
		int32 LibraryGeneration = 0;
		int32 ThumbnailQualityGeneration = 0;
	};

	// Holds the finished texture rather than the decoded pixels. Keeping pixels meant every
	// cached thumbnail cost its bitmap twice over (array + texture) and every cache hit
	// copied the array and re-uploaded a brand new texture to the GPU.
	struct FAndroidThumbnailCacheEntry
	{
		TStrongObjectPtr<UTexture2D> Texture;
		int32 Width = 0;
		int32 Height = 0;
	};

	struct FAndroidExpandedRequest
	{
		TWeakObjectPtr<AMemoryPhotoActor> PhotoActor;
		int32 PhotoIndex = INDEX_NONE;
		FString StableMediaId;
		FString ContentUri;
		int32 AssignmentGeneration = 0;
		int32 SelectionGeneration = 0;
		int32 LibraryGeneration = 0;
	};

	struct FAndroidExpandedCacheEntry
	{
		TStrongObjectPtr<UTexture2D> Texture;
		int32 Width = 0;
		int32 Height = 0;
	};

	TArray<FAndroidThumbnailRequest> PendingAndroidThumbnailQueue;
	// Mirrors PendingAndroidThumbnailQueue for dedupe. The queue is walked once per
	// visible photo per frame, so the old linear FString compare was O(visible * queued)
	// every frame.
	TSet<FString> QueuedAndroidThumbnailIds;
	TMap<FString, FAndroidThumbnailCacheEntry> AndroidThumbnailCache;
	TArray<FString> AndroidThumbnailCacheOrder;
	int32 ActiveAndroidThumbnailDecodes = 0;
	int32 AndroidThumbnailSuccessCount = 0;
	int32 AndroidThumbnailFailureCount = 0;
	int32 AndroidStaleThumbnailDiscardCount = 0;
	TArray<FAndroidExpandedRequest> PendingAndroidExpandedQueue;
	TMap<FString, FAndroidExpandedCacheEntry> AndroidExpandedCache;
	TArray<FString> AndroidExpandedCacheOrder;
	int32 ActiveAndroidExpandedDecodes = 0;
	int32 AndroidExpandedSelectionGeneration = 0;
	int32 PhotoLibraryGeneration = 0;
	int32 ThumbnailQualityGeneration = 0;
	EMemoryRoomThumbnailQuality ThumbnailQuality = EMemoryRoomThumbnailQuality::Medium;
	EMemoryRoomExpandedPhotoQuality ExpandedPhotoQuality = EMemoryRoomExpandedPhotoQuality::Ultra;
	int32 ActiveExpandedPhotoIndex = INDEX_NONE;
	FString ActiveExpandedStableMediaId;
	FString ActiveExpandedContentUri;
	TWeakObjectPtr<AMemoryPhotoActor> ActiveExpandedPhotoActor;

	TWeakObjectPtr<AMemoryRoomSkyDomeActor> CachedSkyDome;
	float LastAppliedPhotoBrightness = -1.0f;

	// Ambient bed for the current theme. Spawned by ApplyAmbientAudio (SpawnSound2D), so the
	// manager needs no scene root; torn down and respawned when the theme changes.
	UPROPERTY(Transient)
	TObjectPtr<UAudioComponent> AmbientAudioComponent;

	// The theme actually in effect after ApplyTheme resolved authored-vs-built-in.
	UPROPERTY(Transient)
	TObjectPtr<UMemoryRoomThemeAsset> ActiveTheme;

	// Shared UI materials painted from ActiveTheme (see MemoryRoomUIGlass.h): the gold halo
	// behind a hovered photo and the glass slab under a photo's metadata. Rebuilt by
	// ApplyTheme and handed to every photo by ApplyThemeToPhoto.
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> ThemeHoverFrameMaterial;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> ThemeGlassMaterial;

	// ActiveTheme->Audio with the per-cue overrides folded in. Read by the cue hooks so the
	// shared theme asset is never mutated.
	FMemoryRoomAudioTheme ActiveAudio;

	// Scroll-whoosh edge tracking: true while the spiral is moving, so the cue fires once
	// per "start moving from rest" rather than every frame.
	bool bScrollWhooshActive = false;
	// 0 with nothing open, 1 with a photo fully in focus. Eased so opening and closing a
	// photo never snaps the room.
	float FocusBlend = 0.0f;
	// Keeps the layout pass running after scrolling stops, until every visible photo has
	// its texture; otherwise the queue would stall the moment the player let go.
	bool bHasPendingPhotoTextures = false;
	double LastLayoutDiagnosticSeconds = -10.0;
	// Hash of everything LayoutVirtualizedGallery's per-photo transform/viewing state depends
	// on. While it is unchanged, photos already at rest in their slot are not re-laid-out.
	uint32 LastVirtualLayoutKey = 0;
	bool bHasVirtualLayoutKey = false;

	// 0..3 index into a small set of dust density presets (Off / Low / Medium / Full).
	int32 DustAmountLevel = 3;

	// Ambient particle style picked in the settings menu (Dust / Leaves). Wins over the theme's
	// MoteStyle: ApplyTheme applies this instead of the theme's value.
	EMemoryRoomMoteStyle ParticleStyle = EMemoryRoomMoteStyle::Dust;

	float ScrollVelocity;
	float ScrollInputAmount;
	float HeldScrollInputAmount;
	float LastAppliedScrollOffset;
	float InitialScrollOffset = 0.0f;
	bool bGalleryScrollSuppressed = false;

	FPhotoViewingState BuildPhotoViewingState(float PathIndex) const;
};
