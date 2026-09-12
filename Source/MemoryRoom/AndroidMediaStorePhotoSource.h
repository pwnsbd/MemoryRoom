#pragma once

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

private:
#if PLATFORM_ANDROID
	static jclass GetBridgeClass(JNIEnv* Env);
	static bool CheckAndClearJavaException(JNIEnv* Env, const TCHAR* Context);
	static bool IsPermissionGranted();
#endif
};
