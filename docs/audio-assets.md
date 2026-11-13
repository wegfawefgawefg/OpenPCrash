# Audio assets

What `pc_extract --only audio` writes (`tools/extract/audio*.cpp`), where each piece comes from and what was checked.
Everything lands in `data/` (gitignored). Paths inside the `.sexp` files are relative to `data/`. The decomp's
`docs/port/data.md` sections 18 to 21 and `docs/findings.md` ("Stage ambience and reverb", "Sound start parameters",
"Unused songs") are the spec.

From a worktree the default `--xbe` (`../phantom-crash-decomp/orig/default.xbe` beside the source dir) doesn't exist;
pass `--xbe` with the main checkout's path to it.

```
data/
  audio/
    se/<bank>.sexp           one table per SE bank: every cue's parameters
    se/<bank>/<nn>[_name].wav one WAV per cue, PCM16
    music.sexp               the 202-row music file table, with what exists
    music/<NAME>.wav         the 89 music files, PCM16 44.1 kHz stereo
    movies.sexp              the 11 movies and their decoded sound
    adx/<movie>.wav          each movie's ADX sound track, PCM16
    reverb.sexp              I3DL2 listener presets and which stage sets which
    volume.sexp              the 256-entry level to millibel table
  movies/<movie>.sfd         the Sofdec movies, copied as they are
  manifest/audio.sexp        original asset or file -> output
```

Formats: everything decoded is plain PCM16 WAV at the original rate and channel count. No resampling, no
normalising. WAV because it is lossless, any tool opens it and the cook step can make whatever the game loads
(Ogg/Opus for music if size matters). Music stays WAV rather than FLAC: the samples are the disc's bytes, so the
copy is provably exact (checked byte for byte) and the extractor needs no encoder. It costs 1.7 GB.

## SE banks

Four banks, loaded once at boot by `game_state_task` with `se_bank_load_resource(asset, bank, ..., 1.0f)`:

| Bank | File | Asset | Cues | 3D cues | Notes |
|---|---|---|---|---|---|
| 0 | `sys.bin` | 1923 | 48 | 0 | menus; 11 stereo cues, 5 at 48 kHz |
| 1 | `battle.bin` | 1920 | 48 | 40 | AI fighters, stage ambience, effects; 3 at 48 kHz |
| 2 | `battle_old.bin` | 1921 | 3 | 3 | |
| 3 | `battle_player.bin` | 1922 | 51 | 50 | the human fighter's sounds; 3 at 48 kHz |

150 cues, 178 s in all. Every cue is Xbox IMA ADPCM (format 0x69, 36-byte blocks of 64 frames per channel) decoded
to PCM16 WAV. The cue number in the file name and the table is the number the game plays it by: `(bank, cue)`.

What a bank holds per cue, and so what `se/<bank>.sexp` has: `is3d` (the cue is created as a 3D DirectSound buffer),
`voices` (how many buffers the loader makes over the same wave, i.e. how many copies can sound at once), and the
wave's own `fmt` (rate, channels) and frame count. The bank's volume is 1 for all four. Nothing else is in the
bank: no loop points, no per-cue volume, pitch or priority.

