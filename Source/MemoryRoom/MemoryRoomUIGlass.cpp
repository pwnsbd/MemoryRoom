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
constexpr int32 GlassGradientTextureWidth = 24;

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

// Signed distance in pixels from a point to a rounded rectangle centred on the origin with
// the given half extents and corner radius. Negative inside.
float GlassFrost_RoundedBoxSdf(float Px, float Py, float HalfW, float HalfH, float Radius)
{
	const float Qx = FMath::Abs(Px) - (HalfW - Radius);
	const float Qy = FMath::Abs(Py) - (HalfH - Radius);
	const float Outside = FMath::Sqrt(FMath::Square(FMath::Max(Qx, 0.0f)) + FMath::Square(FMath::Max(Qy, 0.0f)));
	return Outside + FMath::Min(FMath::Max(Qx, Qy), 0.0f) - Radius;
}

// --- Icon shapes. Unit space: the icon square is -1..1 on both axes, Y down. Distances are in
// those units (negative inside), so one pixel is 1 / half-size. ---
double GlassIcon_Segment(const FVector2D& P, const FVector2D& A, const FVector2D& B, double Radius)
{
	const FVector2D Pa = P - A;
	const FVector2D Ba = B - A;
	const double H = FMath::Clamp(FVector2D::DotProduct(Pa, Ba) / FMath::Max(FVector2D::DotProduct(Ba, Ba), 1e-6), 0.0, 1.0);
	return (Pa - Ba * H).Size() - Radius;
}

// A thick ring arc with round caps, running clockwise on screen from StartDeg for SweepDeg.
double GlassIcon_Arc(const FVector2D& P, double Radius, double Thickness, double StartDeg, double SweepDeg)
{
	const double AngleDeg = FMath::RadiansToDegrees(FMath::Atan2(P.Y, P.X));
	const double Relative = FMath::Fmod(AngleDeg - StartDeg + 720.0, 360.0);
	if (Relative <= SweepDeg)
	{
		return FMath::Abs(P.Size() - Radius) - Thickness;
	}
	const double A0 = FMath::DegreesToRadians(StartDeg);
	const double A1 = FMath::DegreesToRadians(StartDeg + SweepDeg);
	const FVector2D E0(Radius * FMath::Cos(A0), Radius * FMath::Sin(A0));
	const FVector2D E1(Radius * FMath::Cos(A1), Radius * FMath::Sin(A1));
	return FMath::Min((P - E0).Size(), (P - E1).Size()) - Thickness;
}

// Arrowhead for an arc whose clockwise end sits at EndDeg: a chevron just past the cap.
double GlassIcon_ArcHead(const FVector2D& P, double Radius, double Thickness, double EndDeg)
{
	const double A = FMath::DegreesToRadians(EndDeg);
	const FVector2D End(Radius * FMath::Cos(A), Radius * FMath::Sin(A));
	const FVector2D Tangent(-FMath::Sin(A), FMath::Cos(A));
	const FVector2D Normal(FMath::Cos(A), FMath::Sin(A));
	const FVector2D Tip = End + Tangent * 0.30;
	return FMath::Min(GlassIcon_Segment(P, Tip, End + Normal * 0.30, Thickness), GlassIcon_Segment(P, Tip, End - Normal * 0.30, Thickness));
}

