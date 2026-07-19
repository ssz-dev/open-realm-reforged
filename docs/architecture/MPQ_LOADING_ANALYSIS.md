# MPQ Loading Analysis

## Scope

This analysis covers the shared filesystem and MPQ paths used by
`openwarcraft3`, with emphasis on:

- `FS_Init`
- `FS_AddDataDirectory`
- `FS_AddArchiveScanDirectory`
- `FS_AddArchive`
- `FS_OpenFile`
- `common/mpq.c`

The local code-review graph identifies `common/common.c` as part of the
`common-fs` community. Its direct startup path is:

```text
main
  -> Com_Init
       -> Cvar_Init
       -> Cvar_ApplyCommandLine
       -> FS_Init
  -> FS_AddDataDirectory(data)
       -> FS_AddArchiveScanDirectory
       -> qsort(FS_ComparePaths)
       -> FS_AddArchive
            -> SFileOpenArchive
```

`FS_OpenFile` is the shared asset lookup entry point used by
`FS_FileExists` and `FS_ReadFile`. It calls `SFileOpenFileEx` once per
mounted archive.

## Current architecture

### Discovery

`main` reads the `data` cvar after `Com_Init` and passes it to
`FS_AddDataDirectory`.

`FS_AddDataDirectory`:

1. verifies that the supplied path is a directory;
2. registers it as a loose-file game directory;
3. recursively scans it with `FS_AddArchiveScanDirectory`;
4. accepts `.mpq` files case-insensitively;
5. sorts the collected paths;
6. opens each archive with `FS_AddArchive`.

The scan is recursive, so MPQs below the installation root are also
considered. The fixed `MAX_ARCHIVES` limit is 64. Files found after the
array fills are not mounted.

For non-SC2 builds, the scan has one game-specific gate:

```text
fs_expansion == 0 and basename starts with "War3x"
  -> skip archive
```

Before the Phase 2 change, `fs_expansion` defaulted to `0`. `-tft` sets it
to `1`, and `-roc` sets it to `0`, before `main` calls
`FS_AddDataDirectory`.

`FS_Init` does not mount archives. It currently contains only disabled
diagnostic/development code. Archive mounting is owned by
`FS_AddDataDirectory`.

### Sorting and mounting

`FS_ComparePaths` sorts case-insensitively by basename, then by full path.
For the Warcraft III 1.29 installation examined here, this produces:

```text
Deprecated.mpq
War3.mpq
War3Local.mpq
War3x.mpq
War3xLocal.mpq
```

`FS_AddArchive` opens the archive in the first empty `archives[]` slot and
stores its path in the parallel `archiveNames[]` array. Re-adding the same
path is idempotent.

`SFileOpenArchive(filename, priority, flags, archive)` in `common/mpq.c`
opens and parses exactly one MPQ. The `priority` and `flags` parameters
are currently ignored. The MPQ layer has no global mount table and does
not decide precedence between archives.

### Lookup priority

The shared filesystem owns precedence. `FS_OpenFile` iterates
`archives[]` from the last slot to the first:

```text
last mounted archive -> highest lookup priority
first mounted archive -> lowest lookup priority
```

`FS_ReadFileAll` intentionally performs the inverse traversal, from first
to last, so consumers that merge data see base definitions first and
later overrides last.

For the four requested Warcraft III archives, current sorting and reverse
lookup therefore give:

```text
highest: War3xLocal.mpq
         War3x.mpq
         War3Local.mpq
lowest:  War3.mpq
```

This is the required base-to-expansion override direction: TFT data wins
over RoC data, and localized expansion data can win over non-localized
expansion data.

### MPQ file access

`common/mpq.c`:

- locates an MPQ header, including headers embedded after a map prefix;
- loads and decrypts the hash and block tables;
- normalizes `/` to `\` while hashing paths;
- looks up a file within one archive;
- reads uncompressed, zlib-compressed, and supported encrypted data;
- supports nested `.mpq`, `.w3m`, and `.w3x` archives;
- caches successful name-to-block lookups per archive.

It does not scan directories, select Warcraft editions, merge archives,
or apply mount priority.

## Confirmed root cause

The reported missing assets are not missing from the supplied Warcraft
III installation. Direct inspection confirms these paths in
`War3x.mpq`:

```text
Units\Critters\SpiderCrab\SpiderCrab.mdx
Buildings\Other\Tavern\Tavern.mdx
ReplaceableTextures\CommandButtons\BTNArcaneVault.blp
```

The bounded startup comparison confirms the mode gate:

- `openwarcraft3 -data <install>` logs that `War3x.mpq` and
  `War3xLocal.mpq` are skipped, then mounts only the RoC/local archives;
- the same command with `-tft` mounts all four requested archives.

The immediate cause is therefore not MPQ decompression, archive discovery,
or an invalid installation path. It is the intentional
`fs_expansion == 0` default combined with a launch command that does not
select TFT.

This behavior is internally consistent with the existing `run-roc` and
`-tft` modes, but it does not match the requested product behavior that a
normal `-data` launch should expose the complete installed RoC + TFT asset
set.

## Expected Warcraft III archive order

For the supplied 1.29 archive set, the filesystem should mount from
lowest to highest priority as:

```text
War3.mpq
War3Local.mpq
War3x.mpq
War3xLocal.mpq
```

Lookup must traverse that list in reverse. Optional patch archives must
sit above the expansion archives, and a loaded map archive must sit above
installation archives when map-local overrides are supported by the
consumer.

`Deprecated.mpq` is not one of the four required gameplay archives. The
current basename sort places it below them, so it cannot override the
standard archives.

The current alphabetical strategy happens to order the four required
archives correctly. It is not a complete Warcraft priority model:
`War3Patch.mpq` sorts before `War3x.mpq`, so reverse lookup would
incorrectly let expansion content override patch content. That is a
separate future requirement and should not be mixed into the minimal TFT
availability fix without a tested policy supplied by the game layer.

## Proposed and implemented solution

Make the complete installed asset set the default Warcraft III mode by
defaulting `fs_expansion` to `1`. Preserve `-roc` as the explicit
base-game-only mode and preserve `-tft` as an explicit, compatible mode.

This is the smallest change because:

- discovery already finds both TFT archives;
- mounting already opens them successfully;
- lookup already gives them higher priority than the RoC archives;
- `common/mpq.c` requires no change;
- no archive paths or installation paths are added;
- SC2 and WoW archive handling is unchanged;
- the existing mode cvar remains the single policy control.

The user-facing usage text and command-line tests must be updated with
the new default. A focused test must cover the default, while the existing
`-tft` and `-roc` tests continue to cover both explicit branches.

A future asset-provider layer should move Warcraft archive classification
out of `common/common.c`. The shared filesystem should retain a generic
ordered provider list, while a Warcraft provider supplies edition and
patch policy. CASC can then implement the same provider contract without
affecting the renderer:

```text
Filesystem
  -> ordered asset providers
       -> MPQ provider
       -> CASC provider
```

## Affected files

Minimal Phase 2:

- `common/cvar.c` — change the default expansion mode.
- `common/main.c` — describe the default and the explicit `-roc` mode.
- `games/warcraft-3/tests/test_commands.c` — cover default, `-tft`, and
  `-roc` behavior.

Analysis/documentation:

- `docs/architecture/MPQ_LOADING_ANALYSIS.md`

No change is required in:

- `common/mpq.c`
- renderer code
- SC2 or WoW game code

## Risks

- Existing users who relied on an implicit RoC-only launch will now need
  `-roc`. This is an intentional behavior change and must be documented.
- TFT overrides can change shared UI, strings, scripts, unit data, and
  models, not only fill missing paths. Both default/TFT and explicit RoC
  modes require regression testing.
- The recursive scanner tries every `.mpq`, including movie containers
  that the MPQ reader cannot open. This produces diagnostics but is not
  the reported TFT failure.
- Alphabetical priority is adequate for the four requested archives but
  remains insufficient for patch archives or future providers.
- The fixed 64-archive capacity may become limiting for SC2/WoW or a
  future mod stack; changing it is outside this fix.
