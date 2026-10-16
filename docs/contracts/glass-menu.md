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
- Textures painted once (on build / style change), not per frame. Rev 2: 11 frosted glass textures (each <= 256 px on the long side) plus 9 painted icon textures (64 x 64, shared by every plane that shows the shape). No new per-frame allocations. Keep 72 fps on Quest.

## Out of scope
- Wrist pucks and the photo hover halo keep their current look (later pass if the user wants).
- Theme asset (`FMemoryRoomWristUIStyle`) values stay; the menu just stops reading them.
- Raw file-system browsing, permissions, Android packaging.

## Done-check
- `MemoryRoomEditor Win64 Development` builds with unity ON, no new warnings.
- In PIE: board shows glass chips; Choose folder opens a separate window; Up / enter / page / Use / × work; choice persists after restart.

## Layout (rev 2, 2026-10-15 — matches the approved mockup; supersedes the first build)
Units: cm in the board's local plane. Text sentence case everywhere (no ALL CAPS).
- **Board** (left): ~34 W, height fits content. Title "Memory room" (size ~3.6), subtitle "Photos: <current folder name>" (size ~2.2, 60% text). Then ONE column of full-width chips (~28 W × 6.5 H, 1.2 gap): Reset view, Jump to newest, Reload photos, Choose folder, Close — plus any settings rows (Particles etc.) in the same single column. Each chip: icon at left (painted into the texture, not a font glyph), label left-aligned after it, size ~2.4.
- **Folder window** (right): sits beside the board in the SAME plane, right of the board with a ~4 gap, top edges aligned, same scale. Optional slight inward yaw ≤ 12° toward the user. No separate far placement. ~62 W × 46 H.
  - Top bar: [↑ Up] chip (~9 × 4.5) at left, breadcrumb "Pictures › Trips › 2024" after it (size ~2.0, 75% text, middle-truncate if long), [×] chip (~4.5 × 4.5) at right.
  - Grid: 3 columns × 2 rows per page, tiles ~18 W × 13 H, 1.6 gap. Each tile: folder glyph (top), name (size ~2.0, truncate "…"), "N photos" (size ~1.5, 60% text).
  - Bottom bar: "‹ 1 / 3 ›" page control at left (prev/next arrows are their own hit boxes), primary "✓ Use this folder" amber chip (~18 × 5) at right.
- Icons (Up arrow, ×, ‹ ›, ✓, folder, and the board row icons: reset, clock, refresh, folder, x) are painted shapes in the chip textures — the TextRender font may lack these glyphs.
- Highlight stays warm amber (user choice), not the mockup's blue.

## Desktop / PIE folder source (rev 2)
VR Preview runs on the PC, so there are no headset folders there; the PC path should feel like a gallery:
- Root level lists "places" that exist and contain photos: PhotoFolderPath (if set), Pictures, OneDrive\Pictures, Desktop, Downloads, Documents — de-duplicated by resolved path. Each place is a top-level album (ParentId empty) showing total count, drillable into subfolders.
- A folder with loose photos AND subfolders is still selectable ("Use this folder" uses that folder's own photos + descendants, as today).
- Folders with zero photos in their subtree stay hidden. Scan off the game thread (as now); cap depth at 6 and skip hidden/system folders (names starting with "." , AppData, node_modules).
- Android path (MediaStore buckets) unchanged.

## Build notes (rev 2 implementation)
- Settings (Spiral twist, Dust, ...) stay a compact two-per-line [<] value [>] block under the single column of action chips: eleven more full-width rows would make the board over a metre tall. The "Photos" album-cycling setting is off the board (Choose folder + the subtitle replace it).
- The folder window is a child of the board root (same plane, scale and open animation). While it is open the board slides left by half the pair offset so the pair is centred in view. Tunables: FolderWindowGapCm (4), FolderWindowInwardYawDegrees (8, max 12).
- On the PC the "All photos" tile is not offered (it only ever scanned the Pictures folder).

## Rev 3 (2026-10-15, after first VR look) — supersedes conflicting lines above
User feedback: glass too transparent to read; folder window pops up on menu open; board too high (must look up).
1. **Smoky warm glass** (user choice): panel body = dark warm brown (~#281E16) at ~0.75 alpha, chips ~#3A2C20 at ~0.6, rim light warm (~#FFE6C8) at ~0.35, faint top sheen kept. Hover/selected/primary = amber (~#F2B866) body ~0.45, rim ~0.9. Text stays warm off-white; secondary text ~70%. Text must read clearly against a bright sky.
2. **Tabs on the board** (user choice B): one small board. Under the title + "Photos: <folder>" subtitle, a tab strip: Home / Gallery / Atmosphere / Comfort (active tab amber). Below it only the active tab's rows:
   - Home: Reset view, Jump to newest, Reload photos, Choose folder, Close (full-width icon chips, as now).
   - Gallery: Spiral twist, Photo spacing, Photos in view, Scroll speed.
   - Atmosphere: Time of day, Night photo glow, Particles (Theme setting), Dust amount.
   - Comfort: Float, Thumbnail quality.
   Setting row = one full-width slim row: label left-aligned, "‹ value ›" right with ‹ and › as their own hit boxes (painted icons). Board height fixed to the tallest tab so it doesn't jump between tabs. Menu opens on Home. PhotoAlbum setting stays off the board.
3. **No auto-open**: the folder window opens only when Choose folder is pressed (remove the OpenMenu auto-open). First run without a saved choice: subtitle reads "Choose a folder to begin" and the Choose folder chip is amber-highlighted.
4. **Placement**: board centre ~28 cm below eye height, ~65 cm ahead, tilted back to face the eye (pitch so the board normal points at the head, ~20°). Top edge must be below eye level whatever the board height. The folder window keeps sitting beside the board with the same tilt.
