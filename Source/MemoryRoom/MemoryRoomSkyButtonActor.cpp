// Fill out your copyright notice in the Description page of Project Settings.

#include "MemoryRoomSkyButtonActor.h"
#include "MemoryRoom.h"

#include "Camera/CameraComponent.h"
#include "Components/BoxComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Haptics/HapticFeedbackEffect_Base.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "AMemoryRoomManager.h"
#include "MemoryRoomMenuActor.h"
#include "MemoryRoomUIGlass.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
// Watch geometry, in centimetres before ButtonScale. The actor's +X is aimed at the headset
// every tick, so "in front" is +X and the layers stack backward along -X.
constexpr float FaceDiameter = 6.0f;
constexpr float FaceThickness = 0.6f;
constexpr float RingDiameter = 7.2f;
constexpr float RingThickness = 0.5f;
constexpr float RingOffsetX = -0.2f;
constexpr float CaseDiameter = 8.2f;
constexpr float CaseThickness = 0.4f;
constexpr float CaseOffsetX = -0.55f;
constexpr float GlyphBarWidth = 2.6f;
constexpr float GlyphBarHeight = 0.28f;
constexpr float GlyphBarDepth = 0.25f;
constexpr float GlyphBarPitch = 0.75f;
constexpr float GlyphOffsetX = 0.45f;
constexpr int32 GlyphBarCount = 3;
}

AMemoryRoomSkyButtonActor::AMemoryRoomSkyButtonActor()
{
	PrimaryActorTick.bCanEverTick = true;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);
	BuildComponents();
}

