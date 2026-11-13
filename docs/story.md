# Story and conversations

The story is two halves. `src/story/` is world code: the progress counters, the trigger table that turns state changes into
conversations, days, today's line-up and the area select's gates. `src/talk/` is presentation: the event script interpreter that
plays a conversation, bark, artist bio or dealer talk, and the conversation state's task around it. The spec is the decomp's
`docs/port/progression.md`; function names and addresses below are the decomp's.

Conversations change nothing. Every bit of progress is written by game code through `src/story/`, and the only thing playing a
conversation does to the world is set its trigger row's done bit, which the resolver does before the script starts. So the talk
code can stay entirely on the presentation side, and a conversation can be skipped without changing anything ("Skipping" below).

## src/story (world)

`Story` (`story.hpp`) sits in `World` as plain integers: `pools[272]` (primary and eight alternates, 0 / 1 / -1),
`tournament_won[56]`, `day`, `money` and the beaten bit, `music_owned[210]`, `events_done[5]` (a bit per trigger row), `lineup[4]`
(`rumble[s].tournament`), `completed_cycles`, `days_played`, `map_spin_money`, and the unsaved fields the resolver reads:
`rumble_selected`, `class_wins_at_entry`, `ranker_wins_at_entry`, and the interrupted transition `talk_src` / `talk_dst`.

`StoryTables` (`story_tables.*`) is loaded once from `data/tables/` and passed in; it isn't world state. It reads
`event_triggers.sexp` (the 110 `EventTrigger` rows, table order kept), `tournaments.sexp`, `tournament_rules.sexp`,
`day_schedule.sexp`, `opponent_pools.sexp` and `tournament_groups.sexp`. Predicates are taken by the function name the tables family
writes beside each slot (`class0_wins_ge_2`, `ranker_bout_unwon`, `story_ge_12`...), which that extractor checks against the
`event_predicates` pointers; the slot number is kept for the "75 or more is none" test. `burning_chrome_tournaments` (0x203574,
52 53 54) isn't extracted by anything yet and is a constant with its address.

What's ported:

