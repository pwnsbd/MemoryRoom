# MemoryRoom meadow

The saved MemoryRoom level now contains a 160m grass floor, 1,500 short grass clumps and 27 distant pines. Grass starts 9.5m from the centre; pines stand 32–50m away and are 8–12.5m tall. Foliage uses seven instanced mesh groups with no collision or shadow casting. The open centre leaves the existing photo helix unobstructed.

Assets are under `Content/MemoryRoomEnvironment`. They derive from the installed Stylized Log Cabin pack; the pack itself was not edited. The meadow foliage materials remove emissive glow and use quieter greens. `MI_MeadowFloor` tiles the grass every 2m instead of stretching it across the floor.

`MemoryRoom_MeadowGround` prevents duplicate runtime grass. The floor component's `MemoryRoomAuthoredGround` tag tells `MemoryRoomGroundActor.cpp` to preserve the authored material. This is the only C++ change: the manager, helix, photos, controls and theme code are unchanged.

`MemoryRoom_MeadowSky` is an instance of the project's existing sky Blueprint. It retains the saved Dawn/Sunset/Night and photo-focus behavior. Its skylight capture threshold is 80m, inside the 120m dome, so the dome can contribute ambient lighting.

Validation: Development Editor build succeeded. Runtime validation confirmed the floor material, collidable ground, exactly one ground actor and sky actor, all 1,527 instances upright/non-colliding, and foliage clearances. The desktop photo folder `D:/img/jpg` was missing, so the helix could not be visually tested with photos. Standalone Quest packaging and headset frame rate were not tested.

Original map and source backup: `Z:/Applications/MemoryRoom-environment/original`. Detailed validation: `Z:/Applications/MemoryRoom-environment/validation-runtime.json`.

The scene is already authored; do not rerun `author_environment.py` on the finished map. It includes a guard against duplicate generation. `tune_meadow.py` updates the dedicated foliage materials; it can be rerun from Unreal's Python console when not playing.
