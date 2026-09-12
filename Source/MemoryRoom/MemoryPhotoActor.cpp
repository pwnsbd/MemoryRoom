// Fill out your copyright notice in the Description page of Project Settings.

#include "MemoryPhotoActor.h"
#include "MemoryRoom.h"

#include "Async/Async.h"
#include "Components/SceneComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture.h"
#include "Engine/Texture2D.h"
#include "HAL/FileManager.h"
#include "ImageUtils.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "MemoryRoomThemeAsset.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
const TCHAR* LexToString(EMemoryPhotoOrientation Orientation)
{
	switch (Orientation)
	{
	case EMemoryPhotoOrientation::Portrait:
		return TEXT("Portrait");
	case EMemoryPhotoOrientation::Landscape:
		return TEXT("Landscape");
	case EMemoryPhotoOrientation::Square:
		return TEXT("Square");
	default:
		return TEXT("Unknown");
	}
}

uint16 ReadExifUInt16(const uint8* Data, int64 Size, int64 Offset, bool bLittleEndian)
{
	if (Offset < 0 || Offset + 1 >= Size)
	{
		return 0;
	}

	return bLittleEndian
		? static_cast<uint16>(Data[Offset] | (Data[Offset + 1] << 8))
		: static_cast<uint16>((Data[Offset] << 8) | Data[Offset + 1]);
}

uint32 ReadExifUInt32(const uint8* Data, int64 Size, int64 Offset, bool bLittleEndian)
{
	if (Offset < 0 || Offset + 3 >= Size)
	{
		return 0;
	}

	return bLittleEndian
		? static_cast<uint32>(Data[Offset] | (Data[Offset + 1] << 8) | (Data[Offset + 2] << 16) | (Data[Offset + 3] << 24))
		: static_cast<uint32>((Data[Offset] << 24) | (Data[Offset + 1] << 16) | (Data[Offset + 2] << 8) | Data[Offset + 3]);
}

int32 ReadJpegExifOrientation(const TArray<uint8>& CompressedFileData)
{
	const uint8* Data = CompressedFileData.GetData();
	const int64 Size = CompressedFileData.Num();
	if (Size < 4 || Data[0] != 0xFF || Data[1] != 0xD8)
	{
		return 1;
	}

	int64 Offset = 2;
	while (Offset + 4 <= Size)
	{
		if (Data[Offset] != 0xFF)
		{
			break;
		}

		while (Offset < Size && Data[Offset] == 0xFF)
		{
			++Offset;
		}
		if (Offset >= Size)
		{
			break;
		}

		const uint8 Marker = Data[Offset++];
		if (Marker == 0xDA || Marker == 0xD9)
		{
			break;
		}
		if (Offset + 2 > Size)
		{
			break;
		}

		const uint16 SegmentLength = static_cast<uint16>((Data[Offset] << 8) | Data[Offset + 1]);
		Offset += 2;
		if (SegmentLength < 2 || Offset + SegmentLength - 2 > Size)
		{
			break;
		}

		const int64 SegmentStart = Offset;
		const int64 SegmentSize = SegmentLength - 2;
		if (Marker == 0xE1 && SegmentSize >= 14 && FMemory::Memcmp(Data + SegmentStart, "Exif\0\0", 6) == 0)
		{
			const int64 TiffStart = SegmentStart + 6;
			const bool bLittleEndian = Data[TiffStart] == 'I' && Data[TiffStart + 1] == 'I';
			const bool bBigEndian = Data[TiffStart] == 'M' && Data[TiffStart + 1] == 'M';
			if (!bLittleEndian && !bBigEndian)
			{
				return 1;
			}

			if (ReadExifUInt16(Data, Size, TiffStart + 2, bLittleEndian) != 42)
			{
				return 1;
			}

			const uint32 FirstIfdOffset = ReadExifUInt32(Data, Size, TiffStart + 4, bLittleEndian);
			const int64 IfdStart = TiffStart + FirstIfdOffset;
			const uint16 EntryCount = ReadExifUInt16(Data, Size, IfdStart, bLittleEndian);
			for (uint16 EntryIndex = 0; EntryIndex < EntryCount; ++EntryIndex)
			{
				const int64 EntryOffset = IfdStart + 2 + static_cast<int64>(EntryIndex) * 12;
				if (EntryOffset + 12 > Size)
				{
					break;
				}

				const uint16 Tag = ReadExifUInt16(Data, Size, EntryOffset, bLittleEndian);
				if (Tag == 0x0112)
				{
					const uint16 Type = ReadExifUInt16(Data, Size, EntryOffset + 2, bLittleEndian);
					const uint32 Count = ReadExifUInt32(Data, Size, EntryOffset + 4, bLittleEndian);
					if (Type == 3 && Count >= 1)
					{
						const int32 Orientation = ReadExifUInt16(Data, Size, EntryOffset + 8, bLittleEndian);
						return Orientation >= 1 && Orientation <= 8 ? Orientation : 1;
					}
					return 1;
				}
			}
			return 1;
		}

		Offset += SegmentSize;
	}

	return 1;
}

void ApplyExifOrientationToPixels(const TArray<FColor>& SourcePixels, int32 SourceWidth, int32 SourceHeight, int32 Orientation, TArray<FColor>& OutPixels, int32& OutWidth, int32& OutHeight)
{
	OutWidth = SourceWidth;
	OutHeight = SourceHeight;
	if (SourceWidth <= 0 || SourceHeight <= 0 || SourcePixels.Num() != SourceWidth * SourceHeight)
	{
		OutPixels = SourcePixels;
		return;
	}

	if (Orientation < 2 || Orientation > 8)
	{
		OutPixels = SourcePixels;
		return;
	}

	const bool bSwapsAxes = Orientation >= 5 && Orientation <= 8;
	OutWidth = bSwapsAxes ? SourceHeight : SourceWidth;
	OutHeight = bSwapsAxes ? SourceWidth : SourceHeight;
	OutPixels.SetNumUninitialized(OutWidth * OutHeight);

	for (int32 Y = 0; Y < SourceHeight; ++Y)
	{
		for (int32 X = 0; X < SourceWidth; ++X)
		{
			int32 DestX = X;
			int32 DestY = Y;
			switch (Orientation)
			{
			case 2: DestX = SourceWidth - 1 - X; DestY = Y; break;
			case 3: DestX = SourceWidth - 1 - X; DestY = SourceHeight - 1 - Y; break;
			case 4: DestX = X; DestY = SourceHeight - 1 - Y; break;
			case 5: DestX = Y; DestY = X; break;
			case 6: DestX = SourceHeight - 1 - Y; DestY = X; break;
			case 7: DestX = SourceHeight - 1 - Y; DestY = SourceWidth - 1 - X; break;
			case 8: DestX = Y; DestY = SourceWidth - 1 - X; break;
			default: break;
			}

			OutPixels[DestY * OutWidth + DestX] = SourcePixels[Y * SourceWidth + X];
		}
	}
}

// Mean colour of the image, sampled rather than summed. A photo's average is what the
// light it casts should look like, and every 17th pixel is more than enough to find it.
FLinearColor ComputeAveragePhotoColor(const TArray<FColor>& Pixels)
{
	if (Pixels.Num() <= 0)
	{
		return FLinearColor::White;
	}

	const int32 Stride = FMath::Max(Pixels.Num() / 4096, 1);
	double SumR = 0.0, SumG = 0.0, SumB = 0.0;
	int32 SampleCount = 0;
	for (int32 Index = 0; Index < Pixels.Num(); Index += Stride)
	{
		const FLinearColor Linear = FLinearColor(Pixels[Index]);
		SumR += Linear.R;
		SumG += Linear.G;
		SumB += Linear.B;
		++SampleCount;
	}

	if (SampleCount <= 0)
	{
		return FLinearColor::White;
	}

	FLinearColor Average(static_cast<float>(SumR / SampleCount), static_cast<float>(SumG / SampleCount), static_cast<float>(SumB / SampleCount), 1.0f);

	// Normalise so a dark photo still casts light, just in its own hue rather than dimly.
	const float Peak = FMath::Max3(Average.R, FMath::Max(Average.G, 0.0001f), Average.B);
	if (Peak > KINDA_SMALL_NUMBER)
	{
		Average.R /= Peak;
		Average.G /= Peak;
		Average.B /= Peak;
	}
	return Average;
}

