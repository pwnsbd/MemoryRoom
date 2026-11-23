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
constexpr float PanelDepthX = -0.6f;        // glass board plane; its face looks toward -X
constexpr float WidgetDepthX = -1.6f;       // chips float just in front of the board
constexpr float TextDepthX = WidgetDepthX - 0.9f;
constexpr float TilePopLiftX = 0.6f;        // a hot tile comes this much closer
constexpr float LabelSize = 2.0f;
constexpr float ValueSize = 2.6f;
constexpr float GlyphSize = 3.2f;
constexpr float ActionSize = 2.2f;
constexpr float TitleSize = 4.2f;
constexpr float StatusSize = 2.4f;
constexpr int32 SettingColumns = 2;
constexpr int32 ActionColumns = 2;

// Folder window layout, in cm, in the window's own frame (content on -X, +Y to the viewer's
// right, +Z up, origin at the window centre). Named GlassMenu_ so the file-local constants do
// not clash in a unity build.
constexpr float GlassMenu_WinWidth = 52.0f;
constexpr float GlassMenu_WinHeight = 46.0f;
constexpr float GlassMenu_WinPanelX = -0.6f;
constexpr float GlassMenu_WinChipX = -1.6f;
constexpr float GlassMenu_WinTextX = -2.5f;
constexpr float GlassMenu_WinBoxX = -2.8f;
constexpr float GlassMenu_WinBarZ = 17.5f;       // top bar: Up, breadcrumb, close
constexpr float GlassMenu_WinHintZ = 13.0f;
constexpr float GlassMenu_WinBottomZ = -17.5f;   // bottom bar: page arrows, Use this folder
constexpr float GlassMenu_WinSideY = 21.0f;
constexpr float GlassMenu_WinPageGapY = 7.0f;
constexpr float GlassMenu_WinPageTextY = -14.0f;
constexpr float GlassMenu_WinUseY = 17.0f;
constexpr float GlassMenu_WinBtnHalf = 2.0f;
constexpr float GlassMenu_WinTileW = 14.0f;
constexpr float GlassMenu_WinTileH = 10.0f;
constexpr int32 GlassMenu_WinTileCount = 6;
constexpr int32 GlassMenu_WinColumns = 3;
constexpr float GlassMenu_WinColPitchY = 16.0f;
constexpr float GlassMenu_WinRowZ0 = 6.0f;
constexpr float GlassMenu_WinRowPitchZ = 12.0f;
constexpr float GlassMenu_WinPathSize = 2.0f;
constexpr float GlassMenu_WinHintSize = 1.6f;
constexpr float GlassMenu_WinEmptySize = 2.2f;
constexpr float GlassMenu_WinNameSize = 1.8f;
constexpr float GlassMenu_WinCountSize = 1.4f;
constexpr int32 GlassMenu_NameMaxChars = 11;
constexpr int32 GlassMenu_PathMaxChars = 32;
constexpr float GlassMenu_PendingSeconds = 4.0f;  // "looking for folders" before "none found"
constexpr float GlassMenu_FolderStartScale = 0.85f;

float EaseOutCubic(float T)
{
	const float Clamped = FMath::Clamp(T, 0.0f, 1.0f);
	return 1.0f - FMath::Pow(1.0f - Clamped, 3.0f);
}

