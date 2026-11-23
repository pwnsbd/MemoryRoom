#include "AndroidMediaStorePhotoSource.h"
#include "MemoryRoom.h"

#include "Async/Async.h"
#include "Dom/JsonObject.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

#if PLATFORM_ANDROID
#include "AndroidPermissionCallbackProxy.h"
#include "AndroidPermissionFunctionLibrary.h"
#include "Android/AndroidApplication.h"
#include "Android/AndroidJava.h"
#include "Android/AndroidPlatformMisc.h"
#endif

#if PLATFORM_ANDROID
jclass FAndroidMediaStorePhotoSource::GetBridgeClass(JNIEnv* Env)
{
	if (!Env)
	{
		return nullptr;
	}

	jclass BridgeClass = FAndroidApplication::FindJavaClass("com/memoryroom/MemoryRoomMediaStoreBridge");
	if (CheckAndClearJavaException(Env, TEXT("FindJavaClass MemoryRoomMediaStoreBridge")) || !BridgeClass)
	{
		UE_LOG(LogMemoryRoom, Warning, TEXT("Android MediaStore bridge class was not found."));
		return nullptr;
	}
	return BridgeClass;
}

bool FAndroidMediaStorePhotoSource::CheckAndClearJavaException(JNIEnv* Env, const TCHAR* Context)
{
	if (!Env || !Env->ExceptionCheck())
	{
		return false;
	}

	UE_LOG(LogMemoryRoom, Warning, TEXT("Java exception during %s; describing and clearing."), Context ? Context : TEXT("JNI call"));
	Env->ExceptionDescribe();
	Env->ExceptionClear();
	return true;
}

bool FAndroidMediaStorePhotoSource::IsPermissionGranted()
{
	const FString Permission = FAndroidMisc::GetAndroidBuildVersion() >= 33 ? TEXT("android.permission.READ_MEDIA_IMAGES") : TEXT("android.permission.READ_EXTERNAL_STORAGE");
	return UAndroidPermissionFunctionLibrary::CheckPermission(Permission);
}
#endif

void FAndroidMediaStorePhotoSource::RequestPhotoPermissionAsync(FMemoryPhotoPermissionCallback Callback)
{
#if PLATFORM_ANDROID
	const FString Permission = FAndroidMisc::GetAndroidBuildVersion() >= 33 ? TEXT("android.permission.READ_MEDIA_IMAGES") : TEXT("android.permission.READ_EXTERNAL_STORAGE");
	if (UAndroidPermissionFunctionLibrary::CheckPermission(Permission))
	{
		AsyncTask(ENamedThreads::GameThread, [Callback]() { Callback.ExecuteIfBound(true, TEXT("Already granted")); });
		return;
	}

	TArray<FString> Permissions;
	Permissions.Add(Permission);
	UAndroidPermissionCallbackProxy* Proxy = UAndroidPermissionFunctionLibrary::AcquirePermissions(Permissions);
	Proxy->OnPermissionsGrantedDelegate.AddLambda([Callback](const TArray<FString>&, const TArray<bool>& GrantResults)
	{
		const bool bGranted = GrantResults.Num() > 0 && GrantResults[0];
		AsyncTask(ENamedThreads::GameThread, [Callback, bGranted]() { Callback.ExecuteIfBound(bGranted, bGranted ? TEXT("Granted") : TEXT("Permission denied. Allow photo access in headset app settings to show your gallery.")); });
	});
#else
	Callback.ExecuteIfBound(true, TEXT("No Android permission required"));
#endif
}

