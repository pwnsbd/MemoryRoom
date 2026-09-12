#pragma once

#include "CoreMinimal.h"
#include "GameFramework/SaveGame.h"
#include "AMemoryRoomManager.h"
#include "MemoryRoomSkyDomeActor.h"
#include "MemoryRoomQualitySaveGame.generated.h"

UCLASS()
class MEMORYROOM_API UMemoryRoomQualitySaveGame : public USaveGame
{
	GENERATED_BODY()

public:
	UPROPERTY(BlueprintReadWrite, Category = "Memory Room|Quality")
	EMemoryRoomThumbnailQuality ThumbnailQuality = EMemoryRoomThumbnailQuality::Medium;

	UPROPERTY(BlueprintReadWrite, Category = "Memory Room|Quality")
	EMemoryRoomExpandedPhotoQuality ExpandedPhotoQuality = EMemoryRoomExpandedPhotoQuality::Ultra;

	UPROPERTY(BlueprintReadWrite, Category = "Memory Room|Sky")
	EMemoryRoomTimeOfDay TimeOfDay = EMemoryRoomTimeOfDay::Sunset;

	// Distinguishes "player never chose" from "player chose Sunset", so the sky dome's
	// DefaultTimeOfDay still applies on a fresh install.
	UPROPERTY(BlueprintReadWrite, Category = "Memory Room|Sky")
	bool bHasSavedTimeOfDay = false;

	// --- In-headset settings panel values ---
	UPROPERTY(BlueprintReadWrite, Category = "Memory Room|Settings")
	bool bHasSavedUserSettings = false;

	UPROPERTY(BlueprintReadWrite, Category = "Memory Room|Settings")
	float SpiralTwist = 24.0f;

	UPROPERTY(BlueprintReadWrite, Category = "Memory Room|Settings")
	float PhotoSpacing = 40.0f;

	UPROPERTY(BlueprintReadWrite, Category = "Memory Room|Settings")
	int32 PhotosInView = 44;

	UPROPERTY(BlueprintReadWrite, Category = "Memory Room|Settings")
	float ScrollSpeed = 12.0f;

	UPROPERTY(BlueprintReadWrite, Category = "Memory Room|Settings")
	float NightPhotoGlow = 6.0f;

	UPROPERTY(BlueprintReadWrite, Category = "Memory Room|Settings")
	int32 DustAmountLevel = 3;

	UPROPERTY(BlueprintReadWrite, Category = "Memory Room|Settings")
	EMemoryRoomThemeId ThemeId = EMemoryRoomThemeId::Hearth;
};
