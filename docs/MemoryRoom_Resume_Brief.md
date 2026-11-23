# MemoryRoom: Detailed Project Brief for Resume Writing

*Prepared for the person writing this project up as resume experience. Sources: the C++ codebase (~15,600 lines), the project config, the project's naming dictionary (135 named systems, states, signals and assets), and the design and engineering notes kept across development sessions.*

**Status labels used below:**
- **[shipped-to-device]**: ran in the packaged Quest 3 build
- **[PIE-verified]**: confirmed working in the Unreal editor's VR play mode (PC VR via Quest Link)
- **[built, unverified]**: compiled and wired up, but not yet tested in the headset
- **[in progress]**: partly built or uncommitted

Use these labels to pick honest verbs in the bullets.

---

## Table of contents
1. Elevator pitch and fact sheet
2. Product vision, target user and design pillars
3. Full evolution timeline (phase by phase, with the reason for each change)
4. Feature deep-dive (every system, how it works, what it felt like to the user)
5. Technical architecture
6. Performance engineering (with numbers)
7. Design and UX decisions, iterations and trade-offs
8. Debugging case studies (interview stories)
9. Product and project management practice
10. Tooling and development workflow (including AI-agent orchestration)
11. Skills and keyword bank
12. Resume bullets: long list, grouped by target role
13. Honesty guardrails
14. Open questions for the creator

---

## 1. Elevator pitch and fact sheet

**MemoryRoom is a standalone virtual-reality app for Meta Quest that turns your personal photo library into a place you can stand inside.** Instead of scrolling a flat grid on a phone, you stand in a quiet meadow at the centre of a spiral of framed photos that rises into a dome above you. Newer memories sit at eye level and older ones climb overhead. You can walk among them, float up to reach older years, point at a photo to lift it toward you, grab it, open it to read its details, pin it anywhere in the world, or step inside a 360° panorama. A wrist "watch" switches the sky between dawn, sunset and night. At night the sky goes dark and the photos themselves become the light in the room.

| Fact | Detail |
|---|---|
| Product type | Consumer VR app (personal media / memories) |
| Platforms | Meta Quest 2, Quest Pro, Quest 3, Quest 3s (standalone Android, OpenXR). A Windows / PC VR build is used for fast iteration. |
| Engine and language | Unreal Engine 5.8, C++ (primary), Blueprints (thin configuration layer), Python (editor automation) |
| Code size | ~15,600 lines of C++ across 30 source files and 13 main gameplay classes; **501 designer-tunable properties** and **123 Blueprint-callable functions** exposed |
| Content | 1 main level (`MemoryRoom`), an outdoor meadow environment, 2 complete visual themes, 7 custom materials/instances for foliage, a sky material, a photo material, a panorama material |
| Performance target | 72 fps (Quest refresh rate), ~13.9 ms frame budget |
| Result | Packaged Quest 3 build **locked at 72 fps with ~8 ms frame time** [shipped-to-device] |
| Team | Solo creator acting as product owner, designer, technical director and QA, directing AI coding agents (Claude Code) to implement |
| Timeline | Most design and engineering happened in **September 2026** (notes dated 2026-09-03 to 2026-09-12), with the repo put under Git + LFS on 2026-09-12. Work continued into October 2026. *Confirm the start date with the creator.* |

---

## 2. Product vision, target user and design pillars

### The problem
- Phone galleries show years of life as identical thumbnails in a grid. There's no sense of time, place or weight, so memories feel disposable.
- Existing VR photo viewers mostly put a flat 2D gallery on a floating screen. They don't use what makes VR special: scale, presence, space and physical interaction.

### Target user
- Quest owners with large personal photo libraries who want a reflective, emotional way to revisit memories, alone or as a "show someone your life" experience.
- Secondary: people with 360° panoramas from phones or cameras that are hard to appreciate on a flat screen.

### Vision statement
> "Stand inside your memories." Your photo library becomes a physical, living room. Time is something you can see and move through.

### Design pillars (used to judge every decision)
1. **Photos are the hero.** Environment, sky, UI and effects exist to frame the photos, never to compete with them.
2. **Time as space.** Chronology is mapped to physical height and distance on a spiral, so the library reads as a timeline.
3. **Physical, not menu-driven.** You point, lift, grab, pull, pinch and place photos the way you'd handle prints.
4. **Calm and comfortable.** Gentle motion, optional flight, collision keep-outs, no sudden changes and readable UI in every lighting state.
5. **Runs beautifully on a mobile headset.** Every visual feature has to fit inside a standalone Quest's frame budget.

---

## 3. Full evolution timeline

The idea changed significantly over development. Each phase lists what was built and why.

### Phase 1: Core concept, the photo spiral
- Started from Unreal's VR Template (XRFramework: VR pawn, motion controllers, game mode).
- Built `AMemoryRoomManager`, which places photos along a **helix (rising spiral) whose radius tapers toward the top, forming a dome overhead**. Each photo's angle, height and radius come from its position in the timeline.
- Added overlap and anti-cluster passes so photos don't collide or bunch up.
- Built a second layout mode, a **spherical dome layout**, that switches on automatically above a photo-count threshold, so the shape could be compared at different library sizes.
- Added a **"viewing window"**: photos outside a band around the user are hidden, and photos near the top edge fade, shrink and blur into a "thread", while new photos fade in from the bottom. This keeps attention on the current era and keeps rendering cheap.
- The right thumbstick scrolled the whole spiral up and down through time.
- **Why:** prove the central metaphor that a library can be a place.

### Phase 2: Interaction model
- `UMemoryRoomSelectionComponent` on the VR pawn draws a **curved spline laser beam** from the controller, with a hit indicator dot. The beam material changes when it hits something interactive. An alternative **head-gaze "X" reticle** was also built.
- A **photo state machine** (`Layout → Hovered → Expanded / Grabbed → Layout`):
  - **Hover:** the photo scales up slightly, nudges toward the controller and shows a highlight frame.
  - **Expand** (trigger): the photo animates to a large position in front of the viewer, loads a higher-resolution texture and fades in a **metadata panel** (title, date, location, caption).
  - **Grab** (grip): the photo attaches to your hand until released.
  - **Collapse:** it animates back to its spiral slot.
