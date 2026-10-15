#include "MemoryRoomMenuActor.h"
#include "MemoryRoom.h"

#include "Camera/CameraComponent.h"
#include "Components/BoxComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/StaticMesh.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Haptics/HapticFeedbackEffect_Base.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "MemoryRoomSkyButtonActor.h"
#include "MemoryRoomUIGlass.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
// Panel layout, in centimetres before BoardScale.
//
// The old board was one 12-row column: 26 cm of pitch against boxes 21.8 cm tall, so
// neighbouring press targets sat about a centimetre apart, and the whole thing stood over a
// metre high. This lays the same controls out as a desk-style grid instead - two columns of
// setting tiles over a block of action buttons - and every gap below is measured between
// press targets, not between labels.
constexpr float PressHalfX = 1.2f;          // press-box depth, toward the viewer
constexpr float PressHalfY = 2.0f;          // 4 cm square press targets
constexpr float PressHalfZ = 2.0f;
constexpr float StepOffsetY = 5.0f;         // minus and plus, either side of the value
constexpr float TileHalfY = StepOffsetY + PressHalfY;
constexpr float ColumnPitchY = 22.0f;       // 8 cm between the columns' nearest targets
constexpr float SettingRowPitchZ = 10.0f;   // 6 cm between stacked setting targets
constexpr float ActionRowPitchZ = 9.0f;     // 5 cm between stacked action targets
constexpr float SettingsToActionsZ = 6.0f;  // holds the actions clear of the settings block
constexpr float LabelOffsetZ = 4.8f;        // the setting name sits above its own buttons
constexpr float PanelMargin = 4.0f;
constexpr float PanelThickness = 1.2f;      // the glass slab
constexpr float PanelDepthX = 0.4f;         // slab centre; its front face is toward -X
constexpr float WidgetDepthX = -2.2f;       // tiles float just in front of the slab
constexpr float TileThickness = 0.6f;
constexpr float RuleDepthX = -1.4f;
constexpr float RuleThickness = 0.3f;
constexpr float RuleHeight = 0.22f;         // hairline
constexpr float TextDepthX = WidgetDepthX - 0.6f;
constexpr float TilePopLiftX = 0.6f;        // a hot tile comes this much closer
constexpr float LabelSize = 2.0f;
constexpr float ValueSize = 2.6f;
constexpr float GlyphSize = 3.2f;
constexpr float ActionSize = 2.2f;
constexpr float TitleSize = 4.2f;
constexpr float StatusSize = 2.4f;
constexpr int32 SettingColumns = 2;
constexpr int32 ActionColumns = 2;

float EaseOutCubic(float T)
{
	const float Clamped = FMath::Clamp(T, 0.0f, 1.0f);
	return 1.0f - FMath::Pow(1.0f - Clamped, 3.0f);
}
}

AMemoryRoomMenuActor::AMemoryRoomMenuActor()
{
	PrimaryActorTick.bCanEverTick = true;
	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);
	BuildBoard();
	ApplyBoardScale();
	SetActorHiddenInGame(true);
	SetActorEnableCollision(false);
}

