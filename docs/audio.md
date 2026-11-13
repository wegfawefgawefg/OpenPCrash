# Audio

The sound engine in `src/audio/`, built as the `phantomcrash_audio` library from `cmake/audio.cmake`. It is GenkiLib's
sound (decomp `src/hand/genkilib_sound_o2.c`, `genkilib_sound_cmd.c`, `dsound_init_o2.c`, `src/o2/genkilib_0de830.c`) on
SDL3 with our own mixer; no SDL_mixer. The spec is the decomp's `docs/port/audio.md`; data layout is the extractor's
`docs/audio-assets.md`. Presentation calls it from the main thread. The world never touches it.

## Files

| File | What |
|---|---|
| `audio.hpp` | the API, the only header the game includes |
| `audio_state.hpp` | the engine's structs: banks, cues, voices, ghosts, streams, listener, device |
| `audio_banks.cpp` | reads `data/audio`: volume table, music table, reverb presets, SE banks |
| `audio_sounds.cpp` | the effect calls: `sound_start` and the setters around it |
| `audio_mix.cpp` | the mix: voices, 3D placement, ghosts, reverb send, streams, clip |
| `audio_streams.cpp` | music and movie audio: the reader thread, start, stop, level, pause |
| `reverb.cpp/.hpp` | the I3DL2 stand-in |
| `audio_device.cpp` | the SDL output device's stream and its pull, start and shutdown |
| `audio_follow.cpp` | following the device each frame: lost, moved, stalled, opened again; the health line |
| `wav.cpp/.hpp` | RIFF/WAVE reading |
| `tools/audio_render/main.cpp` | `pc_audio_render`, the offline renderer used to check levels |

## Threads

- Main thread: every API call. It owns its views of the banks, cues and voices (the voice in turn, which it started and
  stopped, where it put them) and answers the calls from them: the voice `play_sound` returns, flag 4's refusal, a muted
  bank, `music_track`. What the mix has to do goes to it as a `Command` through `Audio::commands`, a ring with one
  putter (retail queues its sound work the same way, `sound_command_push` 0xDD2F0). A bank is read on the main thread and
  handed over; the mix hands the old one back through `released` and the main thread frees it (`update_audio`).
- Device thread (SDL's stream callback): `render_audio` runs the commands waiting, then mixes 48 kHz stereo float; SDL
  converts to the device. It answers through atomics: the serial of a voice's start it played to the end (so the main
  thread knows the voice is free), a stream done, frames heard, the voices playing. It allocates, frees and waits on
  nothing the main thread holds: `mix_lock` serialises whoever runs the commands, and the main thread takes it only when
  the ring (8192 commands) is full, which needs the device not asking at all (none open, or stalled); the health line
  counts those as `ring-full`. Before (until 2026-11-12) every effect call held `mix_lock`, so a main thread put off the
  CPU while holding it held the device's ask up with it.