void FAndroidMediaStorePhotoSource::QueryPhotosAsync(int32 Offset, int32 Limit, FMemoryPhotoMetadataPageCallback Callback)
{
#if PLATFORM_ANDROID
	AsyncTask(ENamedThreads::AnyBackgroundThreadNormalTask, [Offset, Limit, Callback, BucketId = BucketFilter]()
	{
		TArray<FMemoryPhotoMetadata> Photos;
		int32 TotalCount = 0;
		bool bSuccess = false;
		JNIEnv* Env = FAndroidApplication::GetJavaEnv();
		if (Env && FAndroidMediaStorePhotoSource::IsPermissionGranted())
		{
			jclass BridgeClass = FAndroidMediaStorePhotoSource::GetBridgeClass(Env);
			if (BridgeClass)
			{
				jmethodID Method = Env->GetStaticMethodID(BridgeClass, "queryImagesJson", "(IILjava/lang/String;)Ljava/lang/String;");
				if (!FAndroidMediaStorePhotoSource::CheckAndClearJavaException(Env, TEXT("GetStaticMethodID queryImagesJson")) && Method)
				{
					jstring BucketString = Env->NewStringUTF(TCHAR_TO_UTF8(*BucketId));
					const bool bBucketStringException = FAndroidMediaStorePhotoSource::CheckAndClearJavaException(Env, TEXT("NewStringUTF MediaStore folder filter"));
					if (!BucketString || bBucketStringException)
					{
						Env->DeleteLocalRef(BridgeClass);
						AsyncTask(ENamedThreads::GameThread, [Callback]() { Callback.ExecuteIfBound(false, TArray<FMemoryPhotoMetadata>(), 0); });
						return;
					}
					jstring JsonString = static_cast<jstring>(Env->CallStaticObjectMethod(BridgeClass, Method, Offset, Limit, BucketString));
					Env->DeleteLocalRef(BucketString);
					if (!FAndroidMediaStorePhotoSource::CheckAndClearJavaException(Env, TEXT("CallStaticObjectMethod queryImagesJson")) && JsonString)
					{
						const char* Utf = Env->GetStringUTFChars(JsonString, nullptr);
						if (!FAndroidMediaStorePhotoSource::CheckAndClearJavaException(Env, TEXT("GetStringUTFChars queryImagesJson")) && Utf)
						{
							FString Json = UTF8_TO_TCHAR(Utf);
							Env->ReleaseStringUTFChars(JsonString, Utf);
							TSharedPtr<FJsonObject> Root;
							TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
							if (FJsonSerializer::Deserialize(Reader, Root) && Root.IsValid())
							{
								bSuccess = Root->GetBoolField(TEXT("success"));
								TotalCount = Root->GetIntegerField(TEXT("total"));
								const TArray<TSharedPtr<FJsonValue>>* Items = nullptr;
								if (Root->TryGetArrayField(TEXT("items"), Items))
								{
									for (const TSharedPtr<FJsonValue>& Value : *Items)
									{
										const TSharedPtr<FJsonObject>* Obj = nullptr;
										if (!Value->TryGetObject(Obj)) continue;
										FMemoryPhotoMetadata M;
										(*Obj)->TryGetStringField(TEXT("id"), M.StableMediaId);
										(*Obj)->TryGetStringField(TEXT("name"), M.DisplayName);
										(*Obj)->TryGetStringField(TEXT("uri"), M.ContentUri);
										M.Width = (*Obj)->GetIntegerField(TEXT("width")); M.Height = (*Obj)->GetIntegerField(TEXT("height")); M.Orientation = (*Obj)->GetIntegerField(TEXT("orientation"));
										(*Obj)->TryGetStringField(TEXT("mime"), M.MimeType);
										M.DateTaken = (int64)(*Obj)->GetNumberField(TEXT("dateTaken")); M.DateModified = (int64)(*Obj)->GetNumberField(TEXT("dateModified"));
										Photos.Add(M);
									}
								}
							}
						}
					}
					else
					{
						UE_LOG(LogMemoryRoom, Warning, TEXT("Android MediaStore query returned no JSON string."));
					}
					if (JsonString) Env->DeleteLocalRef(JsonString);
				}
				else
				{
					UE_LOG(LogMemoryRoom, Warning, TEXT("Android MediaStore queryImagesJson method was not found."));
				}
				Env->DeleteLocalRef(BridgeClass);
			}
		}
		AsyncTask(ENamedThreads::GameThread, [Callback, bSuccess, Photos = MoveTemp(Photos), TotalCount]() { Callback.ExecuteIfBound(bSuccess, Photos, TotalCount); });
	});
#else
	Callback.ExecuteIfBound(false, TArray<FMemoryPhotoMetadata>(), 0);
#endif
}