// Reads just enough of the file to learn the image size. Decoding a whole JPEG only to
// find its aspect ratio was costing ~180ms per photo on the game thread at startup.
bool ReadImageDimensionsFromFile(const FString& NormalizedFilePath, int32& OutWidth, int32& OutHeight)
{
	OutWidth = 0;
	OutHeight = 0;

	TUniquePtr<FArchive> Reader(IFileManager::Get().CreateFileReader(*NormalizedFilePath));
	if (!Reader)
	{
		return false;
	}

	// Headers live near the front, but a large embedded EXIF thumbnail can push the JPEG
	// frame header back a long way, so allow a generous prefix.
	const int64 BytesToRead = FMath::Min(Reader->TotalSize(), static_cast<int64>(256 * 1024));
	if (BytesToRead < 16)
	{
		return false;
	}

	TArray<uint8> Header;
	Header.SetNumUninitialized(static_cast<int32>(BytesToRead));
	Reader->Serialize(Header.GetData(), BytesToRead);
	Reader->Close();

	const uint8* Data = Header.GetData();
	const int64 Size = Header.Num();

	// PNG: IHDR always sits at a fixed offset, width then height, big endian.
	if (Size >= 24 && Data[0] == 0x89 && Data[1] == 'P' && Data[2] == 'N' && Data[3] == 'G')
	{
		OutWidth = static_cast<int32>((Data[16] << 24) | (Data[17] << 16) | (Data[18] << 8) | Data[19]);
		OutHeight = static_cast<int32>((Data[20] << 24) | (Data[21] << 16) | (Data[22] << 8) | Data[23]);
		return OutWidth > 0 && OutHeight > 0;
	}

	// JPEG: walk the marker segments looking for a start-of-frame, which carries the size.
	if (Size >= 4 && Data[0] == 0xFF && Data[1] == 0xD8)
	{
		int64 Offset = 2;
		while (Offset + 4 <= Size)
		{
			if (Data[Offset] != 0xFF)
			{
				break;
			}
			while (Offset < Size && Data[Offset] == 0xFF)
			{
				++Offset;
			}
			if (Offset >= Size)
			{
				break;
			}

			const uint8 Marker = Data[Offset++];
			// Start of scan or end of image: pixel data from here, no header left to find.
			if (Marker == 0xDA || Marker == 0xD9)
			{
				break;
			}
			if (Offset + 2 > Size)
			{
				break;
			}

			const int32 SegmentLength = (Data[Offset] << 8) | Data[Offset + 1];
			if (SegmentLength < 2)
			{
				break;
			}

			// SOF0-SOF15 carry the frame size, except DHT/JPG/DAC which share the range.
			const bool bIsStartOfFrame = Marker >= 0xC0 && Marker <= 0xCF && Marker != 0xC4 && Marker != 0xC8 && Marker != 0xCC;
			if (bIsStartOfFrame && Offset + 7 < Size)
			{
				OutHeight = (Data[Offset + 3] << 8) | Data[Offset + 4];
				OutWidth = (Data[Offset + 5] << 8) | Data[Offset + 6];
				break;
			}

			Offset += SegmentLength;
		}

		if (OutWidth > 0 && OutHeight > 0)
		{
			// Rotated photos report their pre-rotation size, so swap to match how it displays.
			const int32 Orientation = ReadJpegExifOrientation(Header);
			if (Orientation >= 5 && Orientation <= 8)
			{
				Swap(OutWidth, OutHeight);
			}
			return true;
		}
	}

	return false;
}

bool DecodePhotoImageFromFile(const FString& NormalizedFilePath, int32 MaximumRuntimeTextureDimension, TArray<FColor>& OutPixels, int32& OutWidth, int32& OutHeight)
{
	TArray<uint8> CompressedFileData;
	if (!FFileHelper::LoadFileToArray(CompressedFileData, *NormalizedFilePath) || CompressedFileData.Num() == 0)
	{
		UE_LOG(LogMemoryRoom, Warning, TEXT("failed to read runtime image file: %s"), *NormalizedFilePath);
		return false;
	}

	const FString Extension = FPaths::GetExtension(NormalizedFilePath, false).ToLower();
	IImageWrapperModule& ImageWrapperModule = FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
	EImageFormat ImageFormat = ImageWrapperModule.DetectImageFormat(CompressedFileData.GetData(), CompressedFileData.Num());
	if (ImageFormat == EImageFormat::Invalid)
	{
		ImageFormat = Extension == TEXT("png") ? EImageFormat::PNG : EImageFormat::JPEG;
	}

	if (ImageFormat != EImageFormat::PNG && ImageFormat != EImageFormat::JPEG)
	{
		UE_LOG(LogMemoryRoom, Warning, TEXT("detected unsupported runtime image format for: %s"), *NormalizedFilePath);
		return false;
	}

	TSharedPtr<IImageWrapper> ImageWrapper = ImageWrapperModule.CreateImageWrapper(ImageFormat);
	if (!ImageWrapper.IsValid() || !ImageWrapper->SetCompressed(CompressedFileData.GetData(), CompressedFileData.Num()))
	{
		UE_LOG(LogMemoryRoom, Warning, TEXT("failed to parse compressed image data: %s"), *NormalizedFilePath);
		return false;
	}

	TArray64<uint8> RawPixelData;
	if (!ImageWrapper->GetRaw(ERGBFormat::BGRA, 8, RawPixelData) || RawPixelData.Num() == 0)
	{
		UE_LOG(LogMemoryRoom, Warning, TEXT("failed to decode image pixels: %s"), *NormalizedFilePath);
		return false;
	}

	const int32 ImageWidth = ImageWrapper->GetWidth();
	const int32 ImageHeight = ImageWrapper->GetHeight();
	if (ImageWidth <= 0 || ImageHeight <= 0)
	{
		UE_LOG(LogMemoryRoom, Warning, TEXT("decoded invalid image dimensions for: %s"), *NormalizedFilePath);
		return false;
	}

	const int64 SourcePixelCount = static_cast<int64>(ImageWidth) * static_cast<int64>(ImageHeight);
	if (RawPixelData.Num() != SourcePixelCount * sizeof(FColor))
	{
		UE_LOG(LogMemoryRoom, Warning, TEXT("decoded unexpected pixel data size for: %s"), *NormalizedFilePath);
		return false;
	}

	TArray<FColor> SourcePixels;
	SourcePixels.SetNumUninitialized(static_cast<int32>(SourcePixelCount));
	FMemory::Memcpy(SourcePixels.GetData(), RawPixelData.GetData(), RawPixelData.Num());

	const int32 ExifOrientation = ImageFormat == EImageFormat::JPEG ? ReadJpegExifOrientation(CompressedFileData) : 1;
	int32 OrientedImageWidth = ImageWidth;
	int32 OrientedImageHeight = ImageHeight;
	TArray<FColor> OrientedPixels;
	ApplyExifOrientationToPixels(SourcePixels, ImageWidth, ImageHeight, ExifOrientation, OrientedPixels, OrientedImageWidth, OrientedImageHeight);

	int32 RuntimeImageWidth = OrientedImageWidth;
	int32 RuntimeImageHeight = OrientedImageHeight;
	TArray<FColor> RuntimePixels;
	const int32 SafeMaximumDimension = FMath::Max(MaximumRuntimeTextureDimension, 1);
	const int32 LargestSourceDimension = FMath::Max(OrientedImageWidth, OrientedImageHeight);
	if (LargestSourceDimension > SafeMaximumDimension)
	{
		const float Scale = static_cast<float>(SafeMaximumDimension) / static_cast<float>(LargestSourceDimension);
		RuntimeImageWidth = FMath::Max(1, FMath::RoundToInt(static_cast<float>(OrientedImageWidth) * Scale));
		RuntimeImageHeight = FMath::Max(1, FMath::RoundToInt(static_cast<float>(OrientedImageHeight) * Scale));
		RuntimePixels.SetNumUninitialized(RuntimeImageWidth * RuntimeImageHeight);
		FImageUtils::ImageResize(OrientedImageWidth, OrientedImageHeight, OrientedPixels, RuntimeImageWidth, RuntimeImageHeight, RuntimePixels, false);
	}
	else
	{
		RuntimePixels = MoveTemp(OrientedPixels);
	}

	OutPixels = MoveTemp(RuntimePixels);
	OutWidth = RuntimeImageWidth;
	OutHeight = RuntimeImageHeight;
	return true;
}
}

