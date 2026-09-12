// Fill out your copyright notice in the Description page of Project Settings.

#include "MemoryRoomUIGlass.h"
#include "MemoryRoom.h"

#include "Engine/Texture2D.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "MemoryRoomThemeAsset.h"
#include "TextureResource.h"

namespace
{
constexpr int32 SolidTextureSize = 4;
// Wide enough for the vignette in MakeVerticalGradientTexture to fall off smoothly under
// bilinear filtering instead of banding across a panel this size.
constexpr int32 GradientTextureWidth = 24;

UTexture2D* PaintTexture(int32 Width, int32 Height, TFunctionRef<FColor(int32 X, int32 Y)> Paint)
{
	UTexture2D* Texture = UTexture2D::CreateTransient(Width, Height, PF_B8G8R8A8);
	if (!Texture)
	{
		UE_LOG(LogMemoryRoom, Warning, TEXT("MemoryRoomUI failed to create a %dx%d UI texture."), Width, Height);
		return nullptr;
	}

	Texture->SRGB = true;
	Texture->NeverStream = true;
	Texture->Filter = TF_Bilinear;
	Texture->AddressX = TA_Clamp;
	Texture->AddressY = TA_Clamp;
	Texture->LODGroup = TEXTUREGROUP_UI;

	if (FTexturePlatformData* PlatformData = Texture->GetPlatformData())
	{
		if (PlatformData->Mips.Num() > 0)
		{
			FTexture2DMipMap& Mip = PlatformData->Mips[0];
			if (FColor* Pixels = static_cast<FColor*>(Mip.BulkData.Lock(LOCK_READ_WRITE)))
			{
				for (int32 Y = 0; Y < Height; ++Y)
				{
					for (int32 X = 0; X < Width; ++X)
					{
						Pixels[Y * Width + X] = Paint(X, Y);
					}
				}
			}
			Mip.BulkData.Unlock();
		}
	}
	Texture->UpdateResource();
	return Texture;
}
} // namespace

namespace MemoryRoomUI
{
const TCHAR* GlassBaseMaterialPath()
{
	return TEXT("/Engine/EngineMaterials/EmissiveTexturedMaterial.EmissiveTexturedMaterial");
}

UMaterialInterface* LoadGlassBaseMaterial()
{
	return LoadObject<UMaterialInterface>(nullptr, GlassBaseMaterialPath());
}

UTexture2D* MakeSolidTexture(const FLinearColor& Color)
{
	const FColor Pixel = Color.ToFColor(true);
	return PaintTexture(SolidTextureSize, SolidTextureSize, [Pixel](int32, int32) { return Pixel; });
}

void UpdateSolidTexture(UTexture2D* Texture, const FLinearColor& Color)
{
	if (!Texture)
	{
		return;
	}

	FTexturePlatformData* PlatformData = Texture->GetPlatformData();
	if (!PlatformData || PlatformData->Mips.Num() == 0)
	{
		return;
	}

	const FColor Pixel = Color.ToFColor(true);
	FTexture2DMipMap& Mip = PlatformData->Mips[0];
	if (FColor* Pixels = static_cast<FColor*>(Mip.BulkData.Lock(LOCK_READ_WRITE)))
	{
		const int32 PixelCount = Mip.SizeX * Mip.SizeY;
		for (int32 Index = 0; Index < PixelCount; ++Index)
		{
			Pixels[Index] = Pixel;
		}
	}
	Mip.BulkData.Unlock();
	Texture->UpdateResource();
}

UTexture2D* MakeVerticalGradientTexture(const FLinearColor& Top, const FLinearColor& Bottom, int32 Rows, float GlowBias, bool bFlip)
{
	const int32 Height = FMath::Clamp(Rows, 2, 1024);
	const int32 Width = GradientTextureWidth;
	const float SafeBias = FMath::Max(GlowBias, 0.05f);
	return PaintTexture(Width, Height, [&](int32 X, int32 Y)
	{
		float V = static_cast<float>(Y) / static_cast<float>(Height - 1);
		if (bFlip)
		{
			V = 1.0f - V;
		}
		FLinearColor Color = FMath::Lerp(Top, Bottom, FMath::Pow(V, SafeBias));

		// A soft vignette toward the left/right rim reads as a bevel catching less light at
		// its edge, instead of the slab looking like one flat colour painted edge to edge -
		// about the one thing a plain box mesh cannot sell on its own.
		const float U = (Width > 1) ? static_cast<float>(X) / static_cast<float>(Width - 1) : 0.5f;
		const float EdgeDistance = FMath::Clamp(FMath::Min(U, 1.0f - U) * 2.0f, 0.0f, 1.0f); // 0 at the rim, 1 at centre
		Color *= FMath::Lerp(0.72f, 1.0f, EdgeDistance);

		// A thin bright hairline right at the slab's top edge reads as its top bevel catching a
		// rim light, the way a real glass edge would - independent of bFlip, which only mirrors
		// which end the Top/Bottom colours land on, not which end is physically "up".
		const float TopEdgeV = bFlip ? (1.0f - V) : V;
		if (TopEdgeV < 0.06f)
		{
			const float HighlightAmount = (1.0f - TopEdgeV / 0.06f) * 0.35f;
			Color = FMath::Lerp(Color, FLinearColor(1.0f, 0.97f, 0.9f), HighlightAmount);
		}

		// The slab colours sit close to black, where 8-bit steps are visible as bands; a
		// sub-step offset per pixel lets the eye average them away.
		const float Dither = ((((X * 7) + (Y * 3)) % 8) / 8.0f - 0.5f) / 255.0f;
		return FLinearColor(Color.R + Dither, Color.G + Dither, Color.B + Dither, 1.0f).ToFColor(true);
	});
}

UMaterialInstanceDynamic* MakeTextureMaterial(UMaterialInterface* Base, UTexture2D* Texture, UObject* Outer)
{
	UMaterialInterface* SafeBase = Base ? Base : LoadGlassBaseMaterial();
	if (!SafeBase || !Texture)
	{
		return nullptr;
	}

	UMaterialInstanceDynamic* Instance = UMaterialInstanceDynamic::Create(SafeBase, Outer);
	if (!Instance)
	{
		return nullptr;
	}

	// "Texture" is the engine material's parameter. "SkyGradient" is set as well so the
	// project's own M_SkyDome can stand in as the base without a code change.
	Instance->SetTextureParameterValue(TEXT("Texture"), Texture);
	Instance->SetTextureParameterValue(TEXT("SkyGradient"), Texture);
	Instance->SetScalarParameterValue(TEXT("StarBrightness"), 0.0f);
	return Instance;
}

UMaterialInstanceDynamic* MakeSolidMaterial(UMaterialInterface* Base, const FLinearColor& Color, UObject* Outer)
{
	return MakeTextureMaterial(Base, MakeSolidTexture(Color), Outer);
}

UMaterialInstanceDynamic* MakeGlassMaterial(UMaterialInterface* Base, const FMemoryRoomWristUIStyle& Style, UObject* Outer, bool bFlip)
{
	return MakeTextureMaterial(Base, MakeVerticalGradientTexture(Style.PanelColor, Style.GlassGlowColor, 64, 1.8f, bFlip), Outer);
}
} // namespace MemoryRoomUI
