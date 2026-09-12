#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MemoryPanoramaActor.generated.h"

class AMemoryPhotoActor;
class UStaticMeshComponent;
class UTextRenderComponent;
class UMaterialInterface;
class UMaterialInstanceDynamic;
class UTexture2D;

/** One unlit draw call for a monoscopic equirectangular photo. Shares the gallery texture. */
UCLASS()
class MEMORYROOM_API AMemoryPanoramaActor : public AActor
{

	GENERATED_BODY()
public:
	AMemoryPanoramaActor();
	bool Open(AMemoryPhotoActor* Photo);
	virtual void Tick(float DeltaSeconds) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	UPROPERTY() TObjectPtr<UStaticMeshComponent> Sphere;
	UPROPERTY() TObjectPtr<UTextRenderComponent> Help;
	UPROPERTY() TObjectPtr<UMaterialInterface> PanoramaMaterial;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> Material;
	UPROPERTY(Transient) TObjectPtr<UTexture2D> BoundTexture;
	TWeakObjectPtr<AMemoryPhotoActor> SourcePhoto;
	TArray<TPair<TWeakObjectPtr<AActor>, bool>> HiddenSceneActors;
	float Elapsed = 0.0f;
};