void AMemoryRoomMenuActor::BuildBoard()
{
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeFinder(TEXT("/Engine/BasicShapes/Cube.Cube"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> GlassFinder(MemoryRoomUI::GlassBaseMaterialPath());
	UStaticMesh* Cube = CubeFinder.Succeeded() ? CubeFinder.Object : nullptr;
	if (GlassFinder.Succeeded())
	{
		GlassBaseMaterial = GlassFinder.Object;
	}

	const int32 SettingCount = static_cast<int32>(EMemoryRoomUserSetting::Count);
	const EMemoryRoomMenuAction Actions[] = { EMemoryRoomMenuAction::ResetView, EMemoryRoomMenuAction::JumpToNewest, EMemoryRoomMenuAction::ReloadPhotos, EMemoryRoomMenuAction::ChoosePhotoSource, EMemoryRoomMenuAction::Close };
	const int32 ActionCount = UE_ARRAY_COUNT(Actions);

	const int32 SettingRows = FMath::DivideAndRoundUp(SettingCount, SettingColumns);
	const int32 ActionRows = FMath::DivideAndRoundUp(ActionCount, ActionColumns);

	// Lay the panel out with the top setting row at Z = 0, measure what that comes to, then
	// shift the lot so the actor's own origin sits at the BOTTOM of the finished panel, not
	// its centre. TickFacePlayer keeps this actor's Z axis pointing world-up (Roll stays 0),
	// so with the origin at the base the panel grows upward from wherever it is attached -
	// the controller - instead of straddling it top and bottom.
	const float SettingsBottomZ = -(SettingRows - 1) * SettingRowPitchZ - PressHalfZ;
	const float ActionsTopZ = SettingsBottomZ - SettingsToActionsZ - PressHalfZ;
	const float ActionsBottomZ = ActionsTopZ - (ActionRows - 1) * ActionRowPitchZ - PressHalfZ;
	const float TitleZ = LabelOffsetZ + LabelSize * 0.5f + TitleSize * 1.1f;
	const float TitleRuleZ = TitleZ - TitleSize * 0.8f;
	const float SectionRuleZ = SettingsBottomZ - SettingsToActionsZ * 0.5f;
	const float StatusZ = ActionsBottomZ - StatusSize * 1.8f;
	const float ContentTopZ = TitleZ + TitleSize * 0.5f;
	const float ContentBottomZ = StatusZ - StatusSize * 0.5f;
	const float BaseAnchorShiftZ = -(ContentBottomZ - PanelMargin);

	const float PanelWidth = ColumnPitchY + 2.0f * TileHalfY + 2.0f * PanelMargin;
	const float PanelHeight = (ContentTopZ - ContentBottomZ) + 2.0f * PanelMargin;
	const float RuleWidth = PanelWidth - 2.0f * PanelMargin - 6.0f;

	// Column 0 is the viewer's left: the widgets carry a 180-degree yaw to face the player,
	// which makes +Y the reading direction and so the viewer's right.
	auto ColumnY = [](int32 Column, int32 ColumnCount)
	{
		return (Column - (ColumnCount - 1) * 0.5f) * ColumnPitchY;
	};

	auto MakeSlab = [this, Cube](const TCHAR* Name, float X, float Y, float Z, float Depth, float Width, float Height) -> UStaticMeshComponent*
	{
		UStaticMeshComponent* Slab = CreateDefaultSubobject<UStaticMeshComponent>(Name);
		Slab->SetupAttachment(SceneRoot);
		Slab->SetStaticMesh(Cube);
		Slab->SetRelativeLocation(FVector(X, Y, Z));
		Slab->SetRelativeScale3D(FVector(Depth / 100.0f, Width / 100.0f, Height / 100.0f));
		Slab->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Slab->SetCastShadow(false);
		Slab->bCastDynamicShadow = false;
		Slab->bReceivesDecals = false;
		return Slab;
	};

	// Dark backing board (a little bigger, so an edge shows), then the cream page in front of
	// it, then the clip across the top.
	const float ContentMidZ = (ContentTopZ + ContentBottomZ) * 0.5f + BaseAnchorShiftZ;
	BackgroundPanel = MakeSlab(TEXT("BackgroundPanel"), PanelDepthX, 0.0f, ContentMidZ, PanelThickness, PanelWidth + 5.0f, PanelHeight + 5.0f);
	PaperPanel = MakeSlab(TEXT("PaperPanel"), PanelDepthX - 0.9f, 0.0f, ContentMidZ, 0.5f, PanelWidth, PanelHeight);
	ClipPanel = MakeSlab(TEXT("ClipPanel"), PanelDepthX - 1.7f, 0.0f, ContentTopZ + PanelMargin + BaseAnchorShiftZ + 0.5f, 1.1f, 15.0f, 2.4f);

	TitleRule = MakeSlab(TEXT("TitleRule"), RuleDepthX, 0.0f, TitleRuleZ + BaseAnchorShiftZ, RuleThickness, RuleWidth, RuleHeight);
	SectionRule = MakeSlab(TEXT("SectionRule"), RuleDepthX, 0.0f, SectionRuleZ + BaseAnchorShiftZ, RuleThickness, RuleWidth * 0.55f, RuleHeight);

	// The page and board are unlit, but the text render components use the engine's stock
	// (lit) font material - a small warm light on the panel keeps the dark ink from going flat
	// black at night and gives the paper a faint reading-lamp warmth.
	BoardLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("BoardLight"));
	BoardLight->SetupAttachment(SceneRoot);
	BoardLight->SetRelativeLocation(FVector(TextDepthX - 10.0f, 0.0f, ContentMidZ));
	BoardLight->SetMobility(EComponentMobility::Movable);
	BoardLight->SetCastShadows(false);
	BoardLight->bAffectsWorld = true;
	BoardLight->SetIntensityUnits(ELightUnits::Unitless);
	BoardLight->SetAttenuationRadius(90.0f);
	BoardLight->SetIntensity(5.0f);
	BoardLight->SetLightColor(FLinearColor(1.0f, 0.95f, 0.86f));

	auto MakeBox = [this](const FString& Name, float Y, float Z, float HalfY) -> UBoxComponent*
	{
		UBoxComponent* Box = CreateDefaultSubobject<UBoxComponent>(*Name);
		Box->SetupAttachment(SceneRoot);
		Box->SetRelativeLocation(FVector(WidgetDepthX - PressHalfX, Y, Z));
		Box->SetBoxExtent(FVector(PressHalfX, HalfY, PressHalfZ));
		Box->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		Box->SetCollisionResponseToAllChannels(ECR_Ignore);
		Box->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
		return Box;
	};
	auto MakePanel = [MakeSlab](const FString& Name, float Y, float Z, float HalfY) -> UStaticMeshComponent*
	{
		return MakeSlab(*Name, WidgetDepthX, Y, Z, TileThickness, HalfY * 2.0f, PressHalfZ * 2.0f);
	};
	auto MakeText = [this](const FString& Name, float Y, float Z, float Size, float Tracking) -> UTextRenderComponent*
	{
		UTextRenderComponent* Text = CreateDefaultSubobject<UTextRenderComponent>(*Name);
		Text->SetupAttachment(SceneRoot);
		Text->SetRelativeLocation(FVector(TextDepthX, Y, Z));
		Text->SetRelativeRotation(FRotator(0.0f, 180.0f, 0.0f));
		Text->SetHorizontalAlignment(EHTA_Center);
		Text->SetVerticalAlignment(EVRTA_TextCenter);
		Text->SetTextRenderColor(LabelColor);
		Text->SetWorldSize(Size);
		Text->SetHorizSpacingAdjust(Tracking);
		Text->SetCastShadow(false);
		return Text;
	};

	TitleText = MakeText(TEXT("TitleText"), 0.0f, TitleZ + BaseAnchorShiftZ, TitleSize, LabelTracking * 2.0f);
	TitleText->SetText(FText::FromString(TEXT("SETTINGS")));
	TitleText->SetTextRenderColor(TitleColor);

	StatusText = MakeText(TEXT("StatusText"), 0.0f, StatusZ + BaseAnchorShiftZ, StatusSize, LabelTracking * 0.5f);
	StatusText->SetTextRenderColor(DimLabelColor);

	for (int32 i = 0; i < SettingCount; ++i)
	{
		const float TileY = ColumnY(i % SettingColumns, SettingColumns);
		const float TileZ = -(i / SettingColumns) * SettingRowPitchZ + BaseAnchorShiftZ;
		const FString Tag = FString::Printf(TEXT("Set%d"), i);

		FMemoryRoomMenuRow Row;
		Row.bIsSetting = true;
		Row.Setting = static_cast<EMemoryRoomUserSetting>(i);
		Row.TileHalfY = PressHalfY;
		Row.MinusBox = MakeBox(Tag + TEXT("Minus"), TileY - StepOffsetY, TileZ, PressHalfY);
		Row.PlusBox = MakeBox(Tag + TEXT("Plus"), TileY + StepOffsetY, TileZ, PressHalfY);
		Row.MinusPanel = MakePanel(Tag + TEXT("MinusP"), TileY - StepOffsetY, TileZ, PressHalfY);
		Row.PlusPanel = MakePanel(Tag + TEXT("PlusP"), TileY + StepOffsetY, TileZ, PressHalfY);
		Row.MinusGlyph = MakeText(Tag + TEXT("MinusG"), TileY - StepOffsetY, TileZ, GlyphSize, 0.0f);
		Row.MinusGlyph->SetText(FText::FromString(TEXT("<")));
		Row.PlusGlyph = MakeText(Tag + TEXT("PlusG"), TileY + StepOffsetY, TileZ, GlyphSize, 0.0f);
		Row.PlusGlyph->SetText(FText::FromString(TEXT(">")));
		Row.Label = MakeText(Tag + TEXT("Label"), TileY, TileZ + LabelOffsetZ, LabelSize, LabelTracking);
		// The value sits in the clear space between the two arrows, so the arrow you want is
		// always the one on the side of the number you are reading.
		Row.ValueText = MakeText(Tag + TEXT("Value"), TileY, TileZ, ValueSize, 0.0f);
		Rows.Add(Row);
	}

	for (int32 i = 0; i < ActionCount; ++i)
	{
		const float TileY = ColumnY(i % ActionColumns, ActionColumns);
		const float TileZ = ActionsTopZ - (i / ActionColumns) * ActionRowPitchZ + BaseAnchorShiftZ;
		const FString Tag = FString::Printf(TEXT("Act%d"), i);

		FMemoryRoomMenuRow Row;
		Row.bIsSetting = false;
		Row.Action = Actions[i];
		Row.TileHalfY = TileHalfY;
		Row.MinusBox = MakeBox(Tag + TEXT("Press"), TileY, TileZ, TileHalfY);
		Row.MinusPanel = MakePanel(Tag + TEXT("P"), TileY, TileZ, TileHalfY);
		Row.Label = MakeText(Tag + TEXT("Label"), TileY, TileZ, ActionSize, LabelTracking);
		Rows.Add(Row);
	}

	// Directory overlay uses the same glass, text, and visibility-ray targets as the settings
	// page. It is built in-place so the existing dual-hand pointer continues to own input.
	auto BrowserSlab = [this, Cube](const TCHAR* Name, FVector Loc, FVector Scale) -> UStaticMeshComponent*
	{
		UStaticMeshComponent* C = CreateDefaultSubobject<UStaticMeshComponent>(Name);
		C->SetupAttachment(SceneRoot); C->SetStaticMesh(Cube); C->SetRelativeLocation(Loc); C->SetRelativeScale3D(Scale);
		C->SetCollisionEnabled(ECollisionEnabled::NoCollision); C->SetCastShadow(false); return C;
	};
	auto BrowserTextComponent = [this](const TCHAR* Name, FVector Loc, float Size) -> UTextRenderComponent*
	{
		UTextRenderComponent* C = CreateDefaultSubobject<UTextRenderComponent>(Name);
		C->SetupAttachment(SceneRoot); C->SetRelativeLocation(Loc); C->SetRelativeRotation(FRotator(0,180,0));
		C->SetHorizontalAlignment(EHTA_Center); C->SetVerticalAlignment(EVRTA_TextCenter); C->SetWorldSize(Size); C->SetTextRenderColor(LabelColor); return C;
	};
	auto BrowserBox = [this](const TCHAR* Name, FVector Loc, FVector Extent) -> UBoxComponent*
	{
		UBoxComponent* C = CreateDefaultSubobject<UBoxComponent>(Name); C->SetupAttachment(SceneRoot); C->SetRelativeLocation(Loc);
		C->SetBoxExtent(Extent); C->SetCollisionEnabled(ECollisionEnabled::QueryOnly); C->SetCollisionResponseToAllChannels(ECR_Ignore); C->SetCollisionResponseToChannel(ECC_Visibility,ECR_Block); return C;
	};
	BrowserPanel = BrowserSlab(TEXT("FolderBrowserPanel"), FVector(0.4f,0,-11), FVector(0.018f,0.35f,0.45f));
	BrowserTitle = BrowserTextComponent(TEXT("FolderBrowserTitle"), FVector(-2.8f,0,0), 4.0f);
	BrowserTitle->SetText(FText::FromString(TEXT("PHOTO FOLDERS")));
	BrowserPathText = BrowserTextComponent(TEXT("FolderBrowserPath"), FVector(-2.8f,0,-5), 2.0f);
	auto MakeBrowserButton = [&](const TCHAR* Name, float Y, float Z, float HalfY, float HalfZ, float TextSize,
		TObjectPtr<UBoxComponent>& Box, TObjectPtr<UStaticMeshComponent>& Panel, TObjectPtr<UTextRenderComponent>& Label, const TCHAR* Caption)
	{
		Box = BrowserBox(*FString(Name).Append(TEXT("Box")), FVector(-3.4f,Y,Z), FVector(1.2f,HalfY,HalfZ));
		Panel = BrowserSlab(*FString(Name).Append(TEXT("Panel")), FVector(-2.2f,Y,Z), FVector(0.006f,HalfY*0.02f,HalfZ*0.02f));
		Label = BrowserTextComponent(*FString(Name).Append(TEXT("Text")), FVector(-3.0f,Y,Z), TextSize); Label->SetText(FText::FromString(Caption));
	};
	MakeBrowserButton(TEXT("BrowserBack"),-13,7,4,2,1.7f,BrowserBackBox,BrowserBackPanel,BrowserBackText,TEXT("BACK"));
	MakeBrowserButton(TEXT("BrowserUp"),-4,7,4,2,1.7f,BrowserUpBox,BrowserUpPanel,BrowserUpText,TEXT("UP"));
	MakeBrowserButton(TEXT("BrowserPrev"),8,7,3,2,1.7f,BrowserPrevBox,BrowserPrevPanel,BrowserPrevText,TEXT("<"));
	MakeBrowserButton(TEXT("BrowserNext"),13,7,3,2,1.7f,BrowserNextBox,BrowserNextPanel,BrowserNextText,TEXT(">"));
	MakeBrowserButton(TEXT("BrowserUse"),0,-24,12,2.2f,2.0f,BrowserUseBox,BrowserUsePanel,BrowserUseText,TEXT("USE THIS FOLDER"));
	for (int32 i=0;i<6;++i)
	{
		const int32 Col=i%3, Row=i/3; const float Y=(Col-1)*9.0f, Z=0.0f-Row*10.0f;
		FMemoryRoomFolderTile Tile;
		Tile.Box=BrowserBox(*FString::Printf(TEXT("FolderTile%dBox"),i),FVector(-3.4f,Y,Z),FVector(1.2f,3.7f,4.0f));
		Tile.Panel=BrowserSlab(*FString::Printf(TEXT("FolderTile%dPanel"),i),FVector(-2.2f,Y,Z),FVector(0.006f,0.074f,0.08f));
		Tile.Text=BrowserTextComponent(*FString::Printf(TEXT("FolderTile%dText"),i),FVector(-3.0f,Y,Z),1.6f);
		FolderTiles.Add(Tile);
	}
	SetFolderBrowserVisible(false);
}

void AMemoryRoomMenuActor::ApplyBoardScale()
{
	if (SceneRoot)
	{
		// The board scales up from 82% as it opens, so it reads as arriving rather than
		// appearing. Fully open is exactly BoardScale.
		const float Grow = FMath::Lerp(0.82f, 1.0f, EaseOutCubic(OpenProgress));
		SceneRoot->SetRelativeScale3D(FVector(FMath::Max(BoardScale, 0.05f) * Grow));
	}
}

void AMemoryRoomMenuActor::BeginPlay()
{
	Super::BeginPlay();
	ResolveManager();

	// Summoned by the wrist Settings puck (AMemoryRoomSkyButtonActor -> ToggleMenu). Starts
	// closed and hidden.
	bMenuOpen = false;
	OpenProgress = 0.0f;
	ApplyBoardScale();
	SetActorHiddenInGame(true);
	SetActorEnableCollision(false);

	if (MemoryRoomManager)
	{
		MemoryRoomManager->OnUserSettingsChanged.AddDynamic(this, &AMemoryRoomMenuActor::HandleSettingsChanged);
		MemoryRoomManager->OnPhotoLibraryReloadStarted.AddDynamic(this, &AMemoryRoomMenuActor::HandleReloadStarted);
		MemoryRoomManager->OnPhotoLibraryReloadCompleted.AddDynamic(this, &AMemoryRoomMenuActor::HandleReloadCompleted);
	}
	// Manager actors may spawn this board before restoring SaveGame settings. Check on the
	// next tick so a returning user with a saved source does not see onboarding again.
	if (GetWorld())
	{
		GetWorld()->GetTimerManager().SetTimerForNextTick(FTimerDelegate::CreateWeakLambda(this, [this]()
		{
			if (MemoryRoomManager && !MemoryRoomManager->HasPhotoSourceChoice())
			{
				StatusMessage = TEXT("Choose a photo folder below to begin.");
				OpenMenu();
			}
		}));
	}

	if (!bStyled)
	{
		RebuildStyleMaterials();
	}
	RefreshText();
}

void AMemoryRoomMenuActor::PositionMenuInFrontOfView()
{
	const APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
	if (!PC)
	{
		return;
	}
	FVector ViewLocation; FRotator ViewRotation;
	PC->GetPlayerViewPoint(ViewLocation, ViewRotation);

	FVector Forward = ViewRotation.Vector();
	Forward.Z = 0.0f;
	if (!Forward.Normalize())
	{
		Forward = FVector::ForwardVector;
	}

	const FVector Location = ViewLocation + Forward * FMath::Max(SummonDistanceCm, 30.0f) + FVector(0.0f, 0.0f, SummonHeightOffsetCm);

	// The board's content is on its -X face (see BuildBoard), so +X must point away from the
	// viewer - i.e. along Forward.
	FRotator Rotation = Forward.Rotation();
	Rotation.Pitch = -MenuPitchDegrees;
	Rotation.Roll = 0.0f;
	SetActorLocationAndRotation(Location, Rotation);
}

void AMemoryRoomMenuActor::ResolveManager()
{
	if (MemoryRoomManager || !GetWorld()) return;
	for (TActorIterator<AMemoryRoomManager> It(GetWorld()); It;) { MemoryRoomManager = *It; break; }
}

USceneComponent* AMemoryRoomMenuActor::FindWristComponent() const
{
	const UWorld* World = GetWorld();
	const APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
	APawn* Pawn = PC ? PC->GetPawn() : nullptr;
	if (!Pawn)
	{
		return nullptr;
	}

	TArray<USceneComponent*> Components;
	Pawn->GetComponents<USceneComponent>(Components);
	const FString Filter = AttachControllerNameFilter.IsNone() ? FString() : AttachControllerNameFilter.ToString();
	for (USceneComponent* Component : Components)
	{
		if (!Component || Component->IsA<UCameraComponent>())
		{
			continue;
		}
		const FString Name = Component->GetName();
		const bool bController = Name.Contains(TEXT("Controller"), ESearchCase::IgnoreCase)
			|| Name.Contains(TEXT("Motion"), ESearchCase::IgnoreCase)
			|| Name.Contains(TEXT("Hand"), ESearchCase::IgnoreCase);
		if (bController && (Filter.IsEmpty() || Name.Contains(Filter, ESearchCase::IgnoreCase)))
		{
			return Component;
		}
	}
	return nullptr;
}

bool AMemoryRoomMenuActor::AttachToLeftController()
{
	USceneComponent* Wrist = FindWristComponent();
	if (!Wrist)
	{
		return false;
	}

	AttachToComponent(Wrist, FAttachmentTransformRules::SnapToTargetNotIncludingScale);
	SetActorRelativeLocation(AttachRelativeLocation);
	// Rotation is not set here: the reveal roll can land anywhere from RevealRollDegrees to
	// 180, so no single relative rotation reads correctly across that whole range. Tick faces
	// the panel at the headset every frame instead - see TickFacePlayer - the same way the
	// wrist puck already does.
	return true;
}

float AMemoryRoomMenuActor::GetWristRollDegrees(bool& bOutValid) const
{
	bOutValid = false;

	const USceneComponent* Wrist = WristComponent.Get();
	const UWorld* World = GetWorld();
	const APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
	if (!Wrist || !PC)
	{
		return 0.0f;
	}

	FVector ViewLocation = FVector::ZeroVector;
	FRotator ViewRotation = FRotator::ZeroRotator;
	PC->GetPlayerViewPoint(ViewLocation, ViewRotation);

	// Roll happens around the forearm. Measured on the device rather than assumed: the grip
	// pose's +X points up out of the top of the controller and +Z points back toward the
	// player, so the direction the controller actually aims - and the axis the wrist turns
	// about - is -Z, which is GetUpVector negated. Flattening "toward the head" into the
	// plane across that axis leaves a direction only the roll can move, so where the arm is
	// held and which way the player is facing both drop out of the measurement.
	const FVector Forearm = (-Wrist->GetUpVector()).GetSafeNormal();
	FVector ToHead = ViewLocation - Wrist->GetComponentLocation();
	ToHead -= Forearm * FVector::DotProduct(ToHead, Forearm);
	if (!ToHead.Normalize() || Forearm.IsNearlyZero())
	{
		return 0.0f;
	}

	// +X is the axis that points up out of the controller, which is the back of the wrist -
	// the face a watch sits on. Measuring from there makes 0 degrees the watch-check pose
	// exactly, and +/-180 the palm-toward-the-face pose at the other end of the roll.
	const FVector WatchFace = Wrist->GetForwardVector().GetSafeNormal();
	const float Cosine = FVector::DotProduct(WatchFace, ToHead);
	// Cross the other way round from the obvious order, so positive is the roll that turns
	// the top of the controller away from the body rather than toward it. That is the turn
	// the panel comes up on.
	const float Sine = FVector::DotProduct(FVector::CrossProduct(ToHead, WatchFace), Forearm);

	bOutValid = true;
	return FMath::RadiansToDegrees(FMath::Atan2(Sine, Cosine));
}

void AMemoryRoomMenuActor::TickWristReveal(float DeltaSeconds)
{
	if (!bRevealByWristRotation)
	{
		return;
	}

	if (!WristComponent.IsValid())
	{
		return;
	}

	bool bValid = false;
	const float RollDegrees = GetWristRollDegrees(bValid);
	if (!bValid)
	{
		return;
	}

	// The watch-check band. Held shut on purpose: this is the twist that reads the wrist
	// pucks, and it is the one the forearm has least room to make, so it is the wrong one to
	// hang a menu on. A panel the player opened by hand is left alone.
	if (FMath::Abs(RollDegrees) <= WatchCheckRollDegrees)
	{
		RevealHeldSeconds = 0.0f;
		if (bMenuOpen && bWristGestureOwnsMenu)
		{
			CloseMenu();
		}
		return;
	}

	// The reveal band, at the far end of the roll the other way. Signed, so only the turn
	// away from the watch check counts - rolling hard into the watch-check side does nothing.
	const float SignedRoll = bInvertRevealRollDirection ? -RollDegrees : RollDegrees;
	if (SignedRoll >= RevealRollDegrees)
	{
		RevealHeldSeconds += DeltaSeconds;
		if (RevealHeldSeconds >= RevealHoldSeconds && !bMenuOpen)
		{
			OpenMenu();
			bWristGestureOwnsMenu = true;
		}
		return;
	}

	// Everything between the two bands holds whatever the panel is already doing. That gap
	// is the hysteresis, and it is also what leaves the settings puck free to open and close
	// the panel by hand without the wrist immediately overruling it.
	RevealHeldSeconds = 0.0f;
}

FString AMemoryRoomMenuActor::GetWristRevealDebugText() const
{
	if (!bRevealByWristRotation)
	{
		return TEXT("wrist reveal off");
	}
	if (!WristComponent.IsValid())
	{
		return TEXT("no wrist found");
	}

	bool bValid = false;
	const float RollDegrees = GetWristRollDegrees(bValid);
	if (!bValid)
	{
		return TEXT("roll unavailable");
	}

	const float SignedRoll = bInvertRevealRollDirection ? -RollDegrees : RollDegrees;
	const TCHAR* Band = TEXT("NEUTRAL (holds)");
	if (FMath::Abs(RollDegrees) <= WatchCheckRollDegrees)
	{
		Band = TEXT("WATCH-CHECK (shut)");
	}
	else if (SignedRoll >= RevealRollDegrees)
	{
		Band = TEXT("REVEAL (open)");
	}

	return FString::Printf(TEXT("roll %+.0f  signed %+.0f\n%s"), RollDegrees, SignedRoll, Band);
}

void AMemoryRoomMenuActor::TickWristAttach(float DeltaSeconds)
{
	// The XR pawn may not have built its controller components yet when this actor spawns,
	// so keep looking at a low rate rather than giving up on the first miss. Without this a
	// panel that rides the wrist would be left standing wherever the manager spawned it.
	const bool bNeedsWrist = !WristComponent.IsValid();
	const bool bNeedsAttach = !bFloatInFrontOfView && !bAttachedToController;
	if (!bNeedsWrist && !bNeedsAttach)
	{
		return;
	}

	WristRetrySeconds += DeltaSeconds;
	if (WristRetrySeconds < 0.5f)
	{
		return;
	}
	WristRetrySeconds = 0.0f;

	if (bNeedsWrist)
	{
		WristComponent = FindWristComponent();
	}
	if (bNeedsAttach && WristComponent.IsValid())
	{
		bAttachedToController = AttachToLeftController();
	}
}

void AMemoryRoomMenuActor::TickFacePlayer()
{
	// Only meaningful for the wrist-mounted panel - the floating-in-front-of-view mode already
	// picks a rotation facing the player when it opens, in OpenMenu.
	if (bFloatInFrontOfView || !bAttachedToController || !bMenuOpen)
	{
		return;
	}

	const UWorld* World = GetWorld();
	const APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
	if (!PC)
	{
		return;
	}

	FVector ViewLocation = FVector::ZeroVector;
	FRotator ViewRotation = FRotator::ZeroRotator;
	PC->GetPlayerViewPoint(ViewLocation, ViewRotation);

	// Aim -X at the headset, not +X. The wrist puck's own face-the-viewer code aims +X because
	// its label carries no extra rotation - but BuildBoard() places every widget on this
	// actor's -X face, in front of a backing panel that sits toward +X, and gives every text
	// component an extra local 180 yaw on top of that so it reads from the -X side (see the
	// comments in BuildBoard). Aiming +X at the viewer here pointed the backing panel at them
	// and the buttons away, which is the "buttons facing away" bug. The panel is still parented
	// to the wrist for position, but a fixed relative rotation can only ever be correct at one
	// exact wrist angle, and the reveal roll can land anywhere from RevealRollDegrees to 180 -
	// so rotation is recomputed live instead of baked in once at attach time.
	const FVector ToViewer = ViewLocation - GetActorLocation();
	if (!ToViewer.IsNearlyZero())
	{
		SetActorRotation((-ToViewer).Rotation());
	}
}

void AMemoryRoomMenuActor::TickOpenAnimation(float DeltaSeconds)
{
	const float Target = bMenuOpen ? 1.0f : 0.0f;
	if (FMath::IsNearlyEqual(OpenProgress, Target))
	{
		return;
	}

	const float Duration = FMath::Max(OpenAnimationSeconds, 0.0f);
	const float Step = Duration > KINDA_SMALL_NUMBER ? DeltaSeconds / Duration : 1.0f;
	OpenProgress = FMath::Clamp(OpenProgress + (bMenuOpen ? Step : -Step), 0.0f, 1.0f);
	ApplyBoardScale();

	// Closed: the scale-out has finished, so the board can actually go away.
	if (!bMenuOpen && OpenProgress <= 0.0f)
	{
		SetActorHiddenInGame(true);
	}
}

void AMemoryRoomMenuActor::ApplyTilePop(UStaticMeshComponent* Panel, float HalfY, float Blend) const
{
	if (!Panel)
	{
		return;
	}

	const float Eased = 1.0f - FMath::Square(1.0f - FMath::Clamp(Blend, 0.0f, 1.0f));
	const float Grow = 1.0f + FMath::Max(TileHoverPop, 0.0f) * Eased;
	const FVector Location = Panel->GetRelativeLocation();
	Panel->SetRelativeLocation(FVector(WidgetDepthX - TilePopLiftX * Eased, Location.Y, Location.Z));
	Panel->SetRelativeScale3D(FVector(TileThickness / 100.0f, HalfY * 2.0f * Grow / 100.0f, PressHalfZ * 2.0f * Grow / 100.0f));
}

void AMemoryRoomMenuActor::TickHoverAnimation(float DeltaSeconds)
{
	if (!bMenuOpen && OpenProgress <= 0.0f)
	{
		return;
	}

	const UPrimitiveComponent* Hovered = HoveredComponent.Get();
	const float Rate = FMath::Clamp(DeltaSeconds * FMath::Max(HoverAnimationSpeed, 1.0f), 0.0f, 1.0f);
	auto Settle = [Rate](float& Blend, bool bHot)
	{
		const float Target = bHot ? 1.0f : 0.0f;
		if (FMath::IsNearlyEqual(Blend, Target, 0.001f))
		{
			return false;
		}
		Blend = FMath::Lerp(Blend, Target, Rate);
		if (FMath::IsNearlyEqual(Blend, Target, 0.005f))
		{
			Blend = Target;
		}
		return true;
	};

	for (FMemoryRoomMenuRow& Row : Rows)
	{
		if (Settle(Row.MinusHoverBlend, Hovered == Row.MinusBox))
		{
			ApplyTilePop(Row.MinusPanel, Row.TileHalfY, Row.MinusHoverBlend);
		}
		if (Row.PlusPanel && Settle(Row.PlusHoverBlend, Hovered == Row.PlusBox))
		{
			ApplyTilePop(Row.PlusPanel, Row.TileHalfY, Row.PlusHoverBlend);
		}
	}
	if (bFolderBrowserOpen)
	{
		for (FMemoryRoomFolderTile& Tile : FolderTiles)
		{
			const float Target=Hovered==Tile.Box ? 1.0f : 0.0f;
			Tile.HoverBlend=FMath::FInterpTo(Tile.HoverBlend,Target,DeltaSeconds,HoverAnimationSpeed);
			if (Tile.Panel) Tile.Panel->SetRelativeScale3D(FVector(0.006f,0.074f*(1.0f+Tile.HoverBlend*TileHoverPop),0.08f*(1.0f+Tile.HoverBlend*TileHoverPop)));
		}
	}
}

void AMemoryRoomMenuActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	TickOpenAnimation(DeltaSeconds);
	TickHoverAnimation(DeltaSeconds);
}

