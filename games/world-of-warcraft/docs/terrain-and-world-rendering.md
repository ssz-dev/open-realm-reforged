# Terrain And World Rendering

## WDT And ADT

The WoW renderer starts from a WDT path and tracks a 64x64 tile grid. Each present tile resolves to an ADT file:

```text
World/Maps/<MapName>/<MapName>_<tile_x>_<tile_y>.adt
```

Important local constants:

| Constant | Value | Meaning |
| --- | --- | --- |
| `WOW_WDT_TILES` | `64` | WDT tile grid size per axis. |
| `WOW_ADT_SIZE` | `533.333313f` | World units per ADT tile. |
| `WOW_ADT_CHUNK_SIZE` | `WOW_ADT_SIZE / 16` | World units per ADT chunk. |
| `WOW_ADT_UNIT_SIZE` | `WOW_ADT_CHUNK_SIZE / 8` | Fine height-grid unit. |
| `WOW_MCVT_COUNT` | `9 * 9 + 8 * 8` | Height samples per ADT chunk. |

The current renderer loads a small ADT window around the active area and keeps an alpha atlas for terrain splat masks.

## ADT Chunk Tags

The code compares chunk tags in reversed byte order. Important ADT tags currently handled:

| Tag In Code | Normal Tag | Purpose |
| --- | --- | --- |
| `XETM` | `MTEX` | Texture filename block. |
| `XDMM` | `MMDX` | Doodad model filename block. |
| `DIMM` | `MMID` | Doodad model filename offsets. |
| `FDDM` | `MDDF` | Doodad placement definitions. |
| `OMWM` | `MWMO` | WMO filename block. |
| `DIWM` | `MWID` | WMO filename offsets. |
| `FDOM` | `MODF` | WMO placement definitions. |
| `KNCM` | `MCNK` | Terrain chunk. |
| `TVCM` | `MCVT` | Terrain heights. |
| `RNCM` | `MCNR` | Terrain normals. |
| `YLCM` | `MCLY` | Texture layers. |
| `LACM` | `MCAL` | Alpha maps. |

## Terrain Layers

Each ADT chunk can carry up to four texture layers. The renderer stores:

- up to four texture handles,
- a per-chunk alpha atlas coordinate,
- decoded alpha maps,
- chunk position,
- `WOW_MCVT_COUNT` heights,
- optional normals,
- bounds for culling.

The splat path has a small Z bias and height-delta guard to keep layer geometry close to terrain without exploding
across sharp height changes. Circular selection, hover, cursor, and ability feedback is submitted as a
diameter-sized rectangle whose texture and subdivided vertices conform to the streamed ADT terrain.

## Grass

The WoW renderer builds lightweight grass geometry while loading each ADT chunk. Placement is derived from ADT texture layer data:

- `MCLY.effect_id` marks terrain layers that should emit ground clutter.
- The decoded 64x64 `MCAL` alpha maps decide where those layers are visible.
- Chunk height samples place each grass clump on the terrain surface.

Each generated clump is two crossed, tapered blade triangles in a chunk-local VAO. Rendering uses a small WoW-owned shader with camera-distance fade and cheap vertex wind, and culls whole chunk grass buffers before drawing. The first-pass tuning constants are:

| Constant | Value | Meaning |
| --- | --- | --- |
| `WOW_GRASS_DENSITY` | `1.0f` | Scales generated clumps per eligible layer sample. |
| `WOW_GRASS_DRAW_DISTANCE` | `220.0f` | Camera-space draw/fade distance for grass chunks. |

This is intentionally a first-pass ground-effect renderer. Exact client-style `GroundEffectTexture.dbc` and `GroundEffectDoodad.dbc` model selection can replace the placeholder blade geometry without changing the ADT placement path.

## Gameplay World Queries

`games/world-of-warcraft/common/world_wow.c` keeps a fixed nine-ADT LRU for
gameplay queries. It loads `MCVT` height samples from `MCNK` chunks and resolves
point height by splitting a local cell around the center sample into triangles,
then using barycentric interpolation.

The same ADT read feeds `MWMO`/`MWID`/`MODF` into the gameplay WMO cache and
`MMDX`/`MMID`/`MDDF` into the verified doodad cache. Renderer, gameplay, and
diagnostics use the single `WowAdt_ParseObjects` view, so placement lookup
cannot drift between subsystems.
`CM_WowQueryGround` resolves terrain, indoor floors, stacked floors, ramps, and
roofs as competing candidates. `CM_WowSweepWorld` performs swept
capsule-shaped obstacle queries with instance/group bounds as broadphase and
`MOBN`/`MOBR` triangle geometry as WMO narrowphase.

### Verified Doodad Collision

An ADT doodad is solid only when its M2 contains complete, bounded
`collision_indices` and `collision_positions` arrays. Render bounds, filenames,
and guessed MDDF flags never select collision. M2s without those dedicated
arrays are explicitly cached as decoration; malformed collision arrays are
logged and rejected.

Classic ADTs may reference a model as `.mdx` while `model.MPQ` stores the
payload as `.m2`. Collision uses the same verified extension resolution as the
renderer. `wow_m2_format.h` centralizes raw/wrapped M2 payload lookup, classic
and modern header offsets, overflow-safe arrays, collision validation, and the
exact non-ground instance transform.

Each active ADT tile owns:

- one reference to each distinct M2 collision model used by that tile,
- transformed bounds per solid instance,
- one center-bucket reference per instance across the native 16x16 chunk grid,
- the largest horizontal instance extent used to expand query chunk ranges.

The exact instance AABB rejects false candidates before the existing analytic
sphere/triangle sweep. The mesh is shared across repeated instances and freed
when the last active tile reference leaves the nine-tile LRU. Sweeps allocate
nothing. Movement, creature navigation, LOS, melee validation, Throw,
projectiles, and camera mode all receive `WOW_SURFACE_DOODAD` through the
existing `CM_WowSweepWorld` path.

### World-query profiler

The disabled-by-default `wow_world_profile` developer cvar instruments that
same query path without allocating per query. Use:

```text
wow_world_profile 1
world_profile_reset
world_profile
```

The summary reports terrain LRU behavior, ground and sweep volume, WMO and
doodad broadphase/narrowphase work, LOS, projectile wall hits, and
obstacle-navigation fallbacks. The first non-empty summary captures observed
candidate, triangle, and cache-miss baselines; later summaries warn only when
an interval exceeds that observed workload. `world_profile_reset` clears every
counter and peak but retains this baseline for interval comparison. Warnings
are summary-only and never alter gameplay. `wow_world_profile 0` disables
collection while leaving all query results unchanged. The shared query layer
has no stable sub-frame timer; the server clock advances per frame, so this
profiler deliberately records work counts rather than misleading per-query
durations.

### World Streaming and AI Budgets

Loaded, visible, physically relevant, and AI-active are separate scopes:

- the renderer owns a camera-centered `3x3` ADT draw window and still culls
  chunks, WMO groups, and doodads inside it;
- gameplay collision owns an independent nine-tile LRU populated only by
  ground and sweep queries, so a neighboring physical tile can stay resident
  without being visible;
- creature entities remain server-owned across both windows, but their update
  rate depends on combat state and player distance;
- projectiles remain frame-rate entities because every flight segment must
  sweep collision before damage can be awarded.

Creature scheduling uses one state rate and one accumulated-millisecond step:

| Rate | Interval | States |
| --- | ---: | --- |
| High | `100 ms` | aggro, chase, attack, pain/cast locks, aggro range, active quest interaction |
| Medium | `200 ms` | evade/return and nearby idle |
| Low | `1000 ms` | distant moving patrol |
| Dead | `500 ms` | death/respawn lifecycle |
| Dormant | none | distant stationary idle or no valid player |

Rate transitions discard the old state's partial interval, preventing a
distant idle budget from becoming an immediate oversized combat step. Due
updates consume the actual accumulated server milliseconds for movement,
animation, attack locks, regeneration, and respawn; player cooldowns and
projectile flight continue to advance every server frame. Dormant frames and
deferred dead frames perform no movement query.

The collision LRU is allocation-bounded at nine ADTs. Each slot owns its WMO
and verified doodad references, and eviction frees those references only after
the current tile operation has completed. Map load/reload and shutdown free all
slot-owned collision data and reset the shared model cache. Player save/load
does not touch world caches. `CM_WowWorldCacheSnapshot` exposes only capacity
and aggregate occupancy for deterministic lifecycle tests; it never exposes a
cache-owned pointer.

The profiler summary includes high, medium, low, deferred, and dormant AI
counts beside query totals. This makes a bounded capture show whether lower
query volume came from legitimate state budgeting rather than skipped combat
or projectile work.

### Camera Collision

OpenWoW derives the camera anchor from the interpolated authoritative player
entity, including its Z coordinate. Reconstructing Z from terrain is incorrect
inside WMOs and on stacked floors. The server keeps three distinct distances:

- the player's desired distance,
- the distance currently allowed by static collision,
- the rate-limited visual distance published in `playerState_t`.

`Wow_CameraUpdate` sweeps a `0.25`-radius sphere from `0.9` units behind the
anchor through the same `CM_WowSweepWorld` path used by movement, LOS, and
projectiles. Camera mode tests native MCVT terrain triangles and every WMO
triangle orientation, including walls, floors, and ceilings; dynamic entities
do not block it. Contact contracts immediately for safety, while normal
snapshot interpolation smooths the rendered result. Release uses hysteresis,
frame-time-independent exponential response, and an `8` unit/second maximum
return speed. Camera state never changes the authoritative player position or
the stored desired distance. The profiler summary exposes camera sweep and
clamp totals for bounded real-client captures.

## Doodads And WMOs

ADT object rendering remains renderer-owned:

- `MDDF` entries produce doodad instances backed by M2 models.
- `MODF` entries produce WMO instances.
- Doodads are bucketed for draw-distance culling.
- Missing doodad/WMO models are counted and can be represented by debug marker geometry when debug flags are enabled.

The renderer, gameplay collision, and diagnostics share ADT object and WMO
chunk interpretation through `games/world-of-warcraft/common/`; gameplay does
not depend on renderer runtime objects. Game entities are not spawned for every
ADT doodad. Decorative M2 doodads stay non-solid by data contract.

## Current Limits

- Terrain rendering is the core focus.
- WMO and verified static doodad collision are present; portals, lighting,
  particles, water, and animation fidelity are incomplete.
- The draw window and asset compatibility are tuned around local classic-era data.
- Production support for arbitrary WoW client versions is not present.
