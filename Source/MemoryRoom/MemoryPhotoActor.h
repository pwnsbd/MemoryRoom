// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MemoryPhotoActor.generated.h"

class UMaterialInstanceDynamic;
class UPointLightComponent;
class UMaterialInterface;
struct FMemoryRoomWristUIStyle;
class USceneComponent;
class UTexture2D;
class UStaticMesh;
class UStaticMeshComponent;
class UTextRenderComponent;

UENUM(BlueprintType)
enum class EMemoryPhotoOrientation : uint8
{
	Portrait,
	Landscape,
	Square
};

UCLASS(Blueprintable)
class MEMORYROOM_API AMemoryPhotoActor : public AActor
{
	GENERATED_BODY()

public:
	AMemoryPhotoActor();

	virtual void Tick(float DeltaTime) override;

	UFUNCTION(BlueprintCallable, Category = "Memory Photo|Runtime Image")
	bool LoadImageFromFile(const FString& FilePath, int32 MaximumRuntimeTextureDimension = 2048);

	UFUNCTION(BlueprintCallable, Category = "Memory Photo|Runtime Image")
	void LoadImageFromFileAsync(const FString& FilePath, int32 MaximumRuntimeTextureDimension = 2048);

	// Binds a file to this photo and sizes it from the image header alone, without decoding
	// any pixels. The gallery build uses this so startup does not block on 100 JPEG decodes;
	// the pixels arrive later, and only for photos that actually become visible.
	UFUNCTION(BlueprintCallable, Category = "Memory Photo|Runtime Image")
	bool PrepareFromFile(const FString& FilePath);

	// Reads width/height from an image file's header without decoding pixels. Static so the
	// gallery can size photos it has not spawned an actor for.
	static bool ReadImageDimensions(const FString& FilePath, int32& OutWidth, int32& OutHeight);

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Memory Photo|Runtime Image")
	bool HasLoadedTexture() const { return RuntimeTexture != nullptr; }

	// Full mono equirectangular panoramas are 2:1. Grip remains available for flat viewing.
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Memory Photo|360")
	bool IsPanoramaCandidate() const { return LoadedImageHeight > 0 && FMath::Abs(LoadedAspectRatio - 2.0f) < 0.025f; }

	// Lets the gallery cap how many decodes are in the air at once.
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Memory Photo|Runtime Image")
	bool IsTextureLoadInFlight() const { return bTextureLoadInFlight; }

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Memory Photo|Runtime Image")
	int32 GetLoadedImageWidth() const { return LoadedImageWidth; }

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Memory Photo|Runtime Image")
	int32 GetLoadedImageHeight() const { return LoadedImageHeight; }

	// Sizes the photo plane from a known aspect ratio without touching the file, for pooled
	// actors that have not loaded their pixels yet.
	UFUNCTION(BlueprintCallable, Category = "Memory Photo|Runtime Image")
	void ApplyAspectRatioHint(float AspectRatio);

	// The display width/height this photo would take for a given aspect ratio, using the
	// same orientation + clamp rules as a loaded image. Const and mesh-free, so the gallery
	// can call it on the class default object while laying out photos it has not spawned.
	void GetDisplaySizeForAspect(float AspectRatio, float& OutWidth, float& OutHeight) const;

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Memory Photo|Runtime Image")
	FString GetLoadedFilePath() const { return LoadedFilePath; }

	UFUNCTION(BlueprintCallable, Category = "Memory Photo|Runtime Image")
	void UnloadRuntimeTexture();

	bool ApplyDecodedRuntimeImage(const FString& SourceId, TArray<FColor> DecodedPixels, int32 DecodedWidth, int32 DecodedHeight);
	bool ApplyRuntimeTexture(const FString& SourceId, UTexture2D* Texture, int32 TextureWidth, int32 TextureHeight);
	UTexture2D* GetRuntimeTexture() const { return RuntimeTexture; }

	UFUNCTION(BlueprintCallable, Category = "Memory Photo")
	void SetHovered(bool bNewHovered);

	UFUNCTION(BlueprintCallable, Category = "Memory Photo")
	void SetHoverSourceLocation(const FVector& SourceLocation);

	UFUNCTION(BlueprintCallable, Category = "Memory Photo")
	void SetLayoutTransform(const FTransform& NewLayoutTransform, float CurrentScrollOffset = 0.0f);