void AMemoryRoomMenuActor::BindInput()
{
	if (ToggleMenuInputActionName.IsNone() || !GetWorld()) return;
	if (APlayerController* PC = GetWorld()->GetFirstPlayerController())
	{
		EnableInput(PC);
		if (InputComponent) InputComponent->BindAction(ToggleMenuInputActionName, IE_Pressed, this, &AMemoryRoomMenuActor::ToggleMenu);
	}
}

void AMemoryRoomMenuActor::ToggleMenu()
{
	bMenuOpen ? CloseMenu() : OpenMenu();
}

void AMemoryRoomMenuActor::OpenMenu()
{
	ResolveManager();
	PositionMenuInFrontOfView();

	bMenuOpen = true;
	// Visible at once; TickOpenAnimation grows it in from ~82%.
	SetActorHiddenInGame(false);
	SetActorEnableCollision(true);
	SetOtherWristButtonsHidden(true);
	ApplyBoardScale();
	RefreshText();
	if (MemoryRoomManager && !MemoryRoomManager->HasPhotoSourceChoice()) OpenFolderBrowser();
}

void AMemoryRoomMenuActor::CloseMenu()
{
	bMenuOpen = false;
	HoveredComponent.Reset();
	// Collision off at once so nothing is pressed mid scale-out; TickOpenAnimation hides the
	// actor when the scale-out lands.
	SetActorEnableCollision(false);
	if (OpenAnimationSeconds <= KINDA_SMALL_NUMBER)
	{
		OpenProgress = 0.0f;
		SetActorHiddenInGame(true);
	}
	SetOtherWristButtonsHidden(false);
	ApplyHoverVisuals();
}

