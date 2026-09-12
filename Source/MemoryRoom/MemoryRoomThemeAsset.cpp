// Fill out your copyright notice in the Description page of Project Settings.

#include "MemoryRoomThemeAsset.h"

namespace
{
// "Hearth": the existing Dawn / Sunset / Night presets pulled toward firelight - the blue is
// taken out of the ambient and fog, the horizons run gold and ember, and night settles to a
// warm near-black instead of a cold one. The photos still carry the room at night
// (PhotoBrightness and StarBrightness are unchanged from the cool baseline).
void AuthorHearth(UMemoryRoomThemeAsset& Theme)
{
	Theme.ThemeName = NSLOCTEXT("MemoryRoom", "ThemeHearth", "Hearth");

	// --- Dawn: soft gold, dusty plum overhead ---
	FMemoryRoomSkyPreset& Dawn = Theme.DawnPreset;
	Dawn.ZenithColor = FLinearColor(0.13f, 0.11f, 0.20f);
	Dawn.HorizonColor = FLinearColor(1.00f, 0.72f, 0.42f);
	Dawn.MidColor = FLinearColor(0.88f, 0.58f, 0.50f);
	Dawn.MidElevation = 0.20f;
	Dawn.GroundColor = FLinearColor(0.06f, 0.05f, 0.055f);
	Dawn.HorizonFalloff = 1.9f;
	Dawn.StarBrightness = 0.0f;
	Dawn.SunColor = FLinearColor(1.00f, 0.86f, 0.66f);
	Dawn.SunIntensity = 3.0f;
	Dawn.SunPitchDegrees = -6.0f;
	Dawn.SunYawDegrees = 20.0f;
	Dawn.SkyLightColor = FLinearColor(0.82f, 0.72f, 0.64f);
	Dawn.SkyLightIntensity = 1.0f;
	Dawn.PhotoBrightness = 1.0f;
	Dawn.FogInscatteringColor = FLinearColor(0.52f, 0.44f, 0.38f);
	Dawn.FogDensity = 0.018f;

	// --- Sunset: ember, no magenta ---
	FMemoryRoomSkyPreset& Sunset = Theme.SunsetPreset;
	Sunset.ZenithColor = FLinearColor(0.06f, 0.05f, 0.11f);
	Sunset.HorizonColor = FLinearColor(1.00f, 0.40f, 0.14f);
	Sunset.MidColor = FLinearColor(0.80f, 0.30f, 0.22f);
	Sunset.MidElevation = 0.20f;
	Sunset.GroundColor = FLinearColor(0.05f, 0.03f, 0.035f);
	Sunset.HorizonFalloff = 1.6f;
	Sunset.StarBrightness = 0.0f;
	Sunset.SunColor = FLinearColor(1.00f, 0.52f, 0.24f);
	Sunset.SunIntensity = 2.6f;
	Sunset.SunPitchDegrees = -2.0f;
	Sunset.SunYawDegrees = 200.0f;
	Sunset.SkyLightColor = FLinearColor(0.84f, 0.50f, 0.36f);
	Sunset.SkyLightIntensity = 0.85f;
	Sunset.PhotoBrightness = 1.05f;
	Sunset.FogInscatteringColor = FLinearColor(0.56f, 0.28f, 0.15f);
	Sunset.FogDensity = 0.020f;

	// --- Night: warm near-black; stars + photo glow unchanged ---
	FMemoryRoomSkyPreset& Night = Theme.NightPreset;
	Night.ZenithColor = FLinearColor(0.0018f, 0.0014f, 0.0016f);
	Night.HorizonColor = FLinearColor(0.020f, 0.012f, 0.009f);
	Night.MidColor = FLinearColor(0.0060f, 0.0040f, 0.0036f);
	Night.MidElevation = 0.15f;
	Night.GroundColor = FLinearColor(0.006f, 0.005f, 0.0045f);
	Night.HorizonFalloff = 1.4f;
	Night.StarBrightness = 1.0f;
	Night.SunColor = FLinearColor(0.92f, 0.86f, 0.76f);
	// A dim "moon": enough directional light to give the floor some shape, still clearly night.
	Night.SunIntensity = 0.09f;
	Night.SunPitchDegrees = -16.0f;
	Night.SunYawDegrees = 120.0f;
	Night.SkyLightColor = FLinearColor(0.24f, 0.20f, 0.17f);
	// Ambient fill so nothing reads as pure black under a sky full of stars.
	Night.SkyLightIntensity = 0.20f;
	Night.PhotoBrightness = 2.6f;
	Night.FogInscatteringColor = FLinearColor(0.030f, 0.024f, 0.020f);
	Night.FogDensity = 0.006f;

	// A soft, slightly warm border falloff on every photo.
	Theme.PhotoFrame.FrameTintColor = FLinearColor(0.015f, 0.012f, 0.010f, 1.0f);
	Theme.PhotoFrame.FrameEdgeWidth = 0.06f;

	// Motes pick up a warm cast so they read as embers at night.
	Theme.DustTint = FLinearColor(1.00f, 0.92f, 0.82f, 1.0f);

	// Cues are left unassigned - drop sound assets in here (or on an authored copy of this
	// theme) and the hover / grab / scroll / ambient wiring picks them up with no code change.
	Theme.Audio = FMemoryRoomAudioTheme();

	// Lantern glass: warm near-black slabs lit by an ember along the bottom edge, gold
	// hairlines and rings, parchment text. Matches the Sunset horizon and the night embers.
	FMemoryRoomWristUIStyle& UI = Theme.WristUI;
	UI.PanelColor = FLinearColor(0.012f, 0.009f, 0.008f, 1.0f);
	UI.GlassGlowColor = FLinearColor(0.16f, 0.060f, 0.020f, 1.0f);
	UI.ButtonColor = FLinearColor(0.085f, 0.050f, 0.032f, 1.0f);
	UI.ButtonHoverColor = FLinearColor(0.46f, 0.24f, 0.085f, 1.0f);
	UI.AccentColor = FLinearColor(0.95f, 0.70f, 0.36f, 1.0f);
	UI.TitleColor = FLinearColor(0.99f, 0.91f, 0.76f, 1.0f);
	UI.LabelColor = FLinearColor(0.86f, 0.82f, 0.74f, 1.0f);
	UI.HighlightColor = FLinearColor(1.00f, 0.78f, 0.42f, 1.0f);
}

// "Void": the cool inverse of Hearth - gold and ember traded for indigo and starlight. Where
// Hearth pulls the blue out of everything and settles warm, Void leans into it: skies run
// violet-to-indigo, stars are up even at "dawn" and never really leave, and the lantern glass
// glows cool blue instead of ember. Photos still carry the room the same way (PhotoBrightness
// climbs at night exactly like Hearth), just under colder light.
void AuthorVoid(UMemoryRoomThemeAsset& Theme)
{
	Theme.ThemeName = NSLOCTEXT("MemoryRoom", "ThemeVoid", "Void");

	// --- Dawn: pale, cool stillness - closer to Hearth's night than its dawn ---
	FMemoryRoomSkyPreset& Dawn = Theme.DawnPreset;
	Dawn.ZenithColor = FLinearColor(0.05f, 0.06f, 0.14f);
	Dawn.HorizonColor = FLinearColor(0.55f, 0.60f, 0.78f);
	Dawn.MidColor = FLinearColor(0.32f, 0.34f, 0.52f);
	Dawn.MidElevation = 0.20f;
	Dawn.GroundColor = FLinearColor(0.030f, 0.035f, 0.045f);
	Dawn.HorizonFalloff = 1.9f;
	// The void never fully brightens - a faint scatter of stars stays up even at "dawn".
	Dawn.StarBrightness = 0.15f;
	Dawn.SunColor = FLinearColor(0.80f, 0.85f, 0.95f);
	Dawn.SunIntensity = 1.6f;
	Dawn.SunPitchDegrees = -6.0f;
	Dawn.SunYawDegrees = 20.0f;
	Dawn.SkyLightColor = FLinearColor(0.55f, 0.60f, 0.75f);
	Dawn.SkyLightIntensity = 0.8f;
	Dawn.PhotoBrightness = 1.05f;
	Dawn.FogInscatteringColor = FLinearColor(0.20f, 0.22f, 0.32f);
	Dawn.FogDensity = 0.020f;

	// --- Sunset: a nebula twilight, violet and magenta instead of Hearth's ember ---
	FMemoryRoomSkyPreset& Sunset = Theme.SunsetPreset;
	Sunset.ZenithColor = FLinearColor(0.03f, 0.02f, 0.08f);
	Sunset.HorizonColor = FLinearColor(0.42f, 0.18f, 0.55f);
	Sunset.MidColor = FLinearColor(0.28f, 0.14f, 0.42f);
	Sunset.MidElevation = 0.20f;
	Sunset.GroundColor = FLinearColor(0.020f, 0.015f, 0.030f);
	Sunset.HorizonFalloff = 1.6f;
	Sunset.StarBrightness = 0.4f;
	Sunset.SunColor = FLinearColor(0.85f, 0.55f, 0.85f);
	Sunset.SunIntensity = 1.3f;
	Sunset.SunPitchDegrees = -2.0f;
	Sunset.SunYawDegrees = 200.0f;
	Sunset.SkyLightColor = FLinearColor(0.40f, 0.22f, 0.50f);
	Sunset.SkyLightIntensity = 0.6f;
	Sunset.PhotoBrightness = 1.4f;
	Sunset.FogInscatteringColor = FLinearColor(0.18f, 0.10f, 0.24f);
	Sunset.FogDensity = 0.022f;

	// --- Night: deep cosmic black, the densest stars of the three, cool photo glow ---
	FMemoryRoomSkyPreset& Night = Theme.NightPreset;
	Night.ZenithColor = FLinearColor(0.0010f, 0.0012f, 0.0022f);
	Night.HorizonColor = FLinearColor(0.0060f, 0.0080f, 0.0160f);
	Night.MidColor = FLinearColor(0.0025f, 0.0030f, 0.0060f);
	Night.MidElevation = 0.15f;
	Night.GroundColor = FLinearColor(0.004f, 0.0045f, 0.007f);
	Night.HorizonFalloff = 1.4f;
	Night.StarBrightness = 1.6f;
	Night.SunColor = FLinearColor(0.75f, 0.80f, 0.95f);
	// A dim cool "moon" plus ambient starlight fill - still deep night, just not pure black.
	Night.SunIntensity = 0.08f;
	Night.SunPitchDegrees = -16.0f;
	Night.SunYawDegrees = 120.0f;
	Night.SkyLightColor = FLinearColor(0.10f, 0.12f, 0.20f);
	Night.SkyLightIntensity = 0.18f;
	Night.PhotoBrightness = 2.8f;
	Night.FogInscatteringColor = FLinearColor(0.020f, 0.024f, 0.040f);
	Night.FogDensity = 0.005f;

	// A cool, faintly blue border falloff on every photo instead of Hearth's warm one.
	Theme.PhotoFrame.FrameTintColor = FLinearColor(0.010f, 0.011f, 0.016f, 1.0f);
	Theme.PhotoFrame.FrameEdgeWidth = 0.06f;

	// Motes pick up a cool cast so they read as starlight rather than embers.
	Theme.DustTint = FLinearColor(0.80f, 0.88f, 1.00f, 1.0f);

	// Cues are left unassigned, same as Hearth - drop sound assets in here (or on an authored
	// copy of this theme) and the hover / grab / scroll / ambient wiring picks them up with no
	// code change.
	Theme.Audio = FMemoryRoomAudioTheme();

	// Lantern glass: cool near-black slabs lit by a moonlit blue glow along the bottom edge,
	// icy-blue hairlines and rings, pale cool-white text. Mirrors Hearth's ember glass in cool
	// tones instead of warm.
	FMemoryRoomWristUIStyle& UI = Theme.WristUI;
	UI.PanelColor = FLinearColor(0.008f, 0.009f, 0.014f, 1.0f);
	UI.GlassGlowColor = FLinearColor(0.030f, 0.050f, 0.090f, 1.0f);
	UI.ButtonColor = FLinearColor(0.035f, 0.045f, 0.070f, 1.0f);
	UI.ButtonHoverColor = FLinearColor(0.18f, 0.30f, 0.50f, 1.0f);
	UI.AccentColor = FLinearColor(0.55f, 0.70f, 0.95f, 1.0f);
	UI.TitleColor = FLinearColor(0.90f, 0.94f, 1.00f, 1.0f);
	UI.LabelColor = FLinearColor(0.78f, 0.83f, 0.92f, 1.0f);
	UI.HighlightColor = FLinearColor(0.65f, 0.82f, 1.00f, 1.0f);
}
} // namespace

UMemoryRoomThemeAsset* UMemoryRoomThemeAsset::MakeBuiltInTheme(EMemoryRoomThemeId ThemeId, UObject* Outer)
{
	UMemoryRoomThemeAsset* Theme = NewObject<UMemoryRoomThemeAsset>(Outer ? Outer : GetTransientPackage());

	switch (ThemeId)
	{
	case EMemoryRoomThemeId::Void:
		AuthorVoid(*Theme);
		break;
	case EMemoryRoomThemeId::Hearth:
	default:
		AuthorHearth(*Theme);
		break;
	}

	return Theme;
}
