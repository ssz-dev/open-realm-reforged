# OpenWoW World Physics

## Authoritative chain

Gameplay movement uses one server-owned chain:

```text
Player / creature intent
  -> Wow_MoveEntity
  -> CM_WowSweepWorld
  -> bounded depenetration / slide
  -> CM_WowQueryGround
  -> loaded ADT terrain + WMO collision cache
  -> entityState_t.origin
  -> snapshot / renderer / HUD
```

`CM_WowQueryGround` is declared in
`games/world-of-warcraft/common/wow_world_query.h`. Its result carries height,
normal, and `WOW_SURFACE_TERRAIN`, `WOW_SURFACE_WMO`, or
`WOW_SURFACE_DOODAD`. A false return is the only no-contact result; gameplay
must not substitute a spawn or global height.

`CM_WowSweepWorld` takes one vertical capsule-shaped query with foot position,
horizontal displacement, radius, and height. It returns earliest contact,
normal, penetration depth, and surface. Player and creature dimensions differ,
but both use this API. `Wow_MoveEntity` limits substeps and contact bumps,
projects remaining motion onto the contact plane, and never scans renderer
objects.

`Wow_PlaceEntityOnGround` is the shared spawn, respawn, and relocation path.
After selecting a floor it performs a zero-length overlap sweep, so a spawn in
a WMO wall remains deferred.
`Wow_MoveEntity` owns grounded/airborne state, bounded substeps, step height,
slope rejection, ground snap, vertical velocity, and gravity. Callers provide
only desired horizontal displacement and elapsed seconds.

## World cache

ADT height data uses a fixed nine-tile LRU. A query derives the MCNK row and
column directly from the world coordinate; it does not scan all 256 chunks.
Normals use cached neighboring height samples.

The same ADT read parses `MWMO`, `MWID`, and `MODF` into tile-local WMO
instances. WMO models are shared by path. Per-instance and per-group AABBs are
broadphase only; final contacts use triangles selected from `MOBN` leaf ranges
and `MOBR` face references. `MOPY` collision/render flags are an explicit,
logged fallback only when a group has no BSP collision chunks.

`games/world-of-warcraft/common/wow_wmo_format.h` is the single WMO root/group
chunk parser for renderer and collision. Do not add another parser for camera,
AI, or diagnostics.

Multiple floors compete in the central query. It selects the highest walkable
surface inside the downward range and the caller's small walkable-up range;
terrain cannot overwrite a higher interior floor, while roofs clearly above
the feet are excluded.

M2 doodads are not made solid by their render bounds. A future doodad slice
must require verified collision geometry or a documented collidable flag; a
blanket AABB over all ADT doodads would close doors and block grass.

## Tests

Run the artificial physics fixtures without WoW assets:

```bash
make test-wow-physics
make test-wow-wmo
make test-wow-combat
make test-wow-game
```

`test_wow_physics.c` supplies analytic ground and obstacle surfaces for wall,
corner, door, slide, thin-wall sweep, depenetration, dead-entity, and respawn
paths. `test_wow_wmo.c` verifies the shared root/group parser and MODF
transform. `test_wow_game.c` builds an artificial ADT and WMO with stacked
floors, roof, ramp, split wall, and open doorway, then runs player movement and
creature chase through the real world query. No fixture reads `data/`.