- **Why:** physical handling of photos is the emotional core, more like holding a print than tapping a thumbnail.

### Phase 3: Real photo ingestion (platform abstraction)
- Designed a **photo-source interface** (`IMemoryPhotoSource`) with three operations: request permission, page through photo metadata, and decode pixels for a given photo.
- **Android implementation** reads the Quest's system photo library (**MediaStore**) through **JNI** (C++ calling Android Java APIs), including a runtime permission request.
- **Desktop implementation** scans a folder (JPG, JPEG, PNG and Canon RAW `.CR2`) and loads photos in batches on a timer, broadcasting progress events.
- A game-instance subsystem (`UMemoryPhotoLibrarySubsystem`) owns whichever source is active, so the rest of the app doesn't care which platform it's on.
- Later added an **Android headset-folder mode**, because a test Quest has no personal photos in its gallery. Photos can be pushed over `adb` into an app-private folder that needs no permission, with automatic fallback to MediaStore if the folder is empty. The resolved path is logged at startup to make testing easy.
- **Why:** the product only has value with the user's own memories, and a clean abstraction keeps platform code from leaking everywhere.

### Phase 4: Atmosphere, the sky and time of day
- Built a **custom sky dome** (`AMemoryRoomSkyDomeActor`): a huge inward-facing sphere drawn in **one unlit draw call**, re-centred on the head every frame so it always feels infinitely far away. It has its own sun (directional light) and sky light (ambient fill).
- **Three presets: Dawn, Sunset, Night.** Each defines zenith, horizon and ground colours, horizon falloff, star brightness, sun colour, intensity and angle, sky-light fill, fog colour and density, and how brightly photos glow. Switching cross-fades over **1.6 seconds**.
- **Sky gradient computed in C++**, not in a material graph. The colour gradient is evaluated in code and written into a small runtime texture (8 × 512), updated in place as presets blend. A **1024 × 512 star-field texture** is generated once in code.
- **Night design:** the sky goes almost black on purpose, and photo brightness rises (**×2.6 at night**) so the memories become the only light in the room.
- **Wrist "watch" puck** on the left controller: a small disc showing the *live* sky gradient as its face. Point at it and pull the trigger to cycle the time of day. The label updates through a `TimeOfDayChanged` event.
- Unreal's physically based SkyAtmosphere was **rejected as too expensive** for a mobile headset.
- The last-chosen time of day is saved and restored on launch.
- **Why:** emotional tone. Dawn feels hopeful, sunset nostalgic, night intimate. The night look is the product's signature moment.

### Phase 5: Performance crisis and re-architecture (detailed in section 6)
- Symptoms: a long hitch at startup, stutter while browsing, and only about 40 photos ever appearing from large folders.
- Profiled and fixed six root causes, then **re-architected the gallery into a virtualized, pooled system**: a fixed number of photo objects recycled as you move through time, regardless of whether the library has 50 or 5,000 photos.
- **Why:** a memories app has to cope with real libraries of thousands of photos on mobile hardware.

### Phase 6: Width-aware layout
- Equal spacing made wide photos and panoramas overlap while narrow portraits left gaps.
- Rebuilt layout so each photo's slot on the spiral is sized by **its actual display width**, using aspect ratios read from file headers without decoding the full image. An overlap-clearance pass follows.
- Photos are classified as **Portrait, Landscape or Square** and sized accordingly.
- Spiral shape tunables (twist, rise, base radius, top radius, taper height, edge gap) were separated from older settings so designers can tune the shape cleanly.
- **Why:** the spiral should look like a carefully hung exhibition, not a random scatter.

### Phase 7: Theme system (data-driven look and sound)
- Built **`UMemoryRoomThemeAsset`**, one data asset that bundles an entire look:
  - 3 sky presets (Dawn, Sunset, Night)
  - Photo frame style (tint colour and edge width, applied by a custom shader node in the photo material)
  - Dust/firefly tint
  - **4 audio cues**, each with its own volume: ambient loop, scroll whoosh, hover chime and grab tone
  - UI palette: panel, glow, button, button hover, accent, title, label and highlight colours
- Authored **two complete themes**:
  - **Hearth:** warm firelight, ember glows, gold accents, parchment text
  - **Void:** cool indigo and violet skies, denser stars, cool blue glass, starlight-tinted dust
- If no theme asset is assigned, Hearth is generated in code, so the app always looks finished.
- **Theme switching in the headset:** a "Theme" row in the settings menu switches Hearth ↔ Void live and saves the choice.
- **Audio override slots** let a sound designer drop audio files onto the manager with no code changes and no data asset.
- Audio is triggered by events: the hover chime plays on the frame the hover target changes, the grab tone on grab, and the whoosh on the rest-to-moving edge of scrolling.
- **Why:** separate the look from the logic. A future reskin, seasonal theme or brand partnership becomes a data change rather than an engineering project.

### Phase 8: In-headset settings
- A **settings board** with rows of `[<]  Label  value  [>]` controls for **10 live settings**: Time of Day, Spiral Twist, Photo Spacing, Photos In View, Scroll Speed, Night Photo Glow, Dust Amount, Thumbnail Quality, Theme and Float. There are also action buttons for Reset View, Jump to Newest, Reload Photos and Close.
- Every change applies **immediately** and is **saved**, then restored at launch.
- Quality tiers: thumbnails at **Low 256 / Medium 384 / High 512 px**, expanded photos at **Standard 1024 / High 1536 / Ultra 2048 px**.
- Status lines show reload progress and **thumbnail cache usage** (count / capacity).
- Opened by a **second wrist puck** (a settings icon) beside the time-of-day puck.
- **Why:** comfort and density preferences vary a lot between people, and in VR you shouldn't have to take the headset off to change them.