AMemoryPhotoActor::AMemoryPhotoActor()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);

	PhotoMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("PhotoMesh"));
	PhotoMesh->SetupAttachment(SceneRoot);
	PhotoMesh->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	PhotoMesh->SetCollisionObjectType(ECC_WorldDynamic);
	PhotoMesh->SetCollisionResponseToAllChannels(ECR_Ignore);
	PhotoMesh->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	// The photo drops a soft shadow so it reads as hanging in the space, not pasted onto it.
	// Only photos inside the sun's short shadow distance actually cast (see the sky dome).
	PhotoMesh->SetCastShadow(bPhotoCastsShadow);
	PhotoMesh->bCastFarShadow = false;
	PhotoMesh->bCastDynamicShadow = bPhotoCastsShadow;

	HoverFrameMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("HoverFrame"));
	HoverFrameMesh->SetupAttachment(PhotoMesh);
	HoverFrameMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	HoverFrameMesh->SetGenerateOverlapEvents(false);
	HoverFrameMesh->SetHiddenInGame(true);
	HoverFrameMesh->SetCastShadow(false);

	GlowLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("GlowLight"));
	GlowLight->SetupAttachment(SceneRoot);
	// Local Z is the photo's normal, so sit the light just off the front face; inside the
	// plane it would light almost nothing.
	GlowLight->SetRelativeLocation(FVector(0.0f, 0.0f, 18.0f));
	GlowLight->SetMobility(EComponentMobility::Movable);
	// Shadow casting from a dozen lights is not affordable on a Quest, and the room has
	// little for them to fall on anyway.
	GlowLight->SetCastShadows(false);
	GlowLight->bAffectsWorld = true;
	GlowLight->SetIntensityUnits(ELightUnits::Unitless);
	GlowLight->SetAttenuationRadius(320.0f);
	GlowLight->SetIntensity(0.0f);
	GlowLight->SetVisibility(false);

	MetadataRoot = CreateDefaultSubobject<USceneComponent>(TEXT("MetadataRoot"));
	MetadataRoot->SetupAttachment(SceneRoot);

	MetadataPanelMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("MetadataPanel"));
	MetadataPanelMesh->SetupAttachment(MetadataRoot);
	MetadataPanelMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	MetadataPanelMesh->SetGenerateOverlapEvents(false);
	MetadataPanelMesh->SetCastShadow(false);

	TitleText = CreateDefaultSubobject<UTextRenderComponent>(TEXT("MetadataTitle"));
	TitleText->SetupAttachment(MetadataRoot);
	DateText = CreateDefaultSubobject<UTextRenderComponent>(TEXT("MetadataDate"));
	DateText->SetupAttachment(MetadataRoot);
	LocationText = CreateDefaultSubobject<UTextRenderComponent>(TEXT("MetadataLocation"));
	LocationText->SetupAttachment(MetadataRoot);
	CaptionText = CreateDefaultSubobject<UTextRenderComponent>(TEXT("MetadataCaption"));
	CaptionText->SetupAttachment(MetadataRoot);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> PanelMeshFinder(TEXT("/Engine/BasicShapes/Plane.Plane"));
	if (PanelMeshFinder.Succeeded())
	{
		MetadataPanelMesh->SetStaticMesh(PanelMeshFinder.Object);
		HoverFrameMesh->SetStaticMesh(PanelMeshFinder.Object);
	}

	// Deliberately left unset. It used to default to M_ValidTargetBeam, which is the
	// pointer's beam material - once that is made bright and additive, sharing it turns the
	// hover frame into a photo-sized glowing rectangle. Assign a dim material in the
	// Blueprint if you want a hover halo; otherwise the frame just stays hidden.
	HoverFrameMaterial = nullptr;

	MoveSpeed = 6.0f;
	RotationSpeed = 6.0f;
	ScaleSpeed = 6.0f;
	PositionTolerance = 0.5f;
	RotationTolerance = 0.5f;
	ScaleTolerance = 0.005f;
	AnimationDuration = 0.22f;
	ExpandedScale = 2.0f;
	HoverAmountParameterName = TEXT("HoverAmount");
	EmissiveStrengthParameterName = TEXT("EmissiveStrength");
	HoverAmount = 1.0f;
	// Kept subtle: the pointer spark already confirms the target, so the photo only needs
	// a faint lift in brightness, not a full glow.
	HoverEmissiveStrength = 0.12f;
	BasePhotoHeight = 1.0f;
	MinPhotoWidth = 0.25f;
	MaxPhotoWidth = 4.0f;
	PhotoTextureParameterName = TEXT("PhotoTexture");
	LoadedImageWidth = 0;
	LoadedImageHeight = 0;
	LoadedAspectRatio = 0.0f;
	PhotoOrientation = EMemoryPhotoOrientation::Square;
	LoadedFilePath = TEXT("");
	SquareAspectTolerance = 0.05f;
	LandscapeDisplayHeight = BasePhotoHeight * 100.0f;
	PortraitDisplayHeight = BasePhotoHeight * 100.0f;
	SquareDisplaySize = BasePhotoHeight * 100.0f;
	MinimumDisplayWidth = MinPhotoWidth * 100.0f;
	MaximumDisplayWidth = MaxPhotoWidth * 100.0f;
	MaximumDisplayHeight = MaxPhotoWidth * 100.0f;
	HoverScaleMultiplier = 1.01f;
	HoverOffset = 1.25f;
	HoverFrameScale = FVector(1.045f, 1.045f, 1.0f);
	HoverFrameDepthOffset = -0.15f;
	bEnableHoverScale = true;
	bEnableHoverOffset = true;
	bEnableHoverOutline = false;
	HoverCustomDepthStencilValue = 1;
	bEnableHoverLayoutDebugLogging = false;
	PhotoTitle = FText::FromString(TEXT("Untitled Memory"));
	PhotoDate = FText::GetEmpty();
	PhotoLocation = FText::GetEmpty();
	PhotoCaption = FText::GetEmpty();
	MetadataRelativeLocation = FVector(0.0f, 0.0f, -70.0f);
	MetadataPanelScale = FVector(0.9f, 0.28f, 1.0f);
	MetadataPanelOpacityParameterName = TEXT("Opacity");
	MetadataFadeDuration = 0.18f;
	MetadataTextColor = FColor::White;

	MetadataRoot->SetRelativeLocation(MetadataRelativeLocation);
	MetadataPanelMesh->SetRelativeScale3D(MetadataPanelScale);
	HoverFrameMesh->SetRelativeLocation(FVector(0.0f, 0.0f, HoverFrameDepthOffset));
	HoverFrameMesh->SetRelativeScale3D(HoverFrameScale);
	if (HoverFrameMaterial)
	{
		HoverFrameMesh->SetMaterial(0, HoverFrameMaterial);
	}

	UTextRenderComponent* TextComponents[] = { TitleText, DateText, LocationText, CaptionText };
	const FVector TextLocations[] = { FVector(0.0f, -28.0f, 2.0f), FVector(0.0f, -10.0f, 2.0f), FVector(0.0f, 8.0f, 2.0f), FVector(0.0f, 28.0f, 2.0f) };
	for (int32 Index = 0; Index < 4; ++Index)
	{
		if (TextComponents[Index])
		{
			TextComponents[Index]->SetHorizontalAlignment(EHorizTextAligment::EHTA_Center);
			TextComponents[Index]->SetVerticalAlignment(EVerticalTextAligment::EVRTA_TextCenter);
			TextComponents[Index]->SetWorldSize(Index == 0 ? 9.0f : 6.0f);
			TextComponents[Index]->SetRelativeLocation(TextLocations[Index]);
			TextComponents[Index]->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		}
	}

	MetadataAlpha = 0.0f;
	MetadataFadeElapsed = 0.0f;
	MetadataFadeStartAlpha = 0.0f;
	MetadataFadeTargetAlpha = 0.0f;
	LastLayoutScrollOffset = 0.0f;
	PhotoMeshAspectScale = FVector::OneVector;
	BaseVisualScale = FVector::OneVector;
	DisplayWidth = SquareDisplaySize;
	DisplayHeight = SquareDisplaySize;
	AssignedPhotoIndex = INDEX_NONE;
	AssignmentGeneration = 0;
	AssignedStableMediaId = TEXT("");
	AssignedContentUri = TEXT("");
	bHasStoredOriginalTransform = false;
	bHasHoverSourceLocation = false;
	bHovered = false;
	bExpanded = false;
	AnimationElapsed = 0.0f;
	bAnimating = false;
	bHoverFollowing = false;
	bMetadataFading = false;
	bPendingMetadataFadeIn = false;
	bPendingCollapse = false;
	bUseMaterialHover = false;
}

void AMemoryPhotoActor::BeginPlay()
{
	Super::BeginPlay();

	if (PhotoMesh && PhotoMesh->GetMaterial(0))
	{
		DynamicMaterial = PhotoMesh->CreateDynamicMaterialInstance(0);
		LastAppliedMaterialOpacity = -1.0f;
		LastAppliedMaterialBlurAmount = -1.0f;
		LastAppliedMaterialBrightness = -1.0f;
		bUseMaterialHover = MaterialHasScalarParameter(HoverAmountParameterName) || MaterialHasScalarParameter(EmissiveStrengthParameterName);
		ApplyFrameStyleToMaterial();
	}

	UpdatePhotoMeshAspectRatio();

	if (MetadataPanelMaterial && MetadataPanelMesh)
	{
		MetadataPanelMesh->SetMaterial(0, MetadataPanelMaterial);
	}
	// The theme's glass slab is already a dynamic instance shared by every photo; a dynamic
	// instance cannot parent another one, and the slab has no opacity parameter anyway.
	if (MetadataPanelMesh && MetadataPanelMesh->GetMaterial(0) && !MetadataPanelMesh->GetMaterial(0)->IsA<UMaterialInstanceDynamic>())
	{
		DynamicMetadataPanelMaterial = MetadataPanelMesh->CreateDynamicMaterialInstance(0);
	}

	MetadataRoot->SetRelativeLocation(MetadataRelativeLocation);
	MetadataPanelMesh->SetRelativeScale3D(MetadataPanelScale);
	if (HoverFrameMesh)
	{
		HoverFrameMesh->SetRelativeLocation(FVector(0.0f, 0.0f, HoverFrameDepthOffset));
		HoverFrameMesh->SetRelativeScale3D(HoverFrameScale);
		if (HoverFrameMaterial)
		{
			HoverFrameMesh->SetMaterial(0, HoverFrameMaterial);
		}
	}
	UpdateHoverFeedback();
	UpdateMetadataText();
	SetMetadataAlpha(0.0f);
}

