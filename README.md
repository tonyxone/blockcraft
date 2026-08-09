## Build

Requires Visual Studio Build Tools (2017+) with the C++ workload.

```
build.bat
```

Produces `blockcraft.exe` in this folder. Run it directly — no assets needed;
textures are generated procedurally at startup.

## World

- **Every new game generates a random world.** The seed is stored in the
  save file, so loading a save reproduces its exact terrain.
- The map is finite: 512x512 blocks with a Minecraft-style world border —
  four translucent aqua faces (with grid lines) that block movement; no
  terrain generates beyond them.
- Layout is zoned by distance from the center (Chebyshev, matching the
  square map):
  - **The world is an ocean map: ~60% water.** A low-frequency continent
    mask (plus a finer octave for ragged coasts) breaks the land into a
    main continent, several separate landmasses and a scattering of
    islands — typically 15-20 distinct landmasses per world, with ~10 of
    island size or bigger. New games spawn on substantial dry land, never
    on a sandbar or the seabed.
  - Of the land, most is grassland; deserts are a minority feature.
  - **Trees vary in size**, from 2-block saplings to 15-block giants. The
    canopy scales with the trunk, so a giant carries a broad, deep crown
    and a sapling only a small tuft — both shaped by the same radial rule,
    so every crown is rounded rather than a box. Small trees dominate (~80% are 2-5
    blocks) and grow in **thickets** — a grove field marks patches where
    saplings gather — while big trees are rare and stand alone in open
    country. Trees sit on a jittered grid rather than a per-column roll, so
    trunks are guaranteed a minimum separation: grove crowns stand a clear
    block apart instead of merging, and giants are spaced much wider.
  - **Snow** covers a wide band (~25%) around the border. Its lakes and
    ocean are ordinary water (no ice sheet); snow-capped icebergs float
    in the open water. Snow also appears inland on any
    peak tall enough: land above y26 turns bare rock and above y32 gets a
    snow cap, so a big mountain reads grass -> rock -> snow at any distance.
  - **Coal** runs in black seams through the stone underground. It starts 5
    blocks below the surface, so a hillside never shows a black face and you
    have to dig for it, and it fills roughly a fifth of the stone once you're
    down there. It replaces stone only (canyon strata stay red rock), and you
    never start with any — mine it and it goes into a free hotbar slot.
  - **Every world has one guaranteed landmark mountain** — a very tall
    peak placed by the seed, with a grassland skirt, rocky flanks and a
    sharp snow summit that comes to a single block at the world ceiling
    (y42), dropping about one block per block of slope.
  - **Canyons** are cut *below* the surrounding land: broad winding gorges
    (up to ~25 blocks deep) whose walls are **terraced**, stepping outward
    as they rise, so from the floor the gorge reads as an inverted stepped
    pyramid. Walls are layered red rock with pale sand strata, and the
    floor dips below sea level so a river pools along the bottom. Water
    follows the normal sea-level rule everywhere — there is no canyon
    special case, so lakes and coastline stay continuous where they meet
    a gorge.
  - **Mountains get taller the farther out they are**, which tiers them by
    type: rock mountains (inner, bare stone tops, lowest) < grass mountains
    (middle) < snow mountains (outer ring, tallest). Enforced by
    distance-scaled mountain amplitude plus per-zone height caps, and
    verified across multiple seeds in the selftest.

## Play

- WASD move, mouse look, Space jump
- In first person your own arm swings into view whenever you mine or place:
  the **right hand collects** (entering from the lower right) and the
  **left hand builds** (from the lower left), then drops back out. It is
  drawn in its own pass with the depth buffer cleared, so it never clips
  into a wall you are standing against. Third person shows the matching arm
  swinging on the character.
- V toggles first/third person (remembered in `settings.txt`, so the game
  reopens in whichever view you last used). Third person shows the player character
  from behind (classic blocky proportions — 8x8x8 head, 8x12x4 torso,
  4x12x4 limbs — with a procedurally painted skin, no image assets); the
  chase camera pulls in automatically so it never clips into terrain.
  Limbs animate like a real gait: legs alternate and each arm swings
  opposite the leg on its side, amplitude scales with movement speed, the
  stride freezes into a split-leg pose mid-jump, and collecting arcs the
  right arm forward while building arcs the left.
- Left-click mine, right-click place, 1-9 and 0 select hotbar slot (10 slots).
  **Mining reach is one block**: you have to walk up to a block to mine it
  rather than picking it off from across the clearing. It can't drop much
  lower — the block under your own feet is already 1.62 away from your eye.
  **Building keeps the original long reach**, deliberately: bridging gaps and
  pillaring up mean placing blocks where you can't stand next to them.
  collecting and building have distinct sound effects (procedurally
  synthesized at startup, like the textures — no audio assets)
