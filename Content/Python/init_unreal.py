"""
Scene Indexer - Unreal-side watcher.

Auto-executed by Unreal on editor startup: Unreal runs any init_unreal.py it
finds under a project's (or an enabled plugin's) Content/Python folder.

The watcher runs a throttled full diff-scan about every 0.5 seconds because
Unreal's Python API does not expose bindable delegates for actor additions,
removals, or property changes. It keeps an in-memory STATE and rolling
CHANGE_LOG, and serves them through a local TCP/JSON socket (default
127.0.0.1:9892; see ../config.json). The standalone
../unreal_mcp_server/server.py process exposes MCP tools over stdio and forwards
requests to this socket. A lock-consistent snapshot is atomically checkpointed
to Saved/SceneIndexer/ every 30 seconds, on map switches, and on clean shutdown;
the next editor startup compares the live level with that checkpoint.
"""

import json
import copy
import math
import os
import socket
import socketserver
import threading
import time
import uuid

import unreal


def _load_config():
    default = {"host": "127.0.0.1", "unreal_port": 9892}
    cfg_path = os.path.join(
        os.path.dirname(os.path.abspath(__file__)), os.pardir, "config.json"
    )
    try:
        with open(cfg_path, "r") as f:
            default.update(json.load(f))
    except Exception:
        pass
    return default


CONFIG = _load_config()
PROJECT_DIR = os.path.abspath(unreal.Paths.project_dir())

POLL_INTERVAL_SECONDS = 0.5
CHECKPOINT_INTERVAL_SECONDS = 30.0
RUNTIME_POLL_INTERVAL_SECONDS = 2.0
MAX_RUNTIME_ACTORS = 500
MAX_CHANGE_LOG = 2000
MAX_INSTANCE_TRANSFORMS = 2000
MAX_RUNTIME_CACHED_POINTS = 100000
INSTANCE_POSITION_SAMPLE_LIMIT = 5
MAX_ATTACHED_CHILDREN = 100
TRACKED_FIELDS = (
    "label", "class_name", "location", "rotation", "scale", "selected",
    "class_asset_path", "components", "asset_paths", "tags", "editor_only",
    "world_bounds", "parent_name", "child_names", "child_names_truncated",
)

_STATE = {}  # internal actor name -> snapshot dict
_CHANGE_LOG = []  # rolling list of {timestamp, name, action, data}
_MAP_NAME = None
_SESSION_ID = str(uuid.uuid4())
_SESSION_START = time.time()
_LAST_POLL = 0.0
_LAST_CHECKPOINT = time.time()
_TICK_HANDLE = None
_STATE_LOCK = threading.RLock()  # socket server runs on its own thread

# Runtime state is sampled only from the editor tick. The socket thread serves
# this cache and never calls into Unreal's API.
_RUNTIME_STATE = {
    "ok": True,
    "playing": False,
    "server_time": time.time(),
    "worlds": [],
}
# Private bounded cache for clearance queries. It is filled only by the editor
# tick; socket handlers read it under _STATE_LOCK and never touch Unreal.
_RUNTIME_INSTANCE_POINTS = {}
_LAST_RUNTIME_POLL = 0.0

_SERVER = None
_SERVER_THREAD = None

_PERSIST_PATH = os.path.join(
    unreal.Paths.project_saved_dir(), "SceneIndexer", "last_session.json"
)


def _now():
    return time.time()


# --------------------------------------------------------------------------
# State store and full level rescan.
# --------------------------------------------------------------------------


def _asset_path(value):
    """Return a content asset path, excluding native classes and transient objects."""
    if value is None:
        return None
    try:
        path = value.get_path_name()
    except Exception:
        return None
    return path if path.startswith("/") and not path.startswith(("/Script/", "/Temp/")) else None


def _component_assets(component):
    paths = set()
    for property_name in ("static_mesh", "skeletal_mesh_asset", "skeletal_mesh"):
        try:
            path = _asset_path(component.get_editor_property(property_name))
            if path:
                paths.add(path)
        except Exception:
            pass  # This component does not expose that property.
    try:
        for index in range(component.get_num_materials()):
            path = _asset_path(component.get_material(index))
            if path:
                paths.add(path)
    except Exception:
        pass  # Only mesh-like components expose material slots.
    return sorted(paths)