void AMemoryRoomMenuActor::SetOtherWristButtonsHidden(bool bShouldHide)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	// Every wrist puck, including Settings, gets out of the way while the much larger panel
	// they sit underneath is open - the panel has its own Close tile, so the puck that opened
	// it does not need to stay reachable to close it again.
	for (TActorIterator<AMemoryRoomSkyButtonActor> It(World); It; ++It)
	{
		AMemoryRoomSkyButtonActor* Button = *It;
		if (Button)
		{
			Button->SetActorHiddenInGame(bShouldHide);
			Button->SetActorEnableCollision(!bShouldHide);
		}
	}
}

bool AMemoryRoomMenuActor::IsMenuComponent(const UPrimitiveComponent* Component) const
{
	if (!bMenuOpen || !Component) return false;
	if (bFolderBrowserOpen)
	{
		if (Component == BrowserBackBox || Component == BrowserUpBox || Component == BrowserPrevBox || Component == BrowserNextBox || Component == BrowserUseBox) return true;
		for (const FMemoryRoomFolderTile& Tile : FolderTiles) if (Tile.Box == Component) return true;
		return Component == BrowserPanel;
	}
	if (Component == BackgroundPanel) return true;
	for (const FMemoryRoomMenuRow& Row : Rows)
	{
		if (Row.MinusBox == Component || Row.PlusBox == Component) return true;
	}
	return false;
}