- Reader thread (raised to high priority): every 10 ms reads music and movie WAVs (0x24000-byte blocks, as the
  original's packets) through an `SDL_AudioStream` of its own, which resamples 44.1 kHz to 48 kHz float, into each
  stream's `FrameRing` (2.7 s), one putter and one taker, kept 1 s ahead. Holds `files_lock`, never `mix_lock`. A stream
  start has a number: the reader fills only for the start the mix has taken, and the mix empties the ring when it takes
  one, so no frame of one track is heard as another's; a start begins sounding once the reader's next pass has filled
  (10 ms at most).

The mix runs at 48 kHz because the Xbox APU does, so the original's pitch values work without conversion.

## API

```cpp
AudioPtr audio = init_audio(data_dir);              // tables, reader thread, device
update_audio(*audio);                               // each frame: follows the device
load_sound_bank(*audio, bank_sys, "sys");           // 0 sys, 1 battle, 2 battle_old, 3 battle_player
int voice = play_sound(*audio, bank, cue, flags, position);  // sound_start; -1 refused
play_sound_named(*audio, "menu_accept", flags, position);    // cue names from the bank's .sexp
stop_sound / stop_sound_voice / stop_all_sounds
set_sound_pitch(*audio, bank, cue, voice, pitch);   // 4096 a octave from 48 kHz
set_sound_position(*audio, bank, cue, voice, pos);  // voice -1: every voice of the cue
pause_sounds(*audio, mode);                         // 0 pause, 1 resume, 2 each voice flips
mute_sound_bank / set_sound_level / sound_is_3d
set_listener(*audio, eye, forward, up);
ReverbPreset p; find_reverb(*audio, "city", p); set_reverb(*audio, p);   // reverb_off on exit
play_music(*audio, track, loop, scale, fade);       // fade 0 is the original's hard cut
stop_music / set_music_level / pause_music / music_track / music_active
play_movie_audio(*audio, "data/audio/adx/opening.wav"); movie_audio_seconds(*audio);
```

Flags are the original's `SoundStart.flags`: `sound_loop` 1, `sound_positioned` 2, `sound_once` 4. Track numbers are rows of
the 202-name music table. Levels are 0..255 through the millibel table.

Wiring into `main.cpp` (not done here so the skeleton's edits don't collide): `AudioPtr audio = init_audio(state.options.data_dir);`
after window init, `update_audio(*audio);` in the frame loop, `audio.reset();` before `SDL_Quit`. Main's `SDL_Init` doesn't
need the audio flag; `init_audio` starts the subsystem itself.

What stays with the caller (presentation), because it's game flow and not engine:
- Which (bank, cue) an event plays: bank 3 for the human fighter, bank 1 (and 2) for AI, by `controller != -1`.
- VS rules: mute banks 1 and 2, every 3D sound at (0, 0, 2), the listener pinned at the origin facing +z.
- The listener update every other battle update, from the camera's eye and target.
- Music choices: state music, the 3 s gap between battle tracks (below, "Fight music"), the 5 s gap in the shuffled pool, the four tournament themes,
  `music_active` is `func_000DD210` for those waits. `music_playlist_advance`'s bias is a retail bug the port fixes
  (docs/bugs.md): pick uniformly among the unplayed tracks.
- The pause: `pause_enter` plays sys 0xD and then pauses everything, so retail cuts the pause-in sound at once and plays it
  only on unpause (`pause_leave` plays it again). The port fixes that (docs/bugs.md): play 0xD after pausing the rest, so
  it is heard going in as well as coming out.
- Stage ambience timers and the per-stage and Shibuya per-area reverb choice (`reverb.sexp`'s `stage-reverb` rows).

## Fight music

Retail's (decomp docs/port/audio.md "Battles"), in `src/screens/shops/music_player.cpp`: `battle_music_update` runs every
update of the battle state's steps 2 and 3 (`battle_enter_next2`, `battle_enter_next3`), free play's (after its own
`12SENTOU.WAV` once, `free_play_screen_step2`) and the replay's (`replay_spawn_begin_next2`), all in
`src/screens/campaign/`; never the test drive's, which plays `12SENTOU.WAV` whenever nothing plays. When nothing plays a
3 s countdown starts; at its end the four tournaments with a theme (14, 31, 48, 49) play it again, any other fight the
next song of the battle's copy of the selected media playlist (`music_playlist_compact`): in order, wrapping to the top
at the first empty slot, or at random among the unplayed. The copy is made by the media screen's destroy and a save's
load, and keeps its place and played marks from fight to fight. Out of the box the selected playlist is song 77
(`12SENTOU.WAV`, "Nuisance") and eleven empty slots, so a player who never builds a playlist hears that one song over
and over with 3 s between: that is retail's, not a port bug.

The port's bug (playtest 2026-11-13, "basically no music in the fights"): `battle_music_update` was written but never
called, so a fight played only its entrance's `9PLAYIN.wav` (7.7 s) and was silent from there to the end. A headless
tournament fight with all 78 songs owned and `PHANTOMCRASH_SOUND_TRACE=1` showed it: one music start at step 181 and
none in the next 4000 steps. It is called now from the places above. Two more were missing beside it: the intro of a
tournament with a theme played the boss track (6) in its place (`tournament_theme_play` 0x9E631 now plays the theme),
and free play never started its `12SENTOU.WAV`.

The PC's additions (`src/screens/shops/fight_music.cpp`, the MUSIC panel in docs/settings.md "Music"), presentation only:

- MODE (`audio.fight_music`): PLAYLIST is retail's. ALL SONGS SHUFFLE deals every owned song into a shuffled deck and
  plays it through, none twice until all have played, then deals again (the song just played not first; a song bought
  since deals again at once). ONE SONG REPEAT plays one song (the one picked, or playing when the mode was set; else the
  playlist's first) over and over. Both keep retail's 3 s gap between songs and unlooped play. Until set the mode is
  ALL SONGS SHUFFLE once more than 12 songs are owned, else PLAYLIST.
- NEXT plays the mode's next song at once (the playlist's next slot, the deck's next song, the next owned song for ONE
  SONG REPEAT); BACK the song before (a history of the last 32 songs the fights played); a song picked plays at once.
  In a paused fight the new song plays while the pause stays up.
- The four themes, the test drive, free play, the intro and result music, the attract demo and the scenes are as
  retail's: the modes only change which song the list's turn plays.
- The draws come from `FightMusic`'s own generator (rand_crt's step, seeded from the clock once a session), never the
  world's and never retail's CRT stream, which still draws only where retail draws (`music_playlist_advance` at a
  song's end in PLAYLIST). A fight's world, record and hashes don't change with the mode, the panel or the songs.
- `PHANTOMCRASH_SOUND_TRACE=1` logs every music start and stop with the step and the time (`music step N at T s start
  TRACK FILE`), for the gaps.

## Data

Read from `data/audio/` as `pc_extract --only audio` writes it:
- `se/<bank>.sexp`: `(se-bank ... (cue (index N) (name ..) (file ..) (rate ..) (is3d 0|1) (voices K) ...))`, each cue's
  WAV at `file` (relative to `data/`). A cue with a `(rate R)` different from its WAV's rate is taken as resampled and
  its pitch kept (not the case today: the extractor writes original rates).
- `music.sexp`: `(track (index N) (name ..) (file ..) ...)`, rows without `file` are the 113 missing names.
- `reverb.sexp`: `(reverb-preset (name ..) (room ..) (room-hf ..) ... (hf-reference ..))`.
- `volume.sexp`: `(volume-millibels v0 .. v255)`, the 256 shorts at 0x410318 (`pc_extract --only audio` writes it).
  Without it the engine uses `2280 log10(level / 255)` floored at -4845 mB, which matches the table to within 20 mB above
  level 10 (and exactly at the defaults 225 and 255) but is louder below level 10.

## What matches the original

- Banks: 8 slots, a name already loaded or a slot in use left alone (`se_bank_load`), bank volume times effects level.
- `sound_start`: refused for a missing bank or cue, a muted bank, or `is3d` disagreeing with flag 2; the cue's `next` voice
  used and advanced round robin even when refused by flag 4; the voice restarts from its start. No voice limit, priority,
  stealing or culling: a cue's 1 or 2 voices are its only polyphony, as in retail.
- Flag 4 refuses a voice still playing. A 3D start with a null or origin position leaves the voice where it last was
  (`sound::differs` compares against spatial rows nothing but the load writes).
- Pitch: absolute, `48000 * 2^(pitch / 4096)` source samples a second, 0 at load (`func_000DDC50` sets pitch 0 on every
  buffer). So a 44.1 kHz cue plays 8.8% fast (+1.5 semitones) unless the game sets -500, which the leg code does for its
  roller loops. Kept per voice across starts. This is read from the code (the 4096 scale confirmed in the XBE's
  DirectSound at 0x154916), not heard in xemu.
- Volume: `table[(uchar)(int)(level * scale)]` for effects and music; 2D effects another -500 mB; 3D effects no mix-bin cut.
  Defaults 225 effects, 255 music. Movie audio at 0.96 of the music level's dB (`* 960 / 10000` into ADX's 0.1 dB units).
- 3D distance: positions halved, min distance 1 (2 game units), rolloff 0.1, max distance never reached, Doppler off.
  The attenuation past the minimum is `-2000 log10(1 + 0.1 (d / min - 1))` mB: read in the XBE's DSOUND section at
  0x152CAD (`d / min - 1`, times the listener's rolloff from `SetRolloffFactor`, through 0x15291E's
  `-2000 log10(x + 1)`), the I3DL2 inverse model. That gives -15.4 dB at 100 game units. **The decomp's
  `docs/port/audio.md` says the power model (`rolloff * 20 log10(d / min)`, -3.4 dB at 100 units) instead**; the engine
  follows the bytes. It is one function, `distance_gain` in `audio_mix.cpp`, if xemu shows otherwise.
- Pause: per voice as `func_000E18C0`: a paused voice resumes where it stopped, a looping one keeps looping.
- Music: one track at a time, whole-file loops from the data start (marker chunks ignored), an unlooped track ends and
  `music_track` goes to -1, a track outside 0..201 ignored, a missing file stops the playing track and plays nothing,
  `stream_volume_set`'s skip of an unchanged level at scale 1, pause/resume/toggle.
- Reverb only on 3D voices (the 2D cues' and the stream's explicit mix bins have no I3DL2 send), at a level that doesn't
  fall with distance (room rolloff 0 in every preset), switched off by `reverb_off`.

## What is approximated

- HRTF: plain panning. A 3D voice is mono; both ears get full level straight ahead, the near ear +3 dB and the far one
  -10 dB at the side (`sqrt(1 +- 0.9 side)`), and a one-pole low-pass closes from 20 kHz to 5 kHz as the source goes behind.
  A real HRTF (the original uses DirectSound's full HRTF) is a later improvement.
- Stereo only. The original's six mix bins (centre and LFE included) are folded to left and right: a 2D cue goes to both at
  -5 dB, music to both at 0 dB.
- Reverb: a stand-in, not the APU's DSP program. Room level and a one-pole HF cut at `hf_reference` on the way in; four
  early taps a side after `reflections_delay`; four Schroeder allpasses (diffusion) into an 8-line feedback delay network
  whose line lengths scale with density, losses set for `decay_time` and low-passes for `decay_hf_ratio`. Measured: City and
  Room decay at their 1.49 s and 0.4 s T60s, City's wet level within 1.5 dB of what its levels predict; the darker
  presets (Room's -454, Aqualine's -2500 room HF) come out quieter than a flat sum of their levels because of the HF cut.
- Stops: a cut or restarted voice fades over 4 ms (a ghost) instead of clicking; `stop_all_sounds` too (the original's
  `StopEx` release envelope is never set, so it stops at once). The music's hard cut also gets a 4 ms fade.
- Resampling is linear for effects (pitch moves per voice), SDL's for streams.
- The original's stream rounds its last packet up to 72 bytes, which plays a few stale frames at a non-looping track's
  end; not reproduced. Retail's damaged first 512 frames of battle cue 0 aren't either (the extractor's WAV is clean).
- The command ring's delay: effects calls take effect at the next mixed block (a few ms), not at the end of the game
  frame as retail's `sound_do_work` runs them; music starts up to one reader pass (10 ms) after its call. Whether a
  voice still plays (flag 4) is as the mix last said, a block old at most, as DirectSound's status is.
- Velocity isn't taken at all (Doppler factor 0 in the original).
- Port-only: `play_music`/`stop_music` with `fade_seconds > 0` crossfade; nothing in the original asks for it and the
  default 0 is the original's cut.

## Device

On Mans In My House's `audio_device.cpp` and `audio_device_follow.cpp` as they stood after MIMH's "sound that died under
load" (b7c7f1277, its docs/architecture.md "Sound that died under load (2026-10-21, audiodie)"). A stream of our own on
the system's default playback device pulls the mix (`audio_device.cpp`), so a device can go and come back with every voice,
stream and setting kept. SDL is 3.4.18 (cmake/deps.cmake, MIMH's pin), which has upstream's fix for SDL 3.4.0's PipeWire
backend taking one late answer as the device gone.

The pull, on the device's thread:
- raises its thread to SDL's time-critical priority on each stream's first ask (SDL's PipeWire backend runs the callback
  on the stream's own thread at the ordinary priority, which a loaded machine starves);
- flushes denormals to zero (the reverb's tail);
- makes the mix a block at a time, sends a block with a NaN or an infinity as silence (counted);
- only counts, in atomics: asks, frames, the first and last ask's time, the worst gap and ask, asks over twice the usual
  gap (a likely underrun), the slowest ask over 80 ms, and whether an ask is being answered now.

The follow, each frame on the main thread (`audio_follow.cpp`, from `update_audio`), opens again only for our own trouble:
- SDL said the device ours is on went away: a stream on a device we named is gone at once; one on the default is given
  1.5 s for SDL to move it, and a device of the lost one's name coming back is opened at once;
- no ask for 3 s, but not across a main-thread frame over a second long (a load), and not while one ask is still being
  answered (our own mix late: closing would wait on it, and a new device wouldn't help);
- a new stream that never asked in 2 s;
- three looks in a row (0.5 s apart) not on the system's default.

A device that isn't ours coming or going (a virtual sink, a screen share's capture, a headset elsewhere) leaves the stream
alone. Opens are on the default; after three misses in a row the devices SDL lists are tried in turn with the default
between, backing off 0.25 to 8 s.

Everything it sees is said with `SDL_Log` (stderr and the run log, below): `start driver= default=`, `open default|"name"
as=<logical id> on="<device>" <ch> <Hz> <frames>, took, opens=`, `asking on= after= priority=raised in|ordinary`,
`device added|removed id= name= [(ours)|(the lost one back)]`, `default now=`, `moved by SDL to on=`, `asks stopped` and
`asks back after`, `one ask took <ms>` (over 80 ms), `one ask has taken <ms> (the mix itself); waiting`, `<why>; opening
again (miss n)`, `a second on: asking|not asking yet, voices, gain`, and every 10 s a health line: `health asks/s=
frames/s= gap<= ask<= late= voices= starts= cut= refused= stopped= ring-full= nan= on= default= opens= priority=`.

## The run log

Every run writes its `SDL_Log` lines to a file of its own, `<pref path>/logs/run-YYYYMMDD-HHMMSS.log`, which on Linux is
`~/.local/share/OpenPCrash/OpenPCrash/logs/` (SDL_GetPrefPath; `src/run_log.cpp`). The newest ten are kept. Each line
has the seconds since the start; the first says the date, SDL's version and the arguments. `PHANTOMCRASH_RUN_LOG=0`
writes none. With a report of sound going away, the run's log says what the device did.

## Weapon sounds

`src/view/combat_sounds.cpp`, presentation after each world step (battle_run.cpp and the viewer), from the step's
`world.combat.events` and the shot pool before and after the step. Retail plays these from inside its weapon and shot
updates (decomp `docs/port/audio.md`, "Who uses which bank"); the world doesn't change for them. Bank 3 for a fighter with
a controller, else bank 1 or 2:

| what | from | human | AI |
|---|---|---|---|
| fire, `muzzle_fx` 0 (`weapon_flash_model_39e`) | `event_muzzle` | 3/0x19, 0x1B, 0x1C, 0x1D or 0x2C by geometry and mount | 1/0x21, 0x23, 0x24, 0x25 or 2/0 |
| fire, `muzzle_fx` 1 (`weapon_flash_model_39f`, flag 4) | `event_muzzle` | 3/0x2C | 2/0 |
| fire, `muzzle_fx` 2 (`weapon_flash_model_3f3`) | `event_muzzle` | 3/0x18 grenades, 3/0x30 else | 1/0x20, 2/2 |
| fire, `muzzle_fx` 3, 4 (`weapon_fire_sound_18`, `_17`) | `event_muzzle` | 3/0x18, 3/0x17 | 1/0x20, 1/0x1F |
| casing (flag 4, a coin toss) | `event_muzzle` of behaviour 0..3 | 3/0x2D or 0x2E | none |
| laser beam, again every 9 s, stopped on release | `event_muzzle` of behaviour 5, the trigger | 3/0x2F | 2/1 |
| melee swing at phase 15, cut at the swing's end | the blade shot and the motion phase | 3/0x1F, 0x21, 0x23, 0x24 | 1/0x28, 0x2A, 0x2B |
| a shot's pass meeting something (stage, craft, crate, prop, fighter) | `event_shot_hit` (a beam's on even steps) | 1/0x2E (flag 4; none for a beam), 1/0xE cannons, 1/0xF rockets | the same |
| a beam meeting a fighter | `event_beam_hit` (even steps) | 1/0x12 at the hit | the same |
| an AI rocket's fire sound moved with it | the shot pool | | 2/2's voice |
| melee hit (`impact_profile_dispatch` case 4) | `event_impact` kind 4 | 3/0x1E | 1/0x26 |
| the wreck | `event_destroyed` | 1/0x10 | 1/0x10 |

The two hit events are world changes (2026-11-12): the shot pool before and after a step couldn't see a shot that hit on
the step it was fired, nor tell a hit on a shot's last tick from its life running out, and nothing marked a beam meeting
a fighter.

## Fighter sounds

`src/view/fighter_sounds.cpp`, presentation after each step beside the weapons' (`actor_sounds_update` 0x935B7 and the
motion code's own sounds), from the world before and after the step; the world doesn't change for them. Bank 3 for a
human, bank 1 for an AI, unless the row says otherwise:

| what | from | human | AI |
|---|---|---|---|
| a footstep, two legs (`legs_sound_two`) | `fighter_footsteps` (src/view/footsteps.cpp), walking or running | 3/2 | 1/2 |
| a footstep, four and six legs (`legs_sound_many`) | the same | 3/3 | 1/3 |
| a footstep, rollers (`legs_sound_roller`) | the same: walking a skate's 2, else 1 looped; running 0 once, then 1 looped; pitch -500 | 3/0..2 | 1/0..2 |
| a footstep, hover (`legs_sound_hover`) | the same: 4 once, then 5 looped | 3/4, 3/5 | 1/4, 1/5 |
| the loops' end | the gait changes (walking stops cue_c, running cue_14) | | |
| a ground dodge, an air dodge (`fighter_leg_sound`) | the dodge clip set this step, then moved with it | 3/7, 3/0x13 | 1/7, none |
| landing (`actor_landing_sound`) | `landed`: contact 0x1400, 0x800, else the legs' | 3/0x11, 3/0x12, 3/8..10 | 3/0x11, 3/0x12, 1/8..10 |
| touching a type 8 surface (`actor_contact_sound`), twice: the stages' edge walls (bit 0x2000 of `collision_surface_bits`; nothing for touching a fighter, whatever the decomp's audio.md table says) | contact 0x2000, even steps | 1/0x11 | 1/0x11 |
| jump: from a run, and at lift-off | state 4 from 2 or 3; state 4's mode 1 to 2 | 3/6 | 1/6 |
| dive (`actor_air_control`) | `dive_used` set | 3/0x14 | |
| fall (`actor_phase_fall_update`) | the fall clip set this step | 3/0x16 | |
| hum, every 304 steps (152 updates: `timer-- < 0`), 2D | alive, outside the ending | 1/0xC | |
| a broken arm's crackle, every 302 steps | Effects' `weapon_state` 4 on an arm | 3/0x25 | |
| twist start and stop (`fighter_turn_sound`) | the twist axis past 0.1 within 45 degrees | 3/0xE, 3/0xF | |
| score, 2D, flag 4 | the score changed in a live fight | 1/0x13 | |
| low armour, every 66 steps (33 updates), followed | under a quarter of the durability | 3/0x27 | |
| the lock alarm (the world's `event_lock_warning`), at the fighter | someone's lock mark on it, checked every 10 updates: stopped at the check while it plays, started again the update after (11 updates, 22 steps, a round) | 3/0x2A | |
| the legs' loops and the low armour stopped (`actor_cloak_effects_release`) | dead, outside the ending | | |

The footsteps are `motion_select_descriptor` 0xAD6B7's: each footstep node (flag 0x20000) of the leg graph against its
track of the playing leg clip, a key of its three marker streams (curve group 0x3800, or the translation curves when the
track has both, asset 1758: docs/bugs.md) passed between `motion_phase_prev` and `motion_phase` while grounded and not
dead, or every sixth step of a skid (src/view/footsteps.cpp). Foot k, numbered in `sv_pose_walk`'s order as the feet's
attachments 14 + k are, also starts its landing set (motion event 0x400 << k, src/effects/fighter_effects.cpp). The
camouflage's sounds and the lock alarm are the world's events (fighter_camo.cpp, `lock_warning_update`).

The voices are the original's: each cue's 1 or 2, restarted in turn (flag 4 refusing one still playing), shared by every
fighter that plays the cue. A gun firing faster than its sample is long restarts its own voice every shot, as on the Xbox.

## Checking

`pc_audio_render --data DIR [--out mix.wav] [--seconds S] [--device] --event "T COMMAND"...` plays commands at times and
prints RMS and peak per quarter second. Offline (default) it renders through `render_audio` into a WAV; with `--device` it
plays through SDL, for `SDL_AUDIO_DRIVER=dummy` or `SDL_AUDIO_DRIVER=disk SDL_AUDIO_DISK_OUTPUT_FILE=out.raw` (48 kHz
stereo float). Never run it on real speakers. Commands are listed at the top of its source.

`PHANTOMCRASH_SOUND_TRACE=1` says every effect call with the step it came from: `sound step S start|start-cut|refused|stop
BANK/CUE name voice V/COUNT [3d D units away] [why]`, `start-cut` with about how loud the voice it cut still was (its distance and the part of its wave left, guessed from
when it started at its pitch: the main thread doesn't read the mix's position), `refused` with
why (bank muted, no such cue, 2D/3D disagreeing, flag 4). It goes through `SDL_Log`, so into the run log as well. A
headless fight with the real mix in a file: a private Xvfb, `SDL_AUDIO_DRIVER=disk SDL_AUDIO_DISK_OUTPUT_FILE=mix.raw`
(48 kHz stereo float), `--opponents o19,o40,12,40,5 --demo --frames 2400`.

Device trouble is checked on a private PipeWire, never the owner's: its own `XDG_RUNTIME_DIR` (short: the socket path must
fit 108 bytes), config, state and data dirs and its own D-Bus session (`dbus-run-session`), WirePlumber 0.4 with
`alsa_monitor`, `bluez_monitor`, `v4l2_monitor` and `libcamera_monitor` `.enabled = false`, and a
`support.null-audio-sink` for speakers, recorded off its monitor with `pw-record --target <sink> -P
stream.capture.sink=true`. As MIMH did (its architecture.md "Sound that died under load").

Measured 2026-11-12 (the owner: "i dont hear gun sounds", "sound cuts out ... and it doesnt recover well ... we are
dropping sounds a lot"):
- Gun sounds: before, no code played any. A player firing (build 37 against five AIs, a scripted pull of arm A, both
  arms, then walking while firing) through the disk driver: 19 of 546 two-second windows over -25 dB before, 145 of 410
  after; the trace has 111 starts of the player's 3/0x2C, 96 casings, 141 of the AIs' 1/0x24 and 77 of bank 2. An AI-only demo: 243
  gatling starts, 438 bullet hits (377 refused by flag 4, as retail), 67 rocket blasts. Bank 2 had never been loaded by
  the stage viewer, so its AI guns were refused there.
- Voices: in that fight the cuts were a gun restarting its own voice (the player's one-voice 3/0x2C, bank 1's two-voice
  gatling cue); taking the least heard voice of the cue instead of the one in turn changed 88 cuts to 86 and cut as many
  still-loud voices (52 over -20 dB either way), so the original's round robin stays.
- Under load on the private PipeWire (the stage viewer's six-fighter demo, the game on four cores shared with eight busy
  loops, the fake speakers recorded): main d58372a had 94 dropouts of 5 ms or more of digital silence in 344 s (2.3 s in
  all, the longest 64 ms); this build 15 in 503 s (0.34 s, the longest 43 ms), its health lines at most one late ask in
  10 s. Neither lost the device.
- The mix lock held by the main thread 150 ms and 1 s, the speakers destroyed and others made, the default moved to a
  third: both builds carried on, SDL 3.4.18 moving the default stream each time (3.4.0, which MIMH had, lost the device
  for good on a held lock).
- The command ring (the same day; the device's thread no longer takes a lock the main thread holds): `pc_audio_render`
  offline gives the levels below unchanged (sys 5, battle 2 at 2 units, music 3 at 255 and 128, paused silent and back, a
  looping effect paused and resumed, flag 4 refused with both voices playing, a 2D cue with flag 2 refused, a missing
  track stopping the music, crossfades); `--device` through the disk driver plays the music through the reader thread
  and its ring; an AI fight (`--opponents o19,o40,12,40,5 --demo --frames 1500`, lavapipe, 160 s of wall clock) through
  the disk driver: 75 of 79 two-second windows over -25 dB, one gap of 5 ms or more (64 ms, at the start), every health
  line `ring-full=0 late=0`, the longest ask 1.1 ms.

Measured on the extracted data (2026-11-11):
- sys cue 5 (accept, 2D) at the default level: peak -6.2 dB, the cue's own peak plus -1.24 and -5 dB. The same through the
  disk driver: -19.0 dB RMS, -6.2 dB peak, as offline.
- battle cue 2 (3D) at 2 units: peak -1.4 dB; at 100 units straight ahead: -16.8 dB (15.4 dB of distance); at 40 units to
  the left: left -16.7, right -29.5 dB RMS.
- Music track 3 (10WIN) at level 255: peak -0.5 dB; at level 128 6.3 dB down; paused silent; a missing track (1) stops the
  music; crossfade and stop work.
- Flag 4 on a cue with both voices playing returns -1; a 2D cue started with flag 2 returns -1; `pause_sounds` 0 then 1
  resumes a looping voice.
