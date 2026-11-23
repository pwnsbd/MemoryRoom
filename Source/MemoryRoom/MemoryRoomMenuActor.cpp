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
// Rev 2 (see docs/contracts/glass-menu.md): the board is one column of full-width action chips
// (painted icon + sentence-case label) under a title and a "Photos: <folder>" subtitle, then a
// compact two-column block of [<] value [>] settings. Every gap is measured between press
// targets, not between labels. Anything new is named GlassMenu_ so file-local constants do not
// clash in a unity build.
constexpr float PressHalfX = 1.2f;          // press-box depth, toward the viewer
constexpr float PanelMargin = 2.5f;
constexpr float PanelDepthX = -0.6f;        // glass board plane; its face looks toward -X
constexpr float WidgetDepthX = -1.6f;       // chips float just in front of the board
constexpr float TextDepthX = WidgetDepthX - 0.9f;
constexpr float TilePopLiftX = 0.6f;        // a hot tile comes this much closer
constexpr float LabelSize = 2.0f;
constexpr float ValueSize = 2.2f;
constexpr float ActionSize = 2.4f;
constexpr float TitleSize = 3.6f;
constexpr float StatusSize = 2.0f;

// Board chips and header (rev 3: tabbed board, one column of full-width rows).
constexpr float GlassMenu_ChipW = 36.0f;
constexpr float GlassMenu_ChipH = 5.4f;
constexpr float GlassMenu_ChipGap = 0.9f;
constexpr float GlassMenu_ChipIconY = -14.2f;     // icon centre, from the chip centre
constexpr float GlassMenu_ChipIconSize = 3.4f;
constexpr float GlassMenu_ChipLabelY = -11.4f;    // left edge of the label, from the chip centre
constexpr float GlassMenu_SubtitleSize = 2.2f;
constexpr float GlassMenu_IconDepthX = WidgetDepthX - 0.4f;
// Tab strip (Home / Gallery / Atmosphere / Comfort). Widths follow label length.
constexpr int32 GlassMenu_TabCount = 4;
constexpr float GlassMenu_TabH = 4.6f;
constexpr float GlassMenu_TabGap = 0.5f;
constexpr float GlassMenu_TabTextSize = 1.7f;
// Setting rows: label left, "< value >" right; the arrows are their own press targets.
constexpr float GlassMenu_SettingLabelY = -16.7f; // left edge of the name, from the row centre
constexpr float GlassMenu_ArrowMinusY = 3.5f;
constexpr float GlassMenu_ValueY = 9.5f;
constexpr float GlassMenu_ArrowPlusY = 15.5f;
constexpr float GlassMenu_ArrowHalfY = 2.0f;
constexpr float GlassMenu_ArrowHalfZ = 2.2f;
constexpr float GlassMenu_ArrowIconSize = 2.8f;

// Folder window layout, in cm, in the window's own frame (content on -X, +Y to the viewer's
// right, +Z up, origin at the window centre).
constexpr float GlassMenu_WinWidth = 62.0f;
constexpr float GlassMenu_WinHeight = 46.0f;
constexpr float GlassMenu_WinPanelX = -0.6f;
constexpr float GlassMenu_WinChipX = -1.6f;
constexpr float GlassMenu_WinIconX = -2.0f;
constexpr float GlassMenu_WinTextX = -2.5f;
constexpr float GlassMenu_WinBoxX = -2.8f;
constexpr float GlassMenu_WinBarZ = 18.0f;        // top bar: Up, breadcrumb, close
constexpr float GlassMenu_WinBottomZ = -18.0f;    // bottom bar: page control, Use this folder
constexpr float GlassMenu_WinEdgeY = 28.0f;       // outer chip edge from the centre (31 - 3 margin)
constexpr float GlassMenu_WinUpHalfY = 4.5f;      // 9 x 4.5
constexpr float GlassMenu_WinBtnHalfZ = 2.25f;
constexpr float GlassMenu_WinCloseHalf = 2.25f;   // 4.5 x 4.5
constexpr float GlassMenu_WinPagerHalf = 2.2f;
constexpr float GlassMenu_WinPageGapY = 6.0f;
constexpr float GlassMenu_WinPageTextY = -20.0f;
constexpr float GlassMenu_WinUseHalfY = 10.0f;    // 20 x 5
constexpr float GlassMenu_WinUseHalfZ = 2.5f;
constexpr float GlassMenu_WinCrumbY = -17.3f;     // left edge of the breadcrumb
constexpr float GlassMenu_WinTileW = 18.0f;
constexpr float GlassMenu_WinTileH = 13.0f;
constexpr int32 GlassMenu_WinTileCount = 6;
constexpr int32 GlassMenu_WinColumns = 3;
constexpr float GlassMenu_WinColPitchY = 19.6f;   // 18 wide + 1.6 gap
constexpr float GlassMenu_WinRowZ0 = 7.45f;
constexpr float GlassMenu_WinRowPitchZ = 14.6f;   // 13 high + 1.6 gap
constexpr float GlassMenu_WinPathSize = 2.0f;
constexpr float GlassMenu_WinHintSize = 1.4f;
constexpr float GlassMenu_WinEmptySize = 2.2f;
constexpr float GlassMenu_WinNameSize = 2.0f;
constexpr float GlassMenu_WinCountSize = 1.5f;
constexpr int32 GlassMenu_NameMaxChars = 14;
constexpr int32 GlassMenu_PathMaxChars = 34;
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

// Keeps the head and the tail, drops the middle.
FString GlassMenu_MiddleTruncate(const FString& Text, int32 MaxChars)
{
	if (Text.Len() <= MaxChars || MaxChars < 5)
	{
		return Text;
	}
	const int32 Keep = MaxChars - 1;
	const int32 Head = Keep / 3;
	return Text.Left(Head) + FString::Chr(0x2026) + Text.Right(Keep - Head);
}

