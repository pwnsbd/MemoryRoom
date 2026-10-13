#pragma once

#include "AMemoryRoomManager.h"
#include "MemoryPhotoSource.h"

#if PLATFORM_ANDROID
#include "Android/AndroidJavaEnv.h"
#include "Android/AndroidJNI.h"
#include "Android/AndroidApplication.h"
#endif

class FAndroidMediaStorePhotoSource final : public IMemoryPhotoSource
{
public:
	virtual void RequestPhotoPermissionAsync(FMemoryPhotoPermissionCallback Callback) override;
	virtual void QueryPhotosAsync(int32 Offset, int32 Limit, FMemoryPhotoMetadataPageCallback Callback) override;
	virtual void LoadPhotoPixelsAsync(const FString& ContentUri, int32 MaximumDimension, int32 Generation, FMemoryPhotoPixelsCallback Callback) override;
	virtual FString GetDebugName() const override { return TEXT("Android MediaStore"); }

	// Restricts QueryPhotosAsync to one MediaStore bucket (album). Empty = every image.
	void SetBucketFilter(const FString& InBucketId) { BucketFilter = InBucketId; }
	// Lists MediaStore image albums off the game thread; the callback runs on the game thread.
	void QueryAlbumsAsync(TFunction<void(bool bSuccess, TArray<FMemoryRoomAlbum> Albums)> Callback);

private:
	FString BucketFilter;
#if PLATFORM_ANDROID
	static jclass GetBridgeClass(JNIEnv* Env);
	static bool CheckAndClearJavaException(JNIEnv* Env, const TCHAR* Context);
	static bool IsPermissionGranted();
#endif
};