FString GlassMenu_Truncate(const FString& Text, int32 MaxChars)
{
	if (Text.Len() <= MaxChars)
	{
		return Text;
	}
	return Text.Left(FMath::Max(1, MaxChars - 1)) + FString::Chr(0x2026);
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
	static ConstructorHelpers::FObjectFinder<UStaticMesh> PlaneFinder(TEXT("/Engine/BasicShapes/Plane.Plane"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> GlassFinder(MemoryRoomUI::GlassTranslucentMaterialPath());
	UStaticMesh* Plane = PlaneFinder.Succeeded() ? PlaneFinder.Object : nullptr;
	if (GlassFinder.Succeeded())
	{
		GlassTranslucentMaterial = GlassFinder.Object;
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
	const float StatusZ = ActionsBottomZ - StatusSize * 1.8f;
	const float ContentTopZ = TitleZ + TitleSize * 0.5f;
	const float ContentBottomZ = StatusZ - StatusSize * 0.5f;
	const float BaseAnchorShiftZ = -(ContentBottomZ - PanelMargin);

	const float PanelWidth = ColumnPitchY + 2.0f * TileHalfY + 2.0f * PanelMargin;
	const float PanelHeight = (ContentTopZ - ContentBottomZ) + 2.0f * PanelMargin;
	BoardPanelWidthCm = PanelWidth;
	BoardPanelHeightCm = PanelHeight;

	// Column 0 is the viewer's left: the widgets carry a 180-degree yaw to face the player,
	// which makes +Y the reading direction and so the viewer's right.
	auto ColumnY = [](int32 Column, int32 ColumnCount)
	{
		return (Column - (ColumnCount - 1) * 0.5f) * ColumnPitchY;
	};

	// A flat glass face. The engine plane lies in XY with its normal +Z; pitching it 90 degrees
	// stands it up with the normal on -X (toward the viewer, like every widget on this board),
	// its local X running up and local Y running to the viewer's right. Scale is (height, width).
	auto MakeFace = [this, Plane](const FString& Name, USceneComponent* Parent, const FVector& Location, float Width, float Height, int32 SortPriority) -> UStaticMeshComponent*
	{
		UStaticMeshComponent* Face = CreateDefaultSubobject<UStaticMeshComponent>(*Name);
		Face->SetupAttachment(Parent);
		Face->SetStaticMesh(Plane);
		Face->SetRelativeLocation(Location);
		Face->SetRelativeRotation(FRotator(90.0f, 0.0f, 0.0f));
		Face->SetRelativeScale3D(FVector(Height / 100.0f, Width / 100.0f, 1.0f));
		Face->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Face->SetCastShadow(false);
		Face->bCastDynamicShadow = false;
		Face->bReceivesDecals = false;
		Face->SetTranslucentSortPriority(SortPriority);
		GlassFaces.Add(Face);
		return Face;
	};
	auto MakeTextAt = [this](const FString& Name, USceneComponent* Parent, const FVector& Location, float Size, float Tracking) -> UTextRenderComponent*
	{
		UTextRenderComponent* Text = CreateDefaultSubobject<UTextRenderComponent>(*Name);
		Text->SetupAttachment(Parent);
		Text->SetRelativeLocation(Location);
		Text->SetRelativeRotation(FRotator(0.0f, 180.0f, 0.0f));
		Text->SetHorizontalAlignment(EHTA_Center);
		Text->SetVerticalAlignment(EVRTA_TextCenter);
		Text->SetTextRenderColor(LabelColor);
		Text->SetWorldSize(Size);
		Text->SetHorizSpacingAdjust(Tracking);
		Text->SetCastShadow(false);
		return Text;
	};
	auto MakeBoxAt = [this](const FString& Name, USceneComponent* Parent, const FVector& Location, const FVector& Extent) -> UBoxComponent*
	{
		UBoxComponent* Box = CreateDefaultSubobject<UBoxComponent>(*Name);
		Box->SetupAttachment(Parent);
		Box->SetRelativeLocation(Location);
		Box->SetBoxExtent(Extent);
		Box->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		Box->SetCollisionResponseToAllChannels(ECR_Ignore);
		Box->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
		return Box;
	};

	// The frosted board, then the rows' chips float just in front of it.
	const float ContentMidZ = (ContentTopZ + ContentBottomZ) * 0.5f + BaseAnchorShiftZ;
	BackgroundPanel = MakeFace(TEXT("BackgroundPanel"), SceneRoot, FVector(PanelDepthX, 0.0f, ContentMidZ), PanelWidth, PanelHeight, 0);

	// The glass is unlit, but the text render components use the engine's stock (lit) font
	// material - a small warm light on the panel keeps the text from going flat at night.
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

	auto MakeBox = [this, MakeBoxAt](const FString& Name, float Y, float Z, float HalfY) -> UBoxComponent*
	{
		return MakeBoxAt(Name, SceneRoot, FVector(WidgetDepthX - PressHalfX, Y, Z), FVector(PressHalfX, HalfY, PressHalfZ));
	};
	auto MakePanel = [this, MakeFace](const FString& Name, float Y, float Z, float HalfY) -> UStaticMeshComponent*
	{
		return MakeFace(Name, SceneRoot, FVector(WidgetDepthX, Y, Z), HalfY * 2.0f, PressHalfZ * 2.0f, 1);
	};
	auto MakeText = [this, MakeTextAt](const FString& Name, float Y, float Z, float Size, float Tracking) -> UTextRenderComponent*
	{
		return MakeTextAt(Name, SceneRoot, FVector(TextDepthX, Y, Z), Size, Tracking);
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

	// --- Folder window ---
	// A separate glass panel with its own root. The root is absolute (it ignores the board's
	// transform and scale) and is placed in front of the view when the window opens. Local axes
	// match the board: content on the -X face, +Y to the viewer's right, +Z up.
	FolderRoot = CreateDefaultSubobject<USceneComponent>(TEXT("FolderWindowRoot"));
	FolderRoot->SetupAttachment(SceneRoot);
	FolderRoot->SetUsingAbsoluteLocation(true);
	FolderRoot->SetUsingAbsoluteRotation(true);
	FolderRoot->SetUsingAbsoluteScale(true);

	BrowserPanel = MakeFace(TEXT("FolderWindowPanel"), FolderRoot, FVector(GlassMenu_WinPanelX, 0.0f, 0.0f), GlassMenu_WinWidth, GlassMenu_WinHeight, 10);

	FolderLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("FolderLight"));
	FolderLight->SetupAttachment(FolderRoot);
	FolderLight->SetRelativeLocation(FVector(GlassMenu_WinTextX - 14.0f, 0.0f, 0.0f));
	FolderLight->SetMobility(EComponentMobility::Movable);
	FolderLight->SetCastShadows(false);
	FolderLight->bAffectsWorld = true;
	FolderLight->SetIntensityUnits(ELightUnits::Unitless);
	FolderLight->SetAttenuationRadius(110.0f);
	FolderLight->SetIntensity(5.0f);
	FolderLight->SetLightColor(FLinearColor(1.0f, 0.95f, 0.86f));

	BrowserPathText = MakeTextAt(TEXT("FwPath"), FolderRoot, FVector(GlassMenu_WinTextX, 0.0f, GlassMenu_WinBarZ), GlassMenu_WinPathSize, 0.0f);
	BrowserHintText = MakeTextAt(TEXT("FwHint"), FolderRoot, FVector(GlassMenu_WinTextX, 0.0f, GlassMenu_WinHintZ), GlassMenu_WinHintSize, 0.0f);
	BrowserHintText->SetTextRenderColor(DimLabelColor);
	BrowserPageText = MakeTextAt(TEXT("FwPage"), FolderRoot, FVector(GlassMenu_WinTextX, GlassMenu_WinPageTextY, GlassMenu_WinBottomZ), GlassMenu_WinPathSize, 0.0f);
	BrowserEmptyText = MakeTextAt(TEXT("FwEmpty"), FolderRoot, FVector(GlassMenu_WinTextX, 0.0f, 0.0f), GlassMenu_WinEmptySize, 0.0f);
	BrowserEmptyText->SetTextRenderColor(DimLabelColor);

	auto MakeWindowButton = [this, MakeFace, MakeTextAt, MakeBoxAt](const TCHAR* Tag, float Y, float Z, float HalfY, float HalfZ, float TextSize, const TCHAR* Caption, bool bPrimary) -> FMemoryRoomFolderButton
	{
		const FString Name(Tag);
		FMemoryRoomFolderButton Button;
		Button.HalfY = HalfY;
		Button.HalfZ = HalfZ;
		Button.bPrimary = bPrimary;
		Button.Box = MakeBoxAt(Name + TEXT("Box"), FolderRoot, FVector(GlassMenu_WinBoxX, Y, Z), FVector(PressHalfX, HalfY, HalfZ));
		Button.Panel = MakeFace(Name + TEXT("Panel"), FolderRoot, FVector(GlassMenu_WinChipX, Y, Z), HalfY * 2.0f, HalfZ * 2.0f, 11);
		Button.Label = MakeTextAt(Name + TEXT("Text"), FolderRoot, FVector(GlassMenu_WinTextX, Y, Z), TextSize, 0.0f);
		Button.Label->SetText(FText::FromString(Caption));
		return Button;
	};
	BrowserUp = MakeWindowButton(TEXT("FwUp"), -GlassMenu_WinSideY, GlassMenu_WinBarZ, GlassMenu_WinBtnHalf, GlassMenu_WinBtnHalf, 1.6f, TEXT("UP"), false);
	BrowserClose = MakeWindowButton(TEXT("FwClose"), GlassMenu_WinSideY, GlassMenu_WinBarZ, GlassMenu_WinBtnHalf, GlassMenu_WinBtnHalf, 2.6f, TEXT("X"), false);
	BrowserPrev = MakeWindowButton(TEXT("FwPrev"), -GlassMenu_WinSideY, GlassMenu_WinBottomZ, GlassMenu_WinBtnHalf, GlassMenu_WinBtnHalf, 2.6f, TEXT("<"), false);
	BrowserNext = MakeWindowButton(TEXT("FwNext"), -GlassMenu_WinSideY + 2.0f * GlassMenu_WinPageGapY, GlassMenu_WinBottomZ, GlassMenu_WinBtnHalf, GlassMenu_WinBtnHalf, 2.6f, TEXT(">"), false);
	BrowserUse = MakeWindowButton(TEXT("FwUse"), GlassMenu_WinUseY, GlassMenu_WinBottomZ, TileHalfY, GlassMenu_WinBtnHalf, 1.7f, TEXT("USE THIS FOLDER"), true);

	for (int32 i = 0; i < GlassMenu_WinTileCount; ++i)
	{
		const int32 Col = i % GlassMenu_WinColumns;
		const int32 Row = i / GlassMenu_WinColumns;
		const float Y = (Col - (GlassMenu_WinColumns - 1) * 0.5f) * GlassMenu_WinColPitchY;
		const float Z = GlassMenu_WinRowZ0 - Row * GlassMenu_WinRowPitchZ;
		const FString Tag = FString::Printf(TEXT("FwTile%d"), i);
		FMemoryRoomFolderTile Tile;
		Tile.Box = MakeBoxAt(Tag + TEXT("Box"), FolderRoot, FVector(GlassMenu_WinBoxX, Y, Z), FVector(PressHalfX, GlassMenu_WinTileW * 0.5f, GlassMenu_WinTileH * 0.5f));
		Tile.Panel = MakeFace(Tag + TEXT("Panel"), FolderRoot, FVector(GlassMenu_WinChipX, Y, Z), GlassMenu_WinTileW, GlassMenu_WinTileH, 11);
		Tile.Text = MakeTextAt(Tag + TEXT("Name"), FolderRoot, FVector(GlassMenu_WinTextX, Y, Z - 1.6f), GlassMenu_WinNameSize, 0.0f);
		Tile.Detail = MakeTextAt(Tag + TEXT("Count"), FolderRoot, FVector(GlassMenu_WinTextX, Y, Z - 3.5f), GlassMenu_WinCountSize, 0.0f);
		Tile.Detail->SetTextRenderColor(DimLabelColor);
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
				StatusMessage = TEXT("Choose a photo folder to begin.");
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

	// While the folder window is up the board slides aside (yawed round the viewer) so the two
	// do not overlap; it comes back to dead ahead when the window closes.
	if (bFolderBrowserOpen)
	{
		Forward = FRotator(0.0f, BoardYawWhenFolderOpenDegrees, 0.0f).RotateVector(Forward);
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

void AMemoryRoomMenuActor::ApplyTilePop(UStaticMeshComponent* Panel, float RestX, float HalfY, float HalfZ, float Blend) const
{
	if (!Panel)
	{
		return;
	}

	const float Eased = 1.0f - FMath::Square(1.0f - FMath::Clamp(Blend, 0.0f, 1.0f));
	const float Grow = 1.0f + FMath::Max(TileHoverPop, 0.0f) * Eased;
	const FVector Location = Panel->GetRelativeLocation();
	Panel->SetRelativeLocation(FVector(RestX - TilePopLiftX * Eased, Location.Y, Location.Z));
	// Glass faces are planes standing up on local X (height) and Y (width).
	Panel->SetRelativeScale3D(FVector(HalfZ * 2.0f * Grow / 100.0f, HalfY * 2.0f * Grow / 100.0f, 1.0f));
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
			ApplyTilePop(Row.MinusPanel, WidgetDepthX, Row.TileHalfY, PressHalfZ, Row.MinusHoverBlend);
		}
		if (Row.PlusPanel && Settle(Row.PlusHoverBlend, Hovered == Row.PlusBox))
		{
			ApplyTilePop(Row.PlusPanel, WidgetDepthX, Row.TileHalfY, PressHalfZ, Row.PlusHoverBlend);
		}
	}
	if (bFolderBrowserOpen)
	{
		for (FMemoryRoomFolderTile& Tile : FolderTiles)
		{
			if (Settle(Tile.HoverBlend, Hovered == Tile.Box))
			{
				ApplyTilePop(Tile.Panel, GlassMenu_WinChipX, GlassMenu_WinTileW * 0.5f, GlassMenu_WinTileH * 0.5f, Tile.HoverBlend);
			}
		}
		for (FMemoryRoomFolderButton* Button : { &BrowserUp, &BrowserClose, &BrowserPrev, &BrowserNext, &BrowserUse })
		{
			if (Settle(Button->HoverBlend, Hovered == Button->Box))
			{
				ApplyTilePop(Button->Panel, GlassMenu_WinChipX, Button->HalfY, Button->HalfZ, Button->HoverBlend);
			}
		}
	}
}

void AMemoryRoomMenuActor::ApplyFolderWindowScale()
{
	if (FolderRoot)
	{
		const float Grow = FMath::Lerp(GlassMenu_FolderStartScale, 1.0f, EaseOutCubic(FolderProgress));
		FolderRoot->SetWorldScale3D(FVector(Grow));
	}
}

void AMemoryRoomMenuActor::TickFolderWindow(float DeltaSeconds)
{
	const float Target = bFolderBrowserOpen ? 1.0f : 0.0f;
	if (!FMath::IsNearlyEqual(FolderProgress, Target))
	{
		const float Duration = FMath::Max(OpenAnimationSeconds, 0.0f);
		const float Step = Duration > KINDA_SMALL_NUMBER ? DeltaSeconds / Duration : 1.0f;
		FolderProgress = FMath::Clamp(FolderProgress + (bFolderBrowserOpen ? Step : -Step), 0.0f, 1.0f);
		ApplyFolderWindowScale();
	}

	if (!bFolderBrowserOpen || !MemoryRoomManager)
	{
		return;
	}

	// The MediaStore query answers asynchronously: repaint when the folder list changes, and
	// once more when the "looking for folders" grace period runs out.
	const int32 AlbumCount = MemoryRoomManager->GetPhotoAlbums().Num();
	const bool bPendingExpired = bBrowserPendingShown && GetWorld()
		&& (GetWorld()->GetTimeSeconds() - BrowserOpenedSeconds) >= GlassMenu_PendingSeconds;
	if (AlbumCount != BrowserLastAlbumCount || bPendingExpired)
	{
		RefreshFolderBrowser();
	}
}

void AMemoryRoomMenuActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	TickOpenAnimation(DeltaSeconds);
	TickHoverAnimation(DeltaSeconds);
	TickFolderWindow(DeltaSeconds);
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
	// The folder window goes with the board.
	if (bFolderBrowserOpen)
	{
		bFolderBrowserOpen = false;
		FolderProgress = 0.0f;
		SetFolderBrowserVisible(false);
	}
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
	// The folder window is a separate panel; the board stays live beside it.
	if (bFolderBrowserOpen && IsFolderWindowComponent(Component)) return true;
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
	bool bIsPlus = false;
	bool bPressed = false;
	FMemoryRoomMenuRow* Row = nullptr;
	if (bFolderBrowserOpen && IsFolderWindowComponent(HoveredComponent.Get()))
	{
		bPressed = PressFolderWindow(HoveredComponent.Get());
	}
	else if ((Row = FindRow(HoveredComponent.Get(), bIsPlus)) != nullptr)
	{
		PressRow(*Row, bIsPlus);
		bPressed = true;
	}
	if (bPressed)
	{
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
		ToggleFolderBrowser();
		break;
	case EMemoryRoomMenuAction::Close:
		CloseMenu();
		break;
	}
	RefreshText();
}

void AMemoryRoomMenuActor::ToggleFolderBrowser()
{
	if (bFolderBrowserOpen)
	{
		CloseFolderBrowser();
	}
	else
	{
		OpenFolderBrowser();
	}
}

void AMemoryRoomMenuActor::OpenFolderBrowser()
{
	ResolveManager();
	if (!MemoryRoomManager || bFolderBrowserOpen) return;

	bFolderBrowserOpen = true;
	BrowserPage = 0;
	BrowserCurrentParentId = MemoryRoomManager->GetSelectedPhotoAlbumId();
	// "All photos" is a selection, not a directory level.
	if (BrowserCurrentParentId == TEXT("@all")) BrowserCurrentParentId.Reset();
	if (!MemoryRoomManager->GetPhotoAlbums().ContainsByPredicate([this](const FMemoryRoomAlbum& Album) { return Album.Id == BrowserCurrentParentId; })) BrowserCurrentParentId.Reset();
	BrowserPendingSelectionId = BrowserCurrentParentId;
	BrowserOpenedSeconds = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0f;
	BrowserLastAlbumCount = -1;
	FolderProgress = 0.0f;

	PositionFolderWindow();
	ApplyFolderWindowScale();
	if (bFloatInFrontOfView && bMenuOpen)
	{
		// Slide the board aside so the window has the middle of the view.
		PositionMenuInFrontOfView();
	}
	SetFolderBrowserVisible(true);
	RefreshFolderBrowser();
	MemoryRoomManager->RefreshPhotoFolders();
	ApplyHoverVisuals();
}

void AMemoryRoomMenuActor::CloseFolderBrowser()
{
	if (!bFolderBrowserOpen) return;
	bFolderBrowserOpen = false;
	FolderProgress = 0.0f;
	if (IsFolderWindowComponent(HoveredComponent.Get())) HoveredComponent.Reset();
	SetFolderBrowserVisible(false);
	if (bFloatInFrontOfView && bMenuOpen)
	{
		PositionMenuInFrontOfView();
	}
	RefreshText();
}

void AMemoryRoomMenuActor::PositionFolderWindow()
{
	const APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
	if (!PC || !FolderRoot) return;
	FVector ViewLocation; FRotator ViewRotation;
	PC->GetPlayerViewPoint(ViewLocation, ViewRotation);

	FVector Forward = ViewRotation.Vector();
	Forward.Z = 0.0f;
	if (!Forward.Normalize()) Forward = FVector::ForwardVector;

	const FVector Location = ViewLocation + Forward * FMath::Max(FolderWindowDistanceCm, 40.0f) + FVector(0.0f, 0.0f, FolderWindowHeightOffsetCm);
	// Content is on the window's -X face, so +X points away from the viewer, like the board.
	FRotator Rotation = Forward.Rotation();
	Rotation.Pitch = -MenuPitchDegrees;
	Rotation.Roll = 0.0f;
	FolderRoot->SetWorldLocationAndRotation(Location, Rotation);
}

bool AMemoryRoomMenuActor::IsFolderWindowComponent(const UPrimitiveComponent* Component) const
{
	if (!Component) return false;
	if (Component == BrowserPanel) return true;
	for (const FMemoryRoomFolderButton* Button : { &BrowserUp, &BrowserClose, &BrowserPrev, &BrowserNext, &BrowserUse })
	{
		if (Button->Box == Component) return true;
	}
	for (const FMemoryRoomFolderTile& Tile : FolderTiles)
	{
		if (Tile.Box == Component) return true;
	}
	return false;
}

FString AMemoryRoomMenuActor::GetParentOfFolder(const FString& FolderId) const
{
	if (!MemoryRoomManager) return FString();
	const TArray<FMemoryRoomAlbum>& Albums = MemoryRoomManager->GetPhotoAlbums();
	const FMemoryRoomAlbum* Found = Albums.FindByPredicate([&FolderId](const FMemoryRoomAlbum& Album) { return Album.Id == FolderId; });
	if (!Found || Found->ParentId.IsEmpty()) return FString();
	// A parent that is not itself listed (desktop roots carry a filesystem path) is the root.
	const bool bParentListed = Albums.ContainsByPredicate([Found](const FMemoryRoomAlbum& Album) { return Album.Id == Found->ParentId; });
	return bParentListed ? Found->ParentId : FString();
}

bool AMemoryRoomMenuActor::PressFolderWindow(UPrimitiveComponent* Hit)
{
	if (!Hit || !MemoryRoomManager) return false;

	if (Hit == BrowserClose.Box)
	{
		CloseFolderBrowser();
		return true;
	}
	if (Hit == BrowserUp.Box)
	{
		BrowserCurrentParentId = GetParentOfFolder(BrowserCurrentParentId);
		BrowserPendingSelectionId = BrowserCurrentParentId;
		BrowserPage = 0;
		RefreshFolderBrowser();
		return true;
	}
	if (Hit == BrowserPrev.Box)
	{
		BrowserPage = FMath::Max(0, BrowserPage - 1);
		RefreshFolderBrowser();
		return true;
	}
	if (Hit == BrowserNext.Box)
	{
		++BrowserPage;
		RefreshFolderBrowser();
		return true;
	}
	if (Hit == BrowserUse.Box)
	{
		// "Use this folder" = the folder the window is currently showing the inside of.
		if (!BrowserPendingSelectionId.IsEmpty() && MemoryRoomManager->SelectPhotoAlbumById(BrowserPendingSelectionId))
		{
			StatusMessage = TEXT("Photo folder saved. Loading photos...");
			CloseFolderBrowser();
			RefreshText();
		}
		return true;
	}
	for (FMemoryRoomFolderTile& Tile : FolderTiles)
	{
		if (Tile.Box == Hit && !Tile.AlbumId.IsEmpty())
		{
			BrowserCurrentParentId = Tile.AlbumId;
			BrowserPendingSelectionId = Tile.AlbumId;
			BrowserPage = 0;
			RefreshFolderBrowser();
			return true;
		}
	}
	return false;
}

void AMemoryRoomMenuActor::SetFolderBrowserVisible(bool bVisible)
{
	if (FolderRoot) FolderRoot->SetVisibility(bVisible, true);
	if (FolderLight) FolderLight->SetVisibility(bVisible);
	// Hiding drops every press target; RefreshFolderBrowser re-enables the ones in use.
	auto SetTargetCollision = [](UBoxComponent* Box, bool bEnabled)
	{
		if (Box) Box->SetCollisionEnabled(bEnabled ? ECollisionEnabled::QueryOnly : ECollisionEnabled::NoCollision);
	};
	if (!bVisible)
	{
		for (FMemoryRoomFolderButton* Button : { &BrowserUp, &BrowserClose, &BrowserPrev, &BrowserNext, &BrowserUse })
		{
			SetTargetCollision(Button->Box, false);
		}
		for (FMemoryRoomFolderTile& Tile : FolderTiles)
		{
			SetTargetCollision(Tile.Box, false);
		}
	}
}

void AMemoryRoomMenuActor::RefreshFolderBrowser()
{
	if (!MemoryRoomManager || !bFolderBrowserOpen) return;
	const TArray<FMemoryRoomAlbum>& Albums = MemoryRoomManager->GetPhotoAlbums();
	BrowserLastAlbumCount = Albums.Num();

	TArray<const FMemoryRoomAlbum*> VisibleFolders;
	for (const FMemoryRoomAlbum& Album : Albums)
	{
		if (Album.ParentId == BrowserCurrentParentId) VisibleFolders.Add(&Album);
		else if (BrowserCurrentParentId.IsEmpty() && !Album.ParentId.IsEmpty())
		{
			const bool bParentListed = Albums.ContainsByPredicate([&Album](const FMemoryRoomAlbum& Candidate) { return Candidate.Id == Album.ParentId; });
			if (!bParentListed) VisibleFolders.Add(&Album); // Desktop roots carry a filesystem path as ParentId.
		}
	}
	VisibleFolders.Sort([](const FMemoryRoomAlbum& A, const FMemoryRoomAlbum& B) { return A.Name < B.Name; });
	const int32 PageCount = FMath::Max(1, FMath::DivideAndRoundUp(VisibleFolders.Num(), FolderTiles.Num()));
	BrowserPage = FMath::Clamp(BrowserPage, 0, PageCount - 1);

	auto SetTargetActive = [](UBoxComponent* Box, UStaticMeshComponent* Panel, UTextRenderComponent* Label, bool bActive)
	{
		if (Box) Box->SetCollisionEnabled(bActive ? ECollisionEnabled::QueryOnly : ECollisionEnabled::NoCollision);
		if (Panel) Panel->SetVisibility(bActive);
		if (Label) Label->SetVisibility(bActive);
	};

	for (int32 i = 0; i < FolderTiles.Num(); ++i)
	{
		FMemoryRoomFolderTile& Tile = FolderTiles[i];
		const int32 Index = BrowserPage * FolderTiles.Num() + i;
		if (VisibleFolders.IsValidIndex(Index))
		{
			const FMemoryRoomAlbum& Album = *VisibleFolders[Index];
			Tile.AlbumId = Album.Id;
			Tile.Text->SetText(FText::FromString(GlassMenu_Truncate(Album.Name, GlassMenu_NameMaxChars)));
			Tile.Detail->SetText(FText::FromString(Album.Count > 0 ? FString::Printf(TEXT("%d photos"), Album.Count) : FString()));
			SetTargetActive(Tile.Box, Tile.Panel, Tile.Text, true);
			Tile.Detail->SetVisibility(true);
		}
		else
		{
			Tile.AlbumId.Reset();
			SetTargetActive(Tile.Box, Tile.Panel, Tile.Text, false);
			Tile.Detail->SetVisibility(false);
		}
	}

	// Empty / pending states: the grid is blank, so say why.
	FString EmptyMessage;
	bBrowserPendingShown = false;
	if (VisibleFolders.Num() == 0)
	{
		if (Albums.Num() == 0)
		{
			const float Elapsed = GetWorld() ? GetWorld()->GetTimeSeconds() - BrowserOpenedSeconds : 0.0f;
			bBrowserPendingShown = Elapsed < GlassMenu_PendingSeconds;
			EmptyMessage = bBrowserPendingShown ? FString(TEXT("Looking for folders") + FString::Chr(0x2026)) : FString(TEXT("No photo folders found on this headset."));
		}
		else
		{
			EmptyMessage = TEXT("No folders inside this one.");
		}
	}
	BrowserEmptyText->SetText(FText::FromString(EmptyMessage));
	BrowserEmptyText->SetVisibility(!EmptyMessage.IsEmpty());

	// Breadcrumb: Headset > ... > current. Long paths keep their tail.
	TArray<FString> Crumbs;
	FString Walk = BrowserCurrentParentId;
	for (int32 Guard = 0; Guard < 8 && !Walk.IsEmpty(); ++Guard)
	{
		const FMemoryRoomAlbum* Found = Albums.FindByPredicate([&Walk](const FMemoryRoomAlbum& Album) { return Album.Id == Walk; });
		if (!Found) break;
		Crumbs.Insert(Found->Name, 0);
		Walk = GetParentOfFolder(Walk);
	}
	FString Path = TEXT("Headset");
	const FString Separator = FString(TEXT(" ")) + FString::Chr(0x203A) + TEXT(" ");
	for (const FString& Crumb : Crumbs) Path += Separator + Crumb;
	if (Path.Len() > GlassMenu_PathMaxChars)
	{
		Path = FString::Chr(0x2026) + Path.Right(GlassMenu_PathMaxChars - 1);
	}
	BrowserPathText->SetText(FText::FromString(Path));

	// Hint line: first-run prompt, else how many photos the current folder holds.
	FString Hint;
	if (!MemoryRoomManager->HasPhotoSourceChoice())
	{
		Hint = TEXT("Choose a photo folder to begin.");
	}
	else if (const FMemoryRoomAlbum* Current = Albums.FindByPredicate([this](const FMemoryRoomAlbum& Album) { return Album.Id == BrowserCurrentParentId; }))
	{
		Hint = Current->Count > 0 ? FString::Printf(TEXT("%d photos in this folder"), Current->Count) : FString();
	}
	BrowserHintText->SetText(FText::FromString(Hint));

	BrowserPageText->SetText(FText::FromString(FString::Printf(TEXT("%d / %d"), BrowserPage + 1, PageCount)));

	// Controls: close is always live; the rest only when they would do something.
	SetTargetActive(BrowserClose.Box, BrowserClose.Panel, BrowserClose.Label, true);
	SetTargetActive(BrowserUp.Box, BrowserUp.Panel, BrowserUp.Label, !BrowserCurrentParentId.IsEmpty());
	SetTargetActive(BrowserPrev.Box, BrowserPrev.Panel, BrowserPrev.Label, BrowserPage > 0);
	SetTargetActive(BrowserNext.Box, BrowserNext.Panel, BrowserNext.Label, BrowserPage + 1 < PageCount);
	SetTargetActive(BrowserUse.Box, BrowserUse.Panel, BrowserUse.Label, !BrowserPendingSelectionId.IsEmpty());
	BrowserPageText->SetVisibility(PageCount > 1);

	ApplyHoverVisuals();
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
			case EMemoryRoomMenuAction::ChoosePhotoSource: Text = TEXT("CHOOSE FOLDER"); break;
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
	// The glass menu look is deliberately theme-independent (warm-tinted, unlit), so the theme's
	// style is ignored. This only guarantees the glass has been painted.
	if (!bStyled)
	{
		RebuildStyleMaterials();
	}
}

void AMemoryRoomMenuActor::RebuildStyleMaterials()
{
	bStyled = true;

	UMaterialInterface* Base = GlassTranslucentMaterial ? GlassTranslucentMaterial.Get() : MemoryRoomUI::LoadGlassTranslucentMaterial();
	GlassTranslucentMaterial = Base;

	// Frosted, warm and fixed. Panels: soft white body, brighter rim. Chips: fainter. Highlight:
	// amber / honey (never blue). Painted once here, never per frame; 8 distinct textures.
	using MemoryRoomUI::FGlassPanelSpec;
	auto Panel = [](float W, float H) { FGlassPanelSpec S; S.WidthCm = W; S.HeightCm = H; S.BodyAlpha = 0.12f; S.RimAlpha = 0.30f; S.RimPixels = 1.5f; S.SheenAlpha = 0.07f; return S; };
	auto Chip = [](float W, float H) { FGlassPanelSpec S; S.WidthCm = W; S.HeightCm = H; S.BodyAlpha = 0.08f; S.RimAlpha = 0.18f; S.RimPixels = 1.5f; S.SheenAlpha = 0.04f; return S; };
	auto Hot = [](float W, float H) { FGlassPanelSpec S; S.WidthCm = W; S.HeightCm = H; S.Tint = FLinearColor(0.95f, 0.72f, 0.40f); S.RimTint = FLinearColor(1.0f, 0.78f, 0.42f); S.BodyAlpha = 0.25f; S.RimAlpha = 0.70f; S.RimPixels = 2.0f; S.SheenAlpha = 0.08f; return S; };
	auto Make = [this, Base](const FGlassPanelSpec& Spec)
	{
		return MemoryRoomUI::MakeTranslucentMaterial(Base, MemoryRoomUI::MakeFrostedPanelTexture(Spec), this);
	};

	BoardGlassMaterial = Make(Panel(BoardPanelWidthCm, BoardPanelHeightCm));
	SquareChipMaterial = Make(Chip(PressHalfY * 2.0f, PressHalfZ * 2.0f));
	SquareChipHotMaterial = Make(Hot(PressHalfY * 2.0f, PressHalfZ * 2.0f));
	WideChipMaterial = Make(Chip(TileHalfY * 2.0f, PressHalfZ * 2.0f));
	WideChipHotMaterial = Make(Hot(TileHalfY * 2.0f, PressHalfZ * 2.0f));
	FolderWindowMaterial = Make(Panel(GlassMenu_WinWidth, GlassMenu_WinHeight));
	FGlassPanelSpec TileSpec = Chip(GlassMenu_WinTileW, GlassMenu_WinTileH);
	TileSpec.bFolderGlyph = true;
	FolderTileMaterial = Make(TileSpec);
	FGlassPanelSpec TileHotSpec = Hot(GlassMenu_WinTileW, GlassMenu_WinTileH);
	TileHotSpec.bFolderGlyph = true;
	FolderTileHotMaterial = Make(TileHotSpec);

	if (BackgroundPanel && BoardGlassMaterial) BackgroundPanel->SetMaterial(0, BoardGlassMaterial);
	if (BrowserPanel && FolderWindowMaterial) BrowserPanel->SetMaterial(0, FolderWindowMaterial);

	// The painted sheen / glyphs assume the engine plane's V runs top to bottom once stood up;
	// bFlipGlassFaces spins every face 180 degrees about its normal if it does not.
	const FQuat Stand = FRotator(90.0f, 0.0f, 0.0f).Quaternion();
	const FQuat FaceRotation = bFlipGlassFaces ? Stand * FQuat(FVector::UpVector, PI) : Stand;
	for (UStaticMeshComponent* Face : GlassFaces)
	{
		if (Face) Face->SetRelativeRotation(FaceRotation);
	}

	ApplyHoverVisuals();
}

void AMemoryRoomMenuActor::ApplyHoverVisuals()
{
	if (!bStyled) return;
	const UPrimitiveComponent* Hovered = HoveredComponent.Get();
	for (FMemoryRoomMenuRow& Row : Rows)
	{
		const bool bMinusHot = Hovered == Row.MinusBox;
		const bool bPlusHot = Hovered == Row.PlusBox;

		// Chips swap to the amber glass at once; size and lift ease in over a few frames
		// (TickHoverAnimation). Setting rows use the square chip, action rows the wide one.
		UMaterialInterface* Rest = Row.bIsSetting ? SquareChipMaterial.Get() : WideChipMaterial.Get();
		UMaterialInterface* HotMat = Row.bIsSetting ? SquareChipHotMaterial.Get() : WideChipHotMaterial.Get();
		if (Row.MinusPanel && Rest && HotMat) Row.MinusPanel->SetMaterial(0, bMinusHot ? HotMat : Rest);
		if (Row.PlusPanel && Rest && HotMat) Row.PlusPanel->SetMaterial(0, bPlusHot ? HotMat : Rest);

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
			Row.ValueText->SetTextRenderColor((bMinusHot || bPlusHot) ? HighlightColor : ValueColor);
		}
	}

	// Folder window. The folder the library currently uses keeps the amber highlight.
	const FString SelectedId = MemoryRoomManager ? MemoryRoomManager->GetSelectedPhotoAlbumId() : FString();
	for (FMemoryRoomFolderTile& Tile : FolderTiles)
	{
		const bool bHot = Hovered == Tile.Box;
		const bool bSelected = !Tile.AlbumId.IsEmpty() && Tile.AlbumId == SelectedId;
		if (Tile.Panel && FolderTileMaterial && FolderTileHotMaterial) Tile.Panel->SetMaterial(0, (bHot || bSelected) ? FolderTileHotMaterial : FolderTileMaterial);
		if (Tile.Text) Tile.Text->SetTextRenderColor(bHot ? HighlightColor : LabelColor);
	}
	for (FMemoryRoomFolderButton* Button : { &BrowserUp, &BrowserClose, &BrowserPrev, &BrowserNext, &BrowserUse })
	{
		const bool bHot = Hovered == Button->Box;
		// "Use this folder" is the primary action: amber at rest, so it always reads as the one to press.
		UMaterialInterface* Rest = Button->bPrimary ? WideChipHotMaterial.Get() : SquareChipMaterial.Get();
		UMaterialInterface* HotMat = Button->bPrimary ? WideChipHotMaterial.Get() : SquareChipHotMaterial.Get();
		if (Button->Panel && Rest && HotMat) Button->Panel->SetMaterial(0, bHot ? HotMat : Rest);
		if (Button->Label) Button->Label->SetTextRenderColor((bHot || Button->bPrimary) ? HighlightColor : LabelColor);
	}
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
