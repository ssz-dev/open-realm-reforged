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

The same ADT read feeds `MWMO`/`MWID`/`MODF` into the gameplay WMO cache.
`CM_WowQueryGround` resolves terrain, indoor floors, stacked floors, ramps, and
roofs as competing candidates. `CM_WowSweepWorld` performs swept
capsule-shaped obstacle queries with instance/group bounds as broadphase and
`MOBN`/`MOBR` triangle geometry as narrowphase.

### World-query profiler

The disabled-by-default `wow_world_profile` developer cvar instruments that
same query path without allocating per query. Use:

```text
wow_world_profile 1
world_profile_reset
world_profile
```

The summary reports terrain LRU behavior, ground and sweep volume, WMO
broadphase/narrowphase work, LOS, projectile wall hits, and obstacle-navigation
fallbacks. The first non-empty summary captures observed candidate, triangle,
and cache-miss baselines; later summaries warn only when an interval exceeds
that observed workload. `world_profile_reset` clears every counter and peak but
retains this baseline for interval comparison. Warnings are summary-only and
never alter gameplay. `wow_world_profile 0` disables collection while leaving
all query results unchanged. The shared query layer has no stable sub-frame
timer; the server clock advances per frame, so this profiler deliberately
records work counts rather than misleading per-query durations.

## Doodads And WMOs

ADT object rendering remains renderer-owned:

- `MDDF` entries produce doodad instances backed by M2 models.
- `MODF` entries produce WMO instances.
- Doodads are bucketed for draw-distance culling.
- Missing doodad/WMO models are counted and can be represented by debug marker geometry when debug flags are enabled.

The renderer and gameplay collision share WMO chunk interpretation through
`games/world-of-warcraft/common/wow_wmo_format.h`; gameplay does not depend on
renderer runtime objects. Game entities are not spawned for every ADT doodad.
Small and decorative M2 doodads remain non-solid until verified collision
geometry or collidable flags can select only relevant static obstacles.

## Current Limits

- Terrain rendering is the core focus.
- WMO rendering/collision is present; portals, doodad collision, lighting,
  particles, water, and animation fidelity are incomplete.
- The draw window and asset compatibility are tuned around local classic-era data.
- Production support for arbitrary WoW client versions is not present.