def _component_instance_count(component):
    """Return an instanced-mesh count when supported, otherwise a mesh count."""
    for method_name in ("get_instance_count", "get_num_instances"):
        try:
            method = getattr(component, method_name, None)
            if callable(method):
                return int(method())
        except Exception:
            pass
    # A regular mesh component represents one mesh instance; components with no
    # mesh reference (or non-mesh components) report zero.
    try:
        if _asset_path(component.get_editor_property("static_mesh")):
            return 1
    except Exception:
        pass
    try:
        if (_asset_path(component.get_editor_property("skeletal_mesh_asset")) or
                _asset_path(component.get_editor_property("skeletal_mesh"))):
            return 1
    except Exception:
        pass
    return 0


def _component_instance_positions(component, instance_count, capture_points=False):
    """Summarize bounded world-space instance origins for instanced components."""
    try:
        get_transform = getattr(component, "get_instance_transform", None)
        if not callable(get_transform) or instance_count <= 0:
            return None
        limit = min(instance_count, MAX_INSTANCE_TRANSFORMS)
        indices = (
            range(instance_count) if instance_count <= MAX_INSTANCE_TRANSFORMS else
            [int(round(i * (instance_count - 1) / float(limit - 1))) for i in range(limit)]
        )
        sample_indices = set()
        sample_size = min(INSTANCE_POSITION_SAMPLE_LIMIT, limit)
        if sample_size == 1:
            sample_indices.add(indices[0])
        else:
            sample_indices.update(
                indices[int(round(i * (limit - 1) / float(sample_size - 1)))]
                for i in range(sample_size)
            )
        minimum = [float("inf")] * 3
        maximum = [float("-inf")] * 3
        samples = []
        origin_points = []
        sampled_count = 0
        for index in indices:
            try:
                transform = get_transform(index, world_space=True)
                if transform is None:
                    continue
                position = transform.translation
                coords = [float(position.x), float(position.y), float(position.z)]
            except Exception:
                continue
            sampled_count += 1
            if capture_points:
                origin_points.append(coords)
            for axis in range(3):
                minimum[axis] = min(minimum[axis], coords[axis])
                maximum[axis] = max(maximum[axis], coords[axis])
            if index in sample_indices:
                samples.append([round(value, 3) for value in coords])
        complete = instance_count <= MAX_INSTANCE_TRANSFORMS and sampled_count == instance_count
        result = {
            "sampled_count": sampled_count,
            "complete": complete,
            "truncated": not complete,
            "sample_positions": samples,
        }
        if capture_points:
            result["_origin_points"] = origin_points
        if sampled_count:
            result["world_origin_bounds"] = {
                "description": "world-space instance origins; excludes mesh extents",
                "min": [round(value, 3) for value in minimum],
                "max": [round(value, 3) for value in maximum],
            }
        return result
    except Exception:
        # A transient component/API error must not break the actor snapshot.
        return None


def _actor_tags(actor):
    """Read actor tags through whichever safe property/method this UE build exposes."""
    for getter in ("get_editor_property",):
        try:
            values = getattr(actor, getter)("tags")
            return sorted(str(value) for value in values)
        except Exception:
            pass
    try:
        values = actor.tags
        return sorted(str(value) for value in values)
    except Exception:
        return []


def _actor_editor_only(actor):
    """Return editor-only status, or None when this build exposes no API."""
    for method_name in ("is_editor_only", "is_editor_only_actor"):
        try:
            method = getattr(actor, method_name, None)
            if callable(method):
                return bool(method())
        except Exception:
            pass
    try:
        return bool(actor.get_editor_property("is_editor_only_actor"))
    except Exception:
        return None


