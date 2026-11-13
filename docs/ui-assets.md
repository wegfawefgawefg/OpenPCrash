# UI assets

What `pc_extract --only ui` writes to `data/ui/`: the original's screen and HUD layout tables as `.sexp`, every 2D
sprite as a PNG with an index, and the sprite player's two timelines. The screens in `screens/` are ported from these
(AGENTS.md); the image index is what a later vector redraw works from. The decomp's `docs/port/screens.md` explains
how each screen uses its tables; this file only says what the files are.

```
data/ui/
  layouts/<screen>.sexp     583 tables in 40 files, one per screen
  images/<set>/<id>.png     3763 render registry sprites, by texture set
  images/portraits/<set>/   the 28 portrait sets
  images/standard/          the 8 Media/systemdata images
  images.sexp               index of all of them
  timelines/level_meter_a.sexp, level_meter_b.sexp
```

Nothing here is committed (data/ is ignored). The code is `tools/extract/ui*.cpp`.

## Where the table list comes from

`tools/ui_tables.py` reads the decomp (`src/data/ui_*.c`, `misc_e.c`, `portraits.c`, `pepper_talk.c`,
`save_dialogs.c`, `jp_tutorial.c`, the UI-typed tables of the other `misc_*.c` files and of `sv_attach.c` (the model
stand's places, cameras and part places, in `model_viewer.sexp`), `include/tables.h`, and every function under
`src/`) and writes `tools/extract/ui_tables.inc`: record layouts with field names, each table's name,
address, layout, count and screen, the symbols a pointer word may name, literal sprite ids in screen code and literal
`texture_set_load` calls. It holds names and addresses only; `pc_extract` reads the values from the XBE. Rerun it when
the decomp renames or retypes tables, and commit the `.inc`:

    python3 tools/ui_tables.py --decomp ../phantom-crash-decomp

A table's screen is the screen of the functions that read it, by function name prefix (the `SCREENS` list in the
script; the screen with the most readers owns the table, the others are listed). The header says how it was found:

- `readers`: code reads it by name (421 tables).
- `pointed-at`: another table points at it (29).
- `neighbour`: no reader names it (code indexes past a neighbour, or it's dead); it takes the screen of the nearest
  read table in the same source file (128). Treat these as likely, not certain.
- `file`: nothing to go by; the screen is the decomp file name (1, `jp_tutorial`, which nothing reads).

Tables read only by non-UI code (shaders, lights, effects, stage data in `misc_e.c` and `ui_d.c`) are left out.

## Layout files

One `(table ...)` form per table, in address order:

```
(table options_sprites (va 0x1fa2c0) (layout SpriteSlot) (count 2) (decomp-file ui_a) (screen-by readers)
  (readers options_screen_draw)
  (row 0 (sprite 2106) (x 0) (y 0) (blend 1) (color 0xffffffff) (z 1) (flags 0x0))
  (row 1 (sprite -1) (x 0) (y 0) (blend 0) (color 0x0) (z 0) (flags 0x0)))
(table D_001FA32C (va 0x1fa32c) (layout float) (count 11) (decomp-file ui_a) (screen-by readers)
  (readers options_screen_draw)
  (values 121 225 164 225 207 225 250 225 293 225 336))
```

- Names are the decomp's (`symbols.txt`, or `D_<address>`). Rows are keyed by index; a 2D array
  (`hud_weapon_layouts[5][22]`) by both indices, `(row 2 7 ...)`. Run ends (`sprite -1` rows) are kept because code
  draws sub-runs by offset.
- Plain arrays print as `(values ...)`. `string` tables are arrays of Shift-JIS pointers, printed as the text;
  `pointer` tables and pointer fields print the symbol they point at (`D_001DADC0+0x30` for a pointer into a table),
  or the address in hex when no symbol is known (the dealer's talk scripts, for one).
- Field kinds: ints and floats as numbers, colours and flags in hex, sprite fields are render registry ids (look them
  up in `images.sexp`). `number_fonts` is split into `(sprite, advance)` as `font_record` reads it.
- Positions are 640x480 screen pixels. `blend` is the layer (0..8 under the 3D, 0x101.. over it), `z` the alpha
  multiplier, `flags` the Sprite flags: 0x2000, 0x4000 and 0x20 are blinks (alpha pulses over 80 ticks from the last
  input), not mirrors; 8 means the position is the centre.
- `Words_*` layouts are stretches the decomp hasn't typed yet: fields `f_00`, `f_04`... with only the word kind known.
- Text is UTF-8 through the font's own order tables (`font_sjis_order`, `font_unicode_order`), cp932 for the kanji
  past them and 0xFA56 as U+FF07, the same as the tables family. Full-width Latin stays full-width, which is what the
  game draws.

Per screen: adjust 9, area_select 16, assemble 1, attract 3, battle 4, club_wired 7, common 2, dealer 27, font 15,
garage 28, hangar 8, hud 23, job 60, jp_tutorial 1, keep_sv 6, key_config 10, level_meter 1, media_playlist 35,
name_entry 11, options 8, pause 12, pepper_talk 12, plus_tech 44, portraits 3, record 58, replay 4, result 9, save 14,
schedule 7, sonic_amp 21, sound_config 8, spec_scoobee 4, stock_module 7, system 7, talk 1, title 12, town_map 9,
vs_mode 19, wild_machines 53. By layout: 105 SpriteSlot, 21 MsgBox, 5 HudRow, 1 HudElemRow, 38 untyped Words, the rest
plain arrays and single records (portraits, help menu items, dealer rows, scale/alpha keys, a texture list).

## Images

Every 2D sprite is a render registry row (3763 of them, `render_registry` 0x276598): a rectangle on a texture slot.
The extractor resolves the slot to the texture set whose descriptor `{slot, count}` covers it (below the portrait
slots there is exactly one per slot, 87 sets in all), cuts the rectangle and writes `images/<set>/<id>.png`, the set
named after its file without `_tex` (`sonic`, `garage`, `scr_common`), or its cabinet (`zzz_music`, the jackets).
Every row resolves.

Portraits share slots 534/535. Each actor's expressions (`portrait_actor_table`) name rows of
`portrait_sprite_table`, whose base, phase and overlay sprites are registry rows; each actor owns a contiguous id range
of its own (36 ids for the 16 two-sheet `mht*` actors, 12 for the 12 `sht*` ones, 34..753 in all, no overlaps), so
each id is cut from its actor's set into `images/portraits/<set>/`.

`images.sexp` has one form per sprite, then one per set, then the standard images:

```
(image (id 2440) (slot 491) (rect 920 192 1000 272) (sheet 1024 1024)
  (file "sonic/2440.png") (size 80 80) (set "sonic") (asset 1668) (texture 2) (art flat)
  (screens sonic_amp) (used-by (sonic_amp sonic_amp_chart_sprites) (sonic_amp sonic_amp_picks_sprites) ...))
(image-set (asset 1668) (set "sonic") (source "Media/scroll/sonic_tex.bin") (slots 489 4) (images 93) (ids 2415 2507)
  (loaded-by (sonic_amp sonic_amp_screen_update)))
(standard-image (file "standard/muteki01.png") (source "Media/systemdata/muteki01.tga") (size 64 64) (use "..."))
```

- `rect` is in texels of the sheet (`sheet` is the size the registry declares; it equals the stored size for every
  sheet). `texture` is the index within the set.
- `screens` / `used-by`: the layout tables whose sprite fields name the id, and `code:<function>` for literal ids in
  screen code (`SPRITE_INIT`). 1478 sprites have one. The rest are drawn through plain id lists, computed ids
  (`base + index`, digit fonts) or event scripts; their set's `loaded-by` (literal `texture_set_load` calls) and the
  set name say which screen they belong to.
- `art` is a guess for the redraw: `render` (70) a large shaded picture, the screens' pre-rendered backdrops and
  processed photos, sometimes with flat text on top; `flat` (2669) text, panels, buttons, icons, line art;
  `portrait` (793) the conversation and fight portraits (CG renders); `jacket` (176) album art; `paint` (55) the
  paint swatches of `module_tex`. `render`/`flat` is measured (more than 100000 opaque pixels with 200+ colours, or
  3000+ colours), so small renders (the grey Scoobee pictures on `dealer_tex`) come out `flat`.

The standard images (`Media/systemdata`, plain BMP and TGA) are decoded by the extractor itself (SDL_image is built
without those formats). Only `bump_light` and `muteki01` are drawn (shader textures, not UI); `wave02` and
`yurayuraTest` are bound for a model flag nothing sets; the other four are never loaded.

## Timelines

`level_meter_a` and `level_meter_b` (`src/data/misc_f.c`), the only data for the sprite player: a Flash movie
flattened to per-frame command lists. Each file has the stage size, fps (60) and frame count (30), the seven parts the
caller hands the player (sprite 3177 at depths 7..1), and per frame its commands in order:

```
(frame 0
  (background 0xff000000)
  (place (character 2) (depth 1) (move 0) (matrix 1 0 0 0.699997008) (x 439) (y 411) (color 1 1 1 1) (color-add 0 0 0 0))
  ...)
```

`place` puts the part with that depth in its slot (move 0 replaces the slot's part, 1 keeps it) with a 2x2 matrix
(scale and rotation), a position in pixels and a colour multiplier; `background` is the clear colour; `(play)` and
`(stop)` are the kind 4 events. Frame 0's command count less one is the slot count.

Words holding a plain char array's address (`(int)&D_001FA224`) print as text, like literal strings, so
pointer tables of help lines and labels (name entry, the garage, the shops) come out readable;
`CODE_STATICS` in `tools/ui_tables.py` adds the `static char *` help lines code files declare
(`shop_level_help`, `chip_level_help`). The decomp names MsgBox's text layer field `text_layer` (it was
`columns`); the port reads either. A `D_<address>` name the decomp has since renamed still finds its
table by address (`ui_table`).