void AMemoryRoomSkyButtonActor::BuildComponents()
{
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CylinderFinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeFinder(TEXT("/Engine/BasicShapes/Cube.Cube"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> GlassFinder(MemoryRoomUI::GlassBaseMaterialPath());
	UStaticMesh* Cylinder = CylinderFinder.Succeeded() ? CylinderFinder.Object : nullptr;
	UStaticMesh* Cube = CubeFinder.Succeeded() ? CubeFinder.Object : nullptr;
	if (GlassFinder.Succeeded())
	{
		GlassBaseMaterial = GlassFinder.Object;
	}

	// The engine cylinder is 100cm tall and 100cm across. The pitch lays its axis along the
	// actor's +X, which is the axis Tick aims at the headset, so the flat face is what you
	// see. Each layer is a disc of a different diameter stacked behind the last.
	auto MakeDisc = [this, Cylinder](const TCHAR* Name, float Diameter, float Thickness, float OffsetX) -> UStaticMeshComponent*
	{
		UStaticMeshComponent* Disc = CreateDefaultSubobject<UStaticMeshComponent>(Name);
		Disc->SetupAttachment(SceneRoot);
		if (Cylinder)
		{
			Disc->SetStaticMesh(Cylinder);
		}
		Disc->SetRelativeLocation(FVector(OffsetX, 0.0f, 0.0f));
		Disc->SetRelativeScale3D(FVector(Diameter / 100.0f, Diameter / 100.0f, Thickness / 100.0f));
		Disc->SetRelativeRotation(FRotator(90.0f, 0.0f, 0.0f));
		Disc->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Disc->SetCastShadow(false);
		Disc->bCastDynamicShadow = false;
		Disc->bReceivesDecals = false;
		return Disc;
	};

	PuckMesh = MakeDisc(TEXT("PuckMesh"), FaceDiameter, FaceThickness, 0.0f);
	BezelRing = MakeDisc(TEXT("BezelRing"), RingDiameter, RingThickness, RingOffsetX);
	CaseMesh = MakeDisc(TEXT("CaseMesh"), CaseDiameter, CaseThickness, CaseOffsetX);

	// The menu glyph: three short gold bars stacked on the face. Built for every puck and
	// hidden on the time-of-day one, so both kinds share one component layout.
	for (int32 Index = 0; Index < GlyphBarCount; ++Index)
	{
		const FString Name = FString::Printf(TEXT("GlyphBar%d"), Index);
		UStaticMeshComponent* Bar = CreateDefaultSubobject<UStaticMeshComponent>(*Name);
		Bar->SetupAttachment(SceneRoot);
		if (Cube)
		{
			Bar->SetStaticMesh(Cube);
		}
		const float Z = (static_cast<float>(GlyphBarCount - 1) * 0.5f - Index) * GlyphBarPitch;
		Bar->SetRelativeLocation(FVector(GlyphOffsetX, 0.0f, Z));
		Bar->SetRelativeScale3D(FVector(GlyphBarDepth / 100.0f, GlyphBarWidth / 100.0f, GlyphBarHeight / 100.0f));
		Bar->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Bar->SetCastShadow(false);
		Bar->bCastDynamicShadow = false;
		Bar->bReceivesDecals = false;
		Bar->SetVisibility(false);
		GlyphBars.Add(Bar);
	}

	// Slightly larger than the case so the beam does not need pixel-perfect aim, and set
	// to block only Visibility so it never interferes with anything else in the room.
	Collision = CreateDefaultSubobject<UBoxComponent>(TEXT("Collision"));
	Collision->SetupAttachment(SceneRoot);
	// Thin along X (the face normal), a little wider than the case across Y and Z.
	Collision->SetBoxExtent(FVector(1.0f, 4.4f, 4.4f));
	Collision->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Collision->SetCollisionResponseToAllChannels(ECR_Ignore);
	Collision->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);

	Label = CreateDefaultSubobject<UTextRenderComponent>(TEXT("Label"));
	Label->SetupAttachment(SceneRoot);
	// Text renders readable from its local +X, which is already the axis pointed at the
	// headset, so it needs no rotation of its own. Sits below the case rather than across
	// it, so it never competes with the face for legibility.
	Label->SetRelativeLocation(FVector(0.6f, 0.0f, -5.4f));
	Label->SetHorizontalAlignment(EHTA_Center);
	Label->SetVerticalAlignment(EVRTA_TextCenter);
	Label->SetTextRenderColor(IdleLabelColor);
	Label->SetWorldSize(1.3f);
	Label->SetHorizSpacingAdjust(LabelTracking);
	Label->SetText(NSLOCTEXT("MemoryRoom", "SkyButtonDefaultLabel", "SUNSET"));
	Label->SetCastShadow(false);
}

void AMemoryRoomSkyButtonActor::BeginPlay()
{
	Super::BeginPlay();

	SceneRoot->SetRelativeScale3D(FVector(FMath::Max(ButtonScale, 0.05f)));
	bAttachedToController = AttachToPlayerController();

	if (Kind == EMemoryRoomWristButtonKind::TimeOfDay)
	{
		if (AMemoryRoomSkyDomeActor* SkyDome = ResolveSkyDome())
		{
			SkyDome->OnTimeOfDayChanged.AddDynamic(this, &AMemoryRoomSkyButtonActor::HandleTimeOfDayChanged);
		}
	}
	else if (Kind == EMemoryRoomWristButtonKind::Settings)
	{
		for (UStaticMeshComponent* Bar : GlyphBars)
		{
			if (Bar)
			{
				Bar->SetVisibility(true);
			}
		}
		if (Label)
		{
			Label->SetText(NSLOCTEXT("MemoryRoom", "SettingsButtonLabel", "SETTINGS"));
		}
	}
	else // ResetPlacements
	{
		// Only the middle bar, so the two dark-glass pucks still read as different buttons at
		// a glance rather than both showing the same three-bar menu glyph.
		for (int32 Index = 0; Index < GlyphBars.Num(); ++Index)
		{
			if (GlyphBars[Index])
			{
				GlyphBars[Index]->SetVisibility(Index == GlyphBars.Num() / 2);
			}
		}
		if (Label)
		{
			Label->SetText(NSLOCTEXT("MemoryRoom", "ResetPlacementsButtonLabel", "RESET"));
		}
	}

	// The manager restyles every puck from the theme right after spawning them; until then,
	// or if there is no manager, the struct defaults are the Hearth palette anyway.
	if (!bStyled)
	{
		RebuildStyleMaterials();
	}

	RefreshLabel();
	ApplyHoverVisuals();
}

bool AMemoryRoomSkyButtonActor::AttachToPlayerController()
{
	const UWorld* World = GetWorld();
	const APlayerController* PlayerController = World ? World->GetFirstPlayerController() : nullptr;
	APawn* Pawn = PlayerController ? PlayerController->GetPawn() : nullptr;
	if (!Pawn)
	{
		// Retried every half second, so log this once rather than every attempt.
		UE_CLOG(!bLoggedAttachFailure, LogMemoryRoom, Warning, TEXT("%s found no player pawn to attach to; retrying until one appears."), *GetName());
		bLoggedAttachFailure = true;
		return false;
	}

	TArray<USceneComponent*> SceneComponents;
	Pawn->GetComponents<USceneComponent>(SceneComponents);

	const FString Filter = AttachControllerNameFilter.IsNone() ? FString() : AttachControllerNameFilter.ToString();
	USceneComponent* BestMatch = nullptr;
	for (USceneComponent* Component : SceneComponents)
	{
		if (!Component || Component->IsA<UCameraComponent>())
		{
			continue;
		}

		const FString ComponentName = Component->GetName();
		const bool bLooksLikeController = ComponentName.Contains(TEXT("Controller"), ESearchCase::IgnoreCase)
			|| ComponentName.Contains(TEXT("Motion"), ESearchCase::IgnoreCase)
			|| ComponentName.Contains(TEXT("Hand"), ESearchCase::IgnoreCase);
		if (!bLooksLikeController)
		{
			continue;
		}

		if (Filter.IsEmpty() || ComponentName.Contains(Filter, ESearchCase::IgnoreCase))
		{
			BestMatch = Component;
			break;
		}
	}

	if (!BestMatch)
	{
		UE_CLOG(!bLoggedAttachFailure, LogMemoryRoom, Warning, TEXT("%s found no controller component matching '%s' on %s; retrying."), *GetName(), *Filter, *Pawn->GetName());
		bLoggedAttachFailure = true;
		return false;
	}

	AttachToComponent(BestMatch, FAttachmentTransformRules::SnapToTargetNotIncludingScale);
	SetActorRelativeLocation(AttachRelativeLocation);
	UE_LOG(LogMemoryRoom, Log, TEXT("%s attached to controller component %s."), *GetName(), *BestMatch->GetName());
	return true;
}

void AMemoryRoomSkyButtonActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	// The dome may not have built its gradient yet when this actor's BeginPlay runs, so
	// bind the disc material on the first tick where both exist.
	if (Kind == EMemoryRoomWristButtonKind::TimeOfDay)
	{
		EnsurePuckMaterial();
	}

	// The XR pawn may not have built its controller components yet when this actor spawns,
	// so keep trying at a low rate until one turns up rather than sitting in the level.
	if (!bAttachedToController)
	{
		AttachRetrySeconds += DeltaSeconds;
		if (AttachRetrySeconds >= 0.5f)
		{
			AttachRetrySeconds = 0.0f;
			bAttachedToController = AttachToPlayerController();
		}
	}

	TickHoverScale(DeltaSeconds);

	if (!bFacePlayer)
	{
		return;
	}

	const UWorld* World = GetWorld();
	const APlayerController* PlayerController = World ? World->GetFirstPlayerController() : nullptr;
	if (!PlayerController)
	{
		return;
	}

	FVector ViewLocation = FVector::ZeroVector;
	FRotator ViewRotation = FRotator::ZeroRotator;
	PlayerController->GetPlayerViewPoint(ViewLocation, ViewRotation);

	// Aim +X at the headset. FVector::Rotation() leaves roll at zero, so the label stays
	// upright no matter how the wrist is turned; building the rotation from the up axis
	// instead would leave roll arbitrary and spin the text around the view direction.
	const FVector ToViewer = ViewLocation - GetActorLocation();
	if (!ToViewer.IsNearlyZero())
	{
		SetActorRotation(ToViewer.Rotation());
	}
}