- **Inventory (I key)**: a Minecraft-style survival inventory with three
  tabs — **Inventory** (a labelled backpack grid above the separate hotbar
  row), **Player** (armor/clothes column and offhand slot around a live
  front-facing character preview — the skin has painted eyes, nose and
  mouth) and **Craft** (a 3x3 crafting area with a result slot and a confirm
  button). Crafting matches on **totals, not layout**: put the right
  quantities anywhere in the nine cells, stacked however you like, and the
  result slot shows what they make. (The handful of recipes that differ only
  in shape — wood stairs / door / trapdoor, and the pickaxe / axe of each
  tier — still need their layout to tell them apart.) Quantities must be
  exact, so tipping a whole stack in makes nothing rather than silently
  eating it. Block-like crafted goods (planks, slabs, stairs, bricks,
  sandstone, snow, packed ice, table, chest, furnace, fence, door, trapdoor,
  ladder) can be **placed and mined back** like any other block, each with
  its own world texture — a ladder shows its rails and rungs, a furnace its
  mouth, a crafting table its grid. Tools and sticks are not building
  material and are refused. The **ladder** is a real 3D object rather than a
  cube: a thin panel that hangs on whichever wall it is placed against
  (worked out from its neighbours, since a cell stores only a block id), that
  you can walk through and still aim at to mine back. Slabs and stairs are
  still full cubes — they would each need their own geometry through the same
  mechanism. **Double-click** a stack while the Craft tab is open to send one of
  it straight to the grid instead of dragging. The **book button** on that
  tab opens a recipe list — every recipe as a line of icons, e.g. wood +
  2 stone = axe — dismissed by any click or ESC. Crafted goods have no cube in the world, so they draw a flat item
  sprite in their slot instead of a block face.
  Hovering any slot names what is in it, including the crafting result.
  The
  backpack and hotbar sections appear on every tab. Items move by drag &
  drop: left-drag moves the whole stack, right-drag a stack onto a
  destination slot and a small dialog asks how many to move (amount textbox,
  Ok and All buttons; Enter confirms, Esc cancels); dropping outside a slot
  returns the items. Mined blocks overflow from the hotbar into the backpack
  automatically. Closing (I or ESC) stows whatever rides the cursor.
  Inventory contents are saved with the game.
- **Equipping tools**: the **Player** tab has a **Hand** slot (above **Off**)
  that only accepts tools — drag a crafted pickaxe there to grip it in the
  right hand. It's drawn gripped at a diagonal in third person and, since the
  right hand is also the mining hand, waves through a mining swing and flips
  upside down at the peak of a left-click. In first person a carried tool
  stays in the corner of the screen the whole time (a bare hand still only
  appears for the length of a swing), settling into a lower, further-away
  rest pose between swings so it frames the view instead of filling it. The tool-visuals table
  (`src/tools.*`) is generic: a new equippable tool is another row mapping a
  `CraftItem` to the block textures used for its head/handle plus a head
  shape. Two exist so far — a **pickaxe** (1 wood + 1 stone), whose head is a
  symmetric arc with a prong drooping fore and aft, and an **axe** (1 wood +
  2 stone), whose head is a single asymmetric blade flaring on the forward
  side. Both are shapeless recipes, so only the ingredients matter, not where
  you put them.
- **Minimap (top-right)**: a north-up map of the 96 blocks around you, one
  pixel per block, following Minecraft's map conventions. Each pixel takes
  the colour of the topmost solid block — averaged from the game's own
  procedural textures, so the map matches what you see — and relief comes
  from comparing each column against its northern neighbour, so slopes and
  cliffs stand out. Water is tinted by depth over the bed beneath it.
  N/E/S/W markers sit on the edges (north tinted red) and a white-and-red
  arrow at the centre turns with your view.
- **Clouds**: a handful of small translucent white puffs drift slowly
  westward, high above the tallest peak — the sky stays mostly open blue
  (~10% covered). Cloud cells are 12 blocks wide and 4 deep, matching
  Minecraft's "fancy" clouds. The pattern is generated once and wraps, so
  it tiles seamlessly as it drifts.
- **Water hides its depths**: the surface gets more opaque the deeper the
  water beneath it, and darker with it. A shallow pool shows its bed; past
  5 blocks deep the bottom is completely hidden, so open ocean reads as
  solid dark blue rather than a see-through pane over the seabed.
- **Underwater view**: swimming below the surface tints the whole screen
  blue and collapses visibility to a few blocks. The tint follows
  Beer-Lambert absorption — it darkens, thickens and closes in the deeper
  you go, since less surface light reaches you. The water surface is also
  visible from below (its faces are drawn double-sided).
- Water flows into space you open up: mine a block next to water (or dig a
  tunnel in from a shoreline) and the water runs in, flooding the cut into
  a river or canal. It spreads sideways and falls, but never climbs above
  its source level, and it washes grass tufts away as it goes.
- Grass tufts are replaceable: building on ground that carries one
  overwrites the tuft instead of refusing the placement.
- Water itself is not a carryable resource — it only exists in the world.
  Ice (slot 7) is an ordinary collectable block: mine it from icebergs and
  place it like any other.
