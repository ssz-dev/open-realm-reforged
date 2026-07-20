# OpenWoW World Physics

## Authoritative chain

Gameplay movement uses one server-owned chain:

```text
Player / creature intent
  -> Wow_MoveEntity
  -> CM_WowQueryGround
  -> loaded ADT height cache
  -> entityState_t.origin
  -> snapshot / renderer / HUD
```

`CM_WowQueryGround` is declared in
`games/world-of-warcraft/common/wow_world_query.h`. Its result carries height,
normal, and `WOW_SURFACE_TERRAIN`, `WOW_SURFACE_WMO`, or
`WOW_SURFACE_DOODAD`. A false return is the only no-contact result; gameplay
must not substitute a spawn or global height.

`Wow_PlaceEntityOnGround` is the shared spawn, respawn, and relocation path.
`Wow_MoveEntity` owns grounded/airborne state, bounded substeps, step height,
slope rejection, ground snap, vertical velocity, and gravity. Callers provide
only desired horizontal displacement and elapsed seconds.

## Terrain cache

ADT height data uses a fixed nine-tile LRU. A query derives the MCNK row and
column directly from the world coordinate; it does not scan all 256 chunks.
Normals use cached neighboring height samples.

## Tests

Run the artificial physics fixtures without WoW assets:

```bash
make test-wow-physics
make test-wow-combat
make test-wow-game
```

`test_wow_physics.c` supplies analytic flat, slope, step, and missing-ground
surfaces. `test_wow_game.c` builds complete artificial ADT tiles in memory.
Neither fixture reads `data/`.