### Phase 9: Hands, wrist UI and two-handed interaction
- Replaced plain controller models with **animated mannequin hands**. The fingers curl closed on grab, partly close on trigger and ease open on release. This is done by scrubbing a grasp animation by a curl value instead of playing it.
- **Measured the Quest controller's real axis orientation** with a custom in-headset **calibration gizmo** that draws each controller's X/Y/Z axes. Finding: the controller aims along −Z, not the assumed +X. Two features were corrected as a result.
- Wrist pucks were restyled as **watches** (dark glass case, gold bezel, sky or menu-glyph face) and moved further along the wrist so they don't crowd the hand.
- A **third wrist puck, "Reset Placements"**, returns every pinned photo to the spiral.
- **Both hands became real pointers:** each hand runs its own independent aim, hover and beam. Only one photo can be held at a time.
- **Two-hand pinch zoom:** grab a photo with one hand, grip it with the other and pull your hands apart to zoom in. Zoom is driven by the distance between your hands [built, unverified].
- **Next/previous browsing** while a photo is expanded or held: flick the thumbstick to step to the next or previous photo, with hysteresis so one flick moves one photo, wrapping around the library.
- A **wrist-roll reveal gesture** (twist your wrist to reveal the menu, like checking a watch) was built and then **turned off by default**, because once the left hand became a full pointer, twisting it fought normal use.
- **Why:** make the hands feel like *your* hands, and make detailed inspection (zoom, browse) natural.

### Phase 10: "Place photos in the world" (spatial curation)
- Grab a photo off the spiral and **release it anywhere: it stays there**, pinned in the meadow like a print on an easel.
- Placements are **saved between sessions** (world position, rotation and scale per photo file) and restored at launch.
- An earlier version duplicated the photo object when placing. It was **reworked** so the same object simply takes the job permanently and leaves a gap on the spiral, with no duplicates.
- Re-grabbing a placed photo keeps its size, so repeated place/re-grab cycles don't keep shrinking it.
- **Gaze-driven quality:** about every 0.25 s, placed photos you're looking at load full resolution and the rest drop back to thumbnails, which keeps memory bounded with no cap on placements.
- **Why:** this turns browsing into **curation**. Users can build their own arrangement of favourite memories around them.

### Phase 11: UI visual design, three iterations
1. **Placeholder** engine shapes and default text.
2. **"Lantern glass"**: dark, warm glass slabs lit by an ember gradient along the bottom edge, gold hairline rules and bezel rings, and parchment small-caps labels. Every surface is **painted in C++ as tiny runtime textures** on one engine material, so there's **no authored UI art at all**. Motion included an 0.18 s scale-in when the board opens, and tiles that grow 10% and lift 0.6 cm on hover.
   - A small always-on light was added to the board because the text component used a lit material and **went dark at night** while the glass kept glowing.
3. **"Clipboard"** (final): a solid dark backing board, cream paper panel and clip, with dark printed-ink text. All surfaces are unlit, so it **looks identical at dawn and at night**. Colours are exposed as tunable properties.
- **Placement also changed:** the menu went from riding the wrist, to floating in front of the view, to being **summoned in front of the user** by the settings puck (75 cm ahead, slightly below eye level, tilted toward the user) and dismissed by Close or a second press.
- **Why:** lantern glass was beautiful but harder to read and less consistent across lighting. The clipboard put legibility and calm first. The summoned placement removed arm fatigue and aiming difficulty.

### Phase 12: Outdoor environment, the meadow
- The abstract room became a **calm meadow clearing**:
  - A **160 m × 160 m grass floor** tiled every 2 m so it doesn't look stretched
  - **Thousands of instanced grass clumps** kept clear of the photo area (several passes: 1,500 to 2,500 painted clumps plus code-scattered grass)
  - A **hand-painted pine forest** (63 trees in one pass), which the code **radially compresses** at runtime so it fits inside the sky dome and keeps its natural shape, then removes only the few trees within 9 m of centre so none stand among the photos
  - A **mountain range faked with scattered giant rocks**: 26–34 rocks at 120–240 m, tall uneven scaling, slight tilt, bases half-buried. This replaced three hand-placed mountain meshes. Because the sky dome is a single cheap draw call, its radius could be raised to 300 m at no cost.
  - Light **height fog** starting about 15 m out, driven by the sky presets. Volumetric fog and clouds were left out for performance.
  - **Ambient motes:** 260 drifting dust particles that pick up the colour of nearby photos and read as **fireflies** at night. A **wind-blown leaf** variant with tumbling cards, wind direction, sway and tumble speed is [in progress].
- Foliage materials were re-tuned (glow removed, quieter greens) from a third-party art pack **without modifying the pack itself**.
- Scenes were authored and tuned with **Python editor scripts** that include a guard against generating duplicates, so they can be rerun safely.
- Experiments that were **tried and cut**: procedural cone trees (replaced), procedural distant pines (cut for looking poor), code-scattered tree rings (now off, because the painted forest looked better).
- **Why:** give the memories a *place*. A natural, calm setting at golden hour feels like somewhere you'd go to remember.

### Phase 13: 360° panorama viewing
- Photos with an approximately **2:1 aspect ratio** (equirectangular panoramas) are automatically detected and labelled **"360"**.
- Point and pull the trigger to **step inside**: the meadow hides and you stand at the centre of a sphere showing the panorama, which follows your head position so it feels infinitely far away. Press trigger or grip to return, and the scene is restored.
- Panorama textures are capped at **4,096 px** on both platforms and reuse the existing bounded loading paths.
- Known limitation, stated honestly: cropped and stereo panoramas aren't supported, and an ordinary 2:1 photo may be treated as a 360.
- **Why:** this is something only VR can offer. A panorama on a phone is a strip, while in VR you're back in the place.

### Phase 14: Locomotion (free movement)
- Moved from a fixed player scrolling the spiral to **free movement through the gallery**:
  - **Walk:** left stick, smooth glide in the direction you face at 170 cm/s, height locked to the floor.
  - **Float:** toggled with the **left X button** or a menu row. **"Fly where you look"** at 260 cm/s, including up and down, capped at about 15 m (3 turns of the spiral), so you can reach older memories high up.
  - **Turn:** right-stick smooth turning that pivots around the head, not the play-space centre [in progress].
