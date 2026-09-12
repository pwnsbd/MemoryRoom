#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "MemoryPhotoSource.h"
#include "MemoryPhotoLibrarySubsystem.generated.h"

UCLASS()
class MEMORYROOM_API UMemoryPhotoLibrarySubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()
public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	IMemoryPhotoSource* GetPhotoSource() const { return PhotoSource.Get(); }

private:
	TUniquePtr<IMemoryPhotoSource> PhotoSource;
};