// "Photo Spacing" -> "Photo spacing".
FString GlassMenu_SentenceCase(const FString& Text)
{
	FString Out = Text.ToLower();
	if (Out.Len() > 0)
	{
		Out[0] = FChar::ToUpper(Out[0]);
	}
	return Out;
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

	using MemoryRoomUI::EGlassIcon;
	struct FActionDef { EMemoryRoomMenuAction Action; EGlassIcon Icon; };
	const FActionDef Actions[] = {
		{ EMemoryRoomMenuAction::ResetView, EGlassIcon::Reset },
		{ EMemoryRoomMenuAction::JumpToNewest, EGlassIcon::Clock },
		{ EMemoryRoomMenuAction::ReloadPhotos, EGlassIcon::Refresh },
		{ EMemoryRoomMenuAction::ChoosePhotoSource, EGlassIcon::Folder },
		{ EMemoryRoomMenuAction::Close, EGlassIcon::Close } };
	const int32 ActionCount = UE_ARRAY_COUNT(Actions);

	// The Photos (album cycling) setting is not on the board: "Choose folder" and the folder
	// window replace it, and the current folder is the subtitle.
	// Rev 3: the settings live on tabs. Home holds the action chips; each other tab lists its
	// settings as slim rows. (Tab 0 is Home; settings tabs are 1..3 in the order below.)
	struct FTabDef { const TCHAR* Name; float Weight; };
	const FTabDef Tabs[GlassMenu_TabCount] = {
		{ TEXT("Home"), 6.0f }, { TEXT("Gallery"), 9.0f }, { TEXT("Atmosphere"), 12.0f }, { TEXT("Comfort"), 9.0f } };
	using EUS = EMemoryRoomUserSetting;
	const TArray<EUS> TabSettings[GlassMenu_TabCount] = {
		{},
		{ EUS::SpiralTwist, EUS::PhotoSpacing, EUS::PhotosInView, EUS::ScrollSpeed },
		{ EUS::TimeOfDay, EUS::NightPhotoGlow, EUS::Theme, EUS::DustAmount },
		{ EUS::Float, EUS::ThumbnailQuality } };
	// Board height is fixed to the tallest tab so it does not jump when switching.
	int32 TallestTabRows = ActionCount;
	for (const TArray<EUS>& TabList : TabSettings)
	{
		TallestTabRows = FMath::Max(TallestTabRows, TabList.Num());
	}

	// Lay the panel out top-down with the content top at Z = 0, measure what that comes to, then
	// shift the lot so the actor's own origin sits at the vertical CENTRE of the finished board.
	// Placement (PositionMenuInFrontOfView) anchors on that centre, so it does not depend on how
	// tall the board is.
	const float TitleZ = -TitleSize * 0.6f;
	const float SubtitleZ = TitleZ - TitleSize * 0.5f - 0.4f - GlassMenu_SubtitleSize * 0.5f;
	const float TabsTopZ = SubtitleZ - GlassMenu_SubtitleSize * 0.5f - 1.6f;
	const float TabsCenterZ = TabsTopZ - GlassMenu_TabH * 0.5f;
	const float ChipsTopZ = TabsTopZ - GlassMenu_TabH - 1.6f;
	const float ChipsBottomZ = ChipsTopZ - TallestTabRows * GlassMenu_ChipH - (TallestTabRows - 1) * GlassMenu_ChipGap;
	const float StatusZ = ChipsBottomZ - 1.2f - StatusSize * 0.5f;
	const float ContentTopZ = 0.0f;
	const float ContentBottomZ = StatusZ - StatusSize * 0.5f;
	const float BaseAnchorShiftZ = -(ContentTopZ + ContentBottomZ) * 0.5f;

	const float PanelWidth = GlassMenu_ChipW + 2.0f * PanelMargin;
	const float PanelHeight = (ContentTopZ - ContentBottomZ) + 2.0f * PanelMargin;
	BoardPanelWidthCm = PanelWidth;
	BoardPanelHeightCm = PanelHeight;
	BoardTopLocalZCm = ContentTopZ + PanelMargin + BaseAnchorShiftZ;
	auto RowZ = [&](int32 Index)
	{
		return ChipsTopZ - GlassMenu_ChipH * 0.5f - Index * (GlassMenu_ChipH + GlassMenu_ChipGap) + BaseAnchorShiftZ;
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

	auto MakeBox = [this, MakeBoxAt](const FString& Name, float Y, float Z, float HalfY, float HalfZ) -> UBoxComponent*
	{
		return MakeBoxAt(Name, SceneRoot, FVector(WidgetDepthX - PressHalfX, Y, Z), FVector(PressHalfX, HalfY, HalfZ));
	};
	auto MakePanel = [this, MakeFace](const FString& Name, float Y, float Z, float HalfY, float HalfZ) -> UStaticMeshComponent*
	{
		return MakeFace(Name, SceneRoot, FVector(WidgetDepthX, Y, Z), HalfY * 2.0f, HalfZ * 2.0f, 1);
	};
	auto MakeText = [this, MakeTextAt](const FString& Name, float Y, float Z, float Size, float Tracking) -> UTextRenderComponent*
	{
		return MakeTextAt(Name, SceneRoot, FVector(TextDepthX, Y, Z), Size, Tracking);
	};

	TitleText = MakeText(TEXT("TitleText"), 0.0f, TitleZ + BaseAnchorShiftZ, TitleSize, LabelTracking * 0.5f);
	TitleText->SetText(FText::FromString(TEXT("Memory room")));
	TitleText->SetTextRenderColor(TitleColor);

	SubtitleText = MakeText(TEXT("SubtitleText"), 0.0f, SubtitleZ + BaseAnchorShiftZ, GlassMenu_SubtitleSize, 0.0f);
	SubtitleText->SetTextRenderColor(DimLabelColor);

	StatusText = MakeText(TEXT("StatusText"), 0.0f, StatusZ + BaseAnchorShiftZ, StatusSize, 0.0f);
	StatusText->SetTextRenderColor(DimLabelColor);

	// Tab strip under the subtitle. Equal-height chips, widths weighted by label length.
	{
		float WeightSum = 0.0f;
		for (const FTabDef& Tab : Tabs) WeightSum += Tab.Weight;
		const float Usable = GlassMenu_ChipW - (GlassMenu_TabCount - 1) * GlassMenu_TabGap;
		float LeftY = -GlassMenu_ChipW * 0.5f;
		for (int32 i = 0; i < GlassMenu_TabCount; ++i)
		{
			const float Width = Usable * Tabs[i].Weight / WeightSum;
			const float CenterY = LeftY + Width * 0.5f;
			const FString Tag = FString::Printf(TEXT("Tab%d"), i);
			FMemoryRoomFolderButton Tab;
			Tab.HalfY = Width * 0.5f;
			Tab.HalfZ = GlassMenu_TabH * 0.5f;
			Tab.Box = MakeBox(Tag + TEXT("Press"), CenterY, TabsCenterZ + BaseAnchorShiftZ, Tab.HalfY, Tab.HalfZ);
			Tab.Panel = MakePanel(Tag + TEXT("P"), CenterY, TabsCenterZ + BaseAnchorShiftZ, Tab.HalfY, Tab.HalfZ);
			Tab.Label = MakeText(Tag + TEXT("Label"), CenterY, TabsCenterZ + BaseAnchorShiftZ, GlassMenu_TabTextSize, 0.0f);
			Tab.Label->SetText(FText::FromString(Tabs[i].Name));
			TabButtons.Add(Tab);
			LeftY += Width + GlassMenu_TabGap;
		}
	}

	// Home tab: one column of full-width action chips, icon at the left, label after it.
	for (int32 i = 0; i < ActionCount; ++i)
	{
		const float ChipZ = RowZ(i);
		const FString Tag = FString::Printf(TEXT("Act%d"), i);
		const float HalfY = GlassMenu_ChipW * 0.5f;
		const float HalfZ = GlassMenu_ChipH * 0.5f;

		FMemoryRoomMenuRow Row;
		Row.Tab = 0;
		Row.bIsSetting = false;
		Row.Action = Actions[i].Action;
		Row.IconKind = Actions[i].Icon;
		Row.TileHalfY = HalfY;
		Row.TileHalfZ = HalfZ;
		Row.MinusBox = MakeBox(Tag + TEXT("Press"), 0.0f, ChipZ, HalfY, HalfZ);
		Row.MinusPanel = MakePanel(Tag + TEXT("P"), 0.0f, ChipZ, HalfY, HalfZ);
		Row.Icon = MakeFace(Tag + TEXT("Icon"), SceneRoot, FVector(GlassMenu_IconDepthX, GlassMenu_ChipIconY, ChipZ), GlassMenu_ChipIconSize, GlassMenu_ChipIconSize, 12);
		Row.Label = MakeText(Tag + TEXT("Label"), GlassMenu_ChipLabelY, ChipZ, ActionSize, 0.0f);
		Row.Label->SetHorizontalAlignment(EHTA_Left);
		Rows.Add(Row);
	}

	// Settings tabs: one slim full-width row per setting. Name at the left, "< value >" at the
	// right with the arrows painted and each its own press target.
	int32 SettingTag = 0;
	for (int32 TabIndex = 1; TabIndex < GlassMenu_TabCount; ++TabIndex)
	{
		for (int32 i = 0; i < TabSettings[TabIndex].Num(); ++i, ++SettingTag)
		{
			const float RowCenterZ = RowZ(i);
			const FString Tag = FString::Printf(TEXT("Set%d"), SettingTag);

			FMemoryRoomMenuRow Row;
			Row.Tab = TabIndex;
			Row.bIsSetting = true;
			Row.Setting = TabSettings[TabIndex][i];
			Row.TileHalfY = GlassMenu_ArrowHalfY;
			Row.TileHalfZ = GlassMenu_ArrowHalfZ;
			Row.RowPanel = MakeFace(Tag + TEXT("RowP"), SceneRoot, FVector(WidgetDepthX, 0.0f, RowCenterZ), GlassMenu_ChipW, GlassMenu_ChipH, 1);
			Row.MinusBox = MakeBox(Tag + TEXT("Minus"), GlassMenu_ArrowMinusY, RowCenterZ, GlassMenu_ArrowHalfY, GlassMenu_ArrowHalfZ);
			Row.PlusBox = MakeBox(Tag + TEXT("Plus"), GlassMenu_ArrowPlusY, RowCenterZ, GlassMenu_ArrowHalfY, GlassMenu_ArrowHalfZ);
			Row.MinusPanel = MakePanel(Tag + TEXT("MinusP"), GlassMenu_ArrowMinusY, RowCenterZ, GlassMenu_ArrowHalfY, GlassMenu_ArrowHalfZ);
			Row.PlusPanel = MakePanel(Tag + TEXT("PlusP"), GlassMenu_ArrowPlusY, RowCenterZ, GlassMenu_ArrowHalfY, GlassMenu_ArrowHalfZ);
			Row.MinusIcon = MakeFace(Tag + TEXT("MinusI"), SceneRoot, FVector(GlassMenu_IconDepthX, GlassMenu_ArrowMinusY, RowCenterZ), GlassMenu_ArrowIconSize, GlassMenu_ArrowIconSize, 12);
			Row.PlusIcon = MakeFace(Tag + TEXT("PlusI"), SceneRoot, FVector(GlassMenu_IconDepthX, GlassMenu_ArrowPlusY, RowCenterZ), GlassMenu_ArrowIconSize, GlassMenu_ArrowIconSize, 12);
			Row.Label = MakeText(Tag + TEXT("Label"), GlassMenu_SettingLabelY, RowCenterZ, LabelSize, 0.0f);
			Row.Label->SetHorizontalAlignment(EHTA_Left);
			Row.ValueText = MakeText(Tag + TEXT("Value"), GlassMenu_ValueY, RowCenterZ, ValueSize, 0.0f);
			Rows.Add(Row);
		}
	}
	ApplyActiveTab();

	// --- Folder window ---
	// A child of the board root, so it shares the board's pose, scale and open animation. It
	// hangs off the board's right edge, top-aligned (PositionFolderWindow places it - the gap
	// and yaw are editable). Local axes match the board: content on the -X face, +Y to the
	// viewer's right, +Z up, origin at the window centre.
	FolderRoot = CreateDefaultSubobject<USceneComponent>(TEXT("FolderWindowRoot"));
	FolderRoot->SetupAttachment(SceneRoot);
	FolderRoot->SetRelativeLocation(FVector(0.0f, PanelWidth * 0.5f + FolderWindowGapCm + GlassMenu_WinWidth * 0.5f, BoardTopLocalZCm - GlassMenu_WinHeight * 0.5f));
	FolderRoot->SetRelativeRotation(FRotator(0.0f, FolderWindowInwardYawDegrees, 0.0f));

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

	BrowserPathText = MakeTextAt(TEXT("FwPath"), FolderRoot, FVector(GlassMenu_WinTextX, GlassMenu_WinCrumbY, GlassMenu_WinBarZ), GlassMenu_WinPathSize, 0.0f);
	BrowserPathText->SetHorizontalAlignment(EHTA_Left);
	BrowserPathText->SetTextRenderColor(FColor(220, 212, 200)); // ~75% of the title colour
	BrowserHintText = MakeTextAt(TEXT("FwHint"), FolderRoot, FVector(GlassMenu_WinTextX, -1.0f, GlassMenu_WinBottomZ), GlassMenu_WinHintSize, 0.0f);
	BrowserHintText->SetTextRenderColor(DimLabelColor);
	BrowserPageText = MakeTextAt(TEXT("FwPage"), FolderRoot, FVector(GlassMenu_WinTextX, GlassMenu_WinPageTextY, GlassMenu_WinBottomZ), GlassMenu_WinPathSize, 0.0f);
	BrowserEmptyText = MakeTextAt(TEXT("FwEmpty"), FolderRoot, FVector(GlassMenu_WinTextX, 0.0f, 0.0f), GlassMenu_WinEmptySize, 0.0f);
	BrowserEmptyText->SetTextRenderColor(DimLabelColor);

	// Icon, when given, sits at IconY (window frame); the caption, when given, is centred on
	// the chip unless LabelY is set, in which case it is left-aligned from there.
	auto MakeWindowButton = [this, MakeFace, MakeTextAt, MakeBoxAt](const TCHAR* Tag, float Y, float Z, float HalfY, float HalfZ, bool bPrimary, const EGlassIcon* IconKind, float IconY, float IconSize, const TCHAR* Caption, float TextSize, const float* LabelY) -> FMemoryRoomFolderButton
	{
		const FString Name(Tag);
		FMemoryRoomFolderButton Button;
		Button.HalfY = HalfY;
		Button.HalfZ = HalfZ;
		Button.bPrimary = bPrimary;
		Button.Box = MakeBoxAt(Name + TEXT("Box"), FolderRoot, FVector(GlassMenu_WinBoxX, Y, Z), FVector(PressHalfX, HalfY, HalfZ));
		Button.Panel = MakeFace(Name + TEXT("Panel"), FolderRoot, FVector(GlassMenu_WinChipX, Y, Z), HalfY * 2.0f, HalfZ * 2.0f, 11);
		if (IconKind)
		{
			Button.IconKind = *IconKind;
			Button.Icon = MakeFace(Name + TEXT("Icon"), FolderRoot, FVector(GlassMenu_WinIconX, IconY, Z), IconSize, IconSize, 12);
		}
		if (Caption && Caption[0] != 0)
		{
			Button.Label = MakeTextAt(Name + TEXT("Text"), FolderRoot, FVector(GlassMenu_WinTextX, LabelY ? *LabelY : Y, Z), TextSize, 0.0f);
			Button.Label->SetText(FText::FromString(Caption));
			if (LabelY)
			{
				Button.Label->SetHorizontalAlignment(EHTA_Left);
			}
		}
		return Button;
	};

	const float WinLeftY = -GlassMenu_WinEdgeY;
	const float UpY = WinLeftY + GlassMenu_WinUpHalfY;
	const float CloseY = GlassMenu_WinEdgeY - GlassMenu_WinCloseHalf;
	const float PrevY = WinLeftY + GlassMenu_WinPagerHalf;
	const float NextY = PrevY + 2.0f * GlassMenu_WinPageGapY;
	const float UseY = GlassMenu_WinEdgeY - GlassMenu_WinUseHalfY;
	const EGlassIcon UpIcon = EGlassIcon::Up;
	const EGlassIcon CloseIcon = EGlassIcon::Close;
	const EGlassIcon PrevIcon = EGlassIcon::Prev;
	const EGlassIcon NextIcon = EGlassIcon::Next;
	const EGlassIcon CheckIcon = EGlassIcon::Check;
	const float UpLabelY = UpY - 1.0f;
	const float UseLabelY = UseY - GlassMenu_WinUseHalfY + 5.0f;

	BrowserUp = MakeWindowButton(TEXT("FwUp"), UpY, GlassMenu_WinBarZ, GlassMenu_WinUpHalfY, GlassMenu_WinBtnHalfZ, false, &UpIcon, UpY - 2.7f, 2.6f, TEXT("Up"), 2.0f, &UpLabelY);
	BrowserUp.bUpShape = true;
	BrowserClose = MakeWindowButton(TEXT("FwClose"), CloseY, GlassMenu_WinBarZ, GlassMenu_WinCloseHalf, GlassMenu_WinCloseHalf, false, &CloseIcon, CloseY, 2.6f, nullptr, 0.0f, nullptr);
	BrowserPrev = MakeWindowButton(TEXT("FwPrev"), PrevY, GlassMenu_WinBottomZ, GlassMenu_WinPagerHalf, GlassMenu_WinPagerHalf, false, &PrevIcon, PrevY, 2.6f, nullptr, 0.0f, nullptr);
	BrowserNext = MakeWindowButton(TEXT("FwNext"), NextY, GlassMenu_WinBottomZ, GlassMenu_WinPagerHalf, GlassMenu_WinPagerHalf, false, &NextIcon, NextY, 2.6f, nullptr, 0.0f, nullptr);
	BrowserUse = MakeWindowButton(TEXT("FwUse"), UseY, GlassMenu_WinBottomZ, GlassMenu_WinUseHalfY, GlassMenu_WinUseHalfZ, true, &CheckIcon, UseY - GlassMenu_WinUseHalfY + 2.8f, 2.8f, TEXT("Use this folder"), 1.8f, &UseLabelY);

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
		Tile.Text = MakeTextAt(Tag + TEXT("Name"), FolderRoot, FVector(GlassMenu_WinTextX, Y, Z - 1.8f), GlassMenu_WinNameSize, 0.0f);
		Tile.Detail = MakeTextAt(Tag + TEXT("Count"), FolderRoot, FVector(GlassMenu_WinTextX, Y, Z - 4.4f), GlassMenu_WinCountSize, 0.0f);
		Tile.Detail->SetTextRenderColor(DimLabelColor);
		FolderTiles.Add(Tile);
	}
	SetFolderBrowserVisible(false);
}

