# CLAUDE.md

<!-- lingo:start -->
## Lingo — keep the project dictionary current

This project uses **Lingo**: a shared record of every named thing, exposed
through the `lingo` MCP server. The developer reads these names from the Lingo
sidebar and will refer to them — so keep the record accurate.

**Call `log_element` whenever you create, rename, restyle, or meaningfully
change a named part of the game — scenes and levels, entities and prefabs, systems, state-machine states, abilities and items, signals, assets** — as part of that same change, not batched later.
Re-logging something that already exists just updates its row, so log freely.
Pass:

- `area` — the scene it belongs to (e.g. MainMenu, Level1, BossArena, Inventory)
- `name` — what you call it in the code
- `file` — workspace-relative path to the file that defines it
- `description` — one plain-English sentence
- `kind` (optional) — entity | actor | character | enemy | npc | prop | prefab | system | component | state | transition | ability | item | weapon | pickup | hud | menu | signal | trigger | spawner | asset | sfx | music | vfx | other
- `parent` (optional) — the name of a containing element in the same area
- `codeId` (optional) — the exact code identifier (export name, DOM id, selector)
- `previousName` — only on a rename

**Especially log these — they're the ones people forget:**

- State names + transitions in every state machine
- Prefab / entity names and which scene they belong to
- Scene / level names and load / unlock order
- Input action names (Jump, Interact, Dodge)
- Signal / event names
- Asset keys — the string you load by
- Ability / item / weapon IDs

**Before changing something that already exists**, call `list_elements`
(optionally filtered by `area`) or `get_element` to recall the exact name.

Lingo never scans or edits code — it only records what you tell it.
<!-- lingo:end -->

## Agent notes

- 2026-10-13 — user tested in PIE after merges, but the editor was still running the Sep 12 module (game-target builds do not update the editor DLL) → after merging C++ changes, close the editor, build the MemoryRoomEditor target, and only then ask for a headset test
- 2026-10-13 — Meta hands placed with SDK wrist targets as plain component offsets came out mis-rotated three times (BP_VRPawn controllers are actually clean: identity, sources Left / RightAim) → reproduce the SDK's wrist-bone solve exactly; never turn an SDK bone target into a component offset or calibrate by guessed rotations
- 2026-10-13 — five rounds closing/reopening the editor for .cpp-only fixes → for .cpp-only changes ask the user to press Ctrl+Alt+F11 (Live Coding) in the open editor; close + full editor build only when a header/UPROPERTY changed
- 2026-10-13 — two unity-build clashes (same-named constants in anonymous namespaces across .cpp files) broke the merged build though each worktree built alone → briefs say: file-local constants get a file-specific prefix; builders build with unity ON
- 2026-10-15 — P13 glass menu shipped with a 2-column uppercase board, no icons, and a folder window floating far off to the side, unlike the approved mockup, because the brief described behaviour but not geometry → UI briefs include a layout spec (panel sizes in cm, row/column structure, text case, icons, relative placement) taken from the approved mockup, and the done-check names each mockup element