void AMemoryRoomSkyButtonActor::TickHoverScale(float DeltaSeconds)
{
	const float Target = bHovered ? 1.0f : 0.0f;
	if (FMath::IsNearlyEqual(HoverBlend, Target, 0.001f))
	{
		return;
	}

	const float Rate = FMath::Clamp(DeltaSeconds * FMath::Max(HoverAnimationSpeed, 1.0f), 0.0f, 1.0f);
	HoverBlend = FMath::Lerp(HoverBlend, Target, Rate);
	if (FMath::IsNearlyEqual(HoverBlend, Target, 0.005f))
	{
		HoverBlend = Target;
	}

	if (SceneRoot)
	{
		// Ease-out: the pop lands quickly and settles softly.
		const float Eased = 1.0f - FMath::Square(1.0f - HoverBlend);
		const float BaseScale = FMath::Max(ButtonScale, 0.05f);
		const float Multiplier = FMath::Lerp(1.0f, FMath::Max(HoverScaleMultiplier, 1.0f), Eased);
		SceneRoot->SetRelativeScale3D(FVector(BaseScale * Multiplier));
	}
}

bool AMemoryRoomSkyButtonActor::IsButtonComponent(const UPrimitiveComponent* Component) const
{
	return Component != nullptr && (Component == Collision || Component == PuckMesh || Component == BezelRing || Component == CaseMesh);
}