void AMemoryPhotoActor::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (bAnimating)
	{
		AnimationElapsed += FMath::Max(DeltaTime, 0.0f);
		const float Duration = FMath::Max(AnimationDuration, KINDA_SMALL_NUMBER);
		const float Alpha = FMath::Clamp(AnimationElapsed / Duration, 0.0f, 1.0f);
		const float SmoothAlpha = Alpha * Alpha * (3.0f - 2.0f * Alpha);

		FTransform NewTransform;
		NewTransform.Blend(AnimationStartTransform, TargetTransform, SmoothAlpha);
		SetActorTransform(NewTransform, false, nullptr, ETeleportType::TeleportPhysics);

		if (Alpha >= 1.0f)
		{
			SetActorTransform(TargetTransform, false, nullptr, ETeleportType::TeleportPhysics);
			bAnimating = false;
			if (bPendingCollapse)
			{
				bPendingCollapse = false;
				bExpanded = false;
			}
			if (bPendingMetadataFadeIn)
			{
				bPendingMetadataFadeIn = false;
				StartMetadataFade(true);
			}
		}
	}
	else if (bHoverFollowing && bHovered && !bExpanded)
	{
		const FTransform CurrentTransform = GetActorTransform();
		const FVector NewLocation = FMath::VInterpTo(CurrentTransform.GetLocation(), TargetTransform.GetLocation(), DeltaTime, MoveSpeed);
		const FQuat NewRotation = FMath::QInterpTo(CurrentTransform.GetRotation(), TargetTransform.GetRotation(), DeltaTime, RotationSpeed);
		const FVector NewScale = FMath::VInterpTo(CurrentTransform.GetScale3D(), TargetTransform.GetScale3D(), DeltaTime, ScaleSpeed);
		SetActorTransform(FTransform(NewRotation, NewLocation, NewScale), false, nullptr, ETeleportType::TeleportPhysics);
		const bool bAlreadyAtTarget = FVector::DistSquared(NewLocation, TargetTransform.GetLocation()) <= FMath::Square(PositionTolerance)
			&& NewRotation.AngularDistance(TargetTransform.GetRotation()) <= FMath::DegreesToRadians(RotationTolerance)
			&& FVector::DistSquared(NewScale, TargetTransform.GetScale3D()) <= FMath::Square(ScaleTolerance);
		bHoverFollowing = !bAlreadyAtTarget;
	}

	if (bMetadataFading)
	{
		MetadataFadeElapsed += FMath::Max(DeltaTime, 0.0f);
		const float Duration = FMath::Max(MetadataFadeDuration, KINDA_SMALL_NUMBER);
		const float Alpha = FMath::Clamp(MetadataFadeElapsed / Duration, 0.0f, 1.0f);
		const float SmoothAlpha = Alpha * Alpha * (3.0f - 2.0f * Alpha);
		SetMetadataAlpha(FMath::Lerp(MetadataFadeStartAlpha, MetadataFadeTargetAlpha, SmoothAlpha));
		if (Alpha >= 1.0f)
		{
			bMetadataFading = false;
			SetMetadataAlpha(MetadataFadeTargetAlpha);
		}
	}

	if (ShouldIdleDrift())
	{
		ApplyIdleDrift();
	}

	UpdateTickEnabled();
}

bool AMemoryPhotoActor::LoadImageFromFile(const FString& FilePath, int32 MaximumRuntimeTextureDimension)
{
	FString NormalizedFilePath = FilePath;
	NormalizedFilePath.TrimStartAndEndInline();
	FPaths::NormalizeFilename(NormalizedFilePath);

	if (NormalizedFilePath.IsEmpty() || !FPaths::FileExists(NormalizedFilePath))
	{
		UE_LOG(LogMemoryRoom, Warning, TEXT("%s cannot load runtime image because file is missing: %s"), *GetName(), *NormalizedFilePath);
		return false;
	}

	const FString Extension = FPaths::GetExtension(NormalizedFilePath, false).ToLower();
	const bool bSupportedExtension = Extension == TEXT("jpg") || Extension == TEXT("jpeg") || Extension == TEXT("png");
	if (!bSupportedExtension)
	{
		UE_LOG(LogMemoryRoom, Warning, TEXT("%s cannot load unsupported runtime image format: %s"), *GetName(), *NormalizedFilePath);
		return false;
	}

	TArray<FColor> DecodedPixels;
	int32 DecodedWidth = 0;
	int32 DecodedHeight = 0;
	if (!DecodePhotoImageFromFile(NormalizedFilePath, MaximumRuntimeTextureDimension, DecodedPixels, DecodedWidth, DecodedHeight))
	{
		return false;
	}

	return ApplyDecodedRuntimeImage(NormalizedFilePath, MoveTemp(DecodedPixels), DecodedWidth, DecodedHeight);
}

bool AMemoryPhotoActor::ReadImageDimensions(const FString& FilePath, int32& OutWidth, int32& OutHeight)
{
	FString NormalizedFilePath = FilePath;
	NormalizedFilePath.TrimStartAndEndInline();
	FPaths::NormalizeFilename(NormalizedFilePath);
	return ReadImageDimensionsFromFile(NormalizedFilePath, OutWidth, OutHeight);
}

void AMemoryPhotoActor::GetDisplaySizeForAspect(float AspectRatio, float& OutWidth, float& OutHeight) const
{
	const float SafeAspect = FMath::Max(AspectRatio, 0.01f);
	const float SafeTolerance = FMath::Max(SquareAspectTolerance, 0.0f);

	float Width;
	float Height;
	if (SafeAspect < 1.0f - SafeTolerance)
	{
		Height = FMath::Max(PortraitDisplayHeight, 0.01f);
		Width = Height * SafeAspect;
	}
	else if (SafeAspect > 1.0f + SafeTolerance)
	{
		Height = FMath::Max(LandscapeDisplayHeight, 0.01f);
		Width = Height * SafeAspect;
	}
	else
	{
		Width = FMath::Max(SquareDisplaySize, 0.01f);
		Height = Width;
	}

	const float SafeMinimumWidth = FMath::Max(MinimumDisplayWidth, 0.0f);
	const float SafeMaximumWidth = MaximumDisplayWidth > 0.0f ? FMath::Max(MaximumDisplayWidth, SafeMinimumWidth) : 0.0f;
	const float SafeMaximumHeight = FMath::Max(MaximumDisplayHeight, 0.0f);
	if (SafeMinimumWidth > 0.0f && Width < SafeMinimumWidth)
	{
		Width = SafeMinimumWidth;
		Height = Width / SafeAspect;
	}
	if (SafeMaximumWidth > 0.0f && Width > SafeMaximumWidth)
	{
		Width = SafeMaximumWidth;
		Height = Width / SafeAspect;
	}
	if (SafeMaximumHeight > 0.0f && Height > SafeMaximumHeight)
	{
		Height = SafeMaximumHeight;
		Width = Height * SafeAspect;
	}

	OutWidth = Width;
	OutHeight = Height;
}

void AMemoryPhotoActor::ApplyAspectRatioHint(float AspectRatio)
{
	const float SafeAspect = FMath::Max(AspectRatio, 0.01f);
	// Store as a nominal pixel size so ApplyDisplaySizeFromLoadedImage classifies orientation.
	LoadedImageWidth = FMath::RoundToInt(SafeAspect * 1000.0f);
	LoadedImageHeight = 1000;
	LoadedAspectRatio = SafeAspect;
	ApplyDisplaySizeFromLoadedImage();
}

bool AMemoryPhotoActor::PrepareFromFile(const FString& FilePath)
{
	FString NormalizedFilePath = FilePath;
	NormalizedFilePath.TrimStartAndEndInline();
	FPaths::NormalizeFilename(NormalizedFilePath);

	int32 HeaderWidth = 0;
	int32 HeaderHeight = 0;
	if (!ReadImageDimensionsFromFile(NormalizedFilePath, HeaderWidth, HeaderHeight))
	{
		UE_LOG(LogMemoryRoom, Warning, TEXT("%s could not read image dimensions from: %s"), *GetName(), *NormalizedFilePath);
		return false;
	}

	LoadedFilePath = NormalizedFilePath;
	LoadedImageWidth = FMath::Max(HeaderWidth, 1);
	LoadedImageHeight = FMath::Max(HeaderHeight, 1);
	LoadedAspectRatio = static_cast<float>(LoadedImageWidth) / static_cast<float>(LoadedImageHeight);
	ApplyDisplaySizeFromLoadedImage();
	PopulateMetadataFromFilePath(NormalizedFilePath);
	return true;
}

void AMemoryPhotoActor::PopulateMetadataFromFilePath(const FString& FilePath)
{
	// Title: the filename without its extension, with underscores/hyphens opened into spaces.
	// Nothing upstream authors a per-photo title, so the filename is the closest thing to one -
	// whatever the visitor (or their camera) named the memory when it was saved.
	FString BaseName = FPaths::GetBaseFilename(FilePath);
	BaseName.ReplaceInline(TEXT("_"), TEXT(" "));
	BaseName.ReplaceInline(TEXT("-"), TEXT(" "));
	BaseName.TrimStartAndEndInline();
	PhotoTitle = BaseName.IsEmpty() ? FText::FromString(TEXT("Untitled Memory")) : FText::FromString(BaseName);

	// Date: the file's own modification time on disk. This is the file's mtime, not its EXIF
	// capture date - only orientation is parsed out of EXIF today (see ReadJpegExifOrientation)
	// - so a copied or re-saved file shows when it landed on disk rather than when it was taken.
	PhotoDate = FText::GetEmpty();
	const FDateTime Timestamp = IFileManager::Get().GetTimeStamp(*FilePath);
	if (Timestamp != FDateTime::MinValue())
	{
		PhotoDate = FText::FromString(Timestamp.ToString(TEXT("%Y-%m-%d")));
	}

	// No source for either of these yet.
	PhotoLocation = FText::GetEmpty();
	PhotoCaption = FText::GetEmpty();

	UpdateMetadataText();
}

