#pragma once

#include "CoreMinimal.h"

struct FMemoryPhotoMetadata
{
	FString StableMediaId;
	FString DisplayName;
	FString ContentUri;
	int32 Width = 0;
	int32 Height = 0;
	int32 Orientation = 0;
	FString MimeType;
	int64 DateTaken = 0;
	int64 DateModified = 0;
};

DECLARE_DELEGATE_TwoParams(FMemoryPhotoPermissionCallback, bool /*bGranted*/, const FString& /*Message*/);
DECLARE_DELEGATE_ThreeParams(FMemoryPhotoMetadataPageCallback, bool /*bSuccess*/, const TArray<FMemoryPhotoMetadata>& /*Photos*/, int32 /*TotalCount*/);
DECLARE_DELEGATE_FourParams(FMemoryPhotoPixelsCallback, bool /*bSuccess*/, TArray<FColor> /*Pixels*/, int32 /*Width*/, int32 /*Height*/);

class IMemoryPhotoSource
{
public:
	virtual ~IMemoryPhotoSource() = default;
	virtual void RequestPhotoPermissionAsync(FMemoryPhotoPermissionCallback Callback) = 0;
	virtual void QueryPhotosAsync(int32 Offset, int32 Limit, FMemoryPhotoMetadataPageCallback Callback) = 0;
	virtual void LoadPhotoPixelsAsync(const FString& ContentUri, int32 MaximumDimension, int32 Generation, FMemoryPhotoPixelsCallback Callback) = 0;
	virtual FString GetDebugName() const = 0;
};