void AMemoryRoomSkyButtonActor::SetHovered(bool bNewHovered)
{
	if (bHovered == bNewHovered)
	{
		return;
	}

	bHovered = bNewHovered;
	ApplyHoverVisuals();
}

void AMemoryRoomSkyButtonActor::ApplyUIStyle(const FMemoryRoomWristUIStyle& NewStyle)
{
	Style = NewStyle;
	IdleLabelColor = Style.LabelColor.ToFColor(true);
	HoveredLabelColor = Style.HighlightColor.ToFColor(true);
	RebuildStyleMaterials();
	ApplyHoverVisuals();
}

void AMemoryRoomSkyButtonActor::RebuildStyleMaterials()
{
	bStyled = true;

	GlassMaterial = MemoryRoomUI::MakeGlassMaterial(GlassBaseMaterial, Style, this, bFlipGlassGradient);
	AccentMaterial = MemoryRoomUI::MakeSolidMaterial(GlassBaseMaterial, Style.AccentColor, this);
	HighlightMaterial = MemoryRoomUI::MakeSolidMaterial(GlassBaseMaterial, Style.HighlightColor, this);

	if (CaseMesh && GlassMaterial)
	{
		CaseMesh->SetMaterial(0, GlassMaterial);
	}
	// The time-of-day face is the live sky (bound in EnsurePuckMaterial); every other kind's
	// face is the same dark glass as the case, one step in front of it.
	if (PuckMesh && GlassMaterial && Kind != EMemoryRoomWristButtonKind::TimeOfDay)
	{
		PuckMesh->SetMaterial(0, GlassMaterial);
	}
	if (Label)
	{
		Label->SetHorizSpacingAdjust(LabelTracking);
	}
}

void AMemoryRoomSkyButtonActor::ApplyHoverVisuals()
{
	if (Label)
	{
		Label->SetTextRenderColor(bHovered ? HoveredLabelColor : IdleLabelColor);
	}

	// The ring and glyph are the hover cue: gold at rest, brighter gold under the pointer.
	// The scale pop is eased in over a few frames by TickHoverScale.
	UMaterialInstanceDynamic* Accent = bHovered ? HighlightMaterial.Get() : AccentMaterial.Get();
	if (!Accent)
	{
		return;
	}
	if (BezelRing)
	{
		BezelRing->SetMaterial(0, Accent);
	}
	for (UStaticMeshComponent* Bar : GlyphBars)
	{
		if (Bar)
		{
			Bar->SetMaterial(0, Accent);
		}
	}
}

void AMemoryRoomSkyButtonActor::EnsurePuckMaterial()
{
	if (PuckMaterialInstance || !PuckMesh)
	{
		return;
	}

	AMemoryRoomSkyDomeActor* SkyDome = ResolveSkyDome();
	if (!SkyDome)
	{
		return;
	}

	// Reuse the dome's own material and gradient so the disc is literally a small piece
	// of the current sky. The dome rewrites that texture in place as the light changes,
	// so this only needs binding once and then follows along on its own.
	UMaterialInterface* SkyMaterial = SkyDome->GetSkyDomeMaterial();
	UTexture2D* Gradient = SkyDome->GetGradientTexture();
	if (!SkyMaterial || !Gradient)
	{
		return;
	}

	PuckMaterialInstance = UMaterialInstanceDynamic::Create(SkyMaterial, this);
	if (!PuckMaterialInstance)
	{
		return;
	}

	const FName GradientParameter = SkyDome->GetGradientTextureParameterName();
	if (!GradientParameter.IsNone())
	{
		PuckMaterialInstance->SetTextureParameterValue(GradientParameter, Gradient);
	}
	PuckMesh->SetMaterial(0, PuckMaterialInstance);
}