FMemoryRoomMenuRow* AMemoryRoomMenuActor::FindRow(UPrimitiveComponent* Component, bool& bOutIsPlus)
{
	bOutIsPlus = false;
	for (FMemoryRoomMenuRow& Row : Rows)
	{
		if (Row.MinusBox == Component) { bOutIsPlus = false; return &Row; }
		if (Row.PlusBox == Component) { bOutIsPlus = true; return &Row; }
	}
	return nullptr;
}

void AMemoryRoomMenuActor::SetHoveredComponent(UPrimitiveComponent* Component)
{
	HoveredComponent = Component;
	ApplyHoverVisuals();
}

bool AMemoryRoomMenuActor::PressHoveredButton()
{
	if (bFolderBrowserOpen)
	{
		UPrimitiveComponent* Hit=HoveredComponent.Get();
		if (Hit == BrowserBackBox) ShowSettingsPage();
		else if (Hit == BrowserUpBox)
		{
			for (const FMemoryRoomAlbum& Album : MemoryRoomManager->GetPhotoAlbums()) if (Album.Id == BrowserCurrentParentId) { BrowserCurrentParentId=Album.ParentId; break; }
			if (!MemoryRoomManager->GetPhotoAlbums().ContainsByPredicate([this](const FMemoryRoomAlbum& Album){return Album.Id==BrowserCurrentParentId;})) BrowserCurrentParentId.Reset();
			BrowserPendingSelectionId=BrowserCurrentParentId;
			BrowserPage=0; RefreshFolderBrowser();
		}
		else if (Hit == BrowserPrevBox) { BrowserPage=FMath::Max(0,BrowserPage-1); RefreshFolderBrowser(); }
		else if (Hit == BrowserNextBox) { ++BrowserPage; RefreshFolderBrowser(); }
		else if (Hit == BrowserUseBox)
		{
			if (MemoryRoomManager && MemoryRoomManager->SelectPhotoAlbumById(BrowserPendingSelectionId))
			{
				StatusMessage=TEXT("Photo folder saved. Loading photos..."); ShowSettingsPage(); RefreshText();
			}
		}
		else
		{
			for (FMemoryRoomFolderTile& Tile : FolderTiles) if (Tile.Box == Hit && !Tile.AlbumId.IsEmpty())
			{
				BrowserCurrentParentId=Tile.AlbumId; BrowserPendingSelectionId=Tile.AlbumId; BrowserPage=0; RefreshFolderBrowser(); break;
			}
		}
		return true;
	}
	bool bIsPlus = false;
	if (FMemoryRoomMenuRow* Row = FindRow(HoveredComponent.Get(), bIsPlus))
	{
		PressRow(*Row, bIsPlus);
		if (PressHapticEffect)
		{
			if (APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr)
			{
				PC->PlayHapticEffect(PressHapticEffect, EControllerHand::Right);
			}
		}
		return true;
	}
	return bMenuOpen;
}