	UFUNCTION(BlueprintCallable, Category = "Memory Photo")
	void Expand(const FTransform& TargetTransform);

	UFUNCTION(BlueprintCallable, Category = "Memory Photo")
	void ExpandWithScale(const FTransform& TargetTransform, float ScaleMultiplier);

	UFUNCTION(BlueprintCallable, Category = "Memory Photo")
	void UpdateExpandedTargetTransform(const FTransform& TargetTransform, float ScaleMultiplier, bool bSnapToTarget = false);

	UFUNCTION(BlueprintCallable, Category = "Memory Photo")
	void Collapse();

	UFUNCTION(BlueprintCallable, Category = "Memory Photo")
	void ToggleExpanded(const FTransform& TargetTransform);

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Memory Photo")
	bool IsExpanded() const { return bExpanded; }

	// True while this actor is a standalone photo someone freely placed in the room (see
	// AMemoryRoomManager::PlacePhotoInWorld), rather than a pooled actor showing whatever the
	// scrolling gallery assigns it. Unlike IsExpanded(), this stays true at rest - the gallery's
	// own layout pass checks it (alongside IsExpanded()) to never reassign or reposition this
	// actor, leaving an empty gap at its old gallery slot instead of backfilling it with a
	// different actor showing the same photo.
	UFUNCTION(BlueprintCallable, Category = "Memory Photo|Pooling")
	void SetPlacedInWorld(bool bNewPlaced) { bPlacedInWorld = bNewPlaced; }

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Memory Photo|Pooling")
	bool IsPlacedInWorld() const { return bPlacedInWorld; }

	// The scale multiplier Expand() uses on its own (ScaleMultiplier == 1 in a manual
	// UpdateExpandedTargetTransform call means "actor's original, unexpanded scale", not "the
	// size it opened at"). Callers that keep re-driving an already-expanded photo's transform -
	// zoom, most currently - need this to build a multiplier relative to what Expand() actually
	// showed rather than relative to the tiny wall-layout scale.
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Memory Photo|Expanded")
	float GetExpandedScale() const { return ExpandedScale; }

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Memory Photo|Aspect Ratio")
	float GetBasePhotoHeight() const { return BasePhotoHeight; }

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Memory Photo|Aspect Ratio")
	float GetMinPhotoWidth() const { return MinPhotoWidth; }

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Memory Photo|Aspect Ratio")
	float GetMaxPhotoWidth() const { return MaxPhotoWidth; }

	UFUNCTION(BlueprintCallable, Category = "Memory Photo|Runtime Image")
	void ApplyDisplaySizeFromLoadedImage();

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Memory Photo")
	float GetPhotoVisualHalfWidthLocal() const;

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Memory Photo|Display")
	float GetDisplayWidth() const;

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Memory Photo|Display")
	float GetDisplayHeight() const;

	UFUNCTION(BlueprintCallable, Category = "Memory Photo|Pooling")
	void AssignPhotoIndex(int32 NewPhotoIndex);

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Memory Photo|Pooling")
	int32 GetAssignedPhotoIndex() const { return AssignedPhotoIndex; }

	UFUNCTION(BlueprintCallable, Category = "Memory Photo|Android")
	void AssignAndroidMediaStoreRecord(int32 LogicalPhotoIndex, const FString& StableMediaId, const FString& ContentUri, int32 CorrectedWidth, int32 CorrectedHeight);

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Memory Photo|Android")
	FString GetAssignedStableMediaId() const { return AssignedStableMediaId; }

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Memory Photo|Android")
	FString GetAssignedContentUri() const { return AssignedContentUri; }

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Memory Photo|Android")
	int32 GetAssignmentGeneration() const { return AssignmentGeneration; }

	UFUNCTION(BlueprintCallable, Category = "Memory Photo|Visibility")
	void ApplyViewingWindowState(float Opacity, float ScaleMultiplier, bool bInteractive, float BlurAmount = 0.0f);

	// Multiplier on the photo image, driven by the sky dome's time of day. 1.0 is the
	// image as captured; above 1.0 makes it glow against a dark room.
	UFUNCTION(BlueprintCallable, Category = "Memory Photo|Runtime Image")
	void SetPhotoBrightness(float NewBrightness);

