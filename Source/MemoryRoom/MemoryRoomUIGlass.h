// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"

class UMaterialInstanceDynamic;
class UMaterialInterface;
class UTexture2D;
struct FMemoryRoomWristUIStyle;

/**
 * The shared "lantern glass" look for every in-headset control: a dark, warm slab lit from
 * below by an ember gradient, gold hairlines and rings, parchment text.
 *
 * Nothing here needs an authored material. Every surface is the engine's unlit
 * EmissiveTexturedMaterial fed a tiny texture this code paints from the theme's colours,
 * so a retheme is only colour values in FMemoryRoomWristUIStyle, and the look survives the
 * night preset (unlit surfaces do not go black when the sky light does).
 */
namespace MemoryRoomUI
{
	// Unlit, opaque, one texture parameter ("Texture") straight into Emissive.
	MEMORYROOM_API const TCHAR* GlassBaseMaterialPath();
	MEMORYROOM_API UMaterialInterface* LoadGlassBaseMaterial();

	// 4x4 texture of one colour.
	MEMORYROOM_API UTexture2D* MakeSolidTexture(const FLinearColor& Color);

	// Repaints a texture MakeSolidTexture returned to a new colour in place, rather than
	// allocating a new UTexture2D - for something whose colour tracks a value that changes
	// every frame (e.g. a live-blended sky preset) without needing a new texture asset each time.
	MEMORYROOM_API void UpdateSolidTexture(UTexture2D* Texture, const FLinearColor& Color);

	// A vertical ramp, Top at V=0 to Bottom at V=1. GlowBias above 1 holds the ramp near Top
	// for longer, so Bottom reads as a glow along the lower edge rather than a 50/50 blend.
	MEMORYROOM_API UTexture2D* MakeVerticalGradientTexture(const FLinearColor& Top, const FLinearColor& Bottom, int32 Rows = 64, float GlowBias = 1.8f, bool bFlip = false);

	// Dynamic instance of Base with Texture bound to its texture parameter. Base may be null,
	// in which case the engine glass material is loaded.
	MEMORYROOM_API UMaterialInstanceDynamic* MakeTextureMaterial(UMaterialInterface* Base, UTexture2D* Texture, UObject* Outer);
	MEMORYROOM_API UMaterialInstanceDynamic* MakeSolidMaterial(UMaterialInterface* Base, const FLinearColor& Color, UObject* Outer);
	// The slab: Style.PanelColor at the top down to Style.GlassGlowColor at the bottom.
	MEMORYROOM_API UMaterialInstanceDynamic* MakeGlassMaterial(UMaterialInterface* Base, const FMemoryRoomWristUIStyle& Style, UObject* Outer, bool bFlip = false);

	// --- Frosted glass panels (settings board + folder window) ---
	// A rounded-rectangle RGBA texture: soft body, brighter rim, faint top sheen. Painted once
	// per style, never per frame. Fixed warm look - deliberately not read from the theme.
	struct FGlassPanelSpec
	{
		// Physical size in cm; only the aspect matters (the long side is painted at MaxSide).
		float WidthCm = 10.0f;
		float HeightCm = 10.0f;
		FLinearColor Tint = FLinearColor(1.0f, 0.97f, 0.92f);
		FLinearColor RimTint = FLinearColor(1.0f, 0.98f, 0.94f);
		float BodyAlpha = 0.12f;
		float RimAlpha = 0.30f;
		float RimPixels = 1.5f;
		float SheenAlpha = 0.07f;
		// Corner radius as a fraction of the short side.
		float CornerFraction = 0.06f;
		// Paints a small folder icon in the upper middle (folder tiles).
		bool bFolderGlyph = false;
	};

	// Translucent, unlit, one texture parameter ("SlateUI") carrying colour + alpha - the
	// material UWidgetComponent uses for its translucent 3D widgets.
	MEMORYROOM_API const TCHAR* GlassTranslucentMaterialPath();
	MEMORYROOM_API UMaterialInterface* LoadGlassTranslucentMaterial();

	MEMORYROOM_API UTexture2D* MakeFrostedPanelTexture(const FGlassPanelSpec& Spec, int32 MaxSide = 256);

	// --- Painted icons ---
	// Small shapes drawn from signed-distance functions into a transparent RGBA texture, so the
	// menu never depends on the TextRender font having an arrow / tick / folder glyph. Each icon
	// is painted once (Size x Size, square) in a single colour; the chip behind it supplies the
	// glass. Put it on a flat plane with MakeTranslucentMaterial.
	enum class EGlassIcon : uint8
	{
		Up,      // arrow pointing up (parent folder)
		Close,   // x
		Prev,    // single chevron, left
		Next,    // single chevron, right
		Check,   // tick
		Reset,   // one counter-clockwise arrow circle
		Clock,   // ring with two hands
		Refresh, // two clockwise arrows chasing round a circle
		Folder   // folder with tab
	};
	MEMORYROOM_API UTexture2D* MakeIconTexture(EGlassIcon Icon, const FLinearColor& Color, int32 Size = 64);
	// Dynamic instance of the translucent base with Texture bound to "SlateUI".
	MEMORYROOM_API UMaterialInstanceDynamic* MakeTranslucentMaterial(UMaterialInterface* Base, UTexture2D* Texture, UObject* Outer);
}