def _actor_hierarchy(actor):
    """Return direct attachment relationships, bounded and safe for old UE builds."""
    parent_name = None
    child_names = []
    truncated = False
    try:
        getter = getattr(actor, "get_attach_parent_actor", None)
        parent = getter() if callable(getter) else None
        if parent:
            parent_name = parent.get_name()
    except Exception:
        pass
    try:
        getter = getattr(actor, "get_attached_actors", None)
        children = getter() if callable(getter) else []
        names = sorted(child.get_name() for child in children if child)
        truncated = len(names) > MAX_ATTACHED_CHILDREN
        child_names = names[:MAX_ATTACHED_CHILDREN]
    except Exception:
        pass
    return parent_name, child_names, truncated


def _actor_world_bounds(actor):
    """Return the actor's world axis-aligned bounds, or None if unavailable."""
    try:
        getter = getattr(actor, "get_actor_bounds", None)
        if not callable(getter):
            return None
        # This UE API returns (center, half extents) in world space. Include
        # non-colliding components so editor geometry is represented as well.
        center, extent = getter(False, True)
        center_values = [float(center.x), float(center.y), float(center.z)]
        extent_values = [float(extent.x), float(extent.y), float(extent.z)]
        if (not all(math.isfinite(v) for v in center_values + extent_values) or
                any(v < 0 for v in extent_values)):
            return None
        low = [center_values[i] - extent_values[i] for i in range(3)]
        high = [center_values[i] + extent_values[i] for i in range(3)]
        return {
            "center": [round(v, 3) for v in center_values],
            "extents": [round(v, 3) for v in extent_values],
            "min": [round(v, 3) for v in low],
            "max": [round(v, 3) for v in high],
        }
    except Exception:
        return None


def _actor_components(actor, capture_instance_points=False):
    components = []
    instance_points = {}
    try:
        source = actor.get_components_by_class(unreal.ActorComponent)
    except Exception:
        return (components, instance_points) if capture_instance_points else components
    for component in source:
        if component is None:
            continue
        instance_count = _component_instance_count(component)
        component_snapshot = {
            "name": component.get_name(),
            "class_name": component.get_class().get_name(),
            "asset_paths": _component_assets(component),
            "instance_count": instance_count,
        }
        positions = _component_instance_positions(
            component, instance_count, capture_points=capture_instance_points
        )
        if positions is not None:
            if capture_instance_points:
                instance_points[component_snapshot["name"]] = {
                    "instance_count": instance_count,
                    "complete": positions.get("complete", False),
                    "points": positions.pop("_origin_points", []),
                }
            component_snapshot["instance_positions"] = positions
        components.append(component_snapshot)
    sorted_components = sorted(components, key=lambda item: item["name"])
    if capture_instance_points:
        return sorted_components, instance_points
    return sorted_components


def _actor_snapshot(actor, selected_names, capture_instance_points=False):
    transform = actor.get_actor_transform()
    location = transform.translation
    rotation = transform.rotation.rotator()
    scale = transform.scale3d
    internal_name = actor.get_name()
    try:
        label = actor.get_actor_label()
    except Exception:
        label = internal_name
    class_asset_path = _asset_path(actor.get_class())
    component_result = _actor_components(actor, capture_instance_points)
    if capture_instance_points:
        components, instance_points = component_result
    else:
        components, instance_points = component_result, None
    asset_paths = sorted({
        path for component in components for path in component["asset_paths"]
    } | ({class_asset_path} if class_asset_path else set()))
    parent_name, child_names, child_names_truncated = _actor_hierarchy(actor)
    snapshot = {
        "name": internal_name,
        "label": label,
        "class_name": actor.get_class().get_name(),
        "class_asset_path": class_asset_path,
        "components": components,
        "asset_paths": asset_paths,
        "tags": _actor_tags(actor),
        "editor_only": _actor_editor_only(actor),
        "location": [round(location.x, 3), round(location.y, 3), round(location.z, 3)],
        "rotation": [round(rotation.pitch, 3), round(rotation.yaw, 3), round(rotation.roll, 3)],
        "scale": [round(scale.x, 3), round(scale.y, 3), round(scale.z, 3)],
        "selected": internal_name in selected_names,
        "world_bounds": _actor_world_bounds(actor),
        "parent_name": parent_name,
        "child_names": child_names,
        "child_names_truncated": child_names_truncated,
        "last_changed": _now(),
    }
    if capture_instance_points:
        return snapshot, instance_points
    return snapshot