void FAndroidMediaStorePhotoSource::QueryAlbumsAsync(TFunction<void(bool bSuccess, TArray<FMemoryRoomAlbum> Albums)> Callback)
{
#if PLATFORM_ANDROID
	AsyncTask(ENamedThreads::AnyBackgroundThreadNormalTask, [Callback]()
	{
		TArray<FMemoryRoomAlbum> Albums;
		bool bSuccess = false;
		JNIEnv* Env = FAndroidApplication::GetJavaEnv();
		if (Env && FAndroidMediaStorePhotoSource::IsPermissionGranted())
		{
			jclass BridgeClass = FAndroidMediaStorePhotoSource::GetBridgeClass(Env);
			if (BridgeClass)
			{
				jmethodID Method = Env->GetStaticMethodID(BridgeClass, "queryAlbumsJson", "()Ljava/lang/String;");
				if (!FAndroidMediaStorePhotoSource::CheckAndClearJavaException(Env, TEXT("GetStaticMethodID queryAlbumsJson")) && Method)
				{
					jstring JsonString = static_cast<jstring>(Env->CallStaticObjectMethod(BridgeClass, Method));
					if (!FAndroidMediaStorePhotoSource::CheckAndClearJavaException(Env, TEXT("CallStaticObjectMethod queryAlbumsJson")) && JsonString)
					{
						const char* Utf = Env->GetStringUTFChars(JsonString, nullptr);
						if (!FAndroidMediaStorePhotoSource::CheckAndClearJavaException(Env, TEXT("GetStringUTFChars queryAlbumsJson")) && Utf)
						{
							FString Json = UTF8_TO_TCHAR(Utf);
							Env->ReleaseStringUTFChars(JsonString, Utf);
							TSharedPtr<FJsonObject> Root;
							TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
							if (FJsonSerializer::Deserialize(Reader, Root) && Root.IsValid())
							{
								bSuccess = Root->GetBoolField(TEXT("success"));
								const TArray<TSharedPtr<FJsonValue>>* Items = nullptr;
								if (Root->TryGetArrayField(TEXT("albums"), Items))
								{
									for (const TSharedPtr<FJsonValue>& Value : *Items)
									{
										const TSharedPtr<FJsonObject>* Obj = nullptr;
										if (!Value->TryGetObject(Obj)) continue;
										FMemoryRoomAlbum Album;
										(*Obj)->TryGetStringField(TEXT("id"), Album.Id);
										(*Obj)->TryGetStringField(TEXT("name"), Album.Name);
										(*Obj)->TryGetStringField(TEXT("parentId"), Album.ParentId);
										Album.Count = (*Obj)->GetIntegerField(TEXT("count"));
										if (!Album.Id.IsEmpty() && Album.Count > 0) Albums.Add(Album);
									}
								}
							}
						}
					}
					if (JsonString) Env->DeleteLocalRef(JsonString);
				}
				else
				{
					UE_LOG(LogMemoryRoom, Warning, TEXT("Android MediaStore queryAlbumsJson method was not found."));
				}
				Env->DeleteLocalRef(BridgeClass);
			}
		}
		AsyncTask(ENamedThreads::GameThread, [Callback, bSuccess, Albums = MoveTemp(Albums)]() mutable { Callback(bSuccess, MoveTemp(Albums)); });
	});
#else
	Callback(false, TArray<FMemoryRoomAlbum>());
#endif
}