void AMemoryPhotoActor::LoadImageFromFileAsync(const FString& FilePath, int32 MaximumRuntimeTextureDimension)
{
	if (bTextureLoadInFlight)
	{
		return;
	}

	FString NormalizedFilePath = FilePath;
	NormalizedFilePath.TrimStartAndEndInline();
	FPaths::NormalizeFilename(NormalizedFilePath);

	if (NormalizedFilePath.IsEmpty() || !FPaths::FileExists(NormalizedFilePath))
	{
		UE_LOG(LogMemoryRoom, Warning, TEXT("%s cannot load runtime image because file is missing: %s"), *GetName(), *NormalizedFilePath);
		return;
	}

	// Covers the full-res load that follows an AssignPhotoIndex from Next/Prev navigation,
	// which goes straight here rather than through PrepareFromFile - without this the info
	// panel would keep showing the previous photo's title/date until something else happened
	// to touch PrepareFromFile again.
	if (LoadedFilePath != NormalizedFilePath)
	{
		PopulateMetadataFromFilePath(NormalizedFilePath);
	}

	bTextureLoadInFlight = true;
	TWeakObjectPtr<AMemoryPhotoActor> WeakThis(this);
	const int32 SafeMaximumDimension = FMath::Max(MaximumRuntimeTextureDimension, 1);

	AsyncTask(ENamedThreads::AnyBackgroundThreadNormalTask, [WeakThis, NormalizedFilePath, SafeMaximumDimension]()
	{
		TArray<FColor> DecodedPixels;
		int32 DecodedWidth = 0;
		int32 DecodedHeight = 0;
		const bool bDecodeSuccess = DecodePhotoImageFromFile(NormalizedFilePath, SafeMaximumDimension, DecodedPixels, DecodedWidth, DecodedHeight);

		AsyncTask(ENamedThreads::GameThread, [WeakThis, NormalizedFilePath, DecodedPixels = MoveTemp(DecodedPixels), DecodedWidth, DecodedHeight, bDecodeSuccess]() mutable
		{
			AMemoryPhotoActor* Photo = WeakThis.Get();
			if (!Photo)
			{
				return;
			}

			Photo->bTextureLoadInFlight = false;
			if (bDecodeSuccess)
			{
				Photo->ApplyDecodedRuntimeImage(NormalizedFilePath, MoveTemp(DecodedPixels), DecodedWidth, DecodedHeight);
			}
			else
			{
				UE_LOG(LogMemoryRoom, Warning, TEXT("%s failed to decode runtime image asynchronously: %s"), *Photo->GetName(), *NormalizedFilePath);
			}
		});
	});
}

bool AMemoryPhotoActor::ApplyDecodedRuntimeImage(const FString& NormalizedFilePath, TArray<FColor> DecodedPixels, int32 DecodedWidth, int32 DecodedHeight)
{
	if (!PhotoMesh)
	{
		UE_LOG(LogMemoryRoom, Warning, TEXT("%s cannot apply runtime image because PhotoMesh is missing."), *GetName());
		return false;
	}

	if (!DynamicMaterial)
	{
		DynamicMaterial = PhotoMesh->CreateDynamicMaterialInstance(0);
		LastAppliedMaterialOpacity = -1.0f;
		LastAppliedMaterialBlurAmount = -1.0f;
		LastAppliedMaterialBrightness = -1.0f;
	}
	if (!DynamicMaterial)
	{
		UE_LOG(LogMemoryRoom, Warning, TEXT("%s cannot apply runtime image because no dynamic material could be created."), *GetName());
		return false;
	}
	if (PhotoTextureParameterName.IsNone())
	{
		UE_LOG(LogMemoryRoom, Warning, TEXT("%s cannot apply runtime image because texture parameter name is None."), *GetName());
		return false;
	}

	UTexture* ExistingTexture = nullptr;
	if (!DynamicMaterial->GetTextureParameterValue(PhotoTextureParameterName, ExistingTexture))
	{
		UE_LOG(LogMemoryRoom, Warning, TEXT("%s material is missing texture parameter '%s'. Add it to the photo material instance/master."), *GetName(), *PhotoTextureParameterName.ToString());
		return false;
	}

	SetAveragePhotoColor(ComputeAveragePhotoColor(DecodedPixels));

	UTexture2D* NewRuntimeTexture = UTexture2D::CreateTransient(DecodedWidth, DecodedHeight, PF_B8G8R8A8);
	if (!NewRuntimeTexture || !NewRuntimeTexture->GetPlatformData() || NewRuntimeTexture->GetPlatformData()->Mips.Num() == 0)
	{
		UE_LOG(LogMemoryRoom, Warning, TEXT("%s failed to create transient texture for: %s"), *GetName(), *NormalizedFilePath);
		return false;
	}

	NewRuntimeTexture->SRGB = true;
	NewRuntimeTexture->NeverStream = true;
	NewRuntimeTexture->Filter = TF_Trilinear;
	NewRuntimeTexture->LODGroup = TEXTUREGROUP_UI;
	FTexture2DMipMap& Mip = NewRuntimeTexture->GetPlatformData()->Mips[0];
	void* TextureData = Mip.BulkData.Lock(LOCK_READ_WRITE);
	FMemory::Memcpy(TextureData, DecodedPixels.GetData(), DecodedPixels.Num() * sizeof(FColor));
	Mip.BulkData.Unlock();
	NewRuntimeTexture->UpdateResource();

	return ApplyRuntimeTexture(NormalizedFilePath, NewRuntimeTexture, DecodedWidth, DecodedHeight);
}

bool AMemoryPhotoActor::ApplyRuntimeTexture(const FString& SourceId, UTexture2D* Texture, int32 TextureWidth, int32 TextureHeight)
{
	if (!Texture || TextureWidth <= 0 || TextureHeight <= 0 || !PhotoMesh)
	{
		return false;
	}
	if (!DynamicMaterial)
	{
		DynamicMaterial = PhotoMesh->CreateDynamicMaterialInstance(0);
		LastAppliedMaterialOpacity = -1.0f;
		LastAppliedMaterialBlurAmount = -1.0f;
		LastAppliedMaterialBrightness = -1.0f;
	}
	if (!DynamicMaterial || PhotoTextureParameterName.IsNone())
	{
		return false;
	}
	UTexture* ExistingTexture = nullptr;
	if (!DynamicMaterial->GetTextureParameterValue(PhotoTextureParameterName, ExistingTexture))
	{
		UE_LOG(LogMemoryRoom, Warning, TEXT("%s material is missing texture parameter '%s'. Add it to the photo material instance/master."), *GetName(), *PhotoTextureParameterName.ToString());
		return false;
	}

	Texture->SRGB = true;
	Texture->Filter = TF_Trilinear;
	Texture->LODGroup = TEXTUREGROUP_UI;
	RuntimeTexture = Texture;
	LoadedImageWidth = TextureWidth;
	LoadedImageHeight = TextureHeight;
	LoadedAspectRatio = static_cast<float>(TextureWidth) / static_cast<float>(TextureHeight);
	LoadedFilePath = SourceId;
	DynamicMaterial->SetTextureParameterValue(PhotoTextureParameterName, RuntimeTexture);
	// A pooled photo may have had its material rebuilt when it was recycled, which clears
	// the cached emissive. Re-assert it here so the glow survives a photo swap instead of
	// waiting for the next time-of-day change.
	SetPhotoBrightness(PhotoBrightness);
	ApplyFrameStyleToMaterial();
	ApplyDisplaySizeFromLoadedImage();
	return true;
}

void AMemoryPhotoActor::UnloadRuntimeTexture()
{
	if (!RuntimeTexture)
	{
		return;
	}

	if (DynamicMaterial && !PhotoTextureParameterName.IsNone())
	{
		DynamicMaterial->SetTextureParameterValue(PhotoTextureParameterName, nullptr);
	}

	RuntimeTexture = nullptr;
}

void AMemoryPhotoActor::SetHovered(bool bNewHovered)
{
	if (bHovered == bNewHovered)
	{
		return;
	}

	EnsureOriginalTransformStored();
	bHovered = bNewHovered;
	UpdateHoverFeedback();
	RebuildTargetTransform();
}

void AMemoryPhotoActor::SetHoverSourceLocation(const FVector& SourceLocation)
{
	HoverSourceLocation = SourceLocation;
	bHasHoverSourceLocation = true;

	if (bHovered && !bExpanded)
	{
		RebuildTargetTransform(false);
	}
}

void AMemoryPhotoActor::SetLayoutTransform(const FTransform& NewLayoutTransform, float CurrentScrollOffset)
{
	LayoutTransform = NewLayoutTransform;
	OriginalTransform = NewLayoutTransform;
	LastLayoutScrollOffset = CurrentScrollOffset;
	bHasStoredOriginalTransform = true;

	if (bExpanded)
	{
		return;
	}

	if (bHovered)
	{
		RebuildTargetTransform(false);
		return;
	}

	TargetTransform = LayoutTransform;
	AnimationStartTransform = LayoutTransform;
	AnimationElapsed = 0.0f;
	bAnimating = false;
	bHoverFollowing = false;
	SetActorTransform(LayoutTransform, false, nullptr, ETeleportType::TeleportPhysics);
	UpdateTickEnabled();
}

void AMemoryPhotoActor::Expand(const FTransform& InTargetTransform)
{
	ExpandWithScale(InTargetTransform, ExpandedScale);
}

