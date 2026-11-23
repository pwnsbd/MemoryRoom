// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "MemoryRoomDustActor.h" // EMemoryRoomMoteStyle
#include "MemoryRoomSkyDomeActor.h" // FMemoryRoomSkyPreset
#include "MemoryRoomThemeAsset.generated.h"

class USoundBase;

// The built-in looks the manager can synthesise without an authored asset. Hearth is the
// warm default; Void is a placeholder that currently just returns the cool baseline.
UENUM(BlueprintType)
enum class EMemoryRoomThemeId : uint8
{
	Hearth,
	Void
};

/**
 * The four sound cues a theme drives. All optional - a null cue simply plays nothing, so
 * the room is silent until real audio is dropped in here.
 */
USTRUCT(BlueprintType)
struct FMemoryRoomAudioTheme
{
	GENERATED_BODY()

	// Looping bed, started when the theme is applied and left running the whole session.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Audio")
	TObjectPtr<USoundBase> AmbientLoop = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Audio", meta = (ClampMin = "0.0", UIMax = "2.0"))
	float AmbientVolume = 1.0f;

	// One-shot the moment the spiral starts moving from rest.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Audio")
	TObjectPtr<USoundBase> ScrollWhoosh = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Audio", meta = (ClampMin = "0.0", UIMax = "2.0"))
	float ScrollWhooshVolume = 0.6f;

	// One-shot when the pointer first lands on a photo (fires on the hover-changed edge).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Audio")
	TObjectPtr<USoundBase> HoverChime = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Audio", meta = (ClampMin = "0.0", UIMax = "2.0"))
	float HoverChimeVolume = 0.5f;

	// One-shot when a photo is lifted into the hand.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Audio")
	TObjectPtr<USoundBase> GrabTone = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Audio", meta = (ClampMin = "0.0", UIMax = "2.0"))
	float GrabToneVolume = 0.8f;
};

/**
 * The look of a framed photo. M_Photo samples FrameTintColor / FrameEdgeWidth to draw a
 * soft border falloff around the image; the maths is a couple of nodes, everything else is
 * these two values.
 */
USTRUCT(BlueprintType)
struct FMemoryRoomPhotoFrameStyle
{
	GENERATED_BODY()

	// Colour the border falloff tints the photo edges toward.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Photo Frame")
	FLinearColor FrameTintColor = FLinearColor(0.015f, 0.012f, 0.010f, 1.0f);

	// Fraction of the photo's half-size the falloff covers on each edge. 0 = no visible frame.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Photo Frame", meta = (ClampMin = "0.0", ClampMax = "0.5"))
	float FrameEdgeWidth = 0.06f;
};

/**
 * Colours for the wrist pucks, the settings board and the photo metadata slab - the
 * "lantern glass" look (see MemoryRoomUIGlass.h). Every surface is unlit, so these read the
 * same at dawn and in the near-black night.
 */
USTRUCT(BlueprintType)
struct FMemoryRoomWristUIStyle
{
	GENERATED_BODY()

	// Top of every glass slab: a warm near-black.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wrist UI|Glass")
	FLinearColor PanelColor = FLinearColor(0.012f, 0.009f, 0.008f, 1.0f);

	// Bottom of every glass slab: the ember the slab is lit by.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wrist UI|Glass")
	FLinearColor GlassGlowColor = FLinearColor(0.16f, 0.060f, 0.020f, 1.0f);

	// A press tile at rest, and the same tile under the pointer.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wrist UI|Tiles")
	FLinearColor ButtonColor = FLinearColor(0.085f, 0.050f, 0.032f, 1.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wrist UI|Tiles")
	FLinearColor ButtonHoverColor = FLinearColor(0.46f, 0.24f, 0.085f, 1.0f);

	// Hairline rules, the puck bezel ring, the menu glyph and setting values.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wrist UI|Text")
	FLinearColor AccentColor = FLinearColor(0.95f, 0.70f, 0.36f, 1.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wrist UI|Text")
	FLinearColor TitleColor = FLinearColor(0.99f, 0.91f, 0.76f, 1.0f);

	// Body text: parchment.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wrist UI|Text")
	FLinearColor LabelColor = FLinearColor(0.86f, 0.82f, 0.74f, 1.0f);

	// Hover / pressed text accent.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wrist UI|Text")
	FLinearColor HighlightColor = FLinearColor(1.00f, 0.78f, 0.42f, 1.0f);
};

/**
 * One complete look for the room: the three sky presets, the photo frame, the dust tint,
 * the audio cues and the wrist-UI colours. Authored as a data asset so a designer can retune
 * the whole set without code; AMemoryRoomManager can also synthesise the built-in themes at
 * runtime when no asset is assigned (see EMemoryRoomThemeId / MakeBuiltInTheme).
 */
UCLASS(BlueprintType)
class MEMORYROOM_API UMemoryRoomThemeAsset : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Theme")
	FText ThemeName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Theme|Sky")
	FMemoryRoomSkyPreset DawnPreset;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Theme|Sky")
	FMemoryRoomSkyPreset SunsetPreset;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Theme|Sky")
	FMemoryRoomSkyPreset NightPreset;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Theme|Photo")
	FMemoryRoomPhotoFrameStyle PhotoFrame;

	// Multiplier tint pushed onto the dust motes' material.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Theme|Dust")
	FLinearColor DustTint = FLinearColor(1.0f, 1.0f, 1.0f, 1.0f);

	// Dust (fireflies) or Leaf (wind-blown leaves). Default Dust leaves existing themes unchanged.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Theme|Dust")
	EMemoryRoomMoteStyle MoteStyle = EMemoryRoomMoteStyle::Dust;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Theme|Audio")
	FMemoryRoomAudioTheme Audio;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Theme|UI")
	FMemoryRoomWristUIStyle WristUI;

	// Builds one of the built-in themes as a transient asset owned by Outer. Used by the
	// manager when CurrentTheme is left empty. Never returns null.
	static UMemoryRoomThemeAsset* MakeBuiltInTheme(EMemoryRoomThemeId ThemeId, UObject* Outer);
};
