# Assets

Three layers. Only the code that makes them is in git; everything taken from the disc or the XBE stays out.

1. **The original**, read-only: the extracted disc (`../phantom-crash-archive/assets/raw` by default)
   and the XBE (`../phantom-crash-decomp/orig/default.xbe`).
2. **`data/`**, the editable source, written once by the extractor (`tools/extract/`). Standard formats a person
   can open and replace: PNG textures, glTF 2.0 models (and animations where glTF fits), WAV audio, MPEG-1 movies
   as they are, `.sexp` for tables, scripts, text (UTF-8) and anything without a standard format.
   `data/manifest.sexp` maps every original asset id (cabinet, index, registry slot, XBE table address) to its
   file. A redrawn or upscaled replacement is just a replaced file here.
3. **`cooked/`**, what the game loads: GPU-ready packs built from `data/` by the cook (`src/cook`, linked into the
   game and built alone as `pc_cook`), rebuilt only when a source file changed: BC-compressed textures, flat
   vertex/index blobs, binary motion, collision and tables. The game maps these straight into memory and loads per
   stage, per fighter part and per screen, the way the original's resource map does (decomp
   `docs/port/architecture.md`, "Memory"), never everything at start. Format and grouping below, under "Packs".

## Layout of `data/`

```
data/
  manifest.sexp
  tables/        parts.sexp mechs.sexp weapons.sexp tournaments.sexp shops.sexp music.sexp stages.sexp ...
                 stage_lighting.sexp (fog, sun, tint, gamma, shadow colour, dof, heat haze, lamps per stage)
  text/          event text and UI strings as UTF-8 .sexp, the font as PNG + glyph map
  scripts/       event scripts (EVENTSEG) as .sexp
  textures/      <id>_<cabinet or file>/<nn>.png   one folder per texture set, pictures in set order
                 systemdata/<name>.png   the loose pictures the renderer loads by name (bump_light, the water's)
  models/        stages/ mechs/ misc/ <id>_<cabinet or file>.gltf + .bin   one glTF per model set
  motion/        <id>_<cabinet>.sexp   curve sets, curve modes kept (glTF can't hold them)
  collision/     <id>_<name>.sexp   stage grids: triangle lists and cells
  nav/           <id>_<name>.sexp   the AI's ENPT route graphs: nodes, kinds, links, Bezier points (--only nav)
  ui/            layouts .sexp and their images .png
  audio/         music/*.wav  se/<bank>/*.wav  adx/<movie>.wav, with music.sexp se/<bank>.sexp
                 movies.sexp reverb.sexp (docs/audio-assets.md)
  movies/        *.sfd (MPEG-1, as on disc)
  shaders/       the original programs' disassembly, for reference only
```

## The extractor

`tools/extract/` is a C++ executable (`pc_extract`), one source pair per asset family (`textures`, `models`,
`tables`, `audio`, `text`, ...), sharing the readers in `tools/extract/disc.*` and `xbe.*` (cabinets, registry,
XBE sections and virtual addresses). `pc_extract [--only family]` runs them. Each family writes only its own
folder and its own manifest entries (`data/manifest/<family>.sexp`, merged into `data/manifest.sexp`), so families
can be built in parallel. Names in the output follow the decomp's names (`../phantom-crash-decomp/data/symbols.txt`,
`include/*.h`), so a file can be traced back to the code that reads it.