- ESC pauses (Resume / Restart / Save / Load / Settings / Quit)
- Settings: mouse sensitivity, render distance, and window resolution
  (1280x720 / 2560x1440 / 3440x1440 — click the value to cycle; applied
  immediately and used as the startup size), persisted to `settings.txt`.
  If a framed window at that size fits in the monitor's work area it opens
  as a normal centered window; otherwise it goes borderless at exactly the
  chosen size, centered and kept above the taskbar while the game has focus
  (alt-tab pauses and lets the taskbar back on top).
- Save/Load: named save slots in `saves\<name>.txt`. Save opens a panel where
  you type a name (Enter or click Save); saving to an existing name asks
  before overwriting. Load lists all saves newest-first with timestamps —
  click one to load, or click a row's X to delete it (with confirmation).
  Each save stores the block-edit diff, player position/orientation, hotbar
  state and inventory contents — terrain regenerates from the fixed seed. A
  legacy single-slot `save.txt` is migrated to `saves\save.txt` automatically.

## Icon

The exe embeds a game icon (grass-over-dirt pixel block with BLOCKCRAFT
lettering at 256px; a clean text-free block face at 48/32/16px). It's drawn
from scratch in the game's own palette — regenerate `icon\blockcraft.ico`
with `tools\gen_icon.ps1`; `build.bat` compiles it in via rc.exe when
available (and builds fine without it).

## Drawing your own item art

Item icons are generated procedurally like everything else, but a tile can be
replaced with a hand-drawn 16x16 PNG (Aseprite or similar):

```
art\axe.png            draw it, transparent background
tools\gen_sprite.ps1   converts every art\*.png into src\sprites_generated.cpp
build.bat              compiles the pixels into the exe
```

The art is compiled in rather than loaded at runtime, so the game still runs
from the exe alone with no assets to ship. Delete the PNG and re-run the
converter to go back to the procedural drawing; hand-drawn and generated
tiles coexist, so replacing one item leaves the rest untouched. Replaceable
tiles are listed in `spriteNameForTile()` (`src\textures.cpp`) — currently the
axe and pickaxe. See `art\README.md`.

This replaces the **slot icon only**. The 3D tool you hold is separate,
purpose-built geometry in `src\tools.cpp` (`drawHead`) — extruding the sprite
into a slab was tried and looked worse in hand than boxes do, so the two are
hand-matched rather than derived from one another. Changing the held shape
means editing `drawHead`.

## Extras

- `blockcraft.exe --selftest` writes `selftest_result.txt` with logic checks
  (worldgen determinism, edit replay, raycast, collision, save/settings I/O).
- `blockcraft.exe --screenshot` renders the menu, a fresh world, and the
  save/load panels to `screenshot*.bmp` and exits (used for automated visual
  verification).
- `blockcraft.exe --watertest` drives the water tool through the real click
  handlers (pour, drain, both refusal paths) and writes
  `watertest_result.txt`.
- `blockcraft.exe --mapdump[=SEED]` writes `mapdump.txt`: an ASCII map of
  the whole world, per-biome share/height stats, canyon depth measurements
  and a vertical cross-section through the deepest gorge. Use this to check
  world layout with data instead of eyeballing screenshots.

## Source layout (mirrors the JS modules)

| C++ | JS original |
| --- | --- |
| `src/blocks.*` | `src/world/blocks.js` |
| `src/textures.*` | `src/world/textures.js` (canvas atlas -> CPU RGBA atlas) |
| `src/noise.*` | `simplex-noise` npm package + mulberry32 |
| `src/constants.h`, `src/chunk.h` | `src/world/constants.js`, `chunk.js` |
| `src/worldgen.*` | `src/world/worldgen.js` |
| `src/world.*` | `src/world/world.js` |
| `src/mesher.*` | `src/world/mesher.js` + `materials.js` (lighting baked into vertex colors) |
| `src/physics.*`, `src/player.*`, `src/raycast.*` | `src/player/*.js` |
| `src/hotbar.*` | `src/ui/hotbar.js` + slot CSS |
| `src/menu.*` | `src/ui/menu.js` + `index.html` panels |
| `src/save.*`, `src/settings.*` | `src/game/save.js`, `settings.js` (files instead of localStorage) |
| `src/gfx.*` | HUD/menu CSS (GDI bitmap fonts, 2D helpers) |
| `src/sound.*` | (new) procedural mine/place sound effects via PlaySound |
| `src/playermodel.*` | (new) blocky player character + procedural skin for third-person view |
| `src/sky.*` | (new) drifting blocky cloud layer |
| `src/recipes.*` | (new) crafting recipe list + 3x3 grid matcher |
| `src/tools.*` | (new) generic gripped-tool visuals + swing animation |
| `src/minimap.*` | (new) north-up HUD minimap with compass + player arrow |
| `src/main.cpp` | `src/main.js` + `src/player/input.js` |