AMemoryRoomSkyDomeActor* AMemoryRoomSkyButtonActor::ResolveSkyDome()
{
	if (CachedSkyDome.IsValid())
	{
		return CachedSkyDome.Get();
	}

	CachedSkyDome = AMemoryRoomSkyDomeActor::FindOrSpawn(GetWorld(), SkyDomeClass);
	return CachedSkyDome.Get();
}

AMemoryRoomMenuActor* AMemoryRoomSkyButtonActor::ResolveSettingsMenu()
{
	if (CachedSettingsMenu.IsValid())
	{
		return CachedSettingsMenu.Get();
	}
	if (UWorld* World = GetWorld())
	{
		for (TActorIterator<AMemoryRoomMenuActor> It(World); It;)
		{
			CachedSettingsMenu = *It;
			break;
		}
	}
	return CachedSettingsMenu.Get();
}

bool AMemoryRoomSkyButtonActor::PressButton()
{
	// Debounce: a controller that reports the trigger as held would otherwise call this
	// every frame and strobe the panel.
	const double Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	if (Now - LastPressSeconds < 0.35)
	{
		return true;
	}
	LastPressSeconds = Now;

	bool bHandled = false;

	if (Kind == EMemoryRoomWristButtonKind::TimeOfDay)
	{
		if (AMemoryRoomSkyDomeActor* SkyDome = ResolveSkyDome())
		{
			SkyDome->CycleTimeOfDay();
			RefreshLabel();
			bHandled = true;
		}
	}
	else if (Kind == EMemoryRoomWristButtonKind::Settings)
	{
		if (AMemoryRoomMenuActor* Menu = ResolveSettingsMenu())
		{
			Menu->ToggleMenu();
			bHandled = true;
		}
		else
		{
			UE_LOG(LogMemoryRoom, Warning, TEXT("%s found no settings menu actor to toggle."), *GetName());
		}
	}
	else // ResetPlacements
	{
		if (UWorld* World = GetWorld())
		{
			TActorIterator<AMemoryRoomManager> It(World);
			if (It)
			{
				It->ResetAllPlacedPhotos();
				bHandled = true;
			}
		}
	}

	if (bHandled && PressHapticEffect)
	{
		if (UWorld* World = GetWorld())
		{
			if (APlayerController* PlayerController = World->GetFirstPlayerController())
			{
				PlayerController->PlayHapticEffect(PressHapticEffect, EControllerHand::Left, 1.0f, false);
			}
		}
	}

	return bHandled;
}

void AMemoryRoomSkyButtonActor::HandleTimeOfDayChanged(EMemoryRoomTimeOfDay TimeOfDay)
{
	if (Label)
	{
		// Small caps with tracking, to match the board's labels.
		Label->SetText(FText::FromString(AMemoryRoomSkyDomeActor::GetDisplayNameForTimeOfDay(TimeOfDay).ToString().ToUpper()));
	}
}

void AMemoryRoomSkyButtonActor::RefreshLabel()
{
	if (!Label || Kind != EMemoryRoomWristButtonKind::TimeOfDay)
	{
		return;
	}

	if (const AMemoryRoomSkyDomeActor* SkyDome = ResolveSkyDome())
	{
		Label->SetText(FText::FromString(SkyDome->GetTimeOfDayDisplayName().ToString().ToUpper()));
	}
}

AMemoryRoomSkyButtonActor* AMemoryRoomSkyButtonActor::FindOrSpawn(UWorld* World, EMemoryRoomWristButtonKind Kind, const FVector& ExtraOffset, TSubclassOf<AMemoryRoomSkyButtonActor> ButtonClass)
{
	if (!World)
	{
		return nullptr;
	}

	for (TActorIterator<AMemoryRoomSkyButtonActor> It(World); It; ++It)
	{
		if (It->GetKind() == Kind)
		{
			return *It;
		}
	}

	UClass* ClassToSpawn = ButtonClass ? ButtonClass.Get() : AMemoryRoomSkyButtonActor::StaticClass();
	AMemoryRoomSkyButtonActor* Spawned = World->SpawnActorDeferred<AMemoryRoomSkyButtonActor>(ClassToSpawn, FTransform::Identity);
	if (Spawned)
	{
		Spawned->Kind = Kind;
		Spawned->AttachRelativeLocation += ExtraOffset;
		Spawned->FinishSpawning(FTransform::Identity);
	}
	return Spawned;
}