- The spiral **stays fixed in the world** while you move, rather than following your head.
- **Comfort keep-outs:** a 130 cm bubble around photos while walking (45 cm while floating) so you never clip through a photo. A **spiral boundary** stops you drifting out past the photo ring, tapering with height to match the spiral's shape.
- Fixed a conflict where grabbing a photo and pulling it close **pushed you away**: the keep-out now applies only while you're actively moving toward a photo.
- **Why:** walking among memories is more immersive than scrolling them, and float makes the full height of the timeline reachable.

### Phase 15: Quest rendering polish and realistic lighting within budget
- **Sharpness:** found the cause of a pixelated floor (mobile forward rendering has no temporal upsampling, so a flat floor aliases at grazing angles) and tuned eye-buffer pixel density.
- **Head-turn judder:** the GPU was missing 72 Hz. Fixed with dynamic foveated rendering, MSAA 4× → 2×, an early depth pass to remove pine-leaf overdraw, grass cull distances, wind animation frozen beyond 30 m, and fewer trees and rocks.
- **Realistic shadows within budget:** a single tight 16 m shadow cascade at 1,024 px, so your hands, body and nearby photos cast shadows while vegetation casts none.
- **Night readability:** after feedback that night was "too dark", ambient fill was raised roughly 8×, dim moonlight was added at a low angle, and fog and ground colours were lifted. Photo glow and stars were left unchanged.

---

## 4. Feature deep-dive (user-facing summary)

| Area | Feature | Status |
|---|---|---|
| Gallery | Tapered helix-to-dome spiral of personal photos, newest at eye level | shipped-to-device |
| Gallery | Alternative dome layout above a photo-count threshold | built |
| Gallery | Viewing window: fade, shrink and blur at the edges | shipped-to-device |
| Gallery | Width-aware spacing; portrait, landscape and square sizing | PIE-verified |
| Gallery | Virtualized pool (56 desktop / 20 Android objects) recycled over thousands of photos | PIE-verified |
| Ingestion | Android MediaStore over JNI with permissions; headset folder via adb; desktop folder (JPG/PNG/CR2) | desktop verified; Android folder built, unverified |
| Interaction | Laser beam and hit dot; head-gaze reticle alternative | PIE-verified |
| Interaction | Hover lift and halo, expand with metadata panel, grab, collapse | PIE-verified |
| Interaction | Metadata from the file (cleaned-up filename as title, file date) | PIE-verified |
| Interaction | Next/previous browsing while expanded or held | built |
| Interaction | Two-hand pinch zoom; dual independent hand pointers | built, unverified (left-hand input bindings pending) |
| Interaction | Place photos anywhere, saved across sessions, gaze-driven resolution | built |
| Interaction | 360° panorama step-inside viewer | PIE-verified with a test image |
| Hands | Animated mannequin hands with finger curl | right hand verified; left hand disabled pending calibration |
| Atmosphere | Dawn/Sunset/Night sky with 1.6 s cross-fades; C++-generated gradient and stars | shipped-to-device |
| Atmosphere | Night mode: photos become the light (×2.6) | shipped-to-device |
| Atmosphere | Hearth and Void themes, switchable live | PIE-verified |
| Atmosphere | Ambient audio, hover chime, grab tone, scroll whoosh | wiring built; audio on device unresolved |
| Atmosphere | Firefly dust motes; wind-blown leaves | dust built; leaves in progress |
| Environment | Meadow, pine forest compressed to fit the dome, rock mountains, fog | PIE-verified |
| UI | 3 wrist watch-pucks: time of day, settings, reset placements | PIE-verified |
| UI | Summoned clipboard settings board, 10 settings and 4 actions, saved | PIE-verified |
| Movement | Walk, fly-where-you-look float, keep-outs, spiral boundary | PIE-verified (walk and float); boundary built |
| Movement | Right-stick smooth turn | in progress |
| Platform | 72 fps / ~8 ms on Quest 3 | shipped-to-device |

---

## 5. Technical architecture

### Class map (C++ module `MemoryRoom`)
| Class | Responsibility |
|---|---|
| `AMemoryRoomManager` (~4,000 lines) | Central orchestrator: layout maths (helix, dome, width-aware offsets), viewing window, virtualized photo pool, load pipeline (desktop batches + Android queues), thumbnail and expanded-image caches, quality tiers, user settings (apply + save), theme application, audio cues, locomotion and turning, placed-photo persistence, player spawn. Broadcasts 9 events (load progress, load complete, photo count changed, reload started/completed, quality changed, and others) that the UI listens to. |
| `AMemoryPhotoActor` (~1,750 lines) | One framed photo: runtime texture binding, aspect classification and sizing, a state machine (Layout/Hovered/Expanded/Grabbed) with animated transitions, metadata panel, hover halo, frame style, night glow, panorama detection, placed-in-world flag. |
| `UMemoryRoomSelectionComponent` (~2,000 lines) | On the VR pawn: per-hand trace, hover and beam; activate, grab, release, pinch zoom, next/prev browsing; hand meshes and finger curl; calibration gizmo; wrist-roll reveal; bridge from Blueprint input events to C++. |
| `AMemoryRoomSkyDomeActor` | Sky dome, gradient and star textures, sun and sky light, preset blending, fog driving, single-cascade shadows, removal of redundant level lighting at start. |
| `AMemoryRoomMenuActor` | Settings board: rows, values, actions, open/close and hover animation, summoned placement. |
| `AMemoryRoomSkyButtonActor` | Wrist watch-pucks (TimeOfDay / Settings / ResetPlacements), live sky face, debounce. |
| `UMemoryRoomThemeAsset` | Data asset for a full look and sound; built-in Hearth and Void authored in code. |
| `AMemoryRoomGroundActor` | Floor dressing, grass scatter, rock-mountain scatter, painted-forest radial reshaping and centre clearing. |
| `AMemoryRoomDustActor` | Instanced mote field (Dust or Leaf style) that wraps around the player. |
| `AMemoryPanoramaActor` | 360° sphere viewer that hides and restores the scene. |
| `IMemoryPhotoSource`, `FAndroidMediaStorePhotoSource`, `UMemoryPhotoLibrarySubsystem` | Platform-abstracted photo ingestion (JNI, permissions, paging, decode). |
| `MemoryRoomUI` helpers | Paint solid and gradient textures in C++ and wrap them in unlit materials, so UI needs no authored art. |
| SaveGames | `UMemoryRoomQualitySaveGame` (settings and time of day); `UMemoryRoomPlacedPhotosSaveGame` (one transform per placed photo, keyed by file). |

