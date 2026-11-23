# Unreal scene work

For tasks involving the open Unreal level, use the `unreal-scene-indexer` MCP when its watcher is connected. Start with `get_scene_state` or a focused `query_actors` call to identify the map and relevant actors. Use project source and asset files to understand how they are made, then check `get_changes_since` or the relevant actor details after edits. The indexer is read-only; use Unreal editing tools for changes. If the watcher is unavailable, continue with normal project inspection and say what could not be verified live.