double GlassIcon_Sdf(MemoryRoomUI::EGlassIcon Icon, const FVector2D& P)
{
	using MemoryRoomUI::EGlassIcon;
	const double Stroke = 0.12;
	switch (Icon)
	{
	case EGlassIcon::Up:
		return FMath::Min3(GlassIcon_Segment(P, FVector2D(0.0, 0.62), FVector2D(0.0, -0.62), Stroke),
			GlassIcon_Segment(P, FVector2D(0.0, -0.66), FVector2D(-0.52, -0.14), Stroke),
			GlassIcon_Segment(P, FVector2D(0.0, -0.66), FVector2D(0.52, -0.14), Stroke));
	case EGlassIcon::Close:
		return FMath::Min(GlassIcon_Segment(P, FVector2D(-0.5, -0.5), FVector2D(0.5, 0.5), Stroke),
			GlassIcon_Segment(P, FVector2D(-0.5, 0.5), FVector2D(0.5, -0.5), Stroke));
	case EGlassIcon::Prev:
		return FMath::Min(GlassIcon_Segment(P, FVector2D(0.28, -0.6), FVector2D(-0.28, 0.0), Stroke + 0.02),
			GlassIcon_Segment(P, FVector2D(-0.28, 0.0), FVector2D(0.28, 0.6), Stroke + 0.02));
	case EGlassIcon::Next:
		return FMath::Min(GlassIcon_Segment(P, FVector2D(-0.28, -0.6), FVector2D(0.28, 0.0), Stroke + 0.02),
			GlassIcon_Segment(P, FVector2D(0.28, 0.0), FVector2D(-0.28, 0.6), Stroke + 0.02));
	case EGlassIcon::Check:
		return FMath::Min(GlassIcon_Segment(P, FVector2D(-0.62, 0.04), FVector2D(-0.2, 0.46), Stroke + 0.02),
			GlassIcon_Segment(P, FVector2D(-0.2, 0.46), FVector2D(0.62, -0.46), Stroke + 0.02));
	case EGlassIcon::Reset:
	{
		// The refresh circle mirrored, so the arrow runs counter-clockwise.
		const FVector2D M(-P.X, P.Y);
		return FMath::Min(GlassIcon_Arc(M, 0.54, 0.10, -50.0, 280.0), GlassIcon_ArcHead(M, 0.54, 0.10, 230.0));
	}
	case EGlassIcon::Clock:
		return FMath::Min3(GlassIcon_Arc(P, 0.70, 0.09, 0.0, 360.0),
			GlassIcon_Segment(P, FVector2D(0.0, 0.0), FVector2D(0.0, -0.42), 0.09),
			GlassIcon_Segment(P, FVector2D(0.0, 0.0), FVector2D(0.30, 0.18), 0.09));
	case EGlassIcon::Refresh:
		return FMath::Min(FMath::Min(GlassIcon_Arc(P, 0.54, 0.10, -60.0, 140.0), GlassIcon_Arc(P, 0.54, 0.10, 120.0, 140.0)),
			FMath::Min(GlassIcon_ArcHead(P, 0.54, 0.10, 80.0), GlassIcon_ArcHead(P, 0.54, 0.10, 260.0)));
	case EGlassIcon::Folder:
	{
		const double Body = GlassFrost_RoundedBoxSdf(static_cast<float>(P.X), static_cast<float>(P.Y - 0.12), 0.7f, 0.46f, 0.10f);
		const double Tab = GlassFrost_RoundedBoxSdf(static_cast<float>(P.X + 0.36), static_cast<float>(P.Y + 0.42), 0.28f, 0.13f, 0.06f);
		return FMath::Min(Body, Tab);
	}
	}
	return 1.0;
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
	const int32 Width = GlassGradientTextureWidth;
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

const TCHAR* GlassTranslucentMaterialPath()
{
	return TEXT("/Engine/EngineMaterials/Widget3DPassThrough_Translucent.Widget3DPassThrough_Translucent");
}

UMaterialInterface* LoadGlassTranslucentMaterial()
{
	return LoadObject<UMaterialInterface>(nullptr, GlassTranslucentMaterialPath());
}

UTexture2D* MakeFrostedPanelTexture(const FGlassPanelSpec& Spec, int32 MaxSide)
{
	const float Long = FMath::Max(FMath::Max(Spec.WidthCm, Spec.HeightCm), 0.01f);
	const int32 Side = FMath::Clamp(MaxSide, 32, 256);
	const int32 Width = FMath::Clamp(FMath::RoundToInt(Side * Spec.WidthCm / Long), 16, Side);
	const int32 Height = FMath::Clamp(FMath::RoundToInt(Side * Spec.HeightCm / Long), 16, Side);
	const float HalfW = Width * 0.5f;
	const float HalfH = Height * 0.5f;
	const float Radius = FMath::Max(1.0f, FMath::Min(HalfW, HalfH) * 2.0f * Spec.CornerFraction);

	// Folder glyph geometry, in pixels from the texture centre (Y down).
	const float GlyphW = Width * 0.30f;
	const float GlyphH = GlyphW * 0.74f;
	const float GlyphCenterY = -HalfH + Height * 0.18f + GlyphH * 0.5f;

	return PaintTexture(Width, Height, [&](int32 X, int32 Y)
	{
		const float Px = X + 0.5f - HalfW;
		const float Py = Y + 0.5f - HalfH;
		const float Sdf = GlassFrost_RoundedBoxSdf(Px, Py, HalfW, HalfH, Radius);
		// One pixel of anti-aliasing on the outer edge.
		const float Coverage = FMath::Clamp(0.5f - Sdf, 0.0f, 1.0f);
		if (Coverage <= 0.0f)
		{
			return FColor(0, 0, 0, 0);
		}

		const float Depth = -Sdf; // pixels inside the edge
		const float RimBlend = 1.0f - FMath::Clamp((Depth - 0.5f) / FMath::Max(Spec.RimPixels, 0.5f), 0.0f, 1.0f);
		float Alpha = FMath::Lerp(Spec.BodyAlpha, Spec.RimAlpha, RimBlend * RimBlend);
		FLinearColor Color = FMath::Lerp(Spec.Tint, Spec.RimTint, RimBlend);

		// Faint sheen across the top half, strongest at the top edge.
		const float V = static_cast<float>(Y) / static_cast<float>(Height - 1);
		if (V < 0.5f)
		{
			const float Sheen = 1.0f - V / 0.5f;
			Alpha += Spec.SheenAlpha * Sheen * Sheen;
		}

		if (Spec.bFolderGlyph)
		{
			// Body + tab, drawn as two rounded boxes in the glyph's own pixel space.
			const float Gy = Py - GlyphCenterY;
			const float Body = GlassFrost_RoundedBoxSdf(Px, Gy + GlyphH * 0.10f, GlyphW * 0.5f, GlyphH * 0.40f, GlyphH * 0.10f);
			const float Tab = GlassFrost_RoundedBoxSdf(Px + GlyphW * 0.20f, Gy - GlyphH * 0.40f, GlyphW * 0.20f, GlyphH * 0.12f, GlyphH * 0.06f);
			const float Glyph = FMath::Clamp(0.5f - FMath::Min(Body, Tab), 0.0f, 1.0f);
			Alpha = FMath::Lerp(Alpha, FMath::Max(Alpha, 0.55f), Glyph);
			Color = FMath::Lerp(Color, Spec.RimTint, Glyph);
		}

		Color.A = FMath::Clamp(Alpha * Coverage, 0.0f, 1.0f);
		return Color.ToFColor(true);
	});
}

UTexture2D* MakeIconTexture(EGlassIcon Icon, const FLinearColor& Color, int32 Size)
{
	const int32 Side = FMath::Clamp(Size, 16, 256);
	const double Half = Side * 0.5;
	return PaintTexture(Side, Side, [&](int32 X, int32 Y)
	{
		const FVector2D P((X + 0.5 - Half) / Half, (Y + 0.5 - Half) / Half);
		const double Sdf = GlassIcon_Sdf(Icon, P);
		// One pixel of anti-aliasing; colour stays set where the icon is clear so bilinear
		// filtering does not pull a dark fringe in from the transparent pixels.
		FLinearColor Out = Color;
		Out.A = static_cast<float>(FMath::Clamp(0.5 - Sdf * Half, 0.0, 1.0)) * Color.A;
		return Out.ToFColor(true);
	});
}

UMaterialInstanceDynamic* MakeTranslucentMaterial(UMaterialInterface* Base, UTexture2D* Texture, UObject* Outer)
{
	UMaterialInterface* SafeBase = Base ? Base : LoadGlassTranslucentMaterial();
	if (!SafeBase || !Texture)
	{
		return nullptr;
	}

	UMaterialInstanceDynamic* Instance = UMaterialInstanceDynamic::Create(SafeBase, Outer);
	if (Instance)
	{
		Instance->SetTextureParameterValue(TEXT("SlateUI"), Texture);
	}
	return Instance;
}
} // namespace MemoryRoomUI