def _record_change(name, action, data):
    _CHANGE_LOG.append({"timestamp": _now(), "name": name, "action": action,
                        "data": data, "map_name": _MAP_NAME})
    if len(_CHANGE_LOG) > MAX_CHANGE_LOG:
        del _CHANGE_LOG[: len(_CHANGE_LOG) - MAX_CHANGE_LOG]
    # Visible, real-time proof-of-life: without this, the watcher is silent
    # between the one startup line and whatever eventually reads _CHANGE_LOG.
    unreal.log(f"[SceneIndexer] {action}: {name}")


def _full_rescan():
    """Diff the live level against _STATE, recording added/modified/removed."""
    global _MAP_NAME
    # A map load briefly clears the editor world. UE 5.8's
    # EditorActorSubsystem.get_all_level_actors() constructs a native actor
    # iterator without checking that world, and asserts if it is null.
    editor_subsystem = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
    if editor_subsystem is None:
        return
    world = editor_subsystem.get_editor_world()
    if world is None:
        return
    try:
        map_name = world.get_path_name()
    except Exception:
        map_name = world.get_name()
    if not map_name:
        map_name = world.get_name()
    actor_subsystem = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    if actor_subsystem is None:
        return False

    all_actors = actor_subsystem.get_all_level_actors()
    selected_names = {a.get_name() for a in actor_subsystem.get_selected_level_actors()}

    with _STATE_LOCK:
        map_changed = _MAP_NAME is not None and map_name != _MAP_NAME
        if map_changed:
            previous_map = _MAP_NAME
            _STATE.clear()
            _MAP_NAME = map_name
            _record_change("__map__", "map_changed", {"old_map": previous_map, "new_map": map_name})
        elif _MAP_NAME is None:
            _MAP_NAME = map_name
        current_names = set()
        for actor in all_actors:
            if not actor:
                continue
            internal_name = actor.get_name()
            current_names.add(internal_name)
            snap = _actor_snapshot(actor, selected_names)
            prev = _STATE.get(internal_name)
            if prev is None:
                _STATE[internal_name] = snap
                if not map_changed:
                    _record_change(internal_name, "added", snap)
            elif any(prev.get(k) != snap.get(k) for k in TRACKED_FIELDS):
                _STATE[internal_name] = snap
                _record_change(internal_name, "modified", snap)

        removed = set(_STATE.keys()) - current_names
        for name in removed:
            _record_change(name, "removed", _STATE.get(name))
            del _STATE[name]
    return map_changed


def _on_tick(delta_seconds):
    global _LAST_POLL, _LAST_RUNTIME_POLL, _LAST_CHECKPOINT
    now = _now()
    if now - _LAST_RUNTIME_POLL >= RUNTIME_POLL_INTERVAL_SECONDS:
        _LAST_RUNTIME_POLL = now
        try:
            _poll_runtime_state()
        except Exception as e:
            _set_runtime_error(str(e))
    if now - _LAST_POLL < POLL_INTERVAL_SECONDS:
        return
    _LAST_POLL = now
    try:
        map_changed = _full_rescan()
        if map_changed is None:
            return  # editor world is temporarily unavailable during map load
        # Save the newly scanned map immediately after a transition. A periodic
        # save of the previous map here could otherwise overwrite its checkpoint.
        if map_changed or now - _LAST_CHECKPOINT >= CHECKPOINT_INTERVAL_SECONDS:
            _save_session_checkpoint()
            _LAST_CHECKPOINT = now
    except Exception as e:
        unreal.log_error(f"[SceneIndexer] tick error: {e}")


def _set_runtime_error(message):
    with _STATE_LOCK:
        _RUNTIME_STATE.update({
            "ok": False,
            "playing": False,
            "server_time": _now(),
            "worlds": [],
            "error": message,
        })
        _RUNTIME_INSTANCE_POINTS.clear()