### Key engineering patterns
- **Virtualization / object pooling:** a fixed pool of photo objects reassigned by timeline index as the user moves. Memory and object count stay constant whatever the library size.
- **Multi-tier caching:** an LRU thumbnail cache (up to 512 px), a single-slot full-resolution "expanded" loader, panorama textures capped at 4,096 px, at most 3 decodes at a time, and aspect ratios read from image headers without a full decode.
- **Numerical methods for layout:** a closed-form spiral position function plus an **arc-length secant solver** to place photos a precise distance apart along a curve, replacing brute-force stepping.
- **Event-driven UI:** the manager broadcasts events, so the menu and wrist pucks react to them rather than polling.
- **Data-driven design:** themes, sky presets and 501 tunable properties let a designer change the look and feel without recompiling. Material parameter names are a **stable contract** between code and art.
- **"Logic in code, thin graphs":** an architectural rule set by the creator. Calculations live in reviewable, diffable C++; materials and Blueprints only pass data and assign assets. Example: the sky material is one texture node because C++ computes the gradient.
- **Platform-specific configuration:** Quest-only render settings (ray tracing, Lumen GI, virtual shadows and distance fields off for Android) while the desktop editor keeps richer settings.
- **Input architecture under OpenXR:** Enhanced Input actions (Move, Turn, Float, Scroll, Select, Grab, Menu) go from the mapping context to the VR pawn Blueprint event to a C++ function on the selection component, then to the manager.

---

## 6. Performance engineering

### Startup and browsing performance: 6 root causes found and fixed
| # | Problem | Fix | Impact |
|---|---|---|---|
| 1 | The overlap-avoidance pass recomputed 12 neighbour positions on each of up to 500 fixed-step iterations, for every photo | Cached neighbour positions; replaced stepping with an **arc-length secant solve**; max iterations **500 → 32** | Removed up to **~6.5 million** power/trig calls from a single blocking frame (1,000-photo library) |
| 2 | Per-photo, per-frame logging flooded Android's system log | Dedicated log category; per-frame logs moved to verbose | Removed constant I/O cost on the device |
| 3 | Photo sizing re-ran every frame for every visible photo | Early-out when nothing changed | CPU saved per visible photo per frame |
| 4 | 5 material parameter writes per photo per frame | Skip when opacity and blur haven't changed | Fewer render-state updates |
| 5 | Thumbnail cache held raw pixels *and* a GPU texture, and rebuilt textures on every hit | Cache only the GPU texture | **~47 MB** less memory (~1.2 MB × 40 photos) |
| 6 | Duplicate-request check was a linear string search per photo per frame | Hash set | O(n) → O(1) per lookup |

### Architectural fix: virtualized gallery
- **Before:** desktop spawned **one object and one light per photo (209+ of each)** and decoded every image synchronously at 2,048 px on the main thread. A timer bug (zero delay is ignored) **stalled loading after the first 32 photos**, which is why only about 40 ever appeared.
- **After:** a fixed pool of **56** (desktop) / **20** (Android) photo objects recycled by timeline position, 512 px LRU thumbnails, full resolution only on expand, throttled background decodes, and aspect ratios from file headers. The same code path runs on both platforms.

### Mobile build stability and size
- **Launch deadlock on Quest:** editor-only AI plugins were enabled for all platforms. Their runtime modules **deadlocked Android start-up** (every thread asleep, no log file, "app not responding"). Restricted them to the editor target. **Result: the packaged build ran locked at 72 fps, ~8 ms frame time on Quest 3.**
- **Packaging failure while the editor was open:** a plugin's local server port clashed between the editor and the build process, which failed packaging even though cooking succeeded. Handled through a log-severity configuration change, with fallbacks documented.
- **Android-only compiler strictness:** the Android toolchain treats warnings as errors (e.g. unreachable loop increments) that Windows accepts. A fast Android compile check was added before packaging.
- **App size:** the packager was cooking **every level in the project**, including demo maps with 4K/8K textures. Restricted cooking to the one game level and excluded a **449 MB** demo pack and unused sample levels. Also handled a dependency trap: excluding one folder broke the build because the rock meshes used a material inside it.

### GPU / frame-rate tuning (Quest 3 at 72 Hz)
- Eye-buffer pixel density tuned (1.2 for sharpness, back to 1.0 for headroom)
- **Dynamic foveated rendering** (lower resolution in the periphery, only when GPU-bound)
- MSAA 4× → 2×
- **Early depth (Z) pass** to remove alpha-masked pine-leaf overdraw, the usual top cost of a forest on tile-based mobile GPUs
- Grass cull distances (19–25 m), leaf wind animation frozen beyond 30 m so occlusion culling works, tree count 500 → 200, rock count 34 → 26
- No collision and no shadows on any vegetation
- Single shadow cascade (16 m, 1,024 px) for close-range realism
- Custom one-draw-call sky dome instead of a physically based atmosphere; extra level lights removed automatically at startup
- Pointed out to the creator that hand-placed and code-scattered instances cost the same at runtime, so the authoring method can be chosen for workflow rather than performance

---

## 7. Design and UX decisions, iterations and trade-offs