	// Turns this photo into a real light source tinted by its own colours. Emissive alone
	// only makes the image look bright; this is what actually lights the room around it.
	UFUNCTION(BlueprintCallable, Category = "Memory Photo|Runtime Image")
	void SetGlowLightEnabled(bool bEnabled, float Intensity);

	// Mean colour of the loaded image, used to tint the glow light.
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Memory Photo|Runtime Image")
	FLinearColor GetAveragePhotoColor() const { return AveragePhotoColor; }

	void SetAveragePhotoColor(const FLinearColor& NewAverageColor);

	// The theme's border look. M_Photo reads FrameTintColor / FrameEdgeWidth to draw a soft
	// edge falloff; a material without those params just ignores the values. Stored and
	// re-pushed whenever the dynamic material is (re)created.
	UFUNCTION(BlueprintCallable, Category = "Memory Photo|Frame")
	void SetFrameStyle(FLinearColor FrameTint, float EdgeWidth);

	// The theme's UI dressing for a photo: the gold halo shown behind it while hovered, and
	// the glass slab plus text colours of its metadata panel. Materials assigned in the
	// Blueprint win; only empty slots take the theme's.
	void ApplyThemeUI(const FMemoryRoomWristUIStyle& Style, UMaterialInterface* HoverFrame, UMaterialInterface* GlassPanel);

protected:
	virtual void BeginPlay() override;

	// Let the photo drop a shadow on the ground (needs the sun shadow enabled on the sky
	// dome). Only the photos within the sun's short shadow distance actually cast.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Photo")
	bool bPhotoCastsShadow = true;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Memory Photo")
	TObjectPtr<USceneComponent> SceneRoot;

	// Photo display axes: local X = image width, local Y = image height, local Z = thickness/depth.
	// Runtime orientation sizing updates this component's X/Y scale only; actor scale/location are left unchanged.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Memory Photo")
	TObjectPtr<UStaticMeshComponent> PhotoMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Memory Photo|Hover")
	TObjectPtr<UStaticMeshComponent> HoverFrameMesh;

	// Off by default and only switched on for a handful of photos at night; a dynamic light
	// per visible photo would be far too expensive on mobile.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Memory Photo|Glow")
	TObjectPtr<UPointLightComponent> GlowLight;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Memory Photo|Metadata")
	TObjectPtr<USceneComponent> MetadataRoot;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Memory Photo|Metadata")
	TObjectPtr<UStaticMeshComponent> MetadataPanelMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Memory Photo|Metadata")
	TObjectPtr<UTextRenderComponent> TitleText;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Memory Photo|Metadata")
	TObjectPtr<UTextRenderComponent> DateText;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Memory Photo|Metadata")
	TObjectPtr<UTextRenderComponent> LocationText;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Memory Photo|Metadata")
	TObjectPtr<UTextRenderComponent> CaptionText;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Photo|Animation", meta = (ClampMin = "0.0"))
	float MoveSpeed;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Photo|Animation", meta = (ClampMin = "0.0"))
	float RotationSpeed;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Photo|Animation", meta = (ClampMin = "0.0"))
	float ScaleSpeed;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Photo|Animation", meta = (ClampMin = "0.0"))
	float PositionTolerance;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Photo|Animation", meta = (ClampMin = "0.0"))
	float RotationTolerance;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Photo|Animation", meta = (ClampMin = "0.0"))
	float ScaleTolerance;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Photo|Animation", meta = (ClampMin = "0.0"))
	float AnimationDuration;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Photo|Expanded", meta = (ClampMin = "0.01"))
	float ExpandedScale;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Photo|Hover")
	FName HoverAmountParameterName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Photo|Hover")
	FName EmissiveStrengthParameterName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Photo|Aspect Ratio", meta = (ClampMin = "0.01", UIMin = "0.01"))
	float BasePhotoHeight;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Photo|Aspect Ratio", meta = (ClampMin = "0.01", UIMin = "0.01"))
	float MinPhotoWidth;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Photo|Aspect Ratio", meta = (ClampMin = "0.01", UIMin = "0.01"))
	float MaxPhotoWidth;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Photo|Runtime Image")
	FName PhotoTextureParameterName;