void AMemoryRoomMenuActor::PressRow(FMemoryRoomMenuRow& Row, bool bIsPlus)
{
	ResolveManager();
	if (Row.bIsSetting)
	{
		if (MemoryRoomManager)
		{
			MemoryRoomManager->AdjustUserSetting(Row.Setting, bIsPlus ? 1 : -1);
		}
		return;
	}

	switch (Row.Action)
	{
	case EMemoryRoomMenuAction::ResetView:
		if (MemoryRoomManager) MemoryRoomManager->ResetGalleryScroll();
		break;
	case EMemoryRoomMenuAction::JumpToNewest:
		if (MemoryRoomManager) MemoryRoomManager->JumpToNewestPhoto();
		break;
	case EMemoryRoomMenuAction::ReloadPhotos:
		StatusMessage = TEXT("Refreshing photos...");
		if (MemoryRoomManager) MemoryRoomManager->ReloadPhotoLibrary();
		break;
	case EMemoryRoomMenuAction::ChoosePhotoSource:
		OpenFolderBrowser();
		break;
	case EMemoryRoomMenuAction::Close:
		CloseMenu();
		break;
	}
	RefreshText();
}

void AMemoryRoomMenuActor::OpenFolderBrowser()
{
	ResolveManager();
	if (!MemoryRoomManager) return;
	bFolderBrowserOpen=true; BrowserPage=0;
	BrowserCurrentParentId=MemoryRoomManager->GetSelectedPhotoAlbumId();
	// "All photos" is a selection, not a directory level.
	if (BrowserCurrentParentId == TEXT("@all")) BrowserCurrentParentId.Reset();
	if (!MemoryRoomManager->GetPhotoAlbums().ContainsByPredicate([this](const FMemoryRoomAlbum& Album){return Album.Id==BrowserCurrentParentId;})) BrowserCurrentParentId.Reset();
	BrowserPendingSelectionId=BrowserCurrentParentId;
	SetFolderBrowserVisible(true); RefreshFolderBrowser();
	MemoryRoomManager->RefreshPhotoFolders();
}

void AMemoryRoomMenuActor::ShowSettingsPage()
{
	bFolderBrowserOpen=false; SetFolderBrowserVisible(false); RefreshText();
}

void AMemoryRoomMenuActor::SetFolderBrowserVisible(bool bVisible)
{
	auto SetVisible=[](UPrimitiveComponent* C, bool bShow) { if (C) C->SetVisibility(bShow,true); };
	SetVisible(BrowserPanel,bVisible); SetVisible(BrowserTitle,bVisible); SetVisible(BrowserPathText,bVisible);
	SetVisible(BrowserBackBox,bVisible); SetVisible(BrowserBackPanel,bVisible); SetVisible(BrowserBackText,bVisible);
	SetVisible(BrowserUpBox,bVisible); SetVisible(BrowserUpPanel,bVisible); SetVisible(BrowserUpText,bVisible);
	SetVisible(BrowserPrevBox,bVisible); SetVisible(BrowserPrevPanel,bVisible); SetVisible(BrowserPrevText,bVisible);
	SetVisible(BrowserNextBox,bVisible); SetVisible(BrowserNextPanel,bVisible); SetVisible(BrowserNextText,bVisible);
	SetVisible(BrowserUseBox,bVisible); SetVisible(BrowserUsePanel,bVisible); SetVisible(BrowserUseText,bVisible);
	for (FMemoryRoomFolderTile& Tile : FolderTiles) { SetVisible(Tile.Box,bVisible); SetVisible(Tile.Panel,bVisible); SetVisible(Tile.Text,bVisible); }
	for (FMemoryRoomMenuRow& Row : Rows) { SetVisible(Row.MinusBox,!bVisible); SetVisible(Row.PlusBox,!bVisible); SetVisible(Row.MinusPanel,!bVisible); SetVisible(Row.PlusPanel,!bVisible); SetVisible(Row.MinusGlyph,!bVisible); SetVisible(Row.PlusGlyph,!bVisible); SetVisible(Row.Label,!bVisible); SetVisible(Row.ValueText,!bVisible); }
	auto SetTargetCollision=[](UBoxComponent* Box,bool bEnabled){if(Box) Box->SetCollisionEnabled(bEnabled?ECollisionEnabled::QueryOnly:ECollisionEnabled::NoCollision);};
	SetTargetCollision(BrowserBackBox,bVisible); SetTargetCollision(BrowserUpBox,bVisible); SetTargetCollision(BrowserPrevBox,bVisible); SetTargetCollision(BrowserNextBox,bVisible); SetTargetCollision(BrowserUseBox,bVisible);
	for (FMemoryRoomFolderTile& Tile : FolderTiles) SetTargetCollision(Tile.Box,bVisible && Tile.Box->IsVisible());
	for (FMemoryRoomMenuRow& Row : Rows) { SetTargetCollision(Row.MinusBox,!bVisible); SetTargetCollision(Row.PlusBox,!bVisible && Row.PlusBox); }
	SetVisible(TitleText,!bVisible); SetVisible(StatusText,!bVisible); SetVisible(PaperPanel,!bVisible); SetVisible(ClipPanel,!bVisible); SetVisible(TitleRule,!bVisible); SetVisible(SectionRule,!bVisible);
}