| Decision | Options considered | Choice and reasoning |
|---|---|---|
| How to represent time | Grid, wall, carousel, spiral, dome | **Tapered spiral into a dome.** It reads as a timeline (height = age), surrounds the user and uses VR's vertical space. |
| Focus and density | Show everything vs. a window | **Viewing window** with fade, shrink and blur at the edges, which focuses attention and saves performance. |
| Night lighting | Realistic night vs. stylised | **Near-black sky where photos are the light.** Rebalanced after feedback ("too dark") without losing the effect. |
| Sky tech | Physically based atmosphere vs. custom | **Custom dome**: one draw call, art-directable and mobile-friendly. |
| Menu style | Lantern glass vs. clipboard | **Clipboard**: readable in every lighting state; calmer. |
| Menu placement | Wrist-mounted, wrist-roll gesture, floating, summoned | **Summoned in front of the user** by a wrist puck. Wrist mounting caused arm strain and aiming trouble, and the roll gesture clashed with using the left hand. |
| Menu flicker | — | Two input paths toggled it at once. Consolidated to one path plus a **0.35 s debounce**. |
| Wrist puck size and spacing | — | Shrunk (scale 1.0 → 0.45) and moved toward the wrist to leave the hand free and room for future pucks. |
| Movement | Scroll the world vs. move the player | **Move the player** (walk plus optional float). The world stays put, which is more grounded and comfortable. |
| Grab vs. keep-out conflict | — | Keep-out only applies during active movement toward a photo. |
| Placing photos | Spawn a copy vs. reuse the same object | **Reuse.** No duplicates, a bounded pool, and gaze-driven resolution to bound memory. |
| Forest inside the photo area | Delete trees, procedural ring, or reshape the painted forest | **Reshape the painted forest** (radial compression plus a small centre clearing). Keeps the creator's art direction. |
| Distant mountains | Hand-placed meshes vs. scattered rocks | **Scattered giant rocks**, which give a more natural silhouette at no added cost. |
| Player spawn | — | Player was spawning ~4.26 m above the floor or off-centre, which shifted the whole spiral. Spawn is now pinned to the gallery centre and snapped to the floor. |
| Metadata | Placeholder vs. real data | Pull title from the filename and date from the file. Location and caption are honestly left blank until a data source exists. |
| UI art | Commission assets vs. procedural | **Procedural UI in code**: finished look with no art dependency. |

---

## 8. Debugging case studies (STAR format)

**1. "Only 40 photos load, and the app hitches."**
- *Situation:* large libraries stalled and stuttered.
- *Task:* scale to thousands of photos on a mobile headset.
- *Action:* profiled and ranked 6 causes; rewrote placement maths (6.5M calls → bounded secant solve); fixed a zero-delay timer bug; replaced per-photo objects and lights with a virtualized 56-object pool and LRU caching.
- *Result:* constant memory and object count at any library size; 72 fps on Quest 3.

**2. "The Quest app freezes on launch with no log."**
- *Action:* noticed every thread asleep just after the engine entry point, traced it to editor-only plugin modules included in the mobile build, and restricted them to editor builds.
- *Result:* a stable launch locked at 72 fps / ~8 ms.

**3. "The thumbsticks do nothing in VR."**
- *Action:* systematically eliminated polling, then legacy input bindings, then a 2D composite key binding (each silently returned zero under OpenXR). Established the one working path (Enhanced Input actions → pawn Blueprint → C++) and the exact key setup (two 1D axes with an axis swizzle).
- *Result:* walk and float worked first time after the change, and the pattern became the project standard.

**4. "Images stopped loading after a build", with nothing wrong in the code.**
- *Action:* found a single "property size mismatch" warning. The editor's hot reload had refused the new class layout and kept running old memory offsets against new code.
- *Result:* a process rule: restart the editor after header changes, and check the log for this warning first.

**5. "The left hand mesh is rotated wrong."**
- *Action:* after three in-headset guesses each gave a different wrong result, stopped guessing. Built a calibration gizmo, measured the real axes (the controller aims along −Z) and found the two controllers weren't configured symmetrically.
- *Result:* the right hand was solved and kept. The left was deliberately disabled until real configuration values are read from the editor. It's a quality call not to ship a fourth guess.

**6. "Night is too dark" vs. "photos should be the light."**
- *Action:* raised ambient fill and moonlight and lifted fog and ground tones, while keeping the photo glow and stars.
- *Result:* night stayed readable and kept its signature look.

**7. "Night photo glow does nothing."**
- *Action:* traced a name mismatch between the C++ parameter and the material parameter.
- *Result:* fixed, and parameter names were then treated as a code-to-art contract.

---

## 9. Product and project management practice

- **Owned the vision and the roadmap:** ran 15 phases from core concept to atmosphere, scale, theming, settings, hands, spatial curation, UI, environment, 360°, locomotion and polish, re-prioritising whenever testing exposed a scale, comfort or legibility problem.
- **Iteration strategy:** fast loops in PC VR (Quest Link / play-in-editor), with slow Android packaging only at milestones. Editor-target compiles were used for day-to-day checks.
- **Clear definitions of done:** each module had a contract (inputs, outputs, error cases, performance budget, out-of-scope, done-check) before work began.
- **Honest status tracking:** every feature was labelled compiled, PIE-verified or device-verified; untested work was never reported as done.
- **Decision and lessons log:** failures became written rules (Android compiler strictness, hot-reload layout trap, controller axes, input path under OpenXR, packaging port clash).
- **Scope discipline:** cut weak experiments (cone trees, distant pines, tree ring, wrist-roll gesture); paused the left-hand feature instead of guessing; documented known limitations (360 detection heuristics, missing location/caption data).
- **Feedback loop:** acted on playtest reactions ("too dark", "trees in the photos", "menu flickers", "feels pixelated", "head-turn judder", "grab pushes me away") with targeted changes.
- **Documentation and handoff:** a design handoff document (layout maths, controls, materials, day/night system); environment and 360° change notes with validation results and backups; a living naming dictionary.
- **Cost awareness:** tracked AI-agent spend with a budget log and used cheap recon agents before expensive build agents.

---

## 10. Tooling and development workflow

