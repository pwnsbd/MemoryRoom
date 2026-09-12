#include "MemoryPhotoLibrarySubsystem.h"

#if PLATFORM_ANDROID
#include "AndroidMediaStorePhotoSource.h"
#endif

namespace
{
class FLocalFolderMemoryPhotoSource final : public IMemoryPhotoSource
{
public:
	virtual void RequestPhotoPermissionAsync(FMemoryPhotoPermissionCallback Callback) override { Callback.ExecuteIfBound(true, TEXT("Local folder source")); }
	virtual void QueryPhotosAsync(int32, int32, FMemoryPhotoMetadataPageCallback Callback) override { Callback.ExecuteIfBound(false, TArray<FMemoryPhotoMetadata>(), 0); }
	virtual void LoadPhotoPixelsAsync(const FString&, int32, int32, FMemoryPhotoPixelsCallback Callback) override { Callback.ExecuteIfBound(false, TArray<FColor>(), 0, 0); }
	virtual FString GetDebugName() const override { return TEXT("Local Folder"); }
};
}

void UMemoryPhotoLibrarySubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
#if PLATFORM_ANDROID
	PhotoSource = MakeUnique<FAndroidMediaStorePhotoSource>();
#else
	PhotoSource = MakeUnique<FLocalFolderMemoryPhotoSource>();
#endif
}

void UMemoryPhotoLibrarySubsystem::Deinitialize()
{
	PhotoSource.Reset();
	Super::Deinitialize();
}