void AMemoryRoomMenuActor::RefreshFolderBrowser()
{
	if (!MemoryRoomManager) return;
	const TArray<FMemoryRoomAlbum>& Albums=MemoryRoomManager->GetPhotoAlbums();
	TArray<const FMemoryRoomAlbum*> VisibleFolders;
	for (const FMemoryRoomAlbum& Album : Albums)
	{
		if (Album.ParentId==BrowserCurrentParentId) VisibleFolders.Add(&Album);
		else if (BrowserCurrentParentId.IsEmpty() && !Album.ParentId.IsEmpty())
		{
			const bool bParentListed=Albums.ContainsByPredicate([&Album](const FMemoryRoomAlbum& Candidate){return Candidate.Id==Album.ParentId;});
			if (!bParentListed) VisibleFolders.Add(&Album); // Desktop roots carry a filesystem path as ParentId.
		}
	}
	VisibleFolders.Sort([](const FMemoryRoomAlbum& A,const FMemoryRoomAlbum& B){return A.Name < B.Name;});
	const int32 PageCount=FMath::Max(1,FMath::DivideAndRoundUp(VisibleFolders.Num(),FolderTiles.Num()));
	BrowserPage=FMath::Clamp(BrowserPage,0,PageCount-1);
	for (int32 i=0;i<FolderTiles.Num();++i)
	{
		FMemoryRoomFolderTile& Tile=FolderTiles[i]; const int32 Index=BrowserPage*FolderTiles.Num()+i;
		if (VisibleFolders.IsValidIndex(Index))
		{
			const FMemoryRoomAlbum& Album=*VisibleFolders[Index]; Tile.AlbumId=Album.Id;
			Tile.Text->SetText(FText::FromString(FString::Printf(TEXT("%s\n%d photos"),*Album.Name,Album.Count)));
			Tile.Panel->SetVisibility(true); Tile.Box->SetVisibility(true); Tile.Text->SetVisibility(true);
		}
		else { Tile.AlbumId.Reset(); Tile.Panel->SetVisibility(false); Tile.Box->SetVisibility(false); Tile.Text->SetVisibility(false); Tile.Box->SetCollisionEnabled(ECollisionEnabled::NoCollision); }
		if (VisibleFolders.IsValidIndex(Index)) Tile.Box->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	}
	FString Path=TEXT("ROOT");
	for (const FMemoryRoomAlbum& Album : Albums) if (Album.Id==BrowserCurrentParentId) { Path=Album.Name; break; }
	const FString ShortPath = Path.Len() > 20 ? Path.Left(18) + TEXT("..") : Path;
	BrowserPathText->SetText(FText::FromString(FString::Printf(TEXT("%s     %d / %d"),*ShortPath,BrowserPage+1,PageCount)));
	BrowserUpBox->SetVisibility(!BrowserCurrentParentId.IsEmpty()); BrowserUpPanel->SetVisibility(!BrowserCurrentParentId.IsEmpty()); BrowserUpText->SetVisibility(!BrowserCurrentParentId.IsEmpty());
	BrowserPrevBox->SetVisibility(BrowserPage>0); BrowserPrevPanel->SetVisibility(BrowserPage>0); BrowserPrevText->SetVisibility(BrowserPage>0);
	BrowserNextBox->SetVisibility(BrowserPage+1<PageCount); BrowserNextPanel->SetVisibility(BrowserPage+1<PageCount); BrowserNextText->SetVisibility(BrowserPage+1<PageCount);
	BrowserUpBox->SetCollisionEnabled(BrowserCurrentParentId.IsEmpty()?ECollisionEnabled::NoCollision:ECollisionEnabled::QueryOnly);
	BrowserPrevBox->SetCollisionEnabled(BrowserPage>0?ECollisionEnabled::QueryOnly:ECollisionEnabled::NoCollision);
	BrowserNextBox->SetCollisionEnabled(BrowserPage+1<PageCount?ECollisionEnabled::QueryOnly:ECollisionEnabled::NoCollision);
	const bool bCanUse=!BrowserPendingSelectionId.IsEmpty(); BrowserUseBox->SetVisibility(bCanUse); BrowserUsePanel->SetVisibility(bCanUse); BrowserUseText->SetVisibility(bCanUse);
	BrowserUseBox->SetCollisionEnabled(bCanUse?ECollisionEnabled::QueryOnly:ECollisionEnabled::NoCollision);
	BrowserUseText->SetText(FText::FromString(FString::Printf(TEXT("USE FOLDER: %s"),*ShortPath)));
}

void AMemoryRoomMenuActor::RefreshText()
{
	ResolveManager();

	for (FMemoryRoomMenuRow& Row : Rows)
	{
		if (!Row.Label) continue;
		if (Row.bIsSetting)
		{
			FText Name, Value;
			if (MemoryRoomManager)
			{
				MemoryRoomManager->GetUserSettingParts(Row.Setting, Name, Value);
			}
			// Small caps for the names; the values keep their own case ("Sunset", "Low").
			Row.Label->SetText(FText::FromString(Name.ToString().ToUpper()));
			if (Row.ValueText) Row.ValueText->SetText(Value);
		}
		else
		{
			const TCHAR* Text = TEXT("CLOSE");
			switch (Row.Action)
			{
			case EMemoryRoomMenuAction::ResetView: Text = TEXT("RESET VIEW"); break;
			case EMemoryRoomMenuAction::JumpToNewest: Text = TEXT("JUMP TO NEWEST"); break;
			case EMemoryRoomMenuAction::ReloadPhotos: Text = TEXT("RELOAD PHOTOS"); break;
			case EMemoryRoomMenuAction::ChoosePhotoSource: Text = TEXT("OPEN FOLDER"); break;
			case EMemoryRoomMenuAction::Close: Text = TEXT("CLOSE"); break;
			}
			Row.Label->SetText(FText::FromString(Text));
		}
	}

	if (StatusText)
	{
		const int32 Photos = MemoryRoomManager ? MemoryRoomManager->GetPhotoCount() : 0;
		StatusText->SetText(FText::FromString(StatusMessage.IsEmpty() ? FString::Printf(TEXT("%d photos"), Photos) : StatusMessage));
	}

	ApplyHoverVisuals();
}

