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
}
