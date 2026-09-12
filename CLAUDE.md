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