	// The scalar M_Photo multiplies into Emissive. Named "PhotoEmissive" in the material, so
	// the time-of-day / NightPhotoGlow brightness push actually lands.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Photo|Runtime Image")
	FName PhotoBrightnessParameterName = TEXT("PhotoEmissive");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Photo|Frame")
	FName FrameTintColorParameterName = TEXT("FrameTintColor");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Photo|Frame")
	FName FrameEdgeWidthParameterName = TEXT("FrameEdgeWidth");

	UPROPERTY(BlueprintReadOnly, Category = "Memory Photo|Runtime Image")
	int32 LoadedImageWidth;

	UPROPERTY(BlueprintReadOnly, Category = "Memory Photo|Runtime Image")
	int32 LoadedImageHeight;

	UPROPERTY(BlueprintReadOnly, Category = "Memory Photo|Runtime Image")
	float LoadedAspectRatio;

	UPROPERTY(BlueprintReadOnly, Category = "Memory Photo|Runtime Image")
	EMemoryPhotoOrientation PhotoOrientation;

	UPROPERTY(BlueprintReadOnly, Category = "Memory Photo|Runtime Image")
	FString LoadedFilePath;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Photo|Display", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float SquareAspectTolerance;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Photo|Display", meta = (ClampMin = "0.01", UIMin = "0.01"))
	float LandscapeDisplayHeight;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Photo|Display", meta = (ClampMin = "0.01", UIMin = "0.01"))
	float PortraitDisplayHeight;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Photo|Display", meta = (ClampMin = "0.01", UIMin = "0.01"))
	float SquareDisplaySize;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Photo|Display", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float MinimumDisplayWidth;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Photo|Display", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float MaximumDisplayWidth;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Photo|Display", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float MaximumDisplayHeight;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Photo|Hover", meta = (ClampMin = "0.0"))
	float HoverAmount;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Photo|Hover", meta = (ClampMin = "0.0"))
	float HoverEmissiveStrength;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Photo|Hover", meta = (ClampMin = "1.0"))
	float HoverScaleMultiplier;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Photo|Hover", meta = (ClampMin = "0.0"))
	float HoverOffset;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Photo|Hover")
	TObjectPtr<UMaterialInterface> HoverFrameMaterial;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Photo|Hover")
	FVector HoverFrameScale;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Photo|Hover")
	float HoverFrameDepthOffset;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Photo|Hover")
	bool bEnableHoverScale;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Photo|Hover")
	bool bEnableHoverOffset;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Photo|Hover")
	bool bEnableHoverOutline;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Photo|Hover", meta = (ClampMin = "0", ClampMax = "255"))
	int32 HoverCustomDepthStencilValue;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Photo|Debug")
	bool bEnableHoverLayoutDebugLogging;

	// A frozen gallery reads as a slideshow. A few millimetres of seeded sway on its own
	// phase per photo makes the room feel alive without anyone noticing why.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Photo|Idle Drift")
	bool bEnableIdleDrift = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Photo|Idle Drift", meta = (ClampMin = "0.0", UIMax = "5.0"))
	float IdleDriftAmplitudeCm = 0.9f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Photo|Idle Drift", meta = (ClampMin = "0.0", UIMax = "5.0"))
	float IdleDriftRotationDegrees = 0.35f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Photo|Idle Drift", meta = (ClampMin = "0.0", UIMax = "3.0"))
	float IdleDriftSpeed = 0.32f;

	// The panel of title/date/location/caption text that slides in below an opened photo. Was
	// off by default because nothing populated it with real data (every photo showed the same
	// "Untitled Memory", no date); PopulateMetadataFromFilePath now fills Title/Date from the
	// file itself on every (re)assignment, so it's worth showing. Flip back off per-instance if
	// it reads too busy.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Photo|Metadata")
	bool bShowMetadataPanel = true;

	// Overwritten by PopulateMetadataFromFilePath whenever a file is (re)assigned - editing this
	// directly only matters until the next load.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Photo|Metadata")
	FText PhotoTitle;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Photo|Metadata")
	FText PhotoDate;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Photo|Metadata")
	FText PhotoLocation;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Photo|Metadata", meta = (MultiLine = "true"))
	FText PhotoCaption;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Photo|Metadata")
	FVector MetadataRelativeLocation;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Photo|Metadata")
	FVector MetadataPanelScale;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Photo|Metadata")
	TObjectPtr<UMaterialInterface> MetadataPanelMaterial;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Photo|Metadata")
	FName MetadataPanelOpacityParameterName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Photo|Metadata", meta = (ClampMin = "0.01"))
	float MetadataFadeDuration;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory Photo|Metadata")
	FColor MetadataTextColor;

	UFUNCTION(BlueprintImplementableEvent, Category = "Memory Photo|Pooling")
	void OnAssignedPhotoIndexChanged(int32 NewPhotoIndex);