def _poll_runtime_state():
    """Refresh the PIE snapshot on the editor thread, never on a socket thread."""
    editor_library = getattr(unreal, "EditorLevelLibrary", None)
    get_pie_worlds = getattr(editor_library, "get_pie_worlds", None)
    if not callable(get_pie_worlds):
        _set_runtime_error("EditorLevelLibrary.get_pie_worlds is unavailable")
        return

    worlds = get_pie_worlds(False) or []
    runtime_worlds = []
    runtime_instance_points = {}
    cached_point_count = 0
    gameplay_statics = getattr(unreal, "GameplayStatics", None)
    get_actors = getattr(gameplay_statics, "get_all_actors_of_class", None)
    actor_class = getattr(unreal, "Actor", None)
    if worlds and (not callable(get_actors) or actor_class is None):
        _set_runtime_error("GameplayStatics.get_all_actors_of_class or Actor is unavailable")
        return

    for world in worlds:
        world_name = world.get_name()
        actors = get_actors(world, actor_class) if callable(get_actors) else []
        actors = actors or []
        objects = {}
        world_points = {}
        snapshot_errors = 0
        for index, actor in enumerate(actors):
            if index >= MAX_RUNTIME_ACTORS:
                break
            if actor is None:
                continue
            try:
                name = actor.get_name()
                # Runtime objects are not editor-selected; use the shared
                # serializer to retain the same component and instance details.
                snapshot, actor_points = _actor_snapshot(
                    actor, set(), capture_instance_points=True
                )
                for component_points in actor_points.values():
                    points = component_points.get("points", [])
                    remaining = max(0, MAX_RUNTIME_CACHED_POINTS - cached_point_count)
                    if len(points) > remaining:
                        component_points["points"] = points[:remaining]
                        component_points["complete"] = False
                    cached_point_count += len(component_points["points"])
                objects[name], world_points[name] = snapshot, actor_points
            except Exception:
                snapshot_errors += 1
                continue
        runtime_worlds.append({
            "world_name": world_name,
            "actor_count": len(actors),
            "truncated": len(actors) > MAX_RUNTIME_ACTORS,
            "snapshot_errors": snapshot_errors,
            "objects": objects,
        })
        runtime_instance_points[world_name] = world_points

    with _STATE_LOCK:
        _RUNTIME_STATE.clear()
        _RUNTIME_STATE.update({
            "ok": True,
            "playing": bool(runtime_worlds),
            "server_time": _now(),
            "worlds": runtime_worlds,
        })
        _RUNTIME_INSTANCE_POINTS.clear()
        _RUNTIME_INSTANCE_POINTS.update(runtime_instance_points)


