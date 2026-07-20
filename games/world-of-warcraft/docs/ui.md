# OpenWoW In-Game UI

The game module owns the in-game HUD through `svc_layout`. The UI module only adds the OpenWoW-native GM overlay;
both use the normalized 1024x768 WoW canvas.

## Fixed HUD Regions

- Zone header: `(879, 8, 128, 22)`.
- Minimap viewport: `(879, 30, 128, 128)`.
- Quest-log button: `(879, 170, 40, 40)`, directly below the minimap.
- GM toggle and panel end before x=866, leaving a gap before the minimap at x=879.
- Quest log: classic 384x512 frame at `(0, 104)`.

The HUD deliberately does not draw `Interface\Minimap\UI-Minimap-Border.blp`: its round gold frame obscures too much
of the square map and includes an unused title strip. The replacement header displays the server-authored area name.
The server reads the active ADT chunk's MCNK area ID and resolves its English name through `AreaTable.dbc`; it refreshes
the HUD only when that name changes.

## Player Profile And Progression

The existing upper-left targeting-frame art contains three live status bars:

- Health is green and displays current/max health.
- Rage is red, starts at zero, and is generated when the player deals or receives damage.
- XP is purple and displays progress toward the next level.

All values are owned by `wowEntityLocal_t`, copied into `playerState_t.stats`, and written into the server-authored HUD.
The HUD layout is rebuilt only when a displayed value changes. Players start at level 1 with 100 health; each level
adds 10 maximum health. The current minimal progression curve requires `400 + (level - 1) * 500` XP and preserves
overflow XP. This scaffold is intentionally server-side so an authoritative progression table can replace the curve.

Ambient creatures currently start at level 1 with 3 health and award 100 XP. Their normalized health is published in
`entityState_t.stats[ENT_HEALTH]`, so the existing renderer shows a live overhead bar for the selected or hovered NPC
and for all living NPCs while Alt is held. Dead NPCs publish zero and therefore no longer draw a bar.

## Quest Log

The console layer sends `questlog toggle` from the book button. The server accepts:

```text
questlog open
questlog close
questlog toggle
```

The dialog is sent on `LAYER_QUESTDIALOG`, and closing it replaces that layer with an empty layout. Until quest
gameplay supplies server-side quest records, the implemented frame deliberately renders the classic empty state
instead of synthetic quest progress.