- **Version control:** Git, with **Git LFS** for large Unreal binary assets.
- **Build and deploy pipeline:** Unreal Build Tool and RunUAT command-line builds; ASTC-compressed Android packaging; `adb` install, log pulling and photo pushing to a Quest 3; Android NDK r27.
- **Python editor automation:** scripts that author the meadow (floor, grass, trees, mountains) with duplicate-generation guards, and a tuning script that adjusts foliage materials and can be rerun safely.
- **Custom "Scene Indexer" tool:** a Python watcher running inside the Unreal editor that diff-scans the open level about every 0.5 s, keeps a change log, checkpoints snapshots every 30 s, and serves state over a local socket to an **MCP (Model Context Protocol) server**. AI agents can then query live actors, changes and runtime state instead of guessing. Test materials and a test map were built for it.
- **"Lingo" project dictionary:** a shared, queryable record of 135 named elements (systems, states, transitions, signals, input actions, assets, components) with descriptions, so the creator and AI agents always use the same names.
- **AI-agent orchestration ("Conductor" model):** the creator worked as conductor of specialised Claude Code agents: **scout** (read-only recon), **builder** (implements one briefed task in an isolated git worktree), **verifier** (tests against the contract), and **reviewer** (independent review of risky changes). Every dispatch used a fixed brief template (goal, context, allowed files, off-limits, done-when, short report). The creator kept taste decisions (UI feel, visual quality, naming) and in-headset visual checks.
- **Unreal MCP integration:** connected the editor to AI tooling through MCP plugins, and debugged the mobile-build and packaging side effects those plugins caused.

---

## 11. Skills and keyword bank

**VR and XR:** Meta Quest 2/3/Pro/3s, OpenXR, standalone Android VR, PC VR / Quest Link, motion controllers, hand meshes and finger animation, laser pointers and gaze reticles, spatial UI, wrist UI, diegetic menus, VR locomotion (smooth move, fly mode, smooth turn), comfort design (keep-outs, boundaries, no forced motion), 360° equirectangular viewing, foveated rendering, 72 Hz frame budgets

**Unreal Engine:** UE5 C++, Blueprints, Enhanced Input, Actors and Components, Game Instance Subsystems, Data Assets, SaveGame, dynamic material instances, runtime `UTexture2D` generation, instanced static meshes (ISM/HISM), foliage, directional, sky and point lights, cascaded shadows, height fog, spline meshes, TextRender, delegates and events, Unreal Python API, UBT/RunUAT packaging, device and platform config, hot reload / Live Coding

**Software engineering:** object pooling and virtualization, LRU caching, async and throttled image decoding, numerical methods (secant solver, arc-length parameterisation), procedural generation, state machines, interface-based platform abstraction, JNI / Android MediaStore, runtime permissions, event-driven architecture, data-driven design, performance profiling, memory optimisation

**Graphics:** mobile forward rendering, MSAA, early-Z and overdraw, LOD and cull distances, unlit and emissive pipelines, custom HLSL material nodes, procedural textures, colour grading through presets

**Product and design:** product vision, user problem framing, design pillars, interaction design, spatial and experience design, art direction, design systems and theming, audio cue design, information hierarchy, iterative prototyping, playtest-driven iteration, scope and trade-off decisions, roadmapping, technical documentation, design handoff

**Process and tooling:** Git and Git LFS, command-line build pipelines, adb, Python automation, MCP / AI tooling integration, AI-agent orchestration (Claude Code), contract-driven development, decision logs and retrospectives, cost and budget tracking

---

## 12. Resume bullets (long list; pick 4–6 per application)

### A. VR Developer / Unreal Engineer
1. Built **MemoryRoom**, a standalone **Meta Quest** VR app in **Unreal Engine 5 / C++** (~15.6k lines, 13 core classes) that turns a user's photo library into an explorable 3D spiral gallery with grab, expand, two-hand zoom, world placement and 360° viewing.
2. Re-architected gallery loading into a **virtualized object pool** (56 desktop / 20 Android) with **LRU thumbnail caching** and throttled async decoding, replacing one object and one light per photo (209+) and fixing a loader that stalled after ~40 photos.
3. Removed a startup hitch of **~6.5M trig evaluations** by replacing brute-force overlap stepping with an **arc-length secant solver** (500 → 32 iterations), and cut thumbnail memory by **~47 MB**.
4. Diagnosed an **Android launch deadlock** caused by editor-only plugins in the mobile build. The fixed build ran **locked at 72 fps (~8 ms) on Quest 3**.
5. Implemented **platform-abstracted photo ingestion**: Android **MediaStore via JNI** with runtime permissions, an adb-pushable headset folder, and a desktop folder scanner (JPG/PNG/CR2).
6. Built a **custom one-draw-call sky system** with **C++-generated gradient and star textures**, three cross-fading time-of-day presets and single-cascade shadows, in place of a physically based atmosphere too costly for mobile VR.
7. Implemented comfort-focused **VR locomotion** (walk, fly-where-you-look, smooth turn, collision keep-outs, height-tapered boundary) through **Enhanced Input under OpenXR**, after isolating why legacy input never reached Quest controllers.
8. Built **dual-hand interaction**: independent per-hand pointers, animated finger curl, **two-hand pinch-to-zoom**, flick-to-browse and a **persistent "place anywhere" system** with gaze-driven resolution to bound memory.
9. Tuned the Quest render pipeline (dynamic foveation, MSAA, early-Z overdraw pass, foliage culling, Android-only feature stripping) to remove head-turn judder, and cut packaged content by limiting the cook to one level and excluding a 449 MB unused pack.