void AMemoryRoomMenuActor::ApplyUIStyle(const FMemoryRoomWristUIStyle& NewStyle)
{
	// The clipboard's paper + ink look is deliberately theme-independent - it should read the
	// same whatever the sky is doing. The theme's accent tints the value text only.
	Style = NewStyle;
	ValueColor = (NewStyle.AccentColor * 0.55f + FLinearColor(0.10f, 0.04f, 0.0f)).ToFColor(true);
	RebuildStyleMaterials();
	if (TitleText)
	{
		TitleText->SetTextRenderColor(TitleColor);
		TitleText->SetHorizSpacingAdjust(LabelTracking * 2.0f);
	}
	if (StatusText)
	{
		StatusText->SetTextRenderColor(DimLabelColor);
	}
	for (FMemoryRoomMenuRow& Row : Rows)
	{
		if (Row.Label)
		{
			Row.Label->SetHorizSpacingAdjust(LabelTracking);
		}
	}
	ApplyHoverVisuals();
}

void AMemoryRoomMenuActor::RebuildStyleMaterials()
{
	bStyled = true;

	// Every surface is the unlit solid-colour material, so the clipboard reads the same in
	// broad daylight and at night.
	BoardMaterial = MemoryRoomUI::MakeSolidMaterial(GlassBaseMaterial, BoardColor, this);
	PaperMaterial = MemoryRoomUI::MakeSolidMaterial(GlassBaseMaterial, PaperColor, this);
	InkMaterial = MemoryRoomUI::MakeSolidMaterial(GlassBaseMaterial, InkColor, this);
	ClipMaterial = MemoryRoomUI::MakeSolidMaterial(GlassBaseMaterial, ClipColor, this);
	// Tiles: a key-cap slightly darker than the page at rest, warm rust under the pointer.
	TileMaterial = MemoryRoomUI::MakeSolidMaterial(GlassBaseMaterial, PaperColor * 0.80f, this);
	TileHotMaterial = MemoryRoomUI::MakeSolidMaterial(GlassBaseMaterial, FLinearColor(0.58f, 0.34f, 0.15f), this);
	BrowserGlassMaterial = MemoryRoomUI::MakeGlassMaterial(GlassBaseMaterial, Style, this);

	if (BackgroundPanel && BoardMaterial) BackgroundPanel->SetMaterial(0, BoardMaterial);
	if (PaperPanel && PaperMaterial) PaperPanel->SetMaterial(0, PaperMaterial);
	if (ClipPanel && ClipMaterial) ClipPanel->SetMaterial(0, ClipMaterial);
	if (TitleRule && InkMaterial) TitleRule->SetMaterial(0, InkMaterial);
	if (SectionRule && InkMaterial) SectionRule->SetMaterial(0, InkMaterial);
	if (BrowserPanel && BrowserGlassMaterial) BrowserPanel->SetMaterial(0, BrowserGlassMaterial);
	for (FMemoryRoomFolderTile& Tile : FolderTiles) if (Tile.Panel && TileMaterial) Tile.Panel->SetMaterial(0, TileMaterial);
	for (UStaticMeshComponent* Panel : {BrowserBackPanel.Get(), BrowserUpPanel.Get(), BrowserPrevPanel.Get(), BrowserNextPanel.Get(), BrowserUsePanel.Get()})
		if (Panel && TileMaterial) Panel->SetMaterial(0, TileMaterial);
	for (FMemoryRoomMenuRow& Row : Rows)
	{
		if (Row.MinusPanel && TileMaterial) Row.MinusPanel->SetMaterial(0, TileMaterial);
		if (Row.PlusPanel && TileMaterial) Row.PlusPanel->SetMaterial(0, TileMaterial);
	}
}

void AMemoryRoomMenuActor::ApplyHoverVisuals()
{
	const UPrimitiveComponent* Hovered = HoveredComponent.Get();
	for (FMemoryRoomMenuRow& Row : Rows)
	{
		const bool bMinusHot = Hovered == Row.MinusBox;
		const bool bPlusHot = Hovered == Row.PlusBox;

		// The tile warms at once; its size and lift ease in over a few frames (TickHoverAnimation).
		if (Row.MinusPanel && TileMaterial && TileHotMaterial)
		{
			Row.MinusPanel->SetMaterial(0, bMinusHot ? TileHotMaterial : TileMaterial);
		}
		if (Row.PlusPanel && TileMaterial && TileHotMaterial)
		{
			Row.PlusPanel->SetMaterial(0, bPlusHot ? TileHotMaterial : TileMaterial);
		}

		if (Row.MinusGlyph) Row.MinusGlyph->SetTextRenderColor(bMinusHot ? HighlightColor : DimLabelColor);
		if (Row.PlusGlyph) Row.PlusGlyph->SetTextRenderColor(bPlusHot ? HighlightColor : DimLabelColor);
		if (Row.Label)
		{
			// A setting tile lights its name only when one of its own arrows is under the
			// pointer, so a crowded glance still tells you which tile you are about to change.
			const bool bTileHot = Row.bIsSetting ? (bMinusHot || bPlusHot) : bMinusHot;
			Row.Label->SetTextRenderColor(bTileHot ? HighlightColor : (Row.bIsSetting ? DimLabelColor : LabelColor));
		}
		if (Row.ValueText)
		{
			// Values are gold: they are the thing on the board you are here to change.
			Row.ValueText->SetTextRenderColor((bMinusHot || bPlusHot) ? HighlightColor : ValueColor);
		}
	}
	for (FMemoryRoomFolderTile& Tile : FolderTiles) if (Tile.Panel && TileMaterial && TileHotMaterial) Tile.Panel->SetMaterial(0,Hovered==Tile.Box ? TileHotMaterial : TileMaterial);
	auto SetButtonHot=[Hovered,this](UPrimitiveComponent* Box,UStaticMeshComponent* Panel,UTextRenderComponent* Text)
	{
		const bool bHot=Hovered==Box;
		if (Panel && TileMaterial && TileHotMaterial) Panel->SetMaterial(0,bHot?TileHotMaterial:TileMaterial);
		if (Text) Text->SetTextRenderColor(bHot?HighlightColor:LabelColor);
	};
	SetButtonHot(BrowserBackBox,BrowserBackPanel,BrowserBackText); SetButtonHot(BrowserUpBox,BrowserUpPanel,BrowserUpText);
	SetButtonHot(BrowserPrevBox,BrowserPrevPanel,BrowserPrevText); SetButtonHot(BrowserNextBox,BrowserNextPanel,BrowserNextText); SetButtonHot(BrowserUseBox,BrowserUsePanel,BrowserUseText);
}

void AMemoryRoomMenuActor::HandleSettingsChanged()
{
	if (MemoryRoomManager && MemoryRoomManager->HasPhotoSourceChoice()) StatusMessage.Reset();
	if (bFolderBrowserOpen) RefreshFolderBrowser();
	RefreshText();
}
void AMemoryRoomMenuActor::HandleReloadStarted() { StatusMessage = TEXT("Refreshing photos..."); RefreshText(); }
void AMemoryRoomMenuActor::HandleReloadCompleted(int32 PhotoCount, bool bSuccess)
{
	StatusMessage = bSuccess ? (PhotoCount > 0 ? FString::Printf(TEXT("%d photos"), PhotoCount) : TEXT("No photos found")) : TEXT("Photo refresh failed");
	RefreshText();
}