def _analyze_instance_clearance(req):
    actor_id = req.get("actor_id")
    center = req.get("center")
    radius = req.get("radius_cm")
    if not isinstance(actor_id, str) or not actor_id.strip():
        return {"ok": False, "error": "invalid_actor_id"}
    if (not isinstance(center, (list, tuple)) or len(center) != 3 or
            any(isinstance(v, bool) or not isinstance(v, (int, float)) or
                not math.isfinite(float(v)) for v in center)):
        return {"ok": False, "error": "invalid_center"}
    if (isinstance(radius, bool) or not isinstance(radius, (int, float)) or
            not math.isfinite(float(radius)) or radius < 0):
        return {"ok": False, "error": "invalid_radius_cm"}
    world_name = req.get("world_name")
    component_prefix = req.get("component_prefix")
    if world_name is not None and not isinstance(world_name, str):
        return {"ok": False, "error": "invalid_world_name"}
    if component_prefix is not None and not isinstance(component_prefix, str):
        return {"ok": False, "error": "invalid_component_prefix"}

    with _STATE_LOCK:
        if not _RUNTIME_STATE.get("playing"):
            return {"ok": False, "error": "pie_inactive"}
        worlds = list(_RUNTIME_STATE.get("worlds", []))
        private_points = _RUNTIME_INSTANCE_POINTS
        if world_name:
            worlds = [w for w in worlds if w.get("world_name") == world_name]
        matches = []
        for world in worlds:
            actor = world.get("objects", {}).get(actor_id)
            if actor is None:
                actor = next((obj for obj in world.get("objects", {}).values()
                              if obj.get("label") == actor_id), None)
            if actor is not None:
                matches.append((world.get("world_name"), actor,
                                private_points.get(world.get("world_name"), {}).get(actor.get("name"), {})))
        center_xy = (float(center[0]), float(center[1]))
        radius_value = float(radius)
        result_components = []
        for matched_world, actor, actor_points in matches:
            for component in actor.get("components", []):
                name = component.get("name", "")
                if component_prefix is not None and not name.startswith(component_prefix):
                    continue
                if name not in actor_points:
                    continue  # This component has no instanced-mesh position cache.
                cached = actor_points.get(name, {})
                points = cached.get("points", [])
                distances = [math.hypot(p[0] - center_xy[0], p[1] - center_xy[1]) for p in points]
                inside = sum(d <= radius_value for d in distances)
                complete = bool(cached.get("complete")) and len(points) == cached.get("instance_count", -1)
                result_components.append({
                    "world_name": matched_world,
                    "component_name": name,
                    "instance_count": int(component.get("instance_count", 0)),
                    "checked_count": len(distances),
                    "inside_count": inside,
                    "inside_count_is_lower_bound": not complete,
                    "nearest_distance_cm": min(distances) if distances else None,
                    "farthest_distance_cm": max(distances) if distances else None,
                    "complete": complete,
                })
        if not matches:
            return {"ok": False, "error": "not_found", "actor_id": actor_id}
        if not result_components:
            return {"ok": False, "error": "no_instanced_components", "actor_id": actor_id}
        counts = [c["instance_count"] for c in result_components]
        checked = [c["checked_count"] for c in result_components]
        inside_counts = [c["inside_count"] for c in result_components]
        nearest = [c["nearest_distance_cm"] for c in result_components if c["nearest_distance_cm"] is not None]
        farthest = [c["farthest_distance_cm"] for c in result_components if c["farthest_distance_cm"] is not None]
        complete = all(c["complete"] for c in result_components)
        return {
            "ok": True, "actor_id": actor_id,
            "instance_count": sum(counts), "checked_count": sum(checked),
            "inside_count": sum(inside_counts),
            "inside_count_is_lower_bound": not complete,
            "nearest_distance_cm": min(nearest) if nearest else None,
            "farthest_distance_cm": max(farthest) if farthest else None,
            "complete": complete, "components": result_components,
        }


# --------------------------------------------------------------------------
# Reconciliation across editor restarts
# --------------------------------------------------------------------------


def _load_previous_session():
    try:
        with open(_PERSIST_PATH, "r") as f:
            return json.load(f)
    except Exception:
        return None


def _save_session_for_shutdown():
    _save_session_checkpoint()


def _save_session_checkpoint():
    """Atomically persist a lock-consistent snapshot; called only on editor thread."""
    try:
        os.makedirs(os.path.dirname(_PERSIST_PATH), exist_ok=True)
        with _STATE_LOCK:
            objects = copy.deepcopy(_STATE)
            map_name = _MAP_NAME
        payload = {
            "session_id": _SESSION_ID,
            "session_start": _SESSION_START,
            "saved_at": _now(),
            "map_name": map_name,
            "objects": objects,
        }
        temp_path = _PERSIST_PATH + ".tmp"
        try:
            with open(temp_path, "w") as f:
                json.dump(payload, f)
                f.flush()
                os.fsync(f.fileno())
            os.replace(temp_path, _PERSIST_PATH)
        finally:
            if os.path.exists(temp_path):
                os.remove(temp_path)
    except Exception as e:
        try:
            unreal.log_error(f"[SceneIndexer] failed to persist state checkpoint: {e}")
        except Exception:
            pass