### B. VR Designer / Experience / Interaction Designer
1. Designed **MemoryRoom**, a VR "memory palace" that maps a photo library's chronology onto a rising spiral, using a fading viewing window to focus attention and a night mode where **photos become the room's only light**.
2. Defined five **design pillars** (photos as hero, time as space, physical interaction, calm comfort, mobile-first) and used them to judge every environment, UI and lighting decision.
3. Designed a **physical photo-handling model** (hover lift and halo, grab, expand with metadata, pinch zoom, flick browsing, place in the world) and supporting audio cues (hover chime, grab tone, scroll whoosh, ambient loop).
4. Iterated the in-VR UI through **three visual directions** (placeholder → "lantern glass" → "clipboard") and **four placements** (wrist, wrist-roll gesture, floating, summoned), settling on a summoned high-contrast board that stays legible in every lighting state and reduces arm strain.
5. Created a **data-driven theme system** and authored two complete themes (*Hearth*, warm firelight; *Void*, cool starlight) covering sky, frames, particles, UI palette and sound, switchable live in the headset.
6. Art-directed an **outdoor meadow** (instanced grass, a pine forest reshaped at runtime to frame the gallery, a rock-built mountain range, fog, firefly motes) within a standalone headset's 72 fps budget.
7. Designed **comfort features**: optional fly mode with a height cap, keep-out bubbles, a spiral boundary, centred spawn, and a fix so pulling a photo close no longer pushes the player away.
8. Ran playtest-driven iterations on night brightness, menu flicker, trees inside the gallery, floor sharpness and judder, each resolved with a targeted design or technical change.

### C. Product Developer / Creative Technologist
1. Took a VR product **from concept to an on-device Quest build** as a solo creator over 15 iterative phases, covering ingestion, interaction, atmosphere, theming, settings, environment, 360° and locomotion.
2. Built a **10-setting in-headset preferences board** (time of day, spiral twist, spacing, density, scroll speed, night glow, dust, quality, theme, float) with live apply and saved state.
3. Designed for **reskinnability**: 501 designer-tunable properties, a theme data asset and no-code audio slots let look and sound change without engineering work.
4. Built the **delivery pipeline**: Git LFS, command-line build and package, Android pre-packaging compile checks, and adb deploy, log retrieval and test-photo seeding.
5. Built **internal developer tools**: a live Unreal **scene indexer** (Python watcher → socket → MCP server), Python scene-authoring scripts with duplicate guards, and a 135-entry naming dictionary.
6. Set up an **AI-agent development workflow**, directing specialised coding agents (recon, builder, verifier, reviewer) with written contracts and briefs while keeping product, design and quality decisions.

### D. Product Manager
1. Defined the vision for **MemoryRoom** ("stand inside your memories"), the problem it solves (flat, disposable photo grids), its target users and its design pillars, then delivered it as a working Meta Quest app.
2. Ran a **15-phase iterative roadmap**, re-prioritising as testing exposed scale (only 40 of 1,000+ photos loading), comfort (judder, arm strain) and legibility (night UI) problems.
3. Turned playtest feedback into prioritised product changes ("too dark", "menu flickers", "trees in my photos", "grabbing pushes me away") and protected the signature experience while fixing usability.
4. Set **non-negotiable quality gates** around platform constraints (72 fps on Quest 3, app size, comfort) and treated them as acceptance criteria for every feature.
5. Ran development as a **contract-driven program with AI agents**: module contracts (inputs, outputs, performance budget, out-of-scope, done-check), task briefs, independent verification, a decision and lessons log and a spend log.
6. Made disciplined **scope calls**: cut weak experiments, paused an uncalibrated feature rather than shipping guesses, and documented known limitations openly.
7. Expanded the product from passive browsing to **active curation** (pin favourite photos in the world, saved across sessions) and immersive **360° memories**, raising engagement depth beyond a viewer.
8. Produced handoff and change documentation (design handoff, environment and 360° change notes with validation results), so others could pick up the work.

### Short version (one project entry, 4 bullets, general VR role)
- Designed and built **MemoryRoom**, a standalone Meta Quest VR app (Unreal Engine 5, C++) that turns a personal photo library into an explorable spiral gallery with physical grab, zoom, world placement, 360° viewing and a dawn-to-night sky where photos become the light.
- Re-architected loading into a virtualized object pool with LRU caching and replaced brute-force layout with an arc-length secant solver (removing ~6.5M trig calls and ~47 MB), reaching a **locked 72 fps (~8 ms) on Quest 3**.
- Iterated UI, environment and comfort through playtesting: three UI styles and four menu placements, a data-driven theme system (Hearth / Void), walk-and-fly locomotion with keep-outs, and a meadow environment within mobile GPU limits.
- Led development as a solo product owner directing AI coding agents through written contracts, with a custom live scene-indexing tool (Python + MCP), Git LFS and a CLI Android build pipeline.

---

## 13. Honesty guardrails (please follow)

- **AI-assisted development.** The creator directed AI coding agents, which wrote most of the code. The creator owned the vision, design, architecture choices, priorities, testing and acceptance. Use verbs like *designed, led, directed, built, architected, shipped*. If asked in interviews, be open about the AI workflow; it's a valuable, current skill.
- **Device verification:** the **72 fps / ~8 ms** figure comes from the packaged Quest 3 build after the launch fix. Later features (locomotion, placed photos, 360°, clipboard UI, meadow) were verified in PC VR (play-in-editor / Quest Link) and **haven't all been re-tested in a fresh device package**.
- **Not finished:** right-stick turning and wind-blown leaves are in progress. Left-hand pinch, grab and zoom input bindings aren't wired yet. The left hand mesh is disabled. Ambient audio and the photo folder on device are unresolved.
- **No formal user research or release:** feedback came from the creator's own playtesting. The app isn't on the Meta Store, so don't invent users, downloads, ratings or retention numbers.
- **Metadata:** location and caption have no data source yet. The date is the file date, not the camera's capture date.
- **360 detection** is a 2:1 heuristic; don't claim support for every panorama format.
- All numbers in this brief come from the code or the engineering notes. Don't round them up.

---

## 14. Open questions for the creator

1. Exact project dates (start, and whether it's ongoing)?
2. How to label it: "Independent Project", "Personal Project", or a studio/brand name?
3. Did anyone besides the creator try it? Any quotes or reactions worth using?
4. Is there a demo video, GIFs, screenshots, a portfolio page or a repo link?
5. Which role is the priority? (Lead with that section's bullets.)
6. How explicitly should the AI-agent workflow appear on the resume itself versus only in interviews?
7. Should the Scene Indexer and Lingo tools be presented as separate "tools I built" entries?
