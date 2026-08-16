# Hand-drawn item art

Drop 16x16 PNGs here to replace a tile's procedurally generated texture with
your own drawing (Aseprite, or anything that exports PNG).

```
powershell -File tools\gen_sprite.ps1
build.bat
```

The converter turns every PNG in this folder into a byte array in
`src\sprites_generated.cpp`, which is compiled into the exe — so the game
still ships as a single file with no assets to load at runtime, and nothing
here needs to be distributed with it.

## Rules

- **32x32 or 16x16, square.** 32 is the native storage size and keeps all
  your detail; 16 is doubled up losslessly. Anything else is rejected rather
  than resized, because downscaling pixel art either fragments the outline
  (nearest) or smears it (averaging).
- **Leave the background transparent.** Alpha is preserved, so the inventory
  slot shows through around the item.
- **This changes the slot icon AND, for equippable tools, the held 3D
  model** — a tool with art here is held as the drawing itself, voxel-
  extruded (`spriteToolVoxelList` in `src\tools.cpp`), not the generic
  per-shape box geometry.
- **Draw handheld art VERTICALLY, grip at the bottom-center.** The bottom
  row of the tile is what the hand grips, and the shared tilt/yaw assumes a
  straight vertical shaft (see `art\sword.png`). A diagonal icon-style
  drawing leaves the hand grabbing empty air and the tool tilted wrong.
- **The filename picks the tile.** `art\axe.png` replaces the tile named
  `"axe"`. The name-to-tile mapping is `spriteNameForTile()` in
  `src\textures.cpp`; a PNG whose name matches no tile is ignored.

Currently replaceable: `sword`, `power_axe`, `spear`. Adding another is one
line in `spriteNameForTile()`.

Delete a PNG and re-run the converter to go back to the procedural drawing —
the two coexist per tile, so replacing one item leaves the rest alone.
