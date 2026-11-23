# PLAN — MemoryRoom release-prep

Branch: `release-prep` (base `c92b23d`). Goal: polish and fix what exists so it can be released.

## PIE test session 2026-10-13 (Quest Link, VR Preview)

| # | Issue | Status | Owner | Done when |
|---|---|---|---|---|
| P1 | Photos never load: `PhotoFolderPath` = `D:/img/jpg` (BP instance) doesn't exist on this PC; scan returns 0 files (cpp:1945). Fix: real folder + non-Android fallback | need folder from user | builder | Images show in PIE |
| P2 ✅ seen in PIE 2026-10-13 (crash on toggle fixed 6a01b0b) | Leaves never shown: `MoteStyle` defaults to Dust (h:61), nothing sets Leaf; no runtime setter (BuildMotes only in BeginPlay, cpp:49). Not a material bug. Fix: setter + theme/manager drives style; two-sided leaf look | merged bac7af9 (dc979bb); themes default Dust → set Leaf on a theme asset to see it | builder | Leaves visible in PIE |
| P3 | Hitches. Suspects: sync `LoadImageFromFile` on game thread (cpp:2839, PrepareFromFile per photo cpp:2716); per-frame layout over 1000 photos (cpp:3199-3309); "Editor is currently in a play mode" error log spam every frame (source not in our code? check) | merged 725d405 (4edd9b2): async first photo, layout skipped when inputs unchanged, Pictures fallback on PC; reviewer running | builder | No visible hitches in PIE |
| P4 | Swap hands to Meta Quest hands as in RayForge. RayForge uses Meta Interaction SDK v1.207.0 (`Plugins/MetaXRInteraction`, ~2.9 GB, git-ignored) controller rig components (ForgedVRPawn.cpp:102-125). MemoryRoom has no such plugin; hands = SKM_MannyXR via EnsureHandVisuals (SelectionComponent.cpp:124-230) | merged 8589c39: 51 assets in Content/MetaHands + CoreRedirect; both hands on; SDK wrist offsets as defaults. TODO: resave MetaHands in editor, then drop redirect; check curl (GripTrigger_Down may be a 1-frame pose); fix stale left-hand comments cpp ~261/~783; check Meta Fab EULA | builder | Both hands visible and aligned in PIE |
| P5 | In-headset photo source picker (user 2026-10-13: keep `PhotoFolderPath` as is; let the user choose images from the headset) | merged 0c41307 (6770dc4): PhotoAlbum row, async album query, 0.6 s debounced reload, PhotoAlbumId saved. MediaStore/JNI path never executed yet | builder | User picks an album/folder in VR, choice persists |
| — | Right-stick smooth turn | OK | — | Verified by user 2026-10-13 |

## Follow-ups from P3 review (2026-10-13, verdict: merge OK)
- F1: layout skip ignores photos hidden by other code (cpp:4059) → add `Photo->IsHidden() != !ViewingState.bVisible` to the skip condition. Queue after P5 merges (same file).
- F2: Pictures fallback uses UserDir()/../Pictures, wrong when Documents is OneDrive-redirected → use the Windows known-folder Pictures path. Same timing.
- Note: head-centred mode (bCenterHelixOnUser, locomotion off) gets no skip benefit; output still correct.

## Not yet tested
Spiral boundary, float + boundary, lantern UI flips, grab pull-in.

| P7 | Right hand missing: BP_VRPawn RightController tracks RightAim, no right grip controller → spawn a RightGrip controller at runtime; rotations → (0,0,0) (left calibrated in PIE) | merged 730c17f (e5f5248); editor built | builder | Both hands right in PIE |
| P9 | PIE 2026-10-13: left = Meta ghost hand (good), right = Unreal Manny hand. Cause: BP_VRPawn overrides HandMeshPath/HandGraspAnimPath with SKM_MannyXR_right. User wants Meta hands on both + Meta controller models. Plan: rename props to Right* (orphans stale override), hide stock XRDeviceVisualization on both sides, copy Meta controller meshes, controller = SDK's static Quest Pro mesh (the only one in the SDK) at identity on the grip pose | merged f00caa8; editor built | builder | Meta hand + Meta controller on both sides |
| P10 | Hands+controllers show but orientation wrong. BP controllers are customised (left reads X=up/Z=back). Fix: own runtime grip controllers on both sides, controller mesh at identity, hand child of controller with SDK transforms (props renamed to drop stale values), log BP controller transforms | merged a6f95b3 (762690d); editor built | builder | Hand holds controller naturally in PIE |
| P11 | P10 still wrong. BP controllers are clean (log: identity, Left / RightAim) — cause is the SDK places the hand by solving wrist bone `XRHand_Wrist` to a target relative to the controller mesh (SK_, offset -4.1 X), not a component offset. Rewrite per docs/contracts/hand-visuals.md; delete old hand code (user OK'd removing Manny fallback + calibration gizmo) | merged 3072d9d; editor built | builder | Hands hold controllers like RayForge |
| P11b | Aim-pose fix 2a25b12: controllers now point right ✅. Remaining: (a) left laser line offset differs from right (BP LeftController is the grip pose); (b) finger curl does nothing (GripTrigger_Down likely 1-frame) | (a) building: left ray from LeftAim visual controller, grab/watch stay on grip. (b) building: C++ anim instance blending Idle/GripTrigger_Down/FrontTrigger_Down by per-bone mask; runtime Enhanced Input for grip/trigger axes (SetHandCurlInput was never wired in BP) | (a) merged 6b1e2bd, editor built; (b) merged 9b114cd + unity-clash fix b3955da, needs editor build | builder ×2 | Left ray matches right; fingers curl |
| P12 | Finger/button anim can't be reproduced without the SDK → user chose to copy the full SDK from RayForge (Plugins/MetaXRInteraction, 679 MB w/o Intermediate, git-ignored). Integrate SDK rigs like RayForge; delete HandVisuals/HandAnimInstance/MetaHands/redirect. Contract rewritten | merged d76290a; editor built. TODO: check SDK ray visual hidden; Quest launch check (plugin not editor-only) | builder | Hands/fingers/buttons like RayForge; laser/watch/grab unchanged |
| P13 | Glass menu + separate folder window (user 2026-10-15: drop current menu look; warm-tinted glassmorphism, not themed; Choose → own window listing MediaStore folders). See docs/contracts/glass-menu.md | merged (4eb5b09); editor built. Unverified: plane UV flip (bFlipGlassFaces), alpha/premultiply of pass-through material, text legibility ; P13b layout rev 2 merged (3272057); P13c rev 3 tabs/smoky glass/placement merged (b92cdca), editor built | builder | Done-check in contract passes in PIE |
| P8 | Leaves render as flat squares → leaf texture/material (masked, two-sided) | later | — | Leaves look like leaves |
| P6 | Menu: replace Theme row with "Particles: Dust / Leaves"; always Hearth, Void hidden (asset kept) | merged 9fc5c55 (fc423d6); editor target built |  builder | Row toggles leaves in PIE |

## Decisions
- 2026-10-13 — Theme fixed to Hearth; Void removed from the menu for now. Particle style is a user setting (Dust/Leaves), not a theme property in the UI.
- 2026-10-13 — DefaultEngine.ini Python remote execution, Content/Python, SceneIndexer test assets stay uncommitted (editor tooling, not release).
