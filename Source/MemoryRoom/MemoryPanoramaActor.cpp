#include "MemoryPanoramaActor.h"
#include "MemoryPhotoActor.h"
#include "MemoryRoomSkyDomeActor.h"
#include "MemoryRoom.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"

AMemoryPanoramaActor::AMemoryPanoramaActor()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PostUpdateWork;
	Sphere = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("PanoramaSphere"));
	SetRootComponent(Sphere);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Mesh(TEXT("/Game/MemoryRoomEnvironment/SM_PanoramaSphere.SM_PanoramaSphere"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> Mat(TEXT("/Game/MemoryRoomEnvironment/M_Panorama360.M_Panorama360"));
	if (Mesh.Succeeded()) Sphere->SetStaticMesh(Mesh.Object);
	if (Mat.Succeeded()) PanoramaMaterial = Mat.Object;
	Sphere->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Sphere->SetCastShadow(false);
	Sphere->bAffectDistanceFieldLighting = false;
	Sphere->SetRelativeScale3D(FVector(100.0f)); // 100m radius avoids false stereo depth.
	Sphere->SetVisibility(false);
	Help = CreateDefaultSubobject<UTextRenderComponent>(TEXT("PanoramaHelp"));
	Help->SetupAttachment(Sphere);
	Help->SetAbsolute(true, true, true);
	Help->SetHorizontalAlignment(EHTA_Center);
	Help->SetWorldSize(1.5f);
	Help->SetTextRenderColor(FColor::White);
	Help->SetText(NSLOCTEXT("MemoryRoom", "PanoramaLoading", "Loading 360 photo | Trigger: return"));
	Help->SetCollisionEnabled(ECollisionEnabled::NoCollision);
}

bool AMemoryPanoramaActor::Open(AMemoryPhotoActor* Photo)
{
	if (!Photo || !Photo->IsPanoramaCandidate() || !PanoramaMaterial) return false;
	SourcePhoto = Photo;
	Material = UMaterialInstanceDynamic::Create(PanoramaMaterial, this);
	if (!Material) return false;
	Sphere->SetMaterial(0, Material);
	for (TActorIterator<AActor> It(GetWorld()); It; ++It)
	{
		AActor* Actor = *It;
		if (Actor == this || Actor->IsA<APawn>() || Actor->IsA<AController>() || Actor->IsA<AMemoryRoomSkyDomeActor>()) continue;
		HiddenSceneActors.Emplace(Actor, Actor->IsHidden());
		Actor->SetActorHiddenInGame(true);
	}
	if (APlayerController* PC = GetWorld()->GetFirstPlayerController())
	{
		FVector Location; FRotator Rotation;
		PC->GetPlayerViewPoint(Location, Rotation);
		SetActorLocation(Location);
		SetActorRotation(FRotator(0.0f, Rotation.Yaw, 0.0f));
	}
	UE_LOG(LogMemoryRoom, Log, TEXT("Opened immersive 360 photo; trigger returns to gallery."));
	return true;
}

void AMemoryPanoramaActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	AMemoryPhotoActor* Photo = SourcePhoto.Get();
	if (!Photo || !Photo->IsExpanded()) { Destroy(); return; }
	Elapsed += DeltaSeconds;
	// Gallery visibility can be updated by its manager; keep it behind the panorama.
	for (const auto& Entry : HiddenSceneActors)
	{
		if (AActor* Actor = Entry.Key.Get(); Actor && !Actor->IsHidden()) Actor->SetActorHiddenInGame(true);
	}
	UTexture2D* Texture = Photo->GetRuntimeTexture();
	if (Texture && Texture != BoundTexture)
	{
		BoundTexture = Texture;
		Material->SetTextureParameterValue(TEXT("PanoramaTexture"), Texture);
		Sphere->SetVisibility(true);
		Help->SetText(NSLOCTEXT("MemoryRoom", "PanoramaReturn", "360 photo | Trigger: return"));
	}
	Help->SetVisibility(!BoundTexture || Elapsed < 6.0f);
	if (!BoundTexture && Elapsed > 20.0f)
	{
		UE_LOG(LogMemoryRoom, Warning, TEXT("360 photo did not load; returned to flat view."));
		Destroy(); return;
	}
	if (APlayerController* PC = GetWorld()->GetFirstPlayerController())
	{
		FVector Location; FRotator Rotation;
		PC->GetPlayerViewPoint(Location, Rotation);
		SetActorLocation(Location); // Translation never introduces false panorama parallax.
		Help->SetWorldLocation(Location + Rotation.RotateVector(FVector(65, 0, -16)));
		Help->SetWorldRotation(Rotation + FRotator(0, 180, 0));
	}
}

void AMemoryPanoramaActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	for (const auto& Entry : HiddenSceneActors)
	{
		if (AActor* Actor = Entry.Key.Get()) Actor->SetActorHiddenInGame(Entry.Value);
	}
	HiddenSceneActors.Reset();
	Super::EndPlay(EndPlayReason);
}
