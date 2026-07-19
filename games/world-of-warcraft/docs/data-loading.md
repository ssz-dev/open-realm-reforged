# Data Loading

## Current Target Shape

The World of Warcraft target is built as `openwow`. It links WoW-specific game, renderer, and UI libraries through the same selected-game runtime boundary as the Warcraft III and StarCraft II targets.

Build:

```bash
make openwow
```

Run with the Makefile sample path:

```bash
make run-wow
```

Direct run:

```bash
build/bin/openwow -data data/world-of-warcraft +map World/Maps/Azeroth/Azeroth.wdt
```

The current data path expects a locally supplied WoW client install or extracted/installable MPQs. The repository does not contain retail client data.

## Installed Data Layout

The local workflow described by `tools/README.md` installs data under:

```text
data/world-of-warcraft/
```

Useful archive examples:

- `model.MPQ`
- `texture.MPQ`
- `dbc.MPQ`
- terrain/world archives containing `World/Maps/...`

The WoW target reads files through the engine filesystem/archive layer. Keep tests and fixtures independent from a developer's local data folder unless the test is explicitly a manual asset experiment.

## Map Entry

The map command accepts a WDT path:

```text
World/Maps/<MapName>/<MapName>.wdt
```

Examples:

```text
World/Maps/Azeroth/Azeroth.wdt
World/Maps/Kalimdor/Kalimdor.wdt
```

The common world path normalizes the map directory/name, then derives ADT paths:

```text
World/Maps/<MapName>/<MapName>_<tile_x>_<tile_y>.adt
```

The coordinate helper maps world coordinates to the 64x64 ADT grid with `32.0 - coord / 533.333313`.

## Classic Minimap Tiles

The 1.x client stores logical minimap names in `Textures\Minimap\md5translate.trs` and the corresponding BLP files
under hashed names in `texture.MPQ`. For example:

```text
Azeroth\map29_31.blp -> textures\Minimap\303d8fede1f036353bfe99b85419eb21.blp
```

The WoW renderer resolves a 3x3 tile window around the camera through this table. It updates the fractional position
inside the center ADT every frame, clips the window to the HUD viewport, and loads a new window only when the camera
crosses an ADT boundary. This keeps the map moving continuously without doing archive I/O every frame. The archive
path uses the entry's actual `textures\Minimap` casing.

The server reads the area ID from MCNK header offset `0x34` in the active ADT and resolves field 11 in the classic
21-field `DBFilesClient\AreaTable.dbc` record. That English area name is the minimap header shown to the player.

## GM Exploration Mode

After Azeroth has loaded, click **GM** beside the upper-right minimap. The native exploration panel remains available after
the archived WoW glue UI shuts down and provides:

- **GM ON (10x)** / **GM OFF** for accelerated WASD movement.
- **X/Y ± 1 tile** for terrain-grounded jumps by one 533.33-unit ADT tile.
- The current world X/Y position for orientation.

Teleporting remains server-authoritative and disabled until GM mode is enabled. Destinations outside the world bounds
are rejected, and OpenWoW samples terrain height at the destination so the player and camera remain grounded.

## DBC Files

The current code reads classic-style `WDBC` files for targeted lookups rather than full DBC/DB2 coverage.

Known useful files:

- `DBFilesClient\Map.dbc`
- `DBFilesClient\WorldSafeLocs.dbc`
- `DBFilesClient\CharStartOutfit.dbc`
- `DBFilesClient\ItemDisplayInfo.dbc`
- `DBFilesClient\CharSections.dbc`
- `DBFilesClient\CharHairGeosets.dbc`
- `DBFilesClient\CharHairTextures.dbc`
- `DBFilesClient\HelmetGeosetVisData.dbc`

`Map.dbc` and `WorldSafeLocs.dbc` are used for map metadata and spawn/safe-location lookup. Character display work uses the character and item DBCs listed above.

Classic-era DBCs can report a logical field count larger than `record_size / 4`. Do not reject the whole file for that alone; validate the envelope and check each accessed field against `record_size`.

## Tools

Inspect MPQs:

```bash
build/bin/mpqtool -mpq data/world-of-warcraft/model.MPQ ls
build/bin/mpqtool -mpq data/world-of-warcraft/dbc.MPQ cat DBFilesClient\\Map.dbc
```

Inspect or preview M2 models:

```bash
build/bin/m2tool \
  -mpq data/world-of-warcraft/model.MPQ \
  -model "Character\\Orc\\Male\\OrcMale.m2" \
  --info
```

DBC-backed player configuration preview:

```bash
build/bin/m2tool \
  -mpq data/world-of-warcraft/model.MPQ \
  -mpq data/world-of-warcraft/dbc.MPQ \
  -mpq data/world-of-warcraft/texture.MPQ \
  -model "Character\\Orc\\Male\\OrcMale.m2" \
  --wow-player-config-only
```