`--disc` takes the original in any of three forms (`tools/extract/disc_image.*`): an extracted folder (the one
holding `Media/`, with `default.xbe` beside it or not), a plain Xbox disc image (XISO: the XDVDFS volume,
"MICROSOFT*XBOX*MEDIA" at sector 32), or a full Redump-style image with the video partition ahead of the game's
(the game partition at 0x18300000 for XGD1 discs like this one; XGD2's and XGD3's offsets are tried too). Images
are read in place, case-insensitively as the Xbox does; nothing is decrypted, and no keys are needed. The XBE is
`--xbe` if given, else the disc's `default.xbe`, else the build's default (the archive's folder has none). Before
anything is written the XBE's certificate must say Phantom Crash (title id 0x504C0001) and the XBE must be the
North American retail one the port's addresses come from (its size and an FNV-1a fingerprint); otherwise it stops
with a plain reason: not an Xbox disc, another game, another release, an archive to unpack first, or an image
that is damaged or cut short. A folder, the US XISO and a Redump-style image made from the same files extract to
byte-identical `data/` (6573 files, 2026-11-12). The families also run as a library (`pc_extract_lib`,
`extract_run.hpp`) for the game's first-run screen, with a progress count and a stop.

## The port's frame

The original is D3D8: left-handed, y up, row vectors. Everything in `data/` is in the port's frame: the original's
with z negated, which is right-handed and y up as glTF wants. Positions, normals, translations and bounds have z
negated; rotations (always applied x, then y, then z in the original) become (-x, -y, z) in the same order; a
heading about y is negated. A camera built from the mirrored eye, target and up with right = forward x up draws
exactly the original's picture (Japanese signs read the right way round in the first captures), and the stored
triangle order needs no change.

Readers in the original also flip components themselves, each its own way (decomp `docs/port/data.md`, "The
disc"). The extractor bakes each reader's flips in, so a port reader applies the values as they are:

- SV articulation graphs (`tables/sv_graphs.sexp`): the static path applies (tx, ty, -tz) and (-rx, -ry, rz);
  mirrored, that is the stored words, so they are written unchanged.
- Motion curves (`motion/`): rotation read as (-x, y, -z), translation as (x, -y, z); written as (x, -y, -z) and
  (x, -y, -z) of the stored curves (every float of a curve, tangents included, is negated together).
- Angles the original's code adds to those (aim yaw, camera pitch, wheel spin, the body class 1 tilt) are negated
  where the mirror negates them (x and y rotations), in `src/mech/pose_walk.cpp` (the world's, fixed point) and
  `src/mech/mech_pose.cpp` (the floats). A `matrix_from_dir_up` (the arms' and backs' aim) is the port's
  `mat3_from_dir_up` of the negated direction (`src/mech/pose_math.hpp`).
- Collision grids keep their cell arrays in the original's indexing: query cell (x, y, -z).

## Model sets as glTF

One glTF per model set (`tools/extract/gltf_write.cpp`). Each object (one model slot of the original) is a root
node named `object<i> slot<s>` with its node tree under it; a node with sections carries a mesh with one primitive
per section. Extras keep what glTF can't say: on the root the slot, flags and `centre` (object header +0x1C, the
point stage LODs measure from); on nodes the exact Euler angles (`euler`, port frame, applied x, y, z), flags, first
section and draw-group counts; on materials the section's flags, `layer[4]`, `tex[4]`, ambient (ARGB) and specular
power. Base colour textures point at `../../textures/<set>/<nn>.png` by the section's `tex[0]`. Fighter parts have
no texture set of their own: the game paints them per build (`part_color_assets`), and the glTF shows a preview
paint from the same maker's cabinet (`(preview-paint id)` in the manifest).

Textures keep only their top level: the cook makes the mips. The disc's own mip chains (`disc-levels` in the
manifest) are dropped.

## Packs

`cooked/` holds one `<name>.pack` per group. The cook (`src/cook`) reads `data/` with the only readers of it in the
game (`src/cook/read_*.cpp`: glTF through cgltf in the extractor's layout above, PNG through SDL3_image, `.sexp`
through gsexp) and nothing else reads `data/` for these assets. The game's loaders take a pack: `load_model_set`,
`load_motion_set`, `load_collision_grid`, `load_stage_tables`, `load_fighter_tables`, `load_manifest`
(`src/assets/pack_assets.cpp`), `pack_sexp` for the files kept as text, and `texture_for` for textures.

### What goes where

The grouping follows the original's loads (`src/cook/cook_plan.hpp` lists it):

| pack | holds | the original's load |
|---|---|---|
| `core` | the manifest (with each asset's pack), the stage and fighter tables, the plan, every `tables/*.sexp`, `ui/images.sexp`, the UI timelines, the font, the standard images, boot models 0xD5 0xD6, `pc_art.sexp` (PC CONFIG's letters, cut by the cook from the label and button PNGs: `src/cook/cook_pc_art.cpp`) | game_init; held from start |
| `battle` | models 0xC2 0xD8 0xB8 0xB9 and texture sets 0x615 0x635 | battle_task_begin |
| `stage_0`..`stage_7` | the selector's model set with its texture set, its collision grid, its AI route graph (`nav/`, ai_nav_resource_selectors) | stage_assets_load, ai_nav_resource_select |
| `body_C` `leg_C` `arm_a_C` `arm_b_C` `back_a_C` `back_b_C` | a part class's model; body: sticker model and body motion; leg: leg motion and the arm motions for that leg class | fighter_assets_load |
| `paint_L` | every paint of leg class L (part_color_assets); only the colour used is touched | resource_load_type3 |
| `screen_X` | the screen's layout tables (`ui/layouts/X.sexp`) and the sprites `ui/images.sexp` says it draws | per screen |
| `ui_S` | sprites of set S no screen claims | |
| `text` | event text and scripts | |
| `cab_X` | every other model, motion and texture set, by the disc cabinet it came from | |

A model set always brings its texture set and every picture its sections name; fighter parts bring none (they are
painted per build). An asset's first pack is the one the manifest names.

### Format

All little-endian, `src/cook/pack_format.hpp`:

```
PackHeader   magic "PCPK", version, entry_count, source_count, entries_at, sources_at, strings_at,
             strings_size, file_size, plan_hash                                          64 bytes
PackEntry[]  kind, id (asset or -1), name (string offset), format, width, height, levels, at, size   48 bytes
PackSource[] path (string offset, relative to data/), size, write time, 64-bit hash        32 bytes
strings      zero-terminated, offset 0 is ""
blobs        each 16-byte aligned
```

Entry kinds: `texture` (name: the PNG's path under `data/`, the key a model section's `picture` holds), `model_set`,
`motion_set`, `collision` (by asset id), `sexp` (name: the file's path, its text as it is), and in core `manifest`,
`stage_tables`, `fighter_tables`, `plan`.

A texture blob is BC blocks for every mip level, top level first, each level whole 4x4 blocks; the renderer copies
it into the image as it is (`renderer_upload_blocks`). Format by content: opaque is BC1; alpha in a picture the
original kept as DXT1 or DXT5 at the same size is BC3 (what it shipped); alpha from an uncompressed original
(A8R8G8B8, P8), a 2D sprite or a resized replacement is BC7. Mips are a 2x2 box down to 1x1 (what the renderer made
at load before), none for 2D sprites. Encoders: rgbcx and bc7enc from bc7enc_rdo.

Every other blob is a struct written field by field by the `io(archive, value)` function beside it (`model_sets.hpp`,
`motion_sets.hpp`, `stage_tables.hpp`, `fighter_tables.hpp`, `manifest.hpp`, `collision_grid.hpp`, `cook_plan.hpp`):
a number or a plain struct as its bytes, a string or a vector as a u32 count and then its items, a vector of plain
structs (vertices, indices, node records, collision triangles) in one copy. The cook writes and the game reads
through the same function. A model set is per object: slot, centre, the 36-byte vertices, the u16 indices, the
section records (flags, layers, tex, ambient, power, triangle range, picture) and the node records (parent,
translate, euler, scale, sections, flags). Motion keeps each curve's keys and mode: the poses evaluate curves at any
time the original does (`motion_curve_eval`), so they aren't resampled.

### When it cooks

A pack is fresh when its version matches, its `plan_hash` matches the plan's entry for it (the whole plan for
core), and every source has the same size and write time, or failing that the same hash (then the new time is
written back so it isn't hashed again). Bump `pack_version` with any change to a layout or to what the cook makes.

- `pc_cook [--data DIR] [--out DIR] [--threads N] [--force] [pack...]` makes the plan from `data/` and cooks every
  stale pack (or the named ones), on up to 16 threads. Output is byte-identical between runs.
- The game (`packs_start`) maps `core.pack`; when it's missing or one of its sources changed (the first run, or
  `data/`'s tables changed) it runs the whole cook first. `pack_open` checks the one pack it opens and re-cooks it
  alone, from the plan in core, when stale. Loaders close a pack once its contents are on the GPU.

Measured on the desktop (32 threads, NVMe), `data/` as extracted: the plan has 414 packs, 5565 textures,
569 MB. A full cook takes 4.5 s on 16 threads (7.9 s on 8); with nothing changed `pc_cook` takes 0.03 s; one stage's
textures changed re-cooks that stage pack alone in 0.2 s. Shinjuku and build 0 load in 10-13 ms warm, 16-22 ms
with the packs dropped from the page cache, against 100-125 ms reading `data/` before (both on lavapipe, where
most of what's left is the renderer's one-submit-per-upload). `--cooked DIR` points the game elsewhere.