private:
	void EnsureOriginalTransformStored();
	FTransform BuildHoverTargetTransform() const;
	void RebuildTargetTransform(bool bStartNewAnimation = true);
	void StartAnimatingTo(const FTransform& NewTargetTransform);
	FVector GetPhotoVisualCenterLocal() const;
	void UpdatePhotoMeshAspectRatio();
	void UpdateHoverFeedback();
	// Pushes the stored FrameTintColor / FrameEdgeWidth into the dynamic material.
	void ApplyFrameStyleToMaterial();
	void UpdateMetadataText();
	void SetMetadataAlpha(float Alpha);
	void StartMetadataFade(bool bFadeIn);

	// Derives PhotoTitle/PhotoDate from the file on disk, since nothing upstream authors a
	// title or capture date per photo. Called whenever a pooled actor is (re)assigned a file,
	// so a reused actor's info panel never shows a previous photo's title.
	void PopulateMetadataFromFilePath(const FString& FilePath);
	void UpdateTickEnabled();
	// Seeded from the photo index so each photo drifts on its own phase, and so the motion
	// is identical every run rather than looking randomly jittery.
	void ApplyIdleDrift();
	bool ShouldIdleDrift() const;
	bool MaterialHasScalarParameter(FName ParameterName) const;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> DynamicMaterial;

	UPROPERTY(Transient)
	TObjectPtr<UTexture2D> RuntimeTexture;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> DynamicMetadataPanelMaterial;

	FTransform OriginalTransform;
	FTransform LayoutTransform;
	FTransform TargetTransform;
	FTransform AnimationStartTransform;
	FVector HoverSourceLocation;
	FVector PhotoMeshAspectScale;
	FVector BaseVisualScale;
	float DisplayWidth;
	float DisplayHeight;
	int32 AssignedPhotoIndex;
	int32 AssignmentGeneration;
	FString AssignedStableMediaId;
	FString AssignedContentUri;
	float AnimationElapsed;
	float MetadataAlpha;
	float MetadataFadeElapsed;
	float MetadataFadeStartAlpha;
	float MetadataFadeTargetAlpha;
	float LastLayoutScrollOffset;
	// Last values pushed to DynamicMaterial, so the per-frame viewing-window update can
	// skip redundant material writes. Reset to NaN-ish sentinels whenever the dynamic
	// material is recreated.
	float LastAppliedMaterialOpacity = -1.0f;
	float LastAppliedMaterialBlurAmount = -1.0f;
	float PhotoBrightness = 1.0f;
	FLinearColor FrameTintColor = FLinearColor(0.015f, 0.012f, 0.010f, 1.0f);
	float FrameEdgeWidth = 0.0f;
	// Which of the two UI materials came from the theme rather than the Blueprint, so a
	// later theme can replace them but never an authored assignment.
	bool bHoverFrameMaterialFromTheme = false;
	bool bMetadataPanelMaterialFromTheme = false;
	FColor MetadataTitleColor = FColor::White;
	FLinearColor AveragePhotoColor = FLinearColor::White;
	float LastAppliedGlowIntensity = -1.0f;
	bool bGlowLightEnabled = false;
	float LastAppliedMaterialBrightness = -1.0f;
	bool bHasStoredOriginalTransform;
	bool bHasHoverSourceLocation;
	bool bHovered;
	bool bExpanded;
	bool bPlacedInWorld = false;
	bool bAnimating;
	bool bHoverFollowing;
	bool bMetadataFading;
	bool bPendingMetadataFadeIn;
	bool bPendingCollapse;
	bool bUseMaterialHover;
	bool bTextureLoadInFlight = false;
};
