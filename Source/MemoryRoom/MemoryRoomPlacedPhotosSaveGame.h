#pragma once

#include "CoreMinimal.h"
#include "GameFramework/SaveGame.h"
#include "MemoryRoomPlacedPhotosSaveGame.generated.h"

// One photo someone pulled out of the gallery and pinned somewhere in the room. Keyed by the
// photo's own absolute file path in the map below - the only stable identifier a desktop photo
// has across sessions - rather than by pool/logical index, which is only meaningful within a
// single play session's virtualized layout.
USTRUCT()
struct FMemoryRoomPlacedPhotoEntry
{
	GENERATED_BODY()

	UPROPERTY()
	FTransform WorldTransform = FTransform::Identity;
};

// Persists the set of photos placed as fixed, standalone copies in the room (see
// AMemoryRoomManager::PlacePhotoInWorld), independent of the scrolling gallery's own state.
UCLASS()
class MEMORYROOM_API UMemoryRoomPlacedPhotosSaveGame : public USaveGame
{
	GENERATED_BODY()

public:
	// Not Blueprint-exposed: TMap of a custom struct isn't Blueprint-compatible, and nothing
	// outside the manager's own save/load code needs to touch this directly.
	UPROPERTY()
	TMap<FString, FMemoryRoomPlacedPhotoEntry> PlacedPhotos;
};