void AMemoryRoomMenuActor::ApplyActiveTab()
{
	// Only the active tab's rows are visible and pressable; the other tabs' rows have collision
	// off so the pointer can never hit something that is not drawn.
	auto SetBox = [](UBoxComponent* Box, bool bOn)
	{
		if (Box) Box->SetCollisionEnabled(bOn ? ECollisionEnabled::QueryOnly : ECollisionEnabled::NoCollision);
	};
	auto SetShown = [](USceneComponent* Component, bool bOn)
	{
		if (Component) Component->SetVisibility(bOn);
	};
	for (FMemoryRoomMenuRow& Row : Rows)
	{
		const bool bOn = Row.Tab == ActiveTab;
		SetBox(Row.MinusBox, bOn);
		SetBox(Row.PlusBox, bOn);
		SetShown(Row.MinusPanel, bOn);
		SetShown(Row.PlusPanel, bOn);
		SetShown(Row.RowPanel, bOn);
		SetShown(Row.MinusIcon, bOn);
		SetShown(Row.PlusIcon, bOn);
		SetShown(Row.Icon, bOn);
		SetShown(Row.Label, bOn);
		SetShown(Row.ValueText, bOn);
	}
	const UPrimitiveComponent* Hovered = HoveredComponent.Get();
	if (Hovered)
	{
		for (const FMemoryRoomMenuRow& Row : Rows)
		{
			if (Row.Tab != ActiveTab && (Row.MinusBox == Hovered || Row.PlusBox == Hovered))
			{
				HoveredComponent.Reset();
				break;
			}
		}
	}
	ApplyHoverVisuals();
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

	// The actor origin is the board's vertical centre (BuildBoard), so the placement does not
	// depend on how tall the board is. Rev 3: sit below eye level, then make sure the top edge
	// is below the eye whatever the height: half the board's height, foreshortened by the tilt,
	// plus a small margin.
	const float BoardScaleNow = FMath::Max(BoardScale, 0.05f);
	const float TopEdgeBelowEyeCm = BoardPanelHeightCm * 0.5f * BoardScaleNow * FMath::Cos(FMath::DegreesToRadians(BoardTiltDegrees)) + 3.0f;
	const float CentreBelowEyeCm = FMath::Max(BoardCentreBelowEyeCm, TopEdgeBelowEyeCm);
	FVector Location = ViewLocation + Forward * FMath::Max(BoardDistanceCm, 30.0f) - FVector(0.0f, 0.0f, CentreBelowEyeCm);

	// The folder window hangs off the board's right edge, so while it is open the board slides
	// left by the pair's off-centre amount and the two sit centred in the view; it comes back to
	// dead ahead when the window closes.
	if (bFolderBrowserOpen)
	{
		const FVector Right = FVector::CrossProduct(FVector::UpVector, Forward);
		Location -= Right * (FolderPairCenterYCm * BoardScaleNow);
	}

	// The board's content is on its -X face (see BuildBoard), so +X must point away from the
	// viewer - i.e. along Forward.
	FRotator Rotation = Forward.Rotation();
	// Negative pitch dips +X, so the -X (content) face looks up and back toward the head.
	Rotation.Pitch = -BoardTiltDegrees;
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

	for (FMemoryRoomFolderButton& Tab : TabButtons)
	{
		if (Settle(Tab.HoverBlend, Hovered == Tab.Box))
		{
			ApplyTilePop(Tab.Panel, WidgetDepthX, Tab.HalfY, Tab.HalfZ, Tab.HoverBlend);
		}
	}
	for (FMemoryRoomMenuRow& Row : Rows)
	{
		if (Settle(Row.MinusHoverBlend, Hovered == Row.MinusBox))
		{
			ApplyTilePop(Row.MinusPanel, WidgetDepthX, Row.TileHalfY, Row.TileHalfZ, Row.MinusHoverBlend);
		}
		if (Row.PlusPanel && Settle(Row.PlusHoverBlend, Hovered == Row.PlusBox))
		{
			ApplyTilePop(Row.PlusPanel, WidgetDepthX, Row.TileHalfY, Row.TileHalfZ, Row.PlusHoverBlend);
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
		FolderRoot->SetRelativeScale3D(FVector(Grow));
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
	// The menu always opens on Home. The folder window is never opened here: only the Choose
	// folder chip opens it (first run just highlights that chip, see ApplyHoverVisuals).
	ActiveTab = 0;
	ApplyActiveTab();
	RefreshText();
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
	for (const FMemoryRoomFolderButton& Tab : TabButtons)
	{
		if (Tab.Box == Component) return true;
	}
	for (const FMemoryRoomMenuRow& Row : Rows)
	{
		if (Row.Tab == ActiveTab && (Row.MinusBox == Component || Row.PlusBox == Component)) return true;
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
	int32 TabHit = INDEX_NONE;
	for (int32 i = 0; i < TabButtons.Num(); ++i)
	{
		if (TabButtons[i].Box && TabButtons[i].Box == HoveredComponent.Get()) TabHit = i;
	}
	if (TabHit != INDEX_NONE)
	{
		ActiveTab = TabHit;
		ApplyActiveTab();
		bPressed = true;
	}
	else if (bFolderBrowserOpen && IsFolderWindowComponent(HoveredComponent.Get()))
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
		// Slide the board left so the board + window pair is centred in the view.
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
	if (!FolderRoot) return;
	// Same plane and scale as the board (it is a child of the board root): right of the board
	// with a gap, top edges aligned, turned a few degrees inward toward the viewer.
	const float WindowY = BoardPanelWidthCm * 0.5f + FolderWindowGapCm + GlassMenu_WinWidth * 0.5f;
	const float WindowZ = BoardTopLocalZCm - GlassMenu_WinHeight * 0.5f;
	FolderRoot->SetRelativeLocationAndRotation(FVector(0.0f, WindowY, WindowZ), FRotator(0.0f, FolderWindowInwardYawDegrees, 0.0f));
	// Middle of the pair, from the board's left edge to the window's right edge.
	FolderPairCenterYCm = (-BoardPanelWidthCm * 0.5f + WindowY + GlassMenu_WinWidth * 0.5f) * 0.5f;
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
#if !PLATFORM_ANDROID
		// On the PC the root level is the list of places; "All photos" only ever scanned the
		// Pictures folder there, which would misread beside the places, so it is not offered.
		if (Album.Id == TEXT("@all")) continue;
#endif
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
			Tile.Detail->SetText(FText::FromString(Album.Count > 0 ? (Album.Count == 1 ? FString(TEXT("1 photo")) : FString::Printf(TEXT("%d photos"), Album.Count)) : FString()));
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

	// Breadcrumb: "Pictures > Trips > 2024". Long paths keep their head and tail.
	TArray<FString> Crumbs;
	FString Walk = BrowserCurrentParentId;
	for (int32 Guard = 0; Guard < 8 && !Walk.IsEmpty(); ++Guard)
	{
		const FMemoryRoomAlbum* Found = Albums.FindByPredicate([&Walk](const FMemoryRoomAlbum& Album) { return Album.Id == Walk; });
		if (!Found) break;
		Crumbs.Insert(Found->Name, 0);
		Walk = GetParentOfFolder(Walk);
	}
	const FString Separator = FString(TEXT(" ")) + FString::Chr(0x203A) + TEXT(" ");
#if PLATFORM_ANDROID
	// Headset folders hang off a "Headset" root; PC places are already named (Pictures, Desktop...).
	Crumbs.Insert(TEXT("Headset"), 0);
#else
	if (Crumbs.Num() == 0) Crumbs.Add(TEXT("Photo places"));
#endif
	FString Path = FString::Join(Crumbs, *Separator);
	Path = GlassMenu_MiddleTruncate(Path, GlassMenu_PathMaxChars);
	BrowserPathText->SetText(FText::FromString(Path));

	// Hint line: first-run prompt, else how many photos the current folder holds.
	FString Hint;
	if (!MemoryRoomManager->HasPhotoSourceChoice())
	{
		Hint = TEXT("Choose a folder to begin.");
	}
	else if (const FMemoryRoomAlbum* Current = Albums.FindByPredicate([this](const FMemoryRoomAlbum& Album) { return Album.Id == BrowserCurrentParentId; }))
	{
		Hint = Current->Count > 0 ? FString::Printf(TEXT("%d photos in this folder"), Current->Count) : FString();
	}
	BrowserHintText->SetText(FText::FromString(Hint));

	BrowserPageText->SetText(FText::FromString(FString::Printf(TEXT("%d / %d"), BrowserPage + 1, PageCount)));

	// Controls: close is always live; the rest only when they would do something.
	auto SetButtonActive = [](FMemoryRoomFolderButton& Button, bool bActive)
	{
		if (Button.Box) Button.Box->SetCollisionEnabled(bActive ? ECollisionEnabled::QueryOnly : ECollisionEnabled::NoCollision);
		if (Button.Panel) Button.Panel->SetVisibility(bActive);
		if (Button.Icon) Button.Icon->SetVisibility(bActive);
		if (Button.Label) Button.Label->SetVisibility(bActive);
	};
	SetButtonActive(BrowserClose, true);
	SetButtonActive(BrowserUp, !BrowserCurrentParentId.IsEmpty());
	SetButtonActive(BrowserPrev, BrowserPage > 0);
	SetButtonActive(BrowserNext, BrowserPage + 1 < PageCount);
	SetButtonActive(BrowserUse, !BrowserPendingSelectionId.IsEmpty());
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
			// Sentence case for the names; the values keep their own case ("Sunset", "Low").
			Row.Label->SetText(FText::FromString(GlassMenu_SentenceCase(Name.ToString())));
			if (Row.ValueText) Row.ValueText->SetText(Value);
		}
		else
		{
			const TCHAR* Text = TEXT("Close");
			switch (Row.Action)
			{
			case EMemoryRoomMenuAction::ResetView: Text = TEXT("Reset view"); break;
			case EMemoryRoomMenuAction::JumpToNewest: Text = TEXT("Jump to newest"); break;
			case EMemoryRoomMenuAction::ReloadPhotos: Text = TEXT("Reload photos"); break;
			case EMemoryRoomMenuAction::ChoosePhotoSource: Text = TEXT("Choose folder"); break;
			case EMemoryRoomMenuAction::Close: Text = TEXT("Close"); break;
			}
			Row.Label->SetText(FText::FromString(Text));
		}
	}

	if (SubtitleText)
	{
		// "Photos: <current folder>". The album list may not have arrived yet, so fall back to
		// the last path piece of the saved id (desktop ids are folder paths).
		FString FolderName = TEXT("not chosen yet");
		if (MemoryRoomManager && MemoryRoomManager->HasPhotoSourceChoice())
		{
			const FString& SelectedId = MemoryRoomManager->GetSelectedPhotoAlbumId();
			if (SelectedId.IsEmpty() || SelectedId == TEXT("@all"))
			{
				FolderName = TEXT("All photos");
			}
			else if (const FMemoryRoomAlbum* Selected = MemoryRoomManager->GetPhotoAlbums().FindByPredicate([&SelectedId](const FMemoryRoomAlbum& Album) { return Album.Id == SelectedId; }))
			{
				FolderName = Selected->Name;
			}
			else
			{
				FolderName = FPaths::GetCleanFilename(SelectedId);
				if (FolderName.IsEmpty()) FolderName = SelectedId;
			}
		}
		const bool bNoChoice = MemoryRoomManager && !MemoryRoomManager->HasPhotoSourceChoice();
		// First run: say what to do instead of naming a folder (the Choose folder chip is amber).
		SubtitleText->SetText(FText::FromString(bNoChoice ? FString(TEXT("Choose a folder to begin")) : FString(TEXT("Photos: ")) + GlassMenu_Truncate(FolderName, 30)));
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

	// Rev 3 smoky warm glass, fixed. Panels: dark warm brown (#281E16) body at 0.75. Chips:
	// #3A2C20 at 0.6. Rim light #FFE6C8 blended over the body colour (the rim texel's alpha stays
	// opaque enough that the edge reads as a lit line, not a hole). Highlight: amber (#F2B866)
	// body 0.45, rim 0.9. Painted once here, never per frame.
	using MemoryRoomUI::FGlassPanelSpec;
	const FLinearColor SmokePanelTint = FLinearColor::FromSRGBColor(FColor(0x28, 0x1E, 0x16));
	const FLinearColor SmokeChipTint = FLinearColor::FromSRGBColor(FColor(0x3A, 0x2C, 0x20));
	const FLinearColor SmokeRimLight = FLinearColor::FromSRGBColor(FColor(0xFF, 0xE6, 0xC8));
	const FLinearColor SmokeAmber = FLinearColor::FromSRGBColor(FColor(0xF2, 0xB8, 0x66));
	auto Panel = [SmokePanelTint, SmokeRimLight](float W, float H) { FGlassPanelSpec S; S.WidthCm = W; S.HeightCm = H; S.Tint = SmokePanelTint; S.RimTint = FMath::Lerp(SmokePanelTint, SmokeRimLight, 0.35f); S.BodyAlpha = 0.75f; S.RimAlpha = 0.88f; S.RimPixels = 1.5f; S.SheenAlpha = 0.05f; return S; };
	auto Chip = [SmokeChipTint, SmokeRimLight](float W, float H) { FGlassPanelSpec S; S.WidthCm = W; S.HeightCm = H; S.Tint = SmokeChipTint; S.RimTint = FMath::Lerp(SmokeChipTint, SmokeRimLight, 0.35f); S.BodyAlpha = 0.60f; S.RimAlpha = 0.80f; S.RimPixels = 1.5f; S.SheenAlpha = 0.04f; return S; };
	auto Hot = [SmokeAmber](float W, float H) { FGlassPanelSpec S; S.WidthCm = W; S.HeightCm = H; S.Tint = SmokeAmber; S.RimTint = FMath::Lerp(SmokeAmber, FLinearColor::White, 0.15f); S.BodyAlpha = 0.45f; S.RimAlpha = 0.90f; S.RimPixels = 2.0f; S.SheenAlpha = 0.06f; return S; };
	auto Make = [this, Base](const FGlassPanelSpec& Spec)
	{
		return MemoryRoomUI::MakeTranslucentMaterial(Base, MemoryRoomUI::MakeFrostedPanelTexture(Spec), this);
	};

	const float ActionHalfY = GlassMenu_ChipW * 0.5f;
	const float ActionHalfZ = GlassMenu_ChipH * 0.5f;
	BoardGlassMaterial = Make(Panel(BoardPanelWidthCm, BoardPanelHeightCm));
	SquareChipMaterial = Make(Chip(GlassMenu_ArrowHalfY * 2.0f, GlassMenu_ArrowHalfZ * 2.0f));
	SquareChipHotMaterial = Make(Hot(GlassMenu_ArrowHalfY * 2.0f, GlassMenu_ArrowHalfZ * 2.0f));
	TabChipMaterial = Make(Chip(8.6f, GlassMenu_TabH));
	TabChipHotMaterial = Make(Hot(8.6f, GlassMenu_TabH));
	WideChipMaterial = Make(Chip(ActionHalfY * 2.0f, ActionHalfZ * 2.0f));
	WideChipHotMaterial = Make(Hot(ActionHalfY * 2.0f, ActionHalfZ * 2.0f));
	UpChipMaterial = Make(Chip(GlassMenu_WinUpHalfY * 2.0f, GlassMenu_WinBtnHalfZ * 2.0f));
	UpChipHotMaterial = Make(Hot(GlassMenu_WinUpHalfY * 2.0f, GlassMenu_WinBtnHalfZ * 2.0f));
	FolderWindowMaterial = Make(Panel(GlassMenu_WinWidth, GlassMenu_WinHeight));
	FGlassPanelSpec TileSpec = Chip(GlassMenu_WinTileW, GlassMenu_WinTileH);
	TileSpec.bFolderGlyph = true;
	FolderTileMaterial = Make(TileSpec);
	FGlassPanelSpec TileHotSpec = Hot(GlassMenu_WinTileW, GlassMenu_WinTileH);
	TileHotSpec.bFolderGlyph = true;
	FolderTileHotMaterial = Make(TileHotSpec);

	if (BackgroundPanel && BoardGlassMaterial) BackgroundPanel->SetMaterial(0, BoardGlassMaterial);
	// Setting rows' slim backgrounds are static wide chips.
	for (FMemoryRoomMenuRow& Row : Rows)
	{
		if (Row.RowPanel && WideChipMaterial) Row.RowPanel->SetMaterial(0, WideChipMaterial);
	}
	if (BrowserPanel && FolderWindowMaterial) BrowserPanel->SetMaterial(0, FolderWindowMaterial);

	// Painted icons: one small translucent texture per shape, shared by every plane that uses it.
	TMap<uint8, UMaterialInstanceDynamic*> IconMaterials;
	auto IconMaterialFor = [this, Base, &IconMaterials](MemoryRoomUI::EGlassIcon Kind) -> UMaterialInstanceDynamic*
	{
		UMaterialInstanceDynamic*& Slot = IconMaterials.FindOrAdd(static_cast<uint8>(Kind), nullptr);
		if (!Slot)
		{
			Slot = MemoryRoomUI::MakeTranslucentMaterial(Base, MemoryRoomUI::MakeIconTexture(Kind, FLinearColor::FromSRGBColor(TitleColor)), this);
		}
		return Slot;
	};
	for (FMemoryRoomMenuRow& Row : Rows)
	{
		if (Row.Icon) Row.Icon->SetMaterial(0, IconMaterialFor(Row.IconKind));
		if (Row.MinusIcon) Row.MinusIcon->SetMaterial(0, IconMaterialFor(MemoryRoomUI::EGlassIcon::Prev));
		if (Row.PlusIcon) Row.PlusIcon->SetMaterial(0, IconMaterialFor(MemoryRoomUI::EGlassIcon::Next));
	}
	for (FMemoryRoomFolderButton* Button : { &BrowserUp, &BrowserClose, &BrowserPrev, &BrowserNext, &BrowserUse })
	{
		if (Button->Icon) Button->Icon->SetMaterial(0, IconMaterialFor(Button->IconKind));
	}

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
	// Tabs: the active one is amber, the others smoky glass that goes amber under the pointer.
	for (int32 i = 0; i < TabButtons.Num(); ++i)
	{
		FMemoryRoomFolderButton& Tab = TabButtons[i];
		const bool bTabHot = Hovered == Tab.Box;
		const bool bActive = i == ActiveTab;
		if (Tab.Panel && TabChipMaterial && TabChipHotMaterial) Tab.Panel->SetMaterial(0, (bTabHot || bActive) ? TabChipHotMaterial : TabChipMaterial);
		if (Tab.Label) Tab.Label->SetTextRenderColor((bTabHot || bActive) ? HighlightColor : LabelColor);
	}
	// First run (no saved folder): the Choose folder chip is amber at rest.
	const bool bNeedsFolderChoice = MemoryRoomManager && !MemoryRoomManager->HasPhotoSourceChoice();
	for (FMemoryRoomMenuRow& Row : Rows)
	{
		const bool bMinusHot = Hovered == Row.MinusBox;
		const bool bPlusHot = Hovered == Row.PlusBox;

		// Chips swap to the amber glass at once; size and lift ease in over a few frames
		// (TickHoverAnimation). Setting arrows use the square chip, action rows the wide one.
		const bool bPrompt = !Row.bIsSetting && Row.Action == EMemoryRoomMenuAction::ChoosePhotoSource && bNeedsFolderChoice;
		UMaterialInterface* Rest = Row.bIsSetting ? SquareChipMaterial.Get() : (bPrompt ? WideChipHotMaterial.Get() : WideChipMaterial.Get());
		UMaterialInterface* HotMat = Row.bIsSetting ? SquareChipHotMaterial.Get() : WideChipHotMaterial.Get();
		if (Row.MinusPanel && Rest && HotMat) Row.MinusPanel->SetMaterial(0, bMinusHot ? HotMat : Rest);
		if (Row.PlusPanel && Rest && HotMat) Row.PlusPanel->SetMaterial(0, bPlusHot ? HotMat : Rest);

		if (Row.Label)
		{
			// A setting row lights its name only when one of its own arrows is under the
			// pointer, so a crowded glance still tells you which row you are about to change.
			const bool bTileHot = Row.bIsSetting ? (bMinusHot || bPlusHot) : (bMinusHot || bPrompt);
			Row.Label->SetTextRenderColor(bTileHot ? HighlightColor : LabelColor);
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
		UMaterialInterface* Rest = Button->bPrimary ? WideChipHotMaterial.Get() : (Button->bUpShape ? UpChipMaterial.Get() : SquareChipMaterial.Get());
		UMaterialInterface* HotMat = Button->bPrimary ? WideChipHotMaterial.Get() : (Button->bUpShape ? UpChipHotMaterial.Get() : SquareChipHotMaterial.Get());
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