- `app_transition_resolve` (0xA1BCC) exactly: from a trigger talk the saved pair is searched again, so every row that holds for one
  transition plays back to back in table order, each seeing the bits the earlier ones set; the first row of a chain saves the
  transition; the done bit is set as the row fires. When a chain ends the rows marked repeatable are cleared, so rows 14 and 38 ("lost to
  the Area Ranker") play after every lost bout as the table means; retail's clear can't run and they play once (docs/bugs.md).
- `event_trigger_find` (0xD17C1): the nine checks in order, byte fields read as signed chars, the prerequisite range walked first..last
  (retail swaps it the wrong way round so only first == last works; no shipped row has a range), the predicate called only for slots 0..74.
- All 75 predicates by kind, with their counters: `class_wins`, `area_ranker_wins`, `story_progress`, `music_owned_count` (it counts
  the first 0xC0 bytes, as retail), `game_tournament_slot` (49 becomes 52 once beaten), `game_day_tournament`.
- `story_boot` is `game_state_new`'s story part (events, `pool_progress_new`, songs with 77 owned as heard, beaten bit, cycles);
  `story_new_game` is `new_game_reset` (day 0, 20000, the line-up). As progression.md warns, a port that adds "quit to title" must
  run `story_boot` again before a new game.
- `day_advance` is `day_advance_end` (0x2E32D): day + 1 with the 60 wrap, a post-game cycle on the wrap once beaten, `days_played` to
  9999, the line-up refilled, `map_spin_money` cleared.
- `lineup_refill` is the tournament part of `tournaments_refill` (0x6CEDA) and `fill_opponent_queues`' two exceptions: tournament 48
  drops out for good once won, and a 52 is one of 52..54 by roll once beaten. Retail draws CRT `rand` only when it fills a 52, so the
  caller draws the roll from the world's generator when `lineup_wants_roll` says so (the world has no generator yet).
- The area select: `area_pick` (rumble_selected = area * 2 + the tournament's night), `area_entry_status` in the code's order (no
  tournament, ranker lock, First Ranker lock, then the fee; slot 3 is the chip shop), `tournament_enter` (the fee through
  `money_add`'s clamps, then the at-entry counts the first-win talks compare with), `opponent_beaten` (1 into the ticket's byte, which
  for the fixed opponent is `tournament_won`), `game_beaten_set`.
- `event_done_count` / `event_once_count` for the records screen (83 rows not marked repeatable).

Known difference: a predicate or check that reads `tournaments[game_day_tournament()].rule` when the day's slot has no tournament
(-1) reads past the start of the table in retail. Here `tournament_rule` answers -1 for that, which matches no rule; retail reads
whatever bytes sit before the table. With `rumble_selected` 6 or 7 (free play, the test drive) the selected slot is 3, whose schedule word is
the chip shop's row, and retail treats that row number as a tournament id (on day 58 it's 14, an Area Ranker bout); the port's
`game_day_tournament` answers -1 there (docs/bugs.md).

Not done here, and why:

- (Done since: `src/story/rumble.*` and `roster_tables.*` port the opponent queues and drawn rosters over the world's
  `rumble[4]`: `fill_opponent_queues` with its tickets and the Burning Chrome roll, `opponent_fits_tournament`,
  `choose_opponent`, `materialize_actor` with New Game+'s bonus, `release_opponent`, `rumble_fixed_opponent_queue`,
  `tournaments_refill` at New Game and at the night, `rumble_contexts_reset` at boot. `lineup[s]` is
  `rumble[s].tournament`. The night, the area select and the battle call them.)
- Sonic Amp's nightly roll (`music_shop_generate_stock`, CRT `rand`) and the shop screens. `day_row` gives a day's dealer, chip,
  parts and used Scoobee rows, which are all the shops need from the day.
- (Done since: the front end's `menu_set_state` runs the resolver on every state change; a talk state asks for 0x9F.)
- Saves: the fields are the ones progression.md lists as saved; writing them is the save code's job.

## src/talk (presentation)

- `talk_script.*`: loads one script from `data/scripts/` (a talk by name, `0x5b`, or a bark, bio or maker script from its group
  file) and turns each line of `data/text/<group>.sexp` back into glyph tokens through `font.sexp` (the markup `{c:...}`, `{player}`,
  `\n` back into the control codes). Text pairs stay merged as the scripts family writes them: every US furigana is a blank and the
  pair's second half only counts `text_slot`, so one merged command behaves as the pair.
- `sprite_registry.*`: the render registry, portrait tables and texture sets from the ui family's data (`data/ui/images.sexp`,
  `data/ui/layouts/portraits.sexp`) and the manifest. A sprite is a texel rectangle on a texture slot, and a slot holds whatever set
  was loaded into it last, so the talk task binds its sets the way `talk_policy_apply` does, with portraits at 534 + bias by cast
  slot. A portrait of an actor outside the cast would draw from whatever slot 534 holds, as in retail; no script has one.
- `talk_interp.*`, `text_draw.cpp`: `event_interp_step` and the handlers, and `wide_text_draw`, line for line:
  - the walk from `pc_resume`, the frontier rule (`pc_high`), `page_skip` cleared when a non-text command first completes, a
    completed wait ending the frame and moving the walk's start;
  - text: passed, whole (speed 0), A-skipped (whole, sound 0x14 once), typing (`timer[1]` to speed, then one more glyph); the glyph
    budget with colour codes not counting; proportional widths below 0x184; the blip (bank 0 cue 0x10) on the frame a glyph appears
    and the talking flag for glyphs 0x93 and up; the press mark of -4 and its once-flag; names for -5..-8 (the pilot name in fixed
    cyan, 9 glyphs);
  - accept during typing sets `page_skip` and stops the frame; the next frame shows the line whole. `glyphs_shown` is cleared when a
    typed line finishes or is A-skipped (retail clears it only on finishing, so its next line starts part revealed; fixed);
  - the skip chord (both triggers and both stick clicks held) marks text done without drawing it, ends waits and delays, and passes
    page waits once their timer is past 1, so a conversation races through a page a frame;
  - wait clears the fades, countdowns, blinks and mouths; delay and the countdowns; page wait plays bank 0 cue 0x13;
  - portraits: one fade per page shared by every fading portrait (half of the cadence, 30 frames), then the eyes overlay for 8..11
    frames every 60..119, and the mouth stepping 0, 1, 2, 1 every 5 frames while the talking flag is up, cleared by the portrait that
    reads it. CRT `rand` is an MSVC-style LCG here; it only moves blinks.
- `talk_task.*`: the talk state's task. A start frame begins the half-second fade from black, the script starts once the counter
  goes negative (31 frames on), the background is drawn every frame (`talk_background_draw`: the location's picture at layer 0 and
  its bottom band at 0,296), and when the interpreter dies the task fades out over half a second and ends. Pads are masked while a
  fade runs, as `screen_fade_frame` masks them. Bark, bio and maker scripts run the interpreter alone with their starters' flags.
- `draw2d.hpp`, `talk_canvas.*`: the skeleton's Vulkan renderer has no 2D yet, so the interpreter writes a list of quads (slot, texel
  rect, screen rect, ARGB colour, alpha, layer) and `talk_canvas` composites it on the CPU: passes by layer (0..8 under the 3D,
  0x101.. over it, 0x7FFF the fade), sorted by layer and then submission (retail's quicksort leaves ties undefined), point sampled at
  whole pixels, and passes 0 and 12 clipped to the 576x448 title-safe viewport at 32,16 as retail's viewport does (the black border in
  the captures; whether a TV showed it is open, render.md). The renderer can draw the same list later.

### Skipping (PC only)

The original has no skip, only the chord that races through a page a frame. The port adds three, all in `talk/talk_skip.*`,
`screens/flow/talk_skips.cpp` and small hooks in `talk_state_step` and `menu_set_state`:

- Skip this conversation: a new press of Back (Escape, Backspace, a pad's Back) or a right click. The original reads only accept and
  the chord in a conversation, so Back is free there.
- Skip the chain: the same button still held 24 updates (0.4 s) later, or pressed with Shift (the keyboard's left stick click) or a
  pad's left stick click held. The conversation ends at once and every conversation chained after it in the same transition passes
  unseen; the game lands where the chain would have ended.
- "Story conversations: Show / Skip" (gubsy, Gameplay, profile scope, `phantomcrash.story.conversations`): with Skip no trigger
  conversation is ever shown. Help talks (rows 81..104 and 109, played from the help menu) always show. "Battle chatter: Show / Skip"
  (`phantomcrash.story.battle_chatter`) is read by `battle_chatter_skipped`, and `battle_barks_update`'s start (src/hud/hud_barks.cpp)
  starts no bark with it (barks have no effects and a dropped bark is never queued, so not starting one changes nothing).
- While a conversation is up, a hint in the top right corner in the conversations' font at 3/4 size, dim grey: "Esc/Back: skip",
  with "Hold: skip all" when the resolver would chain another conversation after this one (`talk_chain_continues`, a resolve on a
  copy of the story).

A skip never changes the outcome. What a conversation does is set its row's done bit (the resolver, before it starts), apply its
policy (music, background, portraits) and ask for 0x9F; its script only draws (decomp progression.md, "What a conversation can do").
So a skipped conversation still runs its script to its end, unseen: `talk_interp_finish` steps the interpreter with the chord held
and accept pressed every frame until `end`, into a list that is thrown away. Every command type was checked: reset, text, wait,
sprite, portrait, delay, page wait, the countdowns and end read only accept and the chord, which end waits and finish lines; 6..9 do
nothing; none writes outside the interpreter. No script asks a question (no command branches or reads a choice), and no talk script
has a negative wait or delay (only bios and dealer talks have `delay -1`), so every conversation ends; one that didn't within 2^20
frames would be shown instead. A chain skip then asks the resolver again from the skipped state with 0x9F, exactly as the state's
update would, sets `fe.state` to it so the next screen's `previous` is the same, starts the last policy's music as
`talk_state_spawn` does and leaves the screen black as `talk_state_destroy` does.

One retail difference this leans on: retail's blinks draw CRT `rand`, which the post-game Burning Chrome roll and Sonic Amp's nightly
stock also draw, so in retail how long a player looks at a portrait moves those rolls. The port's blinks have their own generator and
the world's rolls come from the world's, so watching or skipping can't move them.

Checked headless (`PHANTOMCRASH_STORY_DUMP=path` writes the save image, the done rows and the unsaved story fields at shutdown;
`PHANTOMCRASH_TALK_CHAIN="29 8 won 3-10 done 0-4"` starts a game with Shinjuku's class tournaments won and makes the change town map
-> Club Wired, a chain of six: 0x35..0x39 and 0x5B): a new game's opening (rows 0, 1, 2) played with A, skipped with Back and skipped
by the setting, and the chain played, skipped by a held Back, by Shift + Back and by the setting, give identical dumps.

### The debug player

`phantomcrash --talk NAME` plays a script in a window of its own with an SDL renderer: Z, Enter or Space is accept (A or START on a
pad), Tab held is the skip chord (both triggers and both stick clicks), Escape quits. Talk policies play their catalogue track
through the audio engine and the interpreter's sounds play on the system bank. Headless, it opens no window at all:

    phantomcrash --data DATA --talk talk_5b --capture out.png --frames 212 --talk-press 100 [--talk-chord 60]

`--talk-press N` presses accept every N frames and `--talk-chord F` holds the chord from frame F. A bark, bio or maker script has no
screen of its own, so the player loads each of its sprites' texture sets in place of the starter's screen.

### Not done in talk

- The talk background's garage banner (`selected_garage_banner_draw`) and the day and money (`day_draw`, `money_draw` with the number
  fonts): shared screen widgets for the screens code.
- Music 202, the shuffled catalogue: needs the music player's shuffle. Explicit tracks play.
- (Done since: barks' speakers, `battle_barks_update`'s pick and interrupts and the ranker defeat lines run in the battle's
  HUD, src/hud/hud_barks.cpp. The tutorial's and the ending's scene text aren't ported.)
- The help menu (state 0x13) with its own cast binding and its returns to 0x13; the help talks are ordinary scripts and play here.
- The chip name (-6) uses the line's colour and a 9-glyph budget; `battle_chip_name_draw` wasn't read and no US text uses it.
- Blending is straight alpha on the CPU canvas; retail's spritemat shader is premultiplied with NV2A clamping, which only matters
  for the Vulkan renderer's version.
- The blink flags' clock (`menu_input_age`) restarts on accept within the talk; retail's restarts on any menu input.
