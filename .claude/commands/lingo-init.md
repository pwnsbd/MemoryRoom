---
description: Catalog a project's existing structure into Lingo
argument-hint: "[path to a sub-project, omit if the working directory is the project]"
---

Populate **Lingo** — a project's shared dictionary — from the code that already
exists. This project type is: **Game**. Lingo exposes the
`log_element`, `list_elements`, and `get_element` MCP tools.

**Target:** `$ARGUMENTS`
- If that is empty, catalog the **current working directory** — it is the
  project.
- If it names a path (e.g. `packages/api` or `../Words`), catalog **only
  that directory**, and pass `project: "$ARGUMENTS"` on every
  `log_element` / `list_elements` call so the entries land in that
  project's dictionary, not this one's.

Do this:

1. Map the target into its **scenes** (e.g. MainMenu, Level1, BossArena, Inventory).
2. Take one scene at a time. For each **named part of the game — scenes and levels, entities and prefabs, systems, state-machine states, abilities and items, signals, assets** in it, call
   `log_element` with:
   - `area` — the scene name
   - `name` — the name as it appears in the code
   - `file` — the file that defines it (path relative to the target)
   - `description` — one plain-English sentence
   - `kind` — entity | actor | character | enemy | npc | prop | prefab | system | component | state | transition | ability | item | weapon | pickup | hud | menu | signal | trigger | spawner | asset | sfx | music | vfx | other
   - `parent` — the containing element's name, when there is a clear one
   - `codeId` — the exact code identifier, when there's a clear one
   - `project` — set it to `$ARGUMENTS` if a target path was given above
3. Call `list_elements` for that scene first so you don't repeat work.
   Re-logging is safe (it upserts) but skip the churn.
4. **Do not edit any code.** This is a read-and-record pass only.
5. Use the names already in the code — don't invent new ones.

**Especially capture these — the things people forget:**

- State names + transitions in every state machine
- Prefab / entity names and which scene they belong to
- Scene / level names and load / unlock order
- Input action names (Jump, Interact, Dodge)
- Signal / event names
- Asset keys — the string you load by
- Ability / item / weapon IDs

When done, summarise: which scenes you covered and how many things you logged.