def _reconcile_with_previous_session():
    previous = _load_previous_session()

    _full_rescan()  # seed _STATE with the live level as of this startup

    if previous is None:
        return

    prev_objects = previous.get("objects", {})
    prev_session_id = previous.get("session_id")
    prev_map_name = previous.get("map_name")
    if prev_map_name and _MAP_NAME and prev_map_name != _MAP_NAME:
        _record_change("__map__", "map_changed", {
            "old_map": prev_map_name, "new_map": _MAP_NAME,
        })
        return
    old_names, new_names = set(prev_objects.keys()), set(_STATE.keys())

    diff = []
    for name in sorted(new_names - old_names):
        diff.append({"name": name, "action": "added", "data": _STATE[name]})
    for name in sorted(old_names - new_names):
        diff.append({"name": name, "action": "removed", "data": prev_objects[name]})
    for name in sorted(old_names & new_names):
        # Compare tracked fields only -- last_changed always differs across a
        # restart (it's re-stamped by the seeding rescan above), so comparing
        # full dicts would flag every surviving actor as "modified" even when
        # nothing about it actually changed.
        if any(prev_objects[name].get(k) != _STATE[name].get(k) for k in TRACKED_FIELDS):
            diff.append({"name": name, "action": "modified", "data": _STATE[name]})

    _record_change(
        "__session__",
        "reconciled",
        {
            "previous_session_id": prev_session_id,
            "new_session_id": _SESSION_ID,
            "changes": diff,
        },
    )


# --------------------------------------------------------------------------
# TCP/JSON server for the standalone Unreal MCP process.
# --------------------------------------------------------------------------


