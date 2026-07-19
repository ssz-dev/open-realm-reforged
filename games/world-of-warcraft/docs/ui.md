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