void AMemoryPhotoActor::ExpandWithScale(const FTransform& InTargetTransform, float ScaleMultiplier)
{
	EnsureOriginalTransformStored();

	bHovered = false;
	UpdateHoverFeedback();
	bExpanded = true;
	bPendingCollapse = false;
	bPendingMetadataFadeIn = true;
	StartMetadataFade(false);
	UpdateExpandedTargetTransform(InTargetTransform, ScaleMultiplier, false);
}

void AMemoryPhotoActor::UpdateExpandedTargetTransform(const FTransform& InTargetTransform, float ScaleMultiplier, bool bSnapToTarget)
{
	EnsureOriginalTransformStored();

	const FVector DesiredPhotoCenter = InTargetTransform.GetLocation();
	FTransform ExpandedTarget = InTargetTransform;
	ExpandedTarget.SetScale3D(OriginalTransform.GetScale3D() * FMath::Max(ScaleMultiplier, 0.01f));

	// Treat the incoming target location as the desired center of the visible photo,
	// not the actor pivot. Some photo meshes/blueprints have a pivot offset, which
	// otherwise leaves the grabbed/expanded image above or below the intended target.
	const FVector PhotoCenterOffset = ExpandedTarget.TransformVector(GetPhotoVisualCenterLocal());
	ExpandedTarget.SetLocation(DesiredPhotoCenter - PhotoCenterOffset);

	if (bSnapToTarget)
	{
		TargetTransform = ExpandedTarget;
		AnimationStartTransform = ExpandedTarget;
		AnimationElapsed = 0.0f;
		bAnimating = false;
		SetActorTransform(ExpandedTarget, false, nullptr, ETeleportType::TeleportPhysics);
		if (bPendingMetadataFadeIn)
		{
			bPendingMetadataFadeIn = false;
			StartMetadataFade(true);
		}
		UpdateTickEnabled();
		return;
	}

	StartAnimatingTo(ExpandedTarget);
}

void AMemoryPhotoActor::Collapse()
{
	if (!bHasStoredOriginalTransform)
	{
		return;
	}

	bHovered = false;
	UpdateHoverFeedback();
	bPendingCollapse = true;
	bPendingMetadataFadeIn = false;
	StartMetadataFade(false);
	StartAnimatingTo(LayoutTransform);
}

void AMemoryPhotoActor::ToggleExpanded(const FTransform& InTargetTransform)
{
	if (bExpanded)
	{
		Collapse();
	}
	else
	{
		Expand(InTargetTransform);
	}
}

void AMemoryPhotoActor::AssignPhotoIndex(int32 NewPhotoIndex)
{
	if (AssignedPhotoIndex == NewPhotoIndex)
	{
		return;
	}

	if (AssignedPhotoIndex != INDEX_NONE)
	{
		UnloadRuntimeTexture();
		LoadedFilePath.Reset();
		bTextureLoadInFlight = false;
	}
	AssignedPhotoIndex = NewPhotoIndex;
	OnAssignedPhotoIndexChanged(NewPhotoIndex);
}

void AMemoryPhotoActor::AssignAndroidMediaStoreRecord(int32 LogicalPhotoIndex, const FString& StableMediaId, const FString& ContentUri, int32 CorrectedWidth, int32 CorrectedHeight)
{
	const bool bChanged = AssignedPhotoIndex != LogicalPhotoIndex || AssignedStableMediaId != StableMediaId || AssignedContentUri != ContentUri;

	// The gallery re-asserts the record for every visible photo every frame. Without this
	// early out we re-ran ApplyDisplaySizeFromLoadedImage (and its component rescales) on
	// each of them, every frame, for an assignment that had not actually changed.
	const int32 SafeWidth = FMath::Max(CorrectedWidth, 1);
	const int32 SafeHeight = FMath::Max(CorrectedHeight, 1);
	if (!bChanged && LoadedImageWidth == SafeWidth && LoadedImageHeight == SafeHeight)
	{
		return;
	}

	if (bChanged)
	{
		++AssignmentGeneration;
		UnloadRuntimeTexture();
		bTextureLoadInFlight = false;
	}

	AssignedStableMediaId = StableMediaId;
	AssignedContentUri = ContentUri;
	LoadedFilePath = ContentUri;
	LoadedImageWidth = SafeWidth;
	LoadedImageHeight = SafeHeight;
	LoadedAspectRatio = static_cast<float>(LoadedImageWidth) / static_cast<float>(LoadedImageHeight);
	ApplyDisplaySizeFromLoadedImage();
	if (bChanged)
	{
		// A content:// URI isn't a real filesystem path - IFileManager's timestamp lookup just
		// fails on it and PhotoDate stays empty, which UpdateMetadataText already handles.
		PopulateMetadataFromFilePath(ContentUri);
	}

	if (bChanged)
	{
		OnAssignedPhotoIndexChanged(LogicalPhotoIndex);
	}
}

void AMemoryPhotoActor::ApplyViewingWindowState(float Opacity, float ScaleMultiplier, bool bInteractive, float BlurAmount)
{
	const float ClampedOpacity = FMath::Clamp(Opacity, 0.0f, 1.0f);
	const float ClampedBlurAmount = FMath::Max(BlurAmount, 0.0f);
	static_cast<void>(ScaleMultiplier);
	const bool bVisible = ClampedOpacity > KINDA_SMALL_NUMBER;

	SetActorHiddenInGame(!bVisible);
	if (!bVisible || !bInteractive)
	{
		SetHovered(false);
	}

	if (PhotoMesh)
	{
		PhotoMesh->SetCollisionEnabled(bVisible && bInteractive ? ECollisionEnabled::QueryOnly : ECollisionEnabled::NoCollision);
	}

	// The gallery pushes this state every frame for every visible photo. Each
	// SetScalarParameterValue dirties the material instance and queues a render-thread
	// update, so only write when a value actually moved. Scrolling changes opacity
	// smoothly and will still update; a photo sitting still costs nothing.
	if (DynamicMaterial
		&& (!FMath::IsNearlyEqual(ClampedOpacity, LastAppliedMaterialOpacity)
			|| !FMath::IsNearlyEqual(ClampedBlurAmount, LastAppliedMaterialBlurAmount)))
	{
		LastAppliedMaterialOpacity = ClampedOpacity;
		LastAppliedMaterialBlurAmount = ClampedBlurAmount;
		DynamicMaterial->SetScalarParameterValue(TEXT("Opacity"), ClampedOpacity);
		DynamicMaterial->SetScalarParameterValue(TEXT("Alpha"), ClampedOpacity);
		DynamicMaterial->SetScalarParameterValue(TEXT("Blur"), ClampedBlurAmount);
		DynamicMaterial->SetScalarParameterValue(TEXT("BlurAmount"), ClampedBlurAmount);
		DynamicMaterial->SetScalarParameterValue(TEXT("ThreadBlur"), ClampedBlurAmount);
	}

	// Visibility just changed, and drift only runs on visible photos.
	UpdateTickEnabled();
}

void AMemoryPhotoActor::SetPhotoBrightness(float NewBrightness)
{
	PhotoBrightness = FMath::Max(NewBrightness, 0.0f);

	// Same reasoning as the viewing-window state: the manager pushes this to every visible
	// photo, so only touch the material when the value actually moved.
	if (!DynamicMaterial
		|| PhotoBrightnessParameterName.IsNone()
		|| FMath::IsNearlyEqual(PhotoBrightness, LastAppliedMaterialBrightness))
	{
		return;
	}

	LastAppliedMaterialBrightness = PhotoBrightness;
	DynamicMaterial->SetScalarParameterValue(PhotoBrightnessParameterName, PhotoBrightness);
}

void AMemoryPhotoActor::SetFrameStyle(FLinearColor FrameTint, float EdgeWidth)
{
	FrameTintColor = FrameTint;
	FrameEdgeWidth = FMath::Clamp(EdgeWidth, 0.0f, 0.5f);
	ApplyFrameStyleToMaterial();
}

void AMemoryPhotoActor::ApplyThemeUI(const FMemoryRoomWristUIStyle& Style, UMaterialInterface* HoverFrame, UMaterialInterface* GlassPanel)
{
	if (HoverFrame && (!HoverFrameMaterial || bHoverFrameMaterialFromTheme))
	{
		HoverFrameMaterial = HoverFrame;
		bHoverFrameMaterialFromTheme = true;
	}
	if (GlassPanel && (!MetadataPanelMaterial || bMetadataPanelMaterialFromTheme))
	{
		MetadataPanelMaterial = GlassPanel;
		bMetadataPanelMaterialFromTheme = true;
		if (MetadataPanelMesh)
		{
			// Shared instance, applied directly: see the note in BeginPlay.
			MetadataPanelMesh->SetMaterial(0, GlassPanel);
			DynamicMetadataPanelMaterial = nullptr;
		}
	}

	MetadataTextColor = Style.LabelColor.ToFColor(true);
	MetadataTitleColor = Style.TitleColor.ToFColor(true);

	UpdateHoverFeedback();
	SetMetadataAlpha(MetadataAlpha);
}

void AMemoryPhotoActor::ApplyFrameStyleToMaterial()
{
	if (!DynamicMaterial)
	{
		return;
	}

	// Same "spray the names, the material takes what it knows" approach the viewing-window
	// state uses. A material without these parameters silently ignores the writes.
	if (!FrameTintColorParameterName.IsNone())
	{
		DynamicMaterial->SetVectorParameterValue(FrameTintColorParameterName, FrameTintColor);
	}
	if (!FrameEdgeWidthParameterName.IsNone())
	{
		DynamicMaterial->SetScalarParameterValue(FrameEdgeWidthParameterName, FrameEdgeWidth);
	}
}