- Looping is chosen by the caller, not the bank: `sound_start` flag 1 loops the whole cue (findings, "Sound start
  parameters"). Flag 2 makes it 3D (refused on a 2D cue), flag 4 leaves a still-playing buffer alone.
- Volume: every buffer gets the global effects volume through the 256-entry millibel table at 0x410318 (pitch 0)
  when the bank loads; 2D cues go to all six mix bins at -500 mB. 3D cues use DirectSound's default 3D routing.
- 3D positions and velocities are halved before DirectSound: one game unit is half a metre for distance and Doppler.
- `riff-size` records the two RIFF size conventions on the disc (`span`: the field holds the whole subfile;
  `standard`: span - 8). The game never reads the field. `fact` is listed where the wave has one; a fact count
  below the blocks' capacity would trim the last block (none does).

Names: `<nn>_<name>.wav` where a caller in the decomp says what the cue is, plain `<nn>.wav` where nothing does
(`tools/extract/audio_names.hpp`, the deciding function beside each row). 116 of 150 cues are named. Bank 3 is the
human fighter's set and banks 1 and 2 the AI's (`_ai`); bank 3 cues 23..30 pair with bank 1 cues 31..38. Unnamed
cues have no caller at all (bank 1: 11, 13, 22, 34, 39, 41, 44; bank 3: 26, 32, 34, 40, 41, 43) or none that says
what they are (bank 0's leftovers). Bank 3 cues 8..10 sit only in `leg_sound_table` rows that are never indexed, so
human landings use bank 1's 8..10 too. `(looped-by-caller)` marks the nine cues some caller starts with flag 1
(roller and hover loops, Shibuya and Aqualine ambience loops, burning props, the supply craft); the beams and the drill
are one-shots the code stops. The roller loops are the only cues given a pitch: -500 always (the ramp toward the
running pitch never moves, decomp `src/od/u_0933f0.c`; read, not run).

Retail plays `battle.bin` cue 0 (`roller_run_start_ai`) damaged, the WAV doesn't: the bank's spatial-state arena
overlaps the start of its first wave, and `se_voice_clear_spatial` zeroes 24 bytes of each 3D cue's records after cue
0's buffer was pointed at that memory. Replaying the loader's writes over the file changes 147 bytes, 122 of them in
cue 0's ADPCM data, so the first 512 frames (11.6 ms, about 400 differing samples) decode differently, the opening
frames as silence. In `battle_player.bin` the 17 bytes hit are header only and change nothing. Whether the APU
really reads the overwritten bytes wasn't checked in xemu; the port should use the clean file.

Decoder: the header predictor is frame 0, then 63 nibbles (low first) give frames 1..63; the 64th nibble is padding.
Stereo interleaves 4 bytes per channel. Each nibble expands as `(2 * magnitude + 1) * step >> 3`. Checked: all 150
cues equal FFmpeg's `adpcm_ima_wav` decode sample for sample once its 65th frame per block is dropped (8,234,560
samples), which is also what the archive's SE_BIN decoder and vgmstream's `decode_xbox_ima` give. The Xbox APU
decodes these in hardware and nobody has captured its output, so "matches the reference decoders" is the claim, not
"matches the console". The shift-and-add IMA variant some decoders use drifts from this by up to 90.

Levels in the tables (`peak-db`, `rms-db`, `clipped`) are for checking, not game data: a cue that decoded to garbage
or silence shows there. 79 cues touch full scale for a few samples; that is in the data (the encoder overshoots),
the reference decoders clip the same samples. `battle/34` and `battle_player/26` (the same 0.08 s sound) sit about
1,000 above zero; also in the data.

## Music

`music_files` 0x254398 is 202 names; the sound init opens `D:\Media\sound\<name>` for each at boot. Every music
number in the game (Sonic Amp catalogue `MusicRecord.wave`, `state_music`, the battle playlist, tournament themes) is
a row of this table. `music.sexp` has all 202 rows by index; 113 have `(missing)` (no file on the US disc; the game's
open fails and the row plays nothing), 89 have the file with rate, channels, frames, length and loop.

- All 89 are 44.1 kHz stereo PCM16. The WAV in `data/` holds exactly the disc's samples (checked byte for byte) with a
  plain 44-byte header; the authoring `cue `/`LIST` chunks are dropped from the file and listed under `(markers ...)`
  for reference (18 files have them, e.g. `12SENTOU.WAV`'s "beg loop" at 0 and "end loop" at the last frame).
- Loop: the game's stream reader restarts at the data start, so every track loops whole: `(loop (start 0) (end
  frames))`. The markers are not honoured by the game.
- The game streams from the data chunk to the end of the file whatever the chunk says. On this disc the two agree
  for all 89 (no trailing chunks); a file where they didn't would carry `(data-chunk-bytes)` and `(played-bytes)`.
- Each row lists who plays it: `(catalogue n)` for Sonic Amp catalogue rows and `(state n)` for `state_music` rows.
  All 81 catalogue rows point at files that exist, which confirms the catalogue indexes this table. Files in
  neither table are played by literal track numbers in code (decomp `docs/port/audio.md`, "Which track plays where"):
  13BossStart 6 (battle intro), END1A 57 (ending scene), FORCFULL 58 (the last boss's theme), TVOFF3 178 (the
  in-engine ending). The four unused songs of findings "Unused songs" (ELLNOTHI 55, LOS_TR6 109, NAGAAKAI 120,
  NAGAKIMI 121) have no player at all.
- `state_music` row 159 holds 0, `003HORIZ.WAV`, which isn't on the disc: state 159 is past the 159 real states
  (`app_state_entry_table` has 159 rows), so this reads as padding, not a missing song.

## Movies and ADX

`movie_files` 0x25943C: 0 opening, 1 phantomcrash, 2 ending, 3 staff, 4 kudryavka, 5 epilogue, 6 opening02, 7 dolby,
8 phantagram, 9 the_end, 10 cloud. Each `.sfd` is copied unchanged to `data/movies/`. A Sofdec file is an MPEG-1
program stream: stream 0xE0 is MPEG-1 video (640x480), stream 0xC0 is one CRI ADX file. The video is not converted
yet; the movie player in the port can demux it (pl_mpeg reads MPEG-1 PS) or the cook step can transcode it.

ADX occurs only inside the movies (no loose `.adx`/`.afs` on the disc; the XBE's ADX multi-stream player is
behind a constant-false branch). The extractor demuxes stream 0xC0 and decodes it to `audio/adx/<movie>.wav`, with
rate, channels, frames, cutoff and loop in `movies.sexp`. All eleven are ADX version 3, encoding 3, 500 Hz cutoff,
unencrypted, no loop (`(loop none)`); 44.1 kHz stereo for opening and staff, 48 kHz mono for ending, 48 kHz stereo
for the rest. epilogue, the_end and cloud have a sound track of pure digital silence (FFmpeg agrees).

Decoder: CRI's own, from the decomp (`src/cri/adx_hdr.c` coefficients, `src/cri/adxb_dec.c` blocks): coefficients
truncated, not rounded (`(short)(z * 8192.0)`, `(short)(z * z * -4096.0)`), block scale `(head & 0x1FFF) + 1`, the
prediction `(c1 * h1 + c2 * h2) >> 12` plus nibble times scale, clamped. FFmpeg's `adpcm_adx` uses the raw scale and
rounded coefficients, so it differs from the game's decoder by up to ~700 (about -33 dB below full scale) on loud
passages. Checked: with FFmpeg's two choices swapped in, the extractor's output equals FFmpeg's sample for sample on
five movies, so demuxing and block layout are right and the remaining difference is CRI's formula. FFmpeg also
decodes past the header's total frame count to the end of the last block (up to 31 extra frames); the WAVs stop at
the total.

The ending's subtitles (`ending_subtitles` 0x17CF88) are text, not audio, and belong to the text family.

## Reverb

`dsstdfx.bin` (asset 1924) is the Xbox APU's DSP program (I3DL2 reverb and crosstalk), not data: nothing to convert,
the manifest lists it as replaced by `reverb.sexp`. The parameters live in the XBE and in code:

| Preset | From | Used by |
|---|---|---|
| city | `reverb_city` 0x22E8A0 (Shinjuku's and selector 6/7's enter build the same values in code) | Shinjuku, Shibuya outside its rooms, selectors 6 and 7 |
| room | `reverb_room` 0x22E8D0 | Shibuya areas 0 and 3 |
| room-2 | `reverb_room_2` 0x22E900, same values as room | Shibuya areas 2 and 4 |
| aqualine | built in `aqua_ambience_enter` 0x74020 (the I3DL2 Mountains values) | Aqualine |
| off | built by every exit (all levels -10000 mB) | leaving any stage |

Each preset is DirectSound's `DSI3DL2LISTENER` in its field order: room, room HF (mB), room rolloff, decay time (s),
decay HF ratio, reflections (mB), reflections delay (s), reverb (mB), reverb delay (s), diffusion and density (%), HF
reference (Hz). The three table presets are read from the XBE; aqualine and off are constants in
`tools/extract/audio.cpp` copied from the decomp's matched C, since the game builds them field by field on the stack.
Only 3D cues reach the reverb (the 2D cues' explicit mix bins have no I3DL2 send; decomp `docs/port/audio.md`).
`(stage-reverb ...)` rows map each stage selector (stage * 2 + night) to its enter function, read from
`stage_ambience_enters` 0x22E840, and so to its preset; Shibuya's rows add the per-area switch its update makes.

## Volume table

`volume_millibels` (0xDD2D0) indexes 256 shorts at 0x410318 in `.data`: the millibels for each volume level 0..255,
-9600 at 0, -4845 at 1, -2049 at 32, -685 at 128, -124 at 225 (the effects default), 0 at 255. Effects, music and
movie volume all go through it (decomp `docs/port/audio.md`, "Volumes and the mix"). `volume.sexp` is
`(volume-millibels v0 .. v255)`, read straight from the XBE, 16 to a line; the sound engine reads it back.

## Not done here

- `Media/sound/` holds only the four banks, `dsstdfx.bin` and the 89 WAVs: there is no separate voice data.
- The ambient cue timings (every 3, 4, 5 s, Aqualine's 8 to 13 s) and the leg sound table are gameplay code, ported
  with the stage and fighter code, not data here.