void FAndroidMediaStorePhotoSource::LoadPhotoPixelsAsync(const FString& ContentUri, int32 MaximumDimension, int32 Generation, FMemoryPhotoPixelsCallback Callback)
{
#if PLATFORM_ANDROID
	AsyncTask(ENamedThreads::AnyBackgroundThreadNormalTask, [ContentUri, MaximumDimension, Generation, Callback]()
	{
		TArray<FColor> Pixels; int32 Width = 0; int32 Height = 0; bool bSuccess = false;
		JNIEnv* Env = FAndroidApplication::GetJavaEnv();
		if (Env)
		{
			jclass BridgeClass = FAndroidMediaStorePhotoSource::GetBridgeClass(Env);
			if (BridgeClass)
			{
				jmethodID Method = Env->GetStaticMethodID(BridgeClass, "loadImageArgb", "(Ljava/lang/String;I)[I");
				if (!FAndroidMediaStorePhotoSource::CheckAndClearJavaException(Env, TEXT("GetStaticMethodID loadImageArgb")) && Method)
				{
					jstring UriString = Env->NewStringUTF(TCHAR_TO_UTF8(*ContentUri));
					if (!FAndroidMediaStorePhotoSource::CheckAndClearJavaException(Env, TEXT("NewStringUTF content URI")) && UriString)
					{
						jintArray Data = static_cast<jintArray>(Env->CallStaticObjectMethod(BridgeClass, Method, UriString, FMath::Max(MaximumDimension, 1)));
						if (!FAndroidMediaStorePhotoSource::CheckAndClearJavaException(Env, TEXT("CallStaticObjectMethod loadImageArgb")) && Data)
						{
							jsize Count = Env->GetArrayLength(Data);
							if (!FAndroidMediaStorePhotoSource::CheckAndClearJavaException(Env, TEXT("GetArrayLength loadImageArgb")) && Count > 2)
							{
								TArray<jint> Raw; Raw.SetNumUninitialized(Count);
								Env->GetIntArrayRegion(Data, 0, Count, Raw.GetData());
								if (!FAndroidMediaStorePhotoSource::CheckAndClearJavaException(Env, TEXT("GetIntArrayRegion loadImageArgb")))
								{
									Width = Raw[0]; Height = Raw[1];
									if (Width > 0 && Height > 0 && Count == 2 + Width * Height)
									{
										Pixels.SetNumUninitialized(Width * Height);
										for (int32 i = 0; i < Pixels.Num(); ++i) { const uint32 A = (uint32)Raw[i+2]; Pixels[i] = FColor((A>>16)&255, (A>>8)&255, A&255, (A>>24)&255); }
										bSuccess = true;
									}
									else
									{
										UE_LOG(LogMemoryRoom, Warning, TEXT("Android MediaStore returned invalid image array dimensions %dx%d count %d."), Width, Height, Count);
									}
								}
							}
							else if (Count <= 2)
							{
								UE_LOG(LogMemoryRoom, Warning, TEXT("Android MediaStore returned an empty image array."));
							}
							Env->DeleteLocalRef(Data);
						}
						else
						{
							UE_LOG(LogMemoryRoom, Warning, TEXT("Android MediaStore returned no image pixel array for URI: %s"), *ContentUri);
						}
						Env->DeleteLocalRef(UriString);
					}
				}
				else
				{
					UE_LOG(LogMemoryRoom, Warning, TEXT("Android MediaStore loadImageArgb method was not found."));
				}
				Env->DeleteLocalRef(BridgeClass);
			}
		}
		AsyncTask(ENamedThreads::GameThread, [Callback, bSuccess, Pixels = MoveTemp(Pixels), Width, Height]() mutable { Callback.ExecuteIfBound(bSuccess, MoveTemp(Pixels), Width, Height); });
	});
#else
	Callback.ExecuteIfBound(false, TArray<FColor>(), 0, 0);
#endif
}