void AMemoryPhotoActor::SetAveragePhotoColor(const FLinearColor& NewAverageColor)
{
	AveragePhotoColor = NewAverageColor;
	if (GlowLight && bGlowLightEnabled)
	{
		GlowLight->SetLightColor(AveragePhotoColor);
	}
}

void AMemoryPhotoActor::SetGlowLightEnabled(bool bEnabled, float Intensity)
{
	if (!GlowLight)
	{
		return;
	}

	const float SafeIntensity = bEnabled ? FMath::Max(Intensity, 0.0f) : 0.0f;
	if (bGlowLightEnabled == bEnabled && FMath::IsNearlyEqual(SafeIntensity, LastAppliedGlowIntensity))
	{
		return;
	}

	bGlowLightEnabled = bEnabled;
	LastAppliedGlowIntensity = SafeIntensity;

	if (bEnabled)
	{
		GlowLight->SetLightColor(AveragePhotoColor);
		GlowLight->SetIntensity(SafeIntensity);
	}
	GlowLight->SetVisibility(bEnabled);
}

void AMemoryPhotoActor::EnsureOriginalTransformStored()
{
	if (!bHasStoredOriginalTransform)
	{
		LayoutTransform = GetActorTransform();
		OriginalTransform = LayoutTransform;
		TargetTransform = LayoutTransform;
		bHasStoredOriginalTransform = true;
	}
}

FTransform AMemoryPhotoActor::BuildHoverTargetTransform() const
{
	FTransform NewTarget = LayoutTransform;
	if (bHovered)
	{
		if (bEnableHoverScale)
		{
			NewTarget.SetScale3D(LayoutTransform.GetScale3D() * HoverScaleMultiplier);
		}

		if (bEnableHoverOffset && bHasHoverSourceLocation && HoverOffset > 0.0f)
		{
			const FVector DirectionToSource = (HoverSourceLocation - LayoutTransform.GetLocation()).GetSafeNormal();
			if (!DirectionToSource.IsNearlyZero())
			{
				NewTarget.SetLocation(LayoutTransform.GetLocation() + DirectionToSource * HoverOffset);
			}
		}
	}
	return NewTarget;
}

void AMemoryPhotoActor::RebuildTargetTransform(bool bStartNewAnimation)
{
	EnsureOriginalTransformStored();

	if (bExpanded)
	{
		return;
	}

	const FTransform NewTarget = BuildHoverTargetTransform();
	if (bEnableHoverLayoutDebugLogging && bHovered)
	{
		UE_LOG(LogMemoryRoom, Log, TEXT("HoverLayout PhotoIndex=%d ScrollOffset=%.3f LayoutLocation=%s HoverTargetLocation=%s ActorLocation=%s"),
			AssignedPhotoIndex,
			LastLayoutScrollOffset,
			*LayoutTransform.GetLocation().ToCompactString(),
			*NewTarget.GetLocation().ToCompactString(),
			*GetActorLocation().ToCompactString());
	}

	if (bStartNewAnimation)
	{
		StartAnimatingTo(NewTarget);
	}
	else
	{
		TargetTransform = NewTarget;
		bHoverFollowing = true;
		UpdateTickEnabled();
	}
}

FVector AMemoryPhotoActor::GetPhotoVisualCenterLocal() const
{
	if (!PhotoMesh)
	{
		return FVector::ZeroVector;
	}

	FVector LocalBoundsMin = FVector::ZeroVector;
	FVector LocalBoundsMax = FVector::ZeroVector;
	PhotoMesh->GetLocalBounds(LocalBoundsMin, LocalBoundsMax);
	const FVector ComponentLocalCenter = (LocalBoundsMin + LocalBoundsMax) * 0.5f;
	return PhotoMesh->GetRelativeTransform().TransformPosition(ComponentLocalCenter);
}

float AMemoryPhotoActor::GetPhotoVisualHalfWidthLocal() const
{
	return GetDisplayWidth() * 0.5f;
}

float AMemoryPhotoActor::GetDisplayWidth() const
{
	return FMath::Max(DisplayWidth, 0.01f);
}

float AMemoryPhotoActor::GetDisplayHeight() const
{
	return FMath::Max(DisplayHeight, 0.01f);
}

void AMemoryPhotoActor::ApplyDisplaySizeFromLoadedImage()
{
	if (!PhotoMesh)
	{
		PhotoMeshAspectScale = FVector::OneVector;
		BaseVisualScale = FVector::OneVector;
		DisplayWidth = SquareDisplaySize;
		DisplayHeight = SquareDisplaySize;
		return;
	}

	float AspectRatio = LoadedAspectRatio;
	if (AspectRatio <= 0.0f)
	{
		UTexture* PhotoTexture = nullptr;
		if (DynamicMaterial && !PhotoTextureParameterName.IsNone())
		{
			DynamicMaterial->GetTextureParameterValue(PhotoTextureParameterName, PhotoTexture);
		}

		const UTexture2D* PhotoTexture2D = Cast<UTexture2D>(PhotoTexture);
		if (!PhotoTexture2D || PhotoTexture2D->GetSizeY() <= 0)
		{
			return;
		}
		AspectRatio = static_cast<float>(PhotoTexture2D->GetSizeX()) / static_cast<float>(PhotoTexture2D->GetSizeY());
	}

	const float SafeTolerance = FMath::Max(SquareAspectTolerance, 0.0f);
	if (AspectRatio < 1.0f - SafeTolerance)
	{
		PhotoOrientation = EMemoryPhotoOrientation::Portrait;
	}
	else if (AspectRatio > 1.0f + SafeTolerance)
	{
		PhotoOrientation = EMemoryPhotoOrientation::Landscape;
	}
	else
	{
		PhotoOrientation = EMemoryPhotoOrientation::Square;
	}

	float CalculatedDisplayWidth = SquareDisplaySize;
	float CalculatedDisplayHeight = SquareDisplaySize;
	if (PhotoOrientation == EMemoryPhotoOrientation::Landscape)
	{
		CalculatedDisplayHeight = FMath::Max(LandscapeDisplayHeight, 0.01f);
		CalculatedDisplayWidth = CalculatedDisplayHeight * AspectRatio;
	}
	else if (PhotoOrientation == EMemoryPhotoOrientation::Portrait)
	{
		CalculatedDisplayHeight = FMath::Max(PortraitDisplayHeight, 0.01f);
		CalculatedDisplayWidth = CalculatedDisplayHeight * AspectRatio;
	}
	else
	{
		CalculatedDisplayWidth = FMath::Max(SquareDisplaySize, 0.01f);
		CalculatedDisplayHeight = CalculatedDisplayWidth;
	}

	const float SafeMinimumWidth = FMath::Max(MinimumDisplayWidth, 0.0f);
	const float SafeMaximumWidth = MaximumDisplayWidth > 0.0f ? FMath::Max(MaximumDisplayWidth, SafeMinimumWidth) : 0.0f;
	const float SafeMaximumHeight = FMath::Max(MaximumDisplayHeight, 0.0f);
	if (SafeMinimumWidth > 0.0f && CalculatedDisplayWidth < SafeMinimumWidth)
	{
		CalculatedDisplayWidth = SafeMinimumWidth;
		CalculatedDisplayHeight = CalculatedDisplayWidth / AspectRatio;
	}
	if (SafeMaximumWidth > 0.0f && CalculatedDisplayWidth > SafeMaximumWidth)
	{
		CalculatedDisplayWidth = SafeMaximumWidth;
		CalculatedDisplayHeight = CalculatedDisplayWidth / AspectRatio;
	}
	if (SafeMaximumHeight > 0.0f && CalculatedDisplayHeight > SafeMaximumHeight)
	{
		CalculatedDisplayHeight = SafeMaximumHeight;
		CalculatedDisplayWidth = CalculatedDisplayHeight * AspectRatio;
	}

	FVector LocalBoundsMin = FVector::ZeroVector;
	FVector LocalBoundsMax = FVector::ZeroVector;
	PhotoMesh->GetLocalBounds(LocalBoundsMin, LocalBoundsMax);
	const float NativeWidth = FMath::Max(LocalBoundsMax.X - LocalBoundsMin.X, KINDA_SMALL_NUMBER);
	const float NativeHeight = FMath::Max(LocalBoundsMax.Y - LocalBoundsMin.Y, KINDA_SMALL_NUMBER);

	FVector NewVisualScale = PhotoMesh->GetRelativeScale3D();
	NewVisualScale.X = CalculatedDisplayWidth / NativeWidth;
	NewVisualScale.Y = CalculatedDisplayHeight / NativeHeight;
	PhotoMeshAspectScale = NewVisualScale;
	BaseVisualScale = NewVisualScale;
	DisplayWidth = CalculatedDisplayWidth;
	DisplayHeight = CalculatedDisplayHeight;
	PhotoMesh->SetRelativeScale3D(BaseVisualScale);

	if (!LoadedFilePath.IsEmpty())
	{
		UE_LOG(LogMemoryRoom, Log, TEXT("%s runtime image display: file='%s', pixels=%dx%d, aspect=%.4f, orientation=%s, display=%.3fx%.3f"),
			*GetName(), *LoadedFilePath, LoadedImageWidth, LoadedImageHeight, AspectRatio, LexToString(PhotoOrientation), DisplayWidth, DisplayHeight);
	}
}

