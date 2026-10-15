# Contract — glass menu + folder window

Owner module: `AMemoryRoomMenuActor` (settings board + photo folder browser), `MemoryRoomUI` helpers.
User decision 2026-10-15: drop the current (clipboard / Hearth-derived) menu look. All menu UI becomes warm-tinted glassmorphism, independent of the theme asset. "Choose folder" opens a separate floating folder window.

## Look (fixed, not themed)
- Panels: frosted translucent glass — soft white body at low alpha (~0.10–0.14), brighter 1–2 px rim (alpha ~0.3), faint top sheen, rounded corners (radius ~6% of short side). Painted in C++ as an RGBA texture; no authored material.
- Rows / tiles: smaller glass chips of the same recipe (alpha ~0.08, rim ~0.18).
- Highlight (hover, selected, primary "Use this folder"): warm amber/honey tint (~#F2B866 at alpha ~0.25, rim ~0.7). No blue.
- Text: warm off-white (~#FFF6EA) titles, same at ~60% for secondary lines.
- Unlit, so it reads the same in day and night presets.
- Material: an engine translucent unlit material with one texture param (candidate `/Engine/EngineMaterials/Widget3DPassThrough_Translucent`, param `SlateUI` — verified in UE 5.8: WidgetComponent.cpp loads /Engine/EngineMaterials/Widget3DPassThrough_Translucent and sets "SlateUI"). Hard-reference it (ConstructorHelpers) so it cooks for Quest. Glass faces use a flat plane, not a cube, so rounded corners show.
- No scene-colour blur (Quest cost); frosted look comes from the texture only.

## Inputs
- `FMemoryRoomAlbum { Id, Name, ParentId, Count }` list from `AMemoryRoomManager` (RefreshPhotoFolders / album list). MediaStore folders that contain photos only (user choice). No file-system browse.
- Laser hover/press through existing `SetHoveredComponent` / `PressHoveredButton`.

## Outputs / behaviour
1. Settings board rows (Reset view, Jump to newest, Reload photos, Choose folder, Close, plus any existing rows) render as glass chips on a glass board. Behaviour of each row unchanged.
2. "Choose folder" opens the **folder window**: a separate glass panel placed in front of the user's view (≈1.0–1.2 m ahead, slightly below eye height, facing the user), board stays visible beside/behind it. Pressing it again or the window's close (×) closes only the window.
3. Folder window contents: top bar = Up button, breadcrumb path ("Headset › DCIM › Camera"), × close. Grid of folder tiles (3 columns, paged), each: folder glyph, name, "N photos" when Count > 0. Bottom bar = page "‹ 1 / 3 ›", primary "Use this folder".
4. Tile press = enter that folder (children by ParentId). Up = parent. "Use this folder" = `SelectPhotoAlbumById(current)`, saves, closes window. Selected/current state shows the amber highlight.
5. First run (no saved choice): the folder window opens on its own with status "Choose a photo folder to begin."
6. Hover: chip swaps to highlight + small lift (keep existing hover pop feel).

## Errors / empty
- No folders yet (query pending): window shows "Looking for folders…". Query returned none: "No photo folders found on this headset."
- Long names truncate with "…"; tiles never overlap.

## Perf budget
- Textures painted once (on build / style change), not per frame. ≤ 8 distinct glass textures, each ≤ 256×256. No new per-frame allocations. Keep 72 fps on Quest.

## Out of scope
- Wrist pucks and the photo hover halo keep their current look (later pass if the user wants).
- Theme asset (`FMemoryRoomWristUIStyle`) values stay; the menu just stops reading them.
- Raw file-system browsing, permissions, Android packaging.

## Done-check
- `MemoryRoomEditor Win64 Development` builds with unity ON, no new warnings.
- In PIE: board shows glass chips; Choose folder opens a separate window; Up / enter / page / Use / × work; choice persists after restart.
