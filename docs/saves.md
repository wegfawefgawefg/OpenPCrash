# Saves

What the original's save holds, when it saves and loads, and the port's save. Decomp names and addresses throughout
(`../phantom-crash-decomp`: `include/game_state.h`, `include/actor.h`, `src/hand/save_file.c`, `src/hand/u_06afc0.c`,
`src/od/u_028400.c` the load screen, `src/od/u_034f60.c` the save screen, `src/od/u_057040.c` the records check,
`src/o1/u_0201ab.c` the button config). Everything here was read in matched code, and the layout was checked against a real
save (below).

## The retail file

One Xbox save game named "Game Records" holds `phantomcrash.sav` and a copy of `D:\Media\diskcheck\SaveImage.xbx` (the
dashboard's icon). The file is exactly 0x122B8 bytes: three slots of 0x60E8.

| slot offset | size | field |
|---|---|---|
| +0x0000 | 1 | `present`: 1 once written (`save_write_slot`), 0 in a blank slot |
| +0x0004 | 8 | `time`: a FILETIME, `GetSystemTimeAsFileTime` at the write |
| +0x000C | 0x60C4 | `game`: GameState's first 0x60C4 bytes, raw x86 memory |
| +0x60D0 | 4 | `checksum`: `save_checksum` 0xA0FC5 of +0x0000..+0x60CF |
| +0x60D4 | 20 | `signature`: the `XCalculateSignature` digest of +0x0000..+0x60D3 |

`save_checksum` is the ordinary CRC-32 (reflected 0xEDB88320, start and final xor all ones; the table at 0x254E08 is
zlib's). The signature is keyed to the Xbox title, so a PC can't check it and doesn't need to.

**Correction to the decomp's docs.** architecture.md and findings say a save copies only 0x607C bytes of GameState in, so
+0x607C..+0x60C3 always load as zeros. The retail bytes say otherwise: `save_write_slot` copies with `mov ecx, 0x1831;
rep movsd` at 0x6B4BC, and `save_commit_selected_load` the same at 0x28D6A, and 0x1831 dwords are 0x60C4 bytes. The whole
head goes out and comes back. Nothing reads +0x6050..+0x60C3 either way.

### GameState +0x0000..+0x60C3

Signed unless marked. Bitfields are MSVC's: packed from bit 0 of their word.

| offset | type | decomp name | meaning | port |
|---|---|---|---|---|
| +0x000 | 272 x 9 bytes | `pools[0x110]` | per `opponent_pools` row: the primary's byte, then 8 alternates'; 0 not beaten, 1 beaten, -1 nobody there (`pool_progress_new` 0xBABDA) | `story.pools` |
| +0x990 | 56 bytes | `tournament_won[0x38]` | 1 won, 0 not, -1 the tournament has no fixed opponent; `tournament_won[0x30]` also drops tournament 48 from the schedule | `story.tournament_won` |
| +0x9C8 bits 0..4 | 5 bits | `selected_garage` | the garage in use, -1 none | `garages.selected` |
| +0x9C8 bits 5..23 | 19 bits, unsigned | `play_clock` | `play_time_tick` adds to it; capped at 0xCDFD7F0 that 19 bits can't hold; no reader | `profile.records.play_clock` |
| +0x9C8 bits 24..31 | 8 bits, unsigned | `music_volume` | 0..255, 255 after the defaults | `State::machine.music_volume` |
| +0x9CC bits 0..7 | 8 bits, unsigned | `se_volume` | 0..255, 225 after the defaults | `State::machine.se_volume` |
| +0x9CC bits 8..13 | 6 bits | `screen_shift_x` | -32..30, the screen adjust | `State::machine.screen_shift_x` |
| +0x9CC bits 14..19 | 6 bits | `screen_shift_y` | -16..14 | `State::machine.screen_shift_y` |
| +0x9CC bits 20..29 | 10 bits, unsigned | `day` | 0..59 | `story.day` |
| +0x9CC bit 30 | 1 bit | `vibration_setting` | the options' vibration item, 0 after the defaults | `State::machine.vibration` |
| +0x9CC bit 31 | 1 bit | `flag_9cc_31` | cleared by `sound_volumes_default`, nothing else found | `State::machine.flag_31` |
| +0x9D0 bits 0..30 | 31 bits, unsigned | `money` | units of 10 NY, 0..99,999,999 | `story.money` |
| +0x9D0 bit 31 | 1 bit | `game_beaten` | set entering the credits (`game_beaten_set` 0x6DEF1) | `story.game_beaten` |
| +0x9D4 | 210 bytes, unsigned | `music_owned[210]` | 0 no, 1 bought, 2 heard; 0..77 are songs | `story.music_owned` |
| +0xAA6 | 2 | pad | | |
| +0xAA8 | 8 x 12 ints | `playlists[8][12]` | the media player's playlists, catalogue ids, -1 empty | `profile.music.playlists` |
| +0xC28 | 10 shorts | `sonic_amp_stock[10]` | tonight's songs at Sonic Amp (`music_shop_generate_stock`) | `profile.music.sonic_amp_stock` |
| +0xC3C | 57 bytes | `colors_owned[0x39]` | bit 0: this paint colour is owned | `garages.colors_owned` |
| +0xC75 | 206 bytes | `stickers_owned[0xCE]` | bit 0: this sticker is owned | `garages.stickers_owned` |
| +0xD43 | 1 | pad | | |
| +0xD44 | int | `garage_unlocked` | a bit per garage, 7 for a new game | `garages.unlocked` |
| +0xD48 | 5 ints | `events_unused[5]` | cleared with the events, never used | not kept (zero; the import notes otherwise) |
| +0xD5C | 5 ints | `events_done[5]` | a bit per trigger row, 110 of 160 used | `story.events_done` |
| +0xD70 | int | `playlist_random` | 0 sequential | `profile.music.playlist_random` |
| +0xD74 | int | `playlist_selected` | | `profile.music.playlist_selected` |
| +0xD78 | 8 x 9 shorts | `playlist_names[8][9]` | the playlists' names in font glyphs, -1 padded | `profile.music.playlist_names` |
| +0xE08 | 255 x 0x18 | `inventory[255]` | `InventoryItem`, below | `garages.inventory` |
| +0x25F0 | 10 x 0x36 | `garages[10]` | `Loadout`, below | `garages.garages` |
| +0x280C | 10 shorts | `pilot_name[10]` | the player's alias in glyphs, -1 ends it; "Ｍｒ．ＵＳＡ" after boot | `profile.pilot_name` |
| +0x2820 | 4 x 0xDF0 | `rumble[4]` | `RumbleContext`, below: today's three tournaments, then free play's | `rumble`, `story.lineup` |
| +0x5FE0 | int | `total_earned` | "Total prize money" | `profile.records` |
| +0x5FE4 | int | `best_payout` | "Highest prize money earned" | |
| +0x5FE8 | int | `battles` | "Rumblings played" | |
| +0x5FEC | int | `wins` | "Number of returns"; the return rate is wins / battles | |
| +0x5FF0 | int | `f_5ff0` | only ever cleared (`records_clear`) | not kept |
| +0x5FF4 | int | `kills` | "Total enemies shot down" | |
| +0x5FF8 | int | `drones_shot_down` | | |
| +0x5FFC | int | `days_played` | "Days passed", up to 9999 | `story.days_played` |
| +0x6000 | int | `play_time` | "Total gameplay hours", vblanks, capped at 0xCDFD7F0 | |
| +0x6004 | int | `longest_battle` | | |
| +0x6008 | int | `shortest_battle` | 0xCDFD7F0 until a battle is played | |
| +0x600C | int, unsigned | `save_count` | "Saves", up to 9999 | |
| +0x6010 | int | `enemy_weapons_destroyed` | up to 9999 | |
| +0x6014 | int | `weapons_destroyed` | | |
| +0x6018 | 0x30 | `PlayerConfig` (game_state.h calls it `pad_6018`) | the button config, below | `profile.options.buttons` |
| +0x6048 | int | `camera_view` | the player's view 0..5, 1 after boot | `profile.options.camera_view` |
| +0x604C | int, unsigned | `completed_cycles` | 60-day loops since the ending | `story.completed_cycles` |
| +0x6050 | 0x74 | pad | nothing reads or writes it | not kept |

The achievement rate isn't stored: `record_percent` 0x334D6 works it out from `events_done` (the 83 rows not marked
repeatable) and `tournament_won` (the 50 tournaments of `record_percent_tournaments` 0x1FC0F8): (rows + tournaments) /
133 x 100. The play time on the records screen is `play_time` / `video_rate_nominal`.

Not in the save: everything past +0x60C4, among them `rumble_selected` (+0x6104), the per-player copies of the button
config (+0x10FD4, 40 bytes a player), `class_wins_at_entry` and `ranker_wins_at_entry` (+0x113BC), `selected_save`
(+0x113C0) and `map_spin_money` (+0x113C8). A load leaves them as they were (docs/bugs.md has what that does).

### The records inside

`InventoryItem`, 0x18 bytes: `garage` (+0x00, byte, the garage it's fitted to, -1 none), `tune` (+0x01, byte, -99..99),
`kind` (+0x02, short: 0 body, 1 leg, 2 arm, 3 back, 4 chip, 5 option), `part` (+0x04, short, a row of that part's table;
-1 marks a free slot), `name` (+0x06, 9 glyph shorts: a chip's name, written by `sjis_to_glyphs` from the chip record).
`inventory_drop_invalid` frees a slot by setting only `part`, so a free slot can keep old values in the rest.

`Loadout` (a garage), 0x36 bytes: `body` +0x00, `leg` +0x02, `arm[2]` +0x04, `back[2]` +0x08, `chip` +0x0C,
`option[10]` +0x0E, all shorts holding inventory indexes (-1 nothing); `name` +0x22 (9 glyph shorts, -1 ended); `color`
+0x34 and `sticker` +0x35 (bytes, rows of the colour and sticker tables; a cleared garage has 0 and 0xCD).

`PlayerConfig`, 0x30 bytes at +0x6018: `stick_a` (+0x00, CONTROL TYPE: 0 Standard, 1 Preferred), `stick_b` (+0x04, VIEW
CHANGE TYPE: 0 Normal, 1 Reverse), `flags` (+0x08, the control type word: bit 0 when `stick_a` is 0, bit 1 when `stick_b`
is 0; `player_config_flags_update` 0x27CDA writes it every time the two change), `beginner` (+0x0C, byte), `keys[8]`
(+0x10). `keys[a]` is the pad button for action a, in the order arm A, arm B, back A, back B, right step, left step,
jump, camouflage; the button is an index into `D_001DD470` (A, B, X, Y, left trigger, right trigger, white, black). The
defaults are `D_001DD490` = {5, 4, 1, 3, 7, 6, 0, 2}. On a load and when the key config screen closes,
`player_config_commit` copies it to player 0's per-session config, which the fighters read.

`RumbleContext`, 0xDF0 bytes, one per area slot (3 for free play):
- `queue[3]` +0x000, 0x370 each: 72 `OpponentTicket`s of 12 bytes (`opponent` row of the opponent table, `flag_at` the
  GameState offset of the progress byte a kill writes: `pool * 9` a primary, `+ 1 + k` an alternate, `0x990 + t` a
  tournament's fixed opponent; `role` 0 alternate, 1 primary, 2 fixed), then `drawable`, `count`, `out`, `ready`. Queue 0 is
  opponents not yet beaten, 1 beaten ones (rematches), 2 the reserve pool. Only tickets below `count` are read.
- `regular_quota` +0xA50, `entrants` +0xA54, `tournament` +0xA58 (-1 no tournament).
- `roster[6]` +0xA5C, `RosterEntry` 0x84 bytes: the build (a `BuildRecord` copy: `f_00`, body and tune, leg and tune, four
  weapons with tunes, chip and tune, colour, sticker, ten options; its `name` pointer at +0x38 is cleared around a write
  and pointed back at `names` after a load), the pilot record (+0x3C, a `PilotRecord` copy: identity, Scoobee identity,
  team, `f_06`, the destruction value curve, reaction, `f_1a`), `level` +0x58, `ai_profile` +0x5A, `player` +0x5C (-1 AI,
  -2 empty), `destruction_value` +0x60, the ticket it was drawn from +0x64, `queue` +0x70, `start` +0x74 (three floats),
  `start_set` +0x80.
- `names[6][0x13]` +0xD74: each entry's build name, Shift-JIS.
- `fixed_queued` +0xDE6, `pool_queued[8]` +0xDE7.

Saving the contexts means today's drawn opponents survive a save and load: a load on the same day meets the same people.

### A real save

The decomp's xemu hard disk (`phantom-crash-archive/work/runtime/xemu/xbox_hdd.qcow2`, the same file is on
`build/runtime/hdd/fixtures.qcow2`) holds one: `E:\UDATA\504c0001\F6DCC7123FE2\phantomcrash.sav`, 74,424 bytes, slot 0
written 2026-08-11, Day 3, 234,260 NY, Mr.USA, a garage with seven parts, 2 battles and 27 kills, three saves. Every
field above reads sensibly at its offset, the CRC matches in all three slots, `events_unused`, `f_5ff0` and +0x6050..
+0x60C3 are zero, and the control type word matches its rows. To get it out: `qemu-img dd -U -f qcow2 -O raw bs=524288
skip=5501 count=9767` (the E: partition at 0xABE80000), then walk the FATX directory (16 KB clusters, 32-bit FAT, root at
cluster 1).

## When the original saves and loads

- **Never by itself.** Saving is only the system menu's Save (state 0x27 -> 0x1F). Nothing is saved after a battle, at the
  night or after the ending; switching off loses everything since the last save, the beaten bit included.
- **Slots.** The file has three, but every loop on both screens runs `i < 1` and the slot cursor's range is 0..1, so only
  slot 0 is ever shown, written or loaded. The load screen still picks the newest present slot by FILETIME (over that one
  slot) and sets `selected_save`; the save screen starts on `selected_save`.
- **Save** (`save_menu_screen_update` 0x35017): if the file is missing and there is room it is created first
  (`save_file_create`: the save game, the icon, three blank signed slots). Then the file is read; a read failure shows a
  message and the screen can only be left (status 1), so a damaged file can't be saved over. With no room it shows how many
  16 KB blocks are short (5 + 1 + 2 without the save game, 6 without the file) and B goes to the dashboard's memory screen.
  Writing over a present slot asks first. `save_write_slot` 0x6B43D: roster name pointers cleared, `save_count` + 1 (cap
  9999), the head copied, the time stamped, the slot written in place with its CRC and signature, the pointers back; on a
  failed write the count goes back down.
- **Load** (`save_load_screen_step2` 0x284B3, from the title or the system menu): no save game or no file: "no save data";
  a file that won't read: the damaged-file message; no slot present: "no save data". From the system menu loading asks
  first. `save_commit_selected_load` 0x28D33 copies the slot's 0x60C4 bytes over GameState, sets `selected_save`, resolves
  the selected garage's inventory references, reattaches the roster names, copies the button config to player 0
  (`player_config_commit`), applies the music and effect volumes, and rebuilds the music player's queue
  (`music_playlist_compact`).
- **New game** (state 9, `records_check_update` 0x57123): if the file is missing it is created when there is room, then
  read; a failed read shows `save_dialogs[1]` and the game goes on. State 0x2E checks the 4096-save-games limit and the free
  blocks the VS saves would need. Nothing is written.
- **The title** reads the file to put the cursor on LOAD GAME when any slot is present.
- **What a read rejects** (`save_read_file` 0x6B36D, `read_signed` 0xA0C59): a file of any size but 0x122B8, or any one of
  the three slots whose CRC or signature is wrong. One bad slot fails the whole file.
- VS builds are separate save games (`VsData.sav`, a 0xC4-byte signed `SaveHeader` per machine, named after it plus "for
  VS"); they aren't part of the game save and aren't ported here.

## The PC save

`src/save/` writes the same fields as `.sexp` text through gsexp, one file a slot: `<SDL_GetPrefPath>/saves/slot<N>.sexp`
(`save_dir_default`, `save_slot_path`). Gubsy has no game-save support at its pinned commit (its `saves.hpp` is empty) and
Mans In My House saves only settings through gubsy, so the files are written directly, next to where gubsy keeps its
settings. Graphics, mouse and key bindings stay in gubsy's settings and never go in a save; the original's options and
button config do, as the original keeps them.

The world holds the saved state as plain integers: `Story` (`src/story/story.hpp`) and, in `src/world/game_state.hpp`,
`Garages` (inventory, garages, colours, stickers, selected and unlocked garages), `Profile` (pilot name, records, music
player, the button config and camera view) and `RumbleSlot rumble[4]`. The options screen's volumes, screen shift and
vibration belong to the machine, not the simulation (docs/multiplayer.md "Change now", item 4): they live in
`State::machine` (`MachineOptions`), and `SaveImage::machine` carries them into and out of the same `(options ...)` form,
so the file is unchanged. `game_state_new` is the boot (`game_state_new` 0xB9460); a port with "quit to title" calls it
again before a new game, and the title's New Game resets the machine's options with it, as retail's does.

A file, by example (`build/pc_import_xbox_save --roundtrip out.sexp` writes a full one):

```lisp
(save (version 1) (written "2026-08-11T09:41:02Z"))
(story (day 2) (money 23426) (beaten false) (cycles 0) (days-played 2)
  (lineup 6 20 44 47)
  (events-done 0 1 2 3 66 67 68 105 106 107)
  (songs (77 heard))
  (tournament-won -1 0 0 0 ...))                        ; all 56
(pools (pool 0 0 0 0 0 -1 -1 -1 -1 -1) ...)            ; all 272: id, primary, eight alternates
(garages (selected 0) (unlocked 0 1 2) (colors-owned 0 2 7) (stickers-owned 1 11 205))
(garage 0 (name 164 168 60) (body 0) (leg 1) (arm 2 3) (back 8 -1) (chip 7) (option 5 6 -1 ...) (color 41) (sticker 1))
(item 4 (kind chip) (part 10) (tune 99) (garage -1) (name 187 194 187))
(pilot (name 169 200 4 177 175 157))
(records (total-earned 30141) ... (saves 3) ...)
(music (selected 0) (random false) (sonic-amp-stock 26 71 ...) (playlist 0 (name) (songs 77)) ...)
(options (music-volume 255) (se-volume 225) (screen-shift 0 0) (vibration 0) (flag-31 0) (camera-view 1))
(buttons (control standard) (view-change normal) (beginner 0)
  (arm-a right-trigger) (arm-b left-trigger) (back-a b) (back-b y) (left-step black) (right-step white) (jump a) (camouflage x))
(rumble 0 (regular-quota 3) (entrants 6) (fixed-queued false) (pool-queued 1 1 1 0 0 0 0 0)
  (queue 1 (drawable 10) (count 12) (out 2) (ready 10)
    (ticket 33 alternate (pool 33 0)) ...)
  (roster 1 (player -1) (name "ＰＣＤＤ") (level 10) (ai-profile 3) (value 231) (queue 1)
    (ticket 32 primary (pool 33))
    (build ...) (pilot ...) (start false 0 0 0)))
```

- Names (pilot, garages, chips, playlists) stay font glyph numbers, as the game holds them (`data/text/font.sexp`).
- Every array is written whole except the ones mostly empty: inventory slots equal to a free one, songs not owned, owned
  flags that are 0, queue tickets past `count`, queues and roster entries as a refill leaves them. The reader fills those
  with the same defaults.
- A ticket's progress byte is written as its place, `(pool P)` a primary, `(pool P K)` an alternate, `(tournament T)`;
  retail's GameState offset is gone. The roster's start is Q16.16 raw (floats in retail).
- Left out because they hold nothing: `events_unused`, `f_5ff0`, the control type word (it follows from the two rows),
  the pads, +0x6050..+0x60C3, the roster name pointers.
- The save's tournament per slot is `story.lineup`; the roster names are UTF-8.
- Validation: the `(save (version 1))` header, every form the writer always writes, every field in them, numbers where
  numbers go and indexes in range. Anything else fails the read as `SaveRead::Damaged`. A missing file is
  `SaveRead::Missing`. A new version adds a reader for the old one rather than breaking it.
- The port's additions: `area-selected` and `map-spin-money` in `(story ...)` (docs/bugs.md), and the `(pc-rules
  (cheated B) (infinite-money B) (tune-divisor N) (shop-divisor N) (money-won N))` form after `(pools ...)`: PC CONFIG's
  CHEATS (docs/settings.md, "CHEATS"; `Story::cheats`, `src/story/cheat_rules.hpp`), so a save remembers how it was
  played. It is optional: a save from before it (or one with the form taken out) loads as retail with no cheats, and an
  older build reading a newer save ignores it. When present every field must be there and each value one of its rule's
  choices (tune 1, 10, 20, 100, 200 or 0 for FREE; shop 1, 2, 4; money won 1, 2, 5, 10), else the slot is damaged.
  Checked 2026-11-13: cheats set and saved through the save menu, the slot loaded back with the flags kept, and the
  same file without the form loaded as RETAIL (`captures/cheats/load_*.png`).
- Writing goes to `slot<N>.sexp.tmp` and is renamed over the slot, so a failed or interrupted write leaves the old save.
- Three slots that all work, a file each, so one damaged slot never blocks the others (retail used only slot 0 and failed
  the whole file on one bad slot; docs/bugs.md).

The API (`src/save/save.hpp`) for the screens: `save_slot_read` for each slot (the load and save screens show pilot name,
selected garage's name and chip, day, money, written time and the beaten mark from the image), `save_newest_slot`,
`save_slot_write` (the count bump and undo), and `save_image_put` to load an image into the world. After a load the
caller does what `save_commit_selected_load` does past the copy: the button config to player 0, the two volumes to the
audio engine, the music player's queue from the selected playlist. The story's unsaved fields keep the world's values.

## Importing an Xbox save

`build/pc_import_xbox_save [--data DIR] [--slot N] phantomcrash.sav out.sexp` reads the raw file out of an Xbox save
folder (`UDATA/504c0001/<save>/phantomcrash.sav`), checks the size and each slot's CRC, converts the newest present slot
(or `--slot`), writes it, and reads its own output back to check it gives the same text. The roster names go from
Shift-JIS to UTF-8 through the font's sjis pairs. Anything unexpected (non-zero unused fields, a ticket whose `flag_at`
isn't a progress byte, a control type word that disagrees with its rows) is printed as a note. Copy the result to
`saves/slot0.sexp` to play it. `--roundtrip out.sexp` does the same for a new game with a value in every kind of field,
through the slot files.

## Next

- The save, load, records check and system screens (src/screens) call this API; the title's LOAD GAME cursor reads the
  slots.
- The garage, shops, media player and roster code adopt `Garages`, `Profile` and `RumbleSlot` as they're ported; the
  roster's `OpponentQueue` and `RosterEntry` here are the save's shape and can grow methods there.
- `record_percent` and its tournament list (0x1FC0F8) for the records screen.
- Sonic Amp's nightly stock roll: `game_state_new` leaves the stock empty.
- Exporting back to an Xbox file would need the signature key; not planned.