class _IndexerRequestHandler(socketserver.StreamRequestHandler):
    def handle(self):
        try:
            line = self.rfile.readline()
            if not line:
                return
            try:
                req = json.loads(line.decode("utf-8"))
            except Exception:
                self._respond({"ok": False, "error": "invalid_json"})
                return

            cmd = req.get("cmd")
            if cmd == "ping":
                with _STATE_LOCK:
                    object_count = len(_STATE)
                self._respond(
                    {
                        "ok": True,
                        "session_id": _SESSION_ID,
                        "session_start": _SESSION_START,
                        "server_time": _now(),
                        "project_dir": PROJECT_DIR,
                        "object_count": object_count,
                    }
                )
            elif cmd == "get_state":
                with _STATE_LOCK:
                    objects = dict(_STATE)
                self._respond(
                    {
                        "ok": True,
                        "session_id": _SESSION_ID,
                        "session_start": _SESSION_START,
                        "server_time": _now(),
                        "project_dir": PROJECT_DIR,
                        "map_name": _MAP_NAME,
                        "objects": objects,
                    }
                )
            elif cmd == "get_runtime_state":
                with _STATE_LOCK:
                    runtime_state = dict(_RUNTIME_STATE)
                    runtime_state["project_dir"] = PROJECT_DIR
                    runtime_state["worlds"] = [
                        {
                            "world_name": world["world_name"],
                            "actor_count": world["actor_count"],
                            "truncated": world["truncated"],
                            "snapshot_errors": world["snapshot_errors"],
                            "objects": dict(world["objects"]),
                        }
                        for world in _RUNTIME_STATE.get("worlds", [])
                    ]
                self._respond(runtime_state)
            elif cmd == "analyze_instance_clearance":
                self._respond(_analyze_instance_clearance(req))
            elif cmd == "get_changes_since":
                try:
                    since = float(req.get("since", 0))
                    if not math.isfinite(since):
                        raise ValueError()
                except (TypeError, ValueError, OverflowError):
                    self._respond({"ok": False, "error": "invalid_since"})
                    return
                with _STATE_LOCK:
                    changes = [c for c in _CHANGE_LOG if c["timestamp"] > since]
                self._respond(
                    {
                        "ok": True,
                        "session_id": _SESSION_ID,
                        "server_time": _now(),
                        "project_dir": PROJECT_DIR,
                        "map_name": _MAP_NAME,
                        "changes": changes,
                    }
                )
            elif cmd == "query_actors":
                label = req.get("label_contains", "")
                class_name = req.get("class_contains", "")
                bounds = req.get("bounds")
                limit = req.get("limit", 100)
                if not isinstance(label, str) or not isinstance(class_name, str):
                    self._respond({"ok": False, "error": "invalid_filter"})
                    return
                if isinstance(limit, bool) or not isinstance(limit, int) or not 1 <= limit <= 500:
                    self._respond({"ok": False, "error": "invalid_limit"})
                    return
                if bounds is not None:
                    try:
                        low, high = bounds["min"], bounds["max"]
                        valid = (isinstance(bounds, dict) and len(low) == 3 and len(high) == 3 and
                                 all(not isinstance(v, bool) and isinstance(v, (int, float)) and math.isfinite(float(v)) for v in list(low) + list(high)) and
                                 all(float(low[i]) <= float(high[i]) for i in range(3)))
                    except Exception:
                        valid = False
                    if not valid:
                        self._respond({"ok": False, "error": "invalid_bounds"})
                        return
                with _STATE_LOCK:
                    found = []
                    for actor in _STATE.values():
                        if label.casefold() not in str(actor.get("label", "")).casefold():
                            continue
                        if class_name.casefold() not in str(actor.get("class_name", "")).casefold():
                            continue
                        if bounds is not None:
                            loc = actor.get("location", {})
                            coords = [loc.get(axis) for axis in ("x", "y", "z")] if isinstance(loc, dict) else loc
                            if (not isinstance(coords, (list, tuple)) or len(coords) != 3 or
                                    not all(bounds["min"][i] <= coords[i] <= bounds["max"][i] for i in range(3))):
                                continue
                        found.append(actor)
                        if len(found) > limit:
                            break
                    truncated = len(found) > limit
                    self._respond({"ok": True, "map_name": _MAP_NAME, "objects": found[:limit],
                                   "truncated": truncated, "limit": limit,
                                   "project_dir": PROJECT_DIR})
            elif cmd == "get_object":
                name = req.get("name")
                with _STATE_LOCK:
                    obj = _STATE.get(name)
                    if obj is None:
                        for candidate in _STATE.values():
                            if candidate.get("label") == name:
                                obj = candidate
                                break
                if obj is None:
                    self._respond({"ok": False, "error": "not_found", "name": name, "project_dir": PROJECT_DIR})
                else:
                    self._respond({"ok": True, "object": obj, "project_dir": PROJECT_DIR})
            else:
                self._respond({"ok": False, "error": "unknown_command", "cmd": cmd})
        except Exception as e:
            try:
                self._respond({"ok": False, "error": str(e)})
            except Exception:
                pass

    def _respond(self, payload):
        self.wfile.write((json.dumps(payload) + "\n").encode("utf-8"))


class _IndexerServer(socketserver.ThreadingTCPServer):
    allow_reuse_address = True
    daemon_threads = True


def _start_server():
    global _SERVER, _SERVER_THREAD
    host = CONFIG.get("host", "127.0.0.1")
    port = CONFIG.get("unreal_port", 9892)
    try:
        _SERVER = _IndexerServer((host, port), _IndexerRequestHandler)
    except OSError as e:
        unreal.log_error(
            f"[SceneIndexer] could not bind {host}:{port} ({e}) -- "
            "is another instance already running, or is the port in use?"
        )
        return
    _SERVER_THREAD = threading.Thread(target=_SERVER.serve_forever, daemon=True)
    _SERVER_THREAD.start()
    unreal.log(f"[SceneIndexer] TCP server listening on {host}:{port}")


def _start():
    global _TICK_HANDLE
    _reconcile_with_previous_session()
    _TICK_HANDLE = unreal.register_slate_pre_tick_callback(_on_tick)
    unreal.register_python_shutdown_callback(_save_session_for_shutdown)
    _start_server()
    unreal.log(
        f"[SceneIndexer] watcher started (session {_SESSION_ID}), "
        f"tracking {len(_STATE)} actors"
    )


_start()