void AMemoryPhotoActor::UpdatePhotoMeshAspectRatio()
{
	ApplyDisplaySizeFromLoadedImage();
}

void AMemoryPhotoActor::StartAnimatingTo(const FTransform& NewTargetTransform)
{
	TargetTransform = NewTargetTransform;
	AnimationStartTransform = GetActorTransform();
	AnimationElapsed = 0.0f;
	const bool bAlreadyAtTarget = FVector::DistSquared(AnimationStartTransform.GetLocation(), TargetTransform.GetLocation()) <= FMath::Square(PositionTolerance)
		&& AnimationStartTransform.GetRotation().AngularDistance(TargetTransform.GetRotation()) <= FMath::DegreesToRadians(RotationTolerance)
		&& FVector::DistSquared(AnimationStartTransform.GetScale3D(), TargetTransform.GetScale3D()) <= FMath::Square(ScaleTolerance);
	if (bAlreadyAtTarget)
	{
		SetActorTransform(TargetTransform, false, nullptr, ETeleportType::TeleportPhysics);
		bAnimating = false;
		bHoverFollowing = false;
		if (bPendingCollapse)
		{
			bPendingCollapse = false;
			bExpanded = false;
		}
		if (bPendingMetadataFadeIn)
		{
			bPendingMetadataFadeIn = false;
			StartMetadataFade(true);
		}
		UpdateTickEnabled();
		return;
	}

	bAnimating = true;
	bHoverFollowing = false;
	UpdateTickEnabled();
}

void AMemoryPhotoActor::UpdateHoverFeedback()
{
	const bool bActiveHover = bHovered && !bExpanded;
	if (DynamicMaterial && bUseMaterialHover)
	{
		DynamicMaterial->SetScalarParameterValue(HoverAmountParameterName, bActiveHover ? HoverAmount : 0.0f);
		DynamicMaterial->SetScalarParameterValue(EmissiveStrengthParameterName, bActiveHover ? HoverEmissiveStrength : 0.0f);
	}

	if (PhotoMesh)
	{
		PhotoMesh->SetRenderCustomDepth(bEnableHoverOutline && bActiveHover);
		PhotoMesh->SetCustomDepthStencilValue(FMath::Clamp(HoverCustomDepthStencilValue, 0, 255));
	}

	if (HoverFrameMesh)
	{
		// No material means no halo - only show the frame when one has been assigned.
		HoverFrameMesh->SetHiddenInGame(!bActiveHover || HoverFrameMaterial == nullptr);
		HoverFrameMesh->SetRelativeLocation(FVector(0.0f, 0.0f, HoverFrameDepthOffset));
		HoverFrameMesh->SetRelativeScale3D(HoverFrameScale);
		if (HoverFrameMaterial)
		{
			HoverFrameMesh->SetMaterial(0, HoverFrameMaterial);
		}
	}
}

void AMemoryPhotoActor::UpdateMetadataText()
{
	if (TitleText)
	{
		const FText Title = PhotoTitle.IsEmpty() ? FText::FromString(TEXT("Untitled Memory")) : PhotoTitle;
		TitleText->SetText(IsPanoramaCandidate() ? FText::Format(NSLOCTEXT("MemoryPhoto", "PanoramaTitle", "360 | {0}"), Title) : Title);
	}
	if (DateText)
	{
		DateText->SetText(PhotoDate.IsEmpty() ? FText::GetEmpty() : FText::Format(NSLOCTEXT("MemoryPhoto", "DateLine", "Date: {0}"), PhotoDate));
	}
	if (LocationText)
	{
		LocationText->SetText(PhotoLocation.IsEmpty() ? FText::GetEmpty() : FText::Format(NSLOCTEXT("MemoryPhoto", "LocationLine", "Location: {0}"), PhotoLocation));
	}
	if (CaptionText)
	{
		CaptionText->SetText(PhotoCaption);
	}
}

void AMemoryPhotoActor::SetMetadataAlpha(float Alpha)
{
	MetadataAlpha = bShowMetadataPanel ? FMath::Clamp(Alpha, 0.0f, 1.0f) : 0.0f;
	const bool bVisible = MetadataAlpha > KINDA_SMALL_NUMBER;
	if (MetadataRoot)
	{
		MetadataRoot->SetHiddenInGame(!bVisible, true);
	}

	const uint8 Alpha8 = static_cast<uint8>(FMath::RoundToInt(MetadataAlpha * 255.0f));
	FColor TextColor = MetadataTextColor;
	TextColor.A = Alpha8;
	FColor TitleColor = MetadataTitleColor;
	TitleColor.A = Alpha8;
	UTextRenderComponent* TextComponents[] = { TitleText, DateText, LocationText, CaptionText };
	for (UTextRenderComponent* TextComponent : TextComponents)
	{
		if (TextComponent)
		{
			TextComponent->SetTextRenderColor(TextComponent == TitleText ? TitleColor : TextColor);
			TextComponent->SetHiddenInGame(!bVisible);
		}
	}

	if (MetadataPanelMesh)
	{
		MetadataPanelMesh->SetHiddenInGame(!bVisible);
		MetadataPanelMesh->SetRelativeScale3D(MetadataPanelScale * FMath::Lerp(0.98f, 1.0f, MetadataAlpha));
	}
	if (DynamicMetadataPanelMaterial && !MetadataPanelOpacityParameterName.IsNone())
	{
		DynamicMetadataPanelMaterial->SetScalarParameterValue(MetadataPanelOpacityParameterName, MetadataAlpha);
		DynamicMetadataPanelMaterial->SetScalarParameterValue(TEXT("Alpha"), MetadataAlpha);
	}
}

void AMemoryPhotoActor::StartMetadataFade(bool bFadeIn)
{
	if (!bShowMetadataPanel)
	{
		// Panel disabled: keep it fully hidden and never animate it in.
		bMetadataFading = false;
		bPendingMetadataFadeIn = false;
		SetMetadataAlpha(0.0f);
		UpdateTickEnabled();
		return;
	}

	MetadataFadeStartAlpha = MetadataAlpha;
	MetadataFadeTargetAlpha = bFadeIn ? 1.0f : 0.0f;
	MetadataFadeElapsed = 0.0f;
	bMetadataFading = !FMath::IsNearlyEqual(MetadataFadeStartAlpha, MetadataFadeTargetAlpha, KINDA_SMALL_NUMBER);
	if (!bMetadataFading)
	{
		SetMetadataAlpha(MetadataFadeTargetAlpha);
	}
	UpdateTickEnabled();
}

void AMemoryPhotoActor::UpdateTickEnabled()
{
	SetActorTickEnabled(bAnimating || bHoverFollowing || bMetadataFading || ShouldIdleDrift());
}

bool AMemoryPhotoActor::ShouldIdleDrift() const
{
	// Only visible, settled photos drift. Anything mid-animation, held, or opened is being
	// driven deliberately and must not be nudged.
	return bEnableIdleDrift
		&& !bExpanded
		&& !bHovered
		&& !bAnimating
		&& !bHoverFollowing
		&& !IsHidden()
		&& bHasStoredOriginalTransform;
}

void AMemoryPhotoActor::ApplyIdleDrift()
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	const float Time = World->GetTimeSeconds();

	// Hash the index into a phase so neighbouring photos never sway in unison.
	uint32 Hash = static_cast<uint32>(AssignedPhotoIndex + 1) * 2654435761u;
	Hash ^= Hash >> 15;
	Hash *= 2246822519u;
	Hash ^= Hash >> 13;
	const float Phase = static_cast<float>(Hash & 0x0000FFFFu) / static_cast<float>(0x0000FFFFu) * TWO_PI;

	const float Speed = FMath::Max(IdleDriftSpeed, 0.0f);
	const float Amplitude = FMath::Max(IdleDriftAmplitudeCm, 0.0f);

	// Three slightly different rates keep the path from looking like a simple circle.
	const float SwayUp = FMath::Sin(Time * Speed + Phase) * Amplitude;
	const float SwaySide = FMath::Sin(Time * Speed * 0.77f + Phase * 1.7f) * Amplitude * 0.6f;
	const float SwayYaw = FMath::Sin(Time * Speed * 0.61f + Phase * 0.9f) * IdleDriftRotationDegrees;

	FTransform DriftedTransform = LayoutTransform;
	// Offset along the photo's own axes so the sway reads the same wherever it sits on the helix.
	const FVector LocalOffset(SwaySide, 0.0f, SwayUp);
	DriftedTransform.SetLocation(LayoutTransform.GetLocation() + LayoutTransform.TransformVector(LocalOffset));
	DriftedTransform.SetRotation((LayoutTransform.GetRotation().Rotator() + FRotator(0.0f, SwayYaw, 0.0f)).Quaternion());

	SetActorTransform(DriftedTransform, false, nullptr, ETeleportType::TeleportPhysics);
}

bool AMemoryPhotoActor::MaterialHasScalarParameter(FName ParameterName) const
{
	if (!DynamicMaterial || ParameterName.IsNone())
	{
		return false;
	}

	float Value = 0.0f;
	return DynamicMaterial->GetScalarParameterValue(ParameterName, Value);
}
