# Battles at 60 Hz

The original steps a battle at 30 Hz. The port steps it at 60 Hz. This file is the one place that says how every
time-dependent number in the battle changes for that, so every gameplay file converts the same way and the fight
plays the same. Every row cites the decomp name and retail address; `tools/refine/show.py NAME` in
`../phantom-crash-decomp` prints the function. Where a straight conversion does not give the same behaviour the row is
flagged and the section "Flagged" says what to do and how to check it against the original by eye.

The decomp's `docs/port/gameplay.md` advises against a 60 Hz battle step unless the feel is re-measured, because the
collision sweeps, the 5-unit caps and the jump thrust don't come out the same. This file is the answer to that: what
changes, the corrections that make it come out the same, and the measurements that show it.

Short version, the things that go wrong if a file only halves velocities and quarters gravity:

1. Jumps: 1.5 to 9% off in apex. Use the jump recipe (crouch 9, retail `jump_speed` halved not re-derived, thrust
   charged per pair, the half-step correction).
2. Reloads: fast guns fire up to a third faster. `reload_left = 2 * ceil(R/2) + 1`.
3. Beams, lingering blasts and melee blades hit every update in retail; per step they do double damage. Even steps.
4. The AI: run it on even steps as retail's 30 Hz code; don't convert it.
5. Every position update with a force in it needs `p += v + dv/2` (or `- dv/2` for the movers that move first).
6. Air forward push is an acceleration: a quarter, not a half.
7. Knockback travels 5% short unless the hit is scaled by 0.527864 instead of 0.5.
8. Shots inherit twice the firer's per-step velocity.
9. Lob gravity is per update on a per-tick velocity: 0.0136111 per step, plus the correction.
10. Camera and aim smoothing (0.3, 1/3, 0.9) go through `1 - sqrt(1 - a)`; they move where shots land.
11. Dodge intangibility: keep retail's 12.5 and count it down 1 a step: 12 ticks, as retail's six shot passes.
12. The dodge's slide starts on an even tick: its clip's `loop_start` (1) is tested as 2 there, or the dodge slides
    5% too far.

The tables hold 153 battle values plus 28 AI clocks and 8 presentation ones; 58 of the battle values need more than
the plain conversion (rules R10 to R14 or a flagged section).

Words used here:

- update: one retail battle step, 1/30 s.
- step: one port step, 1/60 s. Two steps make a pair; a pair stands for one update.
- tick: 1/60 s. Retail counts most battle timers in ticks and steps them by `ticks = video_rate_nominal / frame_cadence`,
  which is 2 in battle. In the port a tick is a step.
- update count: a counter that moves by 1 per update with no scale (`Actor.age_frames`, `Battle.frame`,
  `stick_dodge_window`, the arm raise latch, the AI's timers).

What retail does with these, read in the decomp (`docs/port/architecture.md`, "60 and 30 Hz"):

- Code written with `video_rate_nominal / frame_cadence` converts itself: with `frame_cadence` 60 the factor is 1.
  Most tick timers, the walk and run speeds, turn rates, dodge speeds, animation phase steps, shot moves and the laser
  drain are written that way. The port writes them with a constant `TICKS_PER_STEP = 1` and they need nothing else,
  unless a row below says the factor sits on the wrong power (an acceleration written with one `ticks`).
- Code written with `frame_cadence * seconds` also converts itself (an update count at 30 becomes a step count at 60).
  The AI is written that way. See the AI section for why the port keeps the AI at 30 Hz anyway.
- Gravity is `9.8 / (video_rate_nominal / 2)^2`: it uses the nominal 60, not the cadence, so it is a per-update
  constant and does not convert itself.
- Literal per-update numbers (drag 0.985, decay 0.99, 0.9, 0.8, the 1/8 and 0.3 smoothing, the 5-unit caps, pi/16
  turn clamps, gate swing 0.019333) do not convert themselves.

## Rules

Every row below names the rule it uses. R1 to R9 are the plain conversions; R10 to R14 are the ones that keep
behaviour exact where a plain conversion doesn't.

| rule | applies to | 60 Hz value |
|---|---|---|
| R1 | velocity or displacement per update (units/update) | half |
| R2 | acceleration per update squared (gravity, the dive, thrust steps) | quarter |
| R3 | tick timer stepped by `ticks` | same value, step 1 (automatic) |
| R4 | update count | double (see R12 for strict `>` tests) |
| R5 | angle per update (turn clamps, gate swing, hatch) | half |
| R6 | multiplicative factor per update `f` (drag, decay) | `sqrt(f)` |
| R7 | smoothing `x += (target - x) * a` per update | `1 - sqrt(1 - a)` |
| R8 | "move toward by at most `c` per update" (reticle track, melee lunge, heading clamps) | `c / 2`; exact, it reaches the target at the same time |
| R9 | rate already per tick (shot velocity, `run_rate`, `walk_rate`, `turn_deg`, dodge speeds, `accel`, `turn_rate` of a weapon) | unchanged; its `ticks` factor does the work |
| R10 | position integration with a force in the step | `p += v + dv / 2` (half-step correction, below) |
| R11 | per-update events that must keep their rate: contact damage (beams, lingering blasts, melee blades), per-update random rolls, cosmetic draws from the world RNG | do them on even world steps only, same values |
| R12 | thresholds and countdowns whose length is a gameplay number | the exact 60 Hz value from the counter patterns below |
| R13 | a geometric decay that is applied to a displacement (knockback, skid, dodge slide) | `sqrt(f)` and scale the start value so each pair sums to the update |
| R14 | caps on a per-update length (velocity, push 5; separation push `radius`) | half, applied to the displacement as well as the stored value (the knockback impulse's 5 follows R13 instead: 5 / (1 + sqrt 0.8)) |

### R10, the half-step correction

Retail integrates semi-implicitly: forces change the velocity first, then `position += velocity` (`sv_motion_integrate`
0x7D097: `vy -= g` then `p += v`; `actor_air_control` 0x9052F changes `v` before that; shots do the same). Halving the
velocity and quartering the force does not give the same path: over two steps the position falls short or long by
half a step's velocity change. With `dv` the velocity change the forces made in this step (in per-step units):

```
v += dv          // forces: gravity, jump_step, thrust ease, drag, air push, steering
p += v + dv / 2  // retail: p += v
```

For a constant force this lands exactly on the 30 Hz position at the end of every pair. For drag it is exact to 1e-5
per update. `dv` is only the forces; a velocity that is assigned (ground motion rebuilt every update, lift-off,
takeoff, a dodge impulse turned into velocity, contact zeroing it) is not a force and is not in `dv`.

A few movers use the other order, explicit Euler: move with the old velocity, then apply the force
(`projectile_update_guided_next` 0x97E9A, `projectile_update_guided_next2` 0x980F7, `debris_update` 0x99798). Halved,
those move `dv/2` too far per pair. The correction has the other sign:

```
p += v - dv / 2  // v before this step's force, dv the force applied right after
v += dv
```

Getting the order wrong doubles the error instead of removing it, so each mover's row says which order it has.

### R11, even steps

A global `world.step` counter; "even step" is `step % 2 == 0`. Things that retail does once per update and that would
happen twice as often at 60 Hz (a beam that damages every update it touches, a 15/255 roll every update) run on even
steps only, with the original numbers. This keeps their rate, their damage per second and the number of `rand_table`
draws per second the same as retail, so a replay and a recording compare draw for draw where the fight itself agrees.
A sweep that belongs to such an event uses the positions from two steps ago as its "previous" end, so it covers the
same path the 30 Hz sweep did.

### R12, counter patterns

A counter crosses its threshold on a tick that retail could only reach in steps of two. Retail's event always lands on
an even tick after the start; at 60 Hz it can land one tick earlier or later. For one-shot animation and scene timing
that is the size of the input sampling difference and nobody can feel it; leave those at R3/R4. For the windows and
periods that are gameplay numbers (invulnerability, reloads, locks, crouch, stagger) use these, which give the same
tick count as retail. `S` is the retail value in ticks, `ceil` rounds up.

| pattern | retail shape | example | 60 Hz value |
|---|---|---|---|
| A, counts in its own step | set S, then in the same step and every later one `c -= ticks`; event when `c <= 0` after the decrement (or: phase set 0, `phase += ticks` in the same step, event at `phase >= S`) | crouch 10 | `2 * ceil(S / 2) - 1` |
| B, counts from the next step | set S late in a step; first `c -= ticks` the next step; event at `c <= 0` | shield immunity 30 (set in the shot pass, after the victim's settle); lock marks; stagger | `2 * ceil(S / 2)` |
| C, counts only on idle steps | set S on the step something happens; `c -= ticks` only on steps where it doesn't; it happens again when `c <= 0` at the test | weapon `reload_left` | `2 * ceil(S / 2) + 1` |
| D, update count, `>=` | `n += 1` per update, event at `n >= N` | arm raise 5 | `2N` |
| E, update count, strict `>` | event at `n > N` | `defeat_frames > 120` | `2N + 1` |
| F, tick count tested before stepping, strict `>` | `if (c > T) event; else c += ticks` | run after `motion_time > 30` | `T + 1` when T is even |

With S even, A is S - 1, B is S, C is S + 1.

## How the port is laid out for this

These follow from the rules and the flagged items; every gameplay file assumes them.

- `step_world` runs at 60 Hz and keeps `world.step`. `Battle.frame` counts steps; anything that shows battle time
  divides by 60.
- Humans: the pad is read every step and turned into the 20-byte control record every step (`sv_control_route`
  0x7742D). Edges (`control_buttons` bit 4 jump press, 8 camouflage, 0x400 to 0x2000 d-pad) are per step. A press shorter
  than a step is latched by the input layer so it still appears as one edge.
- AI: runs on even steps with the retail constants, as a 30 Hz system (section "AI"). On odd steps the fighter
  reuses the AI's last control record with the edge bits cleared.
- Contact damage, per-update rolls and the world-RNG cosmetic draws: even steps (R11).
- Replays record one control record per step: 36,000 records for retail's 18,000-update (10 minute) limit
  (`replay_input_record` 0x49F9A).
- One world RNG (`rand_table_next` 0x44685 walk over the table at 0x201578). The rules above keep the number of draws
  per second equal to retail's except where a row says otherwise.
- All constants in gfxp fixed point. The `sqrt` factors are precomputed constants with the retail value and the
  conversion in the comment beside them, per AGENTS.md.

## Tables

Columns: the value, where it is (decomp name and retail address), the retail value, its unit, the value at 60 Hz, and
the rule. "auto" means retail writes it with `ticks` (or `frame_cadence`) and it converts itself when the port's
factor is 1. "Flag" points at the section with the details. Numbers are rounded for reading; the port computes the
constants from the expressions given.

Gravity is used everywhere below: g30 = 9.8 / 900 x 2.5 = 0.0272222 per update squared (`sv_motion_integrate` 0x7D097,
`Battle.gravity_scale` 2.5 set in `battle_task_begin` 0x46A25). g60 = 9.8 / 3600 x 2.5 = 0.00680556 per step squared.

### Rates from the build

| value | where | retail | unit | 60 Hz | rule |
|---|---|---|---|---|---|
| `run_rate`, `walk_rate` | `chassis_apply_performance` 0x759B6 | clamp(leg speed x f, 20, 2000) / 216 and half | units per tick | same | R9; used as `ticks * rate` |
| `turn_deg`, `air_turn_deg` | 0x759B6 | 0.1..90 | degrees per tick | same | R9 |
| `dodge_speed_ground`, `_air` | 0x759B6 | 0.1..30 | units per tick | same | R9 |
| `lunge_speed_max` | 0x759B6 | `run_rate * 2.0 * ticks` (human), `* 1.4 * ticks` (AI) | units per update | half | auto (R8) |
| `jump_speed` | 0x759B6 | sqrt(2 x h x g30) x 0.4 + g30 | units per update | half of the retail value | R1. Don't re-derive it with g60: the `+ g` term then comes out one g60 short (js/2 - g60) and jumps come out 0.5 to 8% off. Flag "Jump" |
| `jump_thrust` | 0x759B6 | h x 0.84 / jump_speed x g30 | units per update (a velocity budget) | half, charged per pair | R1; flag "Jump" |
| `jump_height` | 0x759B6 | 2.5..100 | units | same | none |

### Ground movement, turning, input

| value | where | retail | unit | 60 Hz | rule |
|---|---|---|---|---|---|
| walk speed | `sv_motion_state_0_1` 0x8E636 | `ticks * walk_rate`, ramped by phase over the lead-in and lead-out | units per update | `walk_rate` per step | auto |
| run speed | `sv_motion_state_2_3` 0x8F25A | `ticks * run_rate`, same ramps | units per update | `run_rate` per step | auto |
| root-motion legs | `motion_root_delta` 0x91054 | clip root track delta between `motion_phase_prev` and `motion_phase` | units per update | delta over one step's phase | auto (phase advances `ticks * rate`) |
| clip phase advance | every `sv_motion_state_*` | `ticks`, `ticks * rate * stick`, `0.25 * ticks * stick` (state 12, 0x8E022) | ticks of clip time | step 1 | auto |
| clip thresholds (`motion_loop_start`, `loop_end`, `motion_end`) | every `sv_motion_state_*` | clip data | ticks | R12 A for clips started before the state's handler in that step (`actor_try_jump`, `actor_try_dodge`, `actor_run_start`), B for clips started inside a handler or in `actor_settle_damage` | R12; one tick either way is harmless outside the rows that name it |
| heading turn | `actor_turn_by_stick` 0x8E1D7 | `ticks * turn_deg * pi/180 * stick` | radians per update | per step | auto |
| skid turn | `actor_turn_by_stick_slide` 0x90DA7 | `2 * ticks * turn_deg ...` | radians per update | per step | auto |
| strafe push | `actor_strafe_step` 0x8DE5E | `ticks * actor_walk_rate * -stick` added to the rebuilt ground velocity | units per update | per step | auto |
| walk to run, run to walk | `actor_ground_input` 0x8FE9E | stick over/under 0.7 while `motion_time > 30`, `motion_time += ticks` after the test | ticks | test `> 31` | R12 F (retail switches at 32 ticks; `> 30` at 60 Hz switches at 31). Exact from the update the stick passes 0.7 while walking. From standing, the walk starts on that update and the counting on the next, an update in retail and a step in the port, so the port runs a tick early (33 ticks against 34; measured 2026-11-11). Left so: it is the size of the input sampling difference |
| idle bark | `sv_motion_state_11` 0x8E400 | `motion_time >= 900` | ticks | same | auto |
| airborne to fall | `sv_grounded_state_update` 0x88DD3 | `airborne_time += ticks`, fall state past 20 | ticks | same | auto, exact |
| `turn_rate` lean | `actor_turn_rate_update` 0x88880 | `rate = 0.125 * target + 0.875 * rate`, target pi/8 x stick running, pi/16 x stick walking, idle or in the air | radians (a lean angle, read only by the pose in `beam_quad.c`) | 0.064586 / 0.935414, same targets | R7 |
| `run_lean` | `actor_run_clip_select` 0x8F558 | `lean += (stick - lean) / 16`, clip switch at +-0.3 | stick units | `lean += (stick - lean) * 0.031754` | R7 (it picks the lean clip, so root-motion legs move a little differently without it) |
| stick double flick | `sv_control_route` 0x7742D | `stick_dodge_window` 5, `-1` per update, reset under 0 | updates | 10 | R12 D, exact |
| stick dead zone | 0x7742D | 0.01, rescale by 0.99 | none | same | none |
| pad edges | 0x7742D | bit 4 jump press, 8 camo, 0x400..0x2000 d-pad: per read (per update) | per update | per step | the input layer latches presses shorter than a step |

### Jumping and the air

| value | where | retail | unit | 60 Hz | rule |
|---|---|---|---|---|---|
| crouch | `sv_motion_state_4` 0x8E9DB | lift-off when `motion_phase >= 10` (phase starts 0, `+= ticks` the same update) | ticks | 9 | R12 A; flag "Jump" |
| lift-off velocity | 0x8E9DB | `takeoff_velocity` (y floored 0) + `jump_speed` | units per update | per step (takeoff is last step's velocity, `jump_speed` halved) | R1 |
| air control on the lift-off update | 0x8E9DB | not run (`flag` is taken before lift-off) | | not run on the lift-off step or the step after | flag "Jump" |
| thrust ease | `actor_air_control` 0x9052F, `D_001B5AF4` 0.9, `D_001B5B70` 0.1 | `vy = 0.9 vy + 0.1 jump_speed` while held and thrust is left | per update | `vy = 0.948683 vy + 0.051317 jump_speed` | R6/R7 + R10; flag "Jump" |
| thrust step | 0x9052F | `jump_step = g30` (cancels gravity), `jump_thrust_left -= g30`; the last partial update gets `jump_step = g30 - left` (a retail bug, kept: docs/bugs.md) | per update squared | budget kept in retail units and charged on the first step of each pair; `jump_step` is a quarter of the retail one on both steps | R2; flag "Jump" |
| dive | 0x9052F, `Battle.boost_gravity_scale` 6.0 | `jump_step = -6 g30` every update after the dive press | per update squared | -6 g60 | R2 + R10 |
| dive allowed | 0x9052F | `jump_speed - vy + (jump_thrust - left) > jump_speed` | per update | same test, every term per step | R1 (every term halves) |
| air steer | 0x9052F | `ticks * air_turn_deg * stick` on the heading; with button 1 the velocity turns a third of it | radians per update | per step | auto |
| air forward push | 0x9052F | `stick * 1/256 * ticks` added to the velocity every update | units per update squared | `stick * 0.5/256` per step | R2. Flag "Air push": `ticks` alone gives `stick/256`, twice too strong |
| air drag | 0x9052F | x, z `*= 0.985` | per update | `*= 0.992472`, inside R10's `dv` | R6 + R10 |
| landing recovery | `actor_land` 0x90AFD | `bounce = -(velocity_prev.y + sqrt(2 x 2r x g30)) x 2`, x (100 - leg landing)/100, clamped 0.05..1, recovery length (motion_end - 105) x bounce | velocity per update | compute with `2 * velocity_prev.y` and g30 | flag "Landing" |
| fall clip | `sv_motion_state_5` 0x8ECBE | phases | ticks | auto | R3 |

### Dodging, skidding, stagger

| value | where | retail | unit | 60 Hz | rule |
|---|---|---|---|---|---|
| dodge impulse | `actor_try_dodge` 0x8D90C | last update's horizontal velocity in body space + `ticks * dodge_speed` sideways | units per update | per step (both terms halve) | auto |
| dodge intangibility | 0x8D90C, `Battle.dodge_intangible_frames` 12.5 (0x41480000, `battle_task_begin` 0x46A25), counted in `actor_settle_damage` 0x766DE | 12.5, set in motion, `-= ticks` in the same update: 10.5 .. 0.5 over 0, six shot passes, 12 ticks | ticks | 12.5, `-= 1` a step: 11.5 .. 0.5, 12 steps | R3 with the half kept (an earlier version of this row read 12.0 and gave 11) |
| dodge slide start | `sv_motion_state_6` 0x8EEF9, mode 1 | slides from the update after the phase reaches `loop_start` (1 for every leg class): the dodge's own update reaches it (phase 2) and doesn't move, so the slide starts at tick 2 | ticks | test `loop_start` rounded up to even, 2 | R12 B-like. With 1 the slide starts at tick 1, one step more of the full impulse: 9.18 against retail's 8.82 for the Plain Holy; with 2, 8.77 (the last 0.6% is the idle switch's leftover step, below a tick) |
| dodge slide | `sv_motion_state_6` 0x8EEF9, mode 2 | `impulse *= 0.99`, then velocity = rotate(impulse) | per update | `*= 0.994987`; impulse start x 0.997487 | R13 (0.25%, may be skipped) |
| dodge ease-out, ground | 0x8EEF9, mode 3 | velocity = impulse x ((motion_end - phase) x 0.05 + 0.05) | phase in ticks | `(motion_end - phase - 0.5) * 0.05 + 0.05` | flag "Decays"; without it the ease-out covers 2..4% more ground |
| dodge ease-out, air | 0x8EEF9, mode 3 | velocity x, z `*= 0.9` | per update | `*= 0.948683` inside R10's `dv` | R6 + R10 |
| skid decay | `actor_slide_step` 0x90CA1 | `skid *= leg_bc` (leg +0xBC), then added to the velocity; ends under 0.2 | per update | `skid *= r`, r = sqrt(leg_bc); start value (the halved velocity) x 2r/(1+r); ends under 0.1 x 2r/(1+r) | R13; flag "Decays". The stop test then falls on either step of a pair, so the port's skid can end a step early (within 0.1% measured, 2026-11-11). Applies wherever the skid starts: `actor_ground_input` and `actor_land` |
| stagger length | `actor_settle_damage` 0x766DE, `sv_motion_state_7` 0x8F909 | `stagger_end` = largest weapon `stagger` of the update, capped at the clip's `motion_end` | ticks | `2 * ceil(S / 2)` | R12 B |

### Melee

| value | where | retail | unit | 60 Hz | rule |
|---|---|---|---|---|---|
| unguided step | `sv_motion_state_9` 0x8FB9E | `0.25 * ticks` forward | units per update | 0.25 per step | auto |
| lunge | 0x8FB9E, `melee_home_step` 0x911C5 | the whole tip-to-target distance, capped at `lunge_speed_max` | units per update | cap halves | R8, exact |
| homing turn | 0x911C5 | heading toward the target clamped to +-0.19635 (pi/16, human) or +-0.137445 (AI) | radians per update | 0.098175 / 0.068722 | R8 |
| climb | 0x911C5 | `vy = clamp((aim_height - tip.y) * ticks / 10, +-jump_speed) + g30` | per update | `vy = clamp((aim_height - tip.y) * 0.105573, +-jump_speed/2) + g60` | R7 (gain 0.2 per update); `ticks / 10` alone gives 0.1 a step, 19% a pair instead of 20% |
| blade contact | `shot_hit_fighters` 0x9C942 (behaviour 6) | tip within 1.2 x radius of the centre while `melee_active`; can land every update it stays inside | per update | even steps only | R11; flag "Contact damage" |
| melee lock | `hud_warning_update` 0x51DB7 | `lock_frames` (chip) countdown, started by `lock_mark_start` 0x52410 | ticks | the lock logic runs on even steps with the retail timer (`-= 2`): `ceil(L/2)` updates, as retail | R11 (weapons.md "At 60 Hz" 2) |
| lock range | 0x51DB7 | 25 (20 for an AI) | units | same | none |
| swing sound window | `weapon_melee_controller` 0x9C2B7 | `15 - ticks <= phase < 15` | ticks | auto | R3 |

### Integration, contact, separation

| value | where | retail | unit | 60 Hz | rule |
|---|---|---|---|---|---|
| gravity | `sv_motion_integrate` 0x7D097 | g30 | per update squared | g60 | R2 + R10 |
| position update | 0x7D097 | `p += push + velocity + impulse` | per update | `p += push + velocity + dv/2 + impulse` | R10; flag "Integration" |
| velocity cap | 0x7D097 | length 5 | units per update | 2.5, on the stored velocity and on `velocity + dv/2` | R14 |
| push cap | 0x7D097 | 5 | units per update | 2.5 | R14 |
| knockback cap | 0x7D097 | 5 | units per update | 2.63932 (5 / (1 + sqrt 0.8)) | R13/R14; flag "Knockback" |
| floor reach | `sv_contact_route` 0x7C740 | a grounded fighter reaches down `|velocity|` below the sphere | units | `|velocity|` of the step | auto (halves with the velocity) |
| slide sweep | 0x7C740 | `collision_segment_slide` only if it moved a radius or more | units | same threshold | flag "Collision" |
| through-wall throwback | 0x7C740 | segment old to new position against the stage and props | per update | per step | none (shorter segments, same path) |
| surface bit 0x40000 | 0x7C740 | velocity = resolved velocity + g30 (gravity undone), `airborne_time -= 1` | per update | + g60, `airborne_time -= 0.5`, and move `v - 2 g60` (v before this step's gravity) instead of `v + dv/2` | flag "Integration". Traced 2026-11-11: no type gives it (below), the port leaves the branch out |
| floor projection | `sv_motion_dispatch` 0x88B7E | velocity laid along the floor normal under the feet, horizontal length kept | | same | none |
| separation push | `fighters_separate` 0xB7EA0, `fighter_pair_separate` 0xB81A7 | overlap split, capped at `radius` | units per update | cap `radius / 2` | R14 |

### The pose

The pose is built in the step after the move (`actor_pose_build` 0xAA2AF, `src/mech/pose_walk.cpp`): the clips are
read at the phase the motion states left, which converts itself; these are the rest.

| value | where | retail | unit | 60 Hz | rule |
|---|---|---|---|---|---|
| pose blend | `actor_pose_reset` 0xAD5BA, `sv_collision_spheres_select` 0xAC6D7 | `pose_blend` 1 on a new clip, a run's stop and a landing, `-= 0.05 * ticks` before each pose (0.9 the first), each node slerped that far toward its last matrix | share of the last pose | weight sqrt(1 - 0.1 k) on the two steps of retail's k-th update, 0 from the 20th step | R7: a pair keeps what the update kept (the plain `-= 0.05` a step would let go of the old pose about four times faster by the fifth update) |
| foot target lift | `sv_ik_chain` 0xAA8CB | 0.3 above the stage hit | units | same | none |
| upper body pitch | `sv_pose_walk` 0xAA419 | camera `pitch` held within 0.4 | radians | same | none |
| arm raise | `sv_pose_weapon_node` 0xACD24 | `triggers[i].raise / 5` of a ranged weapon | updates | `raise / 10` (raise counts steps, `arm_raise_full`) | R12 |
| arm pitch toward the reticle | `pose_node_aim_at` 0xAD120 | within 0.8 | radians | same | none |

### Damage

| value | where | retail | unit | 60 Hz | rule |
|---|---|---|---|---|---|
| knockback | `damage_impact_result` 0x98861 | `impulse += dir * knockback * (100 - resist)/100` | units per update (added to the position every update) | `knockback * 0.527864` | R13; flag "Knockback" |
| knockback decay | `actor_settle_damage` 0x766DE, `Battle.impulse_decay` 0.8 | `impulse *= 0.8` after the move | per update | `*= 0.894427` | R13 |
| shield immunity | 0x98861 | `immune_frames = 30`, set in the shot pass | ticks | 30 | R12 B, exact |
| intro and spawn immunity | `scene_intro_update` 0x8920C | 300, rewritten every update of the intro | ticks | 300 | R3 (one tick either way) |
| killer intangible | 0x766DE | 840 | ticks | 840 | R3 |
| door scene immunity | `scene_door_update` 0x8A0E1 | 30 immune and intangible | ticks | 30 | R3 |
| immune / intangible count-down | 0x766DE | `-= ticks` | ticks | 1 per step | auto |
| damage lands | 0x766DE | the victim's next update | | the victim's next step | none (the latency halves, which is closer to the shot's real arrival) |
| crit, shield, break rolls | 0x98861 | `rand_float(1)`, `rand_float(100)` (twice on a crit) per hit | per hit | per hit | none (events); the contact-damage weapons hit on even steps only (R11), so their draw count stays |
| screen static | `hit_static_frames_set` 0x6F794, counted in `stage_props_render` 0x6F5ED | 15, `-1` per update, 5 `rand_float` draws per update (scratches) | updates | 15, counted and drawn on even steps | R11; flag "Cosmetic draws" |
| screen shake | `hit_shake_frames_set` 0x6F7A1, `hit_shake_jitter` 0x7E764 | 15, `-1` per update, 3 draws per update | updates | 15 on even steps, the draws held for the pair | R11; the look-ats read them on the next two steps (below) |
| kill streak window | `actor_step` 0x756F3 | max(180, 900 - 120 x streak), `-= ticks` | ticks | same | auto |
| durability bar roll | `actor_durability_bar_start` 0x762E8 | 300 or 90 x lost fraction | ticks | same | auto (HUD) |

### Weapons and triggers

| value | where | retail | unit | 60 Hz | rule |
|---|---|---|---|---|---|
| reload | `weapon_fire_controller` 0x9B46D | `reload_left = reload_frames` on firing; `-= ticks` only on updates that don't fire; fires when `<= 0` at the test. A shot every ceil(R/2) + 1 updates | ticks | `reload_left = 2 * ceil(R / 2) + 1` | R12 C; flag "Reload". With R as is, R = 2 fires 20 a second instead of 15 |
| volley gap | 0x9B46D | `volley_timer = 10` after each missile, `-= ticks` the same update | ticks | 10 | R12 B, exact (one missile every 10 ticks either way) |
| pellets, spread draws | 0x9B46D | `shots` per pull, 2 `rand_table_next` per extra pellet | per pull | same | none |
| volley lock take | `weapon_lock_take` 0x4D19D | one finished lock per missile | per shot | same | none |
| arm raise | `actor_triggers_update` 0x7645E | `raise` 0..5, +1 per update while `pressed`, fires when `raise == 5`, -1 per update released | updates | 0..10, fires at 10 | R12 D, exact (10 ticks from the press to the first arm shot) |
| trigger states | 0x7645E | `fire` 3 on the first update held, 2 after, 0 released | per update | per step | none |
| laser drain | `weapon_laser_controller` 0x9B97E | `resource -= ticks` per firing update; starts only from a full gauge | ticks | auto | R3 |
| laser recharge | `weapon_restore_resource` 0x97705 | `resource += resource_max / recharge_frames` per update once the beam is gone | per update | `resource_max / (2 * recharge_frames)` per step | R1 |
| beam intensity | 0x9B97E | `reload_left` +ticks while firing up to 8, -ticks after; the beam lives while it is over 0 | ticks | same steps; remove the beam when it is 1 or less after the decrement | flag "Beams". Retail lingers 3 updates (6 ticks) after release; `> 0` at 60 Hz lingers 7 ticks |
| beam growth | 0x9B97E | length `+= ticks * accel` up to `speed_max` | units per update | auto | R9 |
| beam damage | `battle_shots_update` 0x47E59, `shot_hit_fighters` 0x9C942 | muzzle to tip against every fighter, every update, lingering updates included | per update | even steps | R11; flag "Contact damage" |
| beam sweep damage | `impact_volume_scan` 0x9D003 | the triangle swept by the tip since last update | per update | even steps, with the tip from two steps ago | R11 |
| laser sound timer | 0x9B97E | `frame_cadence * 9`, -1 per update | updates | auto | cosmetic |
| reach estimate | `weapon_reach_update` 0x9D3AF, `weapon_reach_straight` 0x9DD05 | distances from speed (per tick), life, accel; the lob branch uses g30 and a literal `2.0` for ticks | units | same result | flag "Reach": compute it with the retail constants, not g60 |
| gauge roll | `weapon_gauge_refill_start` 0x9D5CF | 120 | ticks | auto | HUD |
| weapon break | `actor_weapon_break_apply` 0x8D606 | coin flip of two draws; weapon task paused | per hit | same | none |
| weapon clip time | `anim_player_advance` 0xB0C3E, `weapon_anim_step` 0x9B00F | `time += rate`, rate `video_rate_nominal / frame_cadence`; sampled before the advance; past the end back to `end - rate` | ticks | rate 1 a step (src/weapons/weapon_clip.cpp) | R3. The clip is sampled every tick instead of every second one; a clip with an odd end ends on its last frame where retail steps back to `end - 2` for one update first. Moves the barrel nodes the muzzles hang from |
| weapon clip held open | `weapon_anim_update` 0x9B08B | `anim_turn += anim_rate` (the player's rate) until 90, then the close clip | ticks | +1 a step to 90 | R3 |

### Shots

| value | where | retail | unit | 60 Hz | rule |
|---|---|---|---|---|---|
| shot velocity | `projectile_update_*` 0x9781E .. 0x980F7 | per tick, moved `pos += vel * ticks` | units per tick | same | auto |
| firer's velocity | 0x9781E, 0x97A49, 0x97CC2 | `shot->velocity += owner->velocity` at launch: a per-update velocity into a per-tick one, so it counts double for lobs and a guided shot's first move | | add `2 * owner->velocity` | flag "Inherited velocity" |
| straight accel | `projectile_accelerate` 0x97792, `projectile_update_straight_next` 0x979AA | speed `min(|v| + ticks * accel, speed_max)`, rebuilt along the shot's axis, then moved | per tick | auto, with R10 while accelerating | R9 + R10; flag "Integration" |
| lob gravity | `projectile_update_rotating_next` 0x97BB0 | `vel.y -= 9.8/900 x 2.5` per update on a per-tick velocity | per tick per update | `vel.y -= 0.0136111` per step, move `vel - 0.0068056` | R10; flag "Lobs" |
| guided speed roll | `projectile_update_guided` 0x97CC2 | speed x (r + 3)/3.5, two draws | per shot | same | none |
| guided turn | `projectile_update_guided_next` 0x97E9A | moves first, then turns at most `ticks * turn_rate` toward the target's `aim_point`, kept only under 90 degrees, then accelerates | radians per tick | auto; move `v - dv/2` while accelerating (explicit order) | R8 (the target is re-aimed every step, a slightly tighter pursuit; accept) + R10 |
| unguided accel | `projectile_update_guided_next2` 0x980F7 | moves, then `|v| + ticks * accel` (explicit order) | per tick | auto; move `v - dv/2` while accelerating | R9 + R10 |
| shot life | `battle_shots_update` 0x47E59 | `life -= ticks` on passes that hit nothing, expires at `<= 0`: ceil(L/2) moves | ticks | `2 * ceil(L / 2)` | R12 B. An odd life (15) flies one tick short at 60 Hz otherwise |
| shot hit test | 0x47E59, `swept_segment_vs_sphere` 0x9856A | swept segment last to this position, radius 0.7 x fighter radius + 0.3 | per update | per step | none (same path in half segments) |
| first move | `battle_update` order | the update after firing | | the step after firing | none; positions at every pair match retail |
| lingering blast | `impact_profile_dispatch` 0x98E5A (impact kind 2), shot state 3 | `impact_damage` every update it touches, for `impact_frames` ticks; radius by `impact_radius_at` 0x98218 from the life left | per update | the whole blast (radius, damage, `life -= 2`) runs on even steps with the retail life: `ceil(L / 2)` passes, the same radii as retail. Its capsule starts at the shot's position two steps before the hit | R11 (the same count as `2 ceil(L/2)` steps at 1 a step, and exact radii); flag "Contact damage" |
| prop/craft blast | `shot_blast_at` 0x99631, `blast_weapon_setup` 0x9D9C8 (back record 0x24) | 150 damage, 15 ticks, radius 3 to 15 | ticks | 15 retail ticks, 2 an even step: 8 passes | R11 |
| shot threats for the AI | 0x47E59 (`ShotThreats` at Battle +0xEE008) | rebuilt every pass | per update | per step; the AI reads the latest | none |
| debris | `debris_update` 0x99798 | `pos += ticks * vel`, then `vel.y -= 9.8/900` on a per-tick velocity (explicit order) | per tick | move `vel + 0.0027222`, then `vel.y -= 0.0054444` | R10; cosmetic |

### Camouflage

All in `camo_update` 0x74E00 (`Actor.camo_state` +0x3494), counted `-= ticks` in the same update they are set.
The port (`src/mech/fighter_camo.cpp`, in `combat_fighter_late` before the settle) counts them 1 a step.

| value | where | retail | unit | 60 Hz | rule |
|---|---|---|---|---|---|
| fade in | 0x74E00 state 1/2 | 60 | ticks | 60 (the hold starts a tick later than retail; the hidden window below still matches) | R3 |
| hold | state 3 | 20 | ticks | 20 | R3 |
| hidden at least | states 1..4 | 60 + 20 (`camo_fade_ticks`, `camo_hold_ticks`: the Pocket Watch row of docs/bugs.md) | ticks | 80 steps, as retail's 40 updates | R3, exact |
| active until | state 4 | `time_left - 60 <= 0` | ticks | `time_left - 61 <= 0` when the max is even, `- 60` when odd: hidden `2 ceil((max - 60) / 2) - 2` ticks, as retail | R12 A on the hidden window (states 1..4 hide from missiles, locks, the AI) |
| quarter-left bark | state 4 | `left / max <= 0.25 < (left + ticks) / max` | ticks | `+ 1` for `+ ticks`: the same single crossing | R3 |
| fade out | state 5/6 | 60 | ticks | 60 | R3 |
| broken tail | state 7/8, `camo_enter_state_7` 0x753C4 | 20, or the fade phase x 20/60 | ticks | same | R3 |
| recharge | state 9/10 | 20 tail, then `(max - left)/max x recharge_max` | ticks | same; `left` is 1 tick behind retail's at even ticks (retail takes 2 in the press update), so the recharge is short by `recharge_max / max` of a tick | R3 |
| one-update states | 0x74E00 | 1 to 2, 5 to 6, 7 to 8 after one update | updates | one step | none |
| max, recharge max | options +0x22, +0x24 | data | ticks | same | R3 |
| beginner re-arm | `beginner_camo_wait` | 120, `-= ticks` while waiting in state 0 | ticks | 120, 1 a step | auto |
| the AI's hold | `ai_camo_update` 0x5C9AC | `camo_max x fraction` counted 1 an update | updates (against ticks) | unchanged: the AI runs on even steps (section "AI"); its press is an edge on the even step | none |
| guided missiles | `projectile_update_guided_next` 0x97E9A | a target in 1..4 turns the mover into `_guided_next2` after the move, that update without the turn or the speed change | per update | per step: the step's move is the plain `v` (no `- dv / 2`, nothing accelerates), then unguided | R9 |

### Camera and aim

The camera is the aim: shots go to the world point under the reticle (`reticle_center_world`), so every smoothing
here changes where shots land, not only the picture.

| value | where | retail | unit | 60 Hz | rule |
|---|---|---|---|---|---|
| view yaw follow | `camera_yaw_follow` 0x7FBDE | `view_yaw += diff * 0.3` toward heading + `aim_yaw` | per update | 0.163340 | R7 |
| view pitch follow | `camera_pitch_follow` 0x7FCF2 | `view_pitch += diff * 0.3` | per update | 0.163340 | R7 |
| pitch from stick | `actor_camera_input` 0x7D2B0 | `pitch -= stick * 0.05235988` (3 degrees) | radians per update | 0.02617994 | R5 |
| pitch limit smoothing | 0x7D2B0 | `pitch_stick = 0.1 |stick| + 0.9 pitch_stick`, limit pi/4 + pitch_stick x pi/8 | per update | 0.051317 / 0.948683 | R7 |
| aim yaw | 0x7D2B0 | `aim_yaw += ticks * turn_deg * stick`, clamp +-pi/4 | radians per update | auto | R9 |
| recentre | 0x7D2B0 | `recenter_frames` 8, -1 per update; pitch and `aim_yaw` = start x frames/8 | updates | 16, x frames/16 | R4, exact |
| view switch | 0x7D2B0, `camera_view_blend` 0x7FE28, `hud_update` 0x4E62E | `view_switch_frames` 20, -1 per update; blend `frames/20` | updates | 40, `frames/40` (src/battle/camera_views.cpp, the HUD's slide) | R4 |
| own body hidden in the cockpit | `actor_draw` 0x82DE0 | leaving view 0 while `view_switch_frames > 15`, coming in under 5 | updates | `> 30`, `< 10` (src/view/cockpit_view.cpp) | R4 |
| pad rumble | `actor_settle_damage` 0x766DE, the player 0xDF770 | rows of 20, 10, 5 updates (10, 5, 2 under 500 damage), each then one update off | updates | twice the steps, a gap of 2 (src/input/pad_rumble.cpp) | R4 |
| chase eye | `camera_view1_chase` 0x7ED7A | `eye += (target - eye) / 3`; a prop between pulls it a third toward the fighter; `+0.1` up when too close | per update | 0.183503 for both thirds, `+0.05` | R7, R1 |
| beginner pitch spring | 0x7D2B0 | `pitch = 0.94 pitch + 0.06 pitch_stick` | per update | 0.969536 / 0.030464 | R7 |
| beginner snap turn | 0x7D2B0 | 90 degrees in steps of `ticks * turn_deg * 0.8`; 8-update lockout | radians per update / updates | auto / 16 | R8 / R4 |
| scene cameras | `camera_scene_update` 0x80068 | `scene.timer / 20` blends | ticks | auto | R3 |

### HUD that decides play

| value | where | retail | unit | 60 Hz | rule |
|---|---|---|---|---|---|
| lock-on time | `hud_locks_update` 0x50FC5, `lock_mark_start` 0x52410 | `lock_frames` (chip), `-= ticks` from the next update | ticks | even steps, retail timer (`-= 2`): starts are counted in their own update as retail (pattern A) | R11 (weapons.md "At 60 Hz" 2) |
| melee lock time | `hud_melee_lock_update` 0x51DB7 | same | ticks | same | R12 B |
| reticle tracking | `hud_reticle_track_update` 0x4F620 (`reticle_track`; was listed under `hud_update`) | moves `ticks * (reticle_speed/999 x 0.08 + 0.02)` toward the target, snapping when closer | screen units per update | auto | R8, exact |
| senses, occlusion | `hud_senses_update` 0x4F0D2 | every update | | every step | none |
| lock alarm | `actor_sounds_update` 0x935B7 (`lock_warning_timer`) | 9 updates between checks | updates | even steps, retail counts (`src/hud/hud_world.cpp`) | R11 |
| score and gauge rolls, popup `frames`, blinks | fighter and weapon tasks, `hud_draw_readout`, `color_blink` (from `task_timer`) | ticks | | auto (`src/hud`, presentation) | HUD only |
| popup `shown_scroll` | `hud_draw_readout` | `-= max(0.5 x scroll, 3)` per update | per update | `-= max((1 - sqrt 0.5) x scroll, 1.5)` a step | R7, R8 |
| cockpit `jump_gauge`, `speed_gauge` | `hud_draw_meters` 0x4C3D9 | ease <= 0.05 per update | per update | <= 0.025 a step | R8 |
| cockpit `walk_frame` | `hud_draw_meters` | +1 a draw, wrap 30, cell = /2 | draws | +1 a step while drawn, wrap 60, cell = /4 | R4 |
| lock mark `trail` | `lock_mark_step` 0x5231B | `x timer / lock_frames` once an update (visual) | updates | `x sqrt(timer / lock_frames)` a step: the same over the pair; run every step with retail's factor it would close much faster | R6 |
| bark page delays | event interpreter | update counts (16, 16, 18) | updates | the interpreter runs on even steps (`src/hud/hud_barks.cpp`) | R11 |

### Pickups and the supply craft

`supply_craft_update` 0x670FC runs once an update in `stage_props_render` 0x6F5ED, the stage task's update (installed by
`stage_props_enter` 0x6EDA0), which runs after every fighter, the shot pool and the weapon tasks (decomp
`docs/port/battle.md`, "One update"); `pickups_draw` 0x69CD2 steps the crates there too. Neither is draw-path code. The port
steps both in `step_world` at that point of the order (`src/stage/supply_craft.cpp`). Every number is per update or an
update count; none uses `ticks`.

| value | where | retail | unit | 60 Hz | rule |
|---|---|---|---|---|---|
| route speed | `stage_route_ingress` 0x67B76, `stage_route_pickup_release` 0x67E84, `supply_craft_fall_hover` 0x67FCB (`craft_route_speed`, 0.03; the ending's `ending_route_speeds`) | Bezier parameter per update | per update | half (0.015) | R1 |
| hatch | 0x67FCB | 0.03 rad per update while `> pi/4` is false (27 updates), then back while `< 0` is false (27) | per update | 0.015, opening at `> 0.80` and shut at `< -0.015` | R5 + R12: with pi/4 and 0 the half step opens in 53 steps and shuts in 53; the shifted thresholds give 54 each, as retail |
| crate drop gap | `pickups_set_up` 0x66C4D (50), counted in 0x67FCB | 50, -1 per update, first crate 0: drops 0, 50, 100 updates in | updates | 100: drops 0, 100, 200 steps in | R4 |
| crate fall | `pickups_draw` 0x69CD2 | 0.5 per update to the route's height + 1 | units per update | 0.25 | R1 |
| crate lifetime | 0x69CD2, `pickups_set_up`, `pickup_clear` 0x66D41 | 900, -1 per update, burst below 0 (901 updates), held while the craft is paused | updates | 1801 (1802 steps) | R12 E |
| crash | `supply_craft_shot_down` 0x6779A, `supply_craft_crash` 0x6820E | velocity 0.6 x the last route step, y -0.3, no gravity; `craft_crash_timer` 500, -1 per update after the landing test, burst below 0 (501 updates) | per update; updates | 0.6 x the last step's route step (half already), y -0.15; timer 1001 (1002 steps) | R1, R12 E |
| rotor spin | 0x67B76 (`craft_rotor` += 0.03) | per update | | 0.015 | R5, cosmetic |
| crate glow | 0x69CD2 | phase += 0.05 x (900 / life above 60, else 15) | per update | 0.025 x (1800 / life above 120, else 15) | R5, cosmetic |
| night beacon | `supply_craft_draw` 0x69955 (`craft_beacon_pulse`) | -0.14 per update, +3.5 below -2.5 | per update | -0.07 | R1, cosmetic |
| pick-up radius | `pickup_collect_test` 0x6735B | fighter radius + 1 | units | same | none |
| collection | `sv_contact_route` 0x7C740 (`pickup_collect_test`, then `pickup_apply` 0x66DD0) | every update, each fighter's sphere after the integrate | per update | every step, in the fighter's step; the fighter side right after `fighter_step` (`src/stage/pickup_apply.cpp`), before the settle as retail | none |
| repair, refill | `actor_heal_fraction` 0x6AC5A, `actor_ammo_refill_fraction` 0x6ACA9 | 10/30/50% of max (halved for role 2); 30/50/100% of one weapon, one `rand_float` | per crate | same (whole-number arithmetic gives retail's float truncations) | none |
| the craft's body | `pickup_collect_test` | pushed out to radius + 3, or set on top (+ the craft's per-update velocity) | per update | the step's velocity (half) each step | R1 |
| crate kinds | `pickup_kinds_draw` 0x66B11 | rolls per crate | per crate | same | none |
| second craft | `sky_craft_route_step` 0x6A30D, `sky_craft_draw` 0x6A52C, `sky_craft_screen_update` 0x6A987 | route 0.003, rotors 0.03, panels 0.002 rad to pi/6 (pi/12), glow phase 0.05, screen scroll 0.0005 per call (two calls: 0.001) | per update | 0.0015, 0.015, 0.001, 0.025, 0.00025 per call | R1/R5, cosmetic (presentation) |

### The stage's own animation

`stage_render_lods` 0x6E538 (from `stage_update`, once an update) and `stage_object_animate` 0x6E985. Presentation: the port
steps them per world step in the view (`src/stage/stage_animate.cpp`) and interpolates.

| value | where | retail | 60 Hz | rule |
|---|---|---|---|---|
| sky turn (kinds 1 and 2) | 0x6E538 (0x96CE40 += 0.0005, wrap 2 pi) | per update; kind 2 turns twice as fast | 0.00025 | R5 |
| red lamp (kind 7) | 0x6E538 (0x96CE48 -= 0.02, back to 1 below 0) | per update | 0.01 | R1 |
| scroll (kinds 3, 4) | 0x6E985 (v += 0.0005, 0.001 per draw of the object) | per update | 0.00025, 0.0005 | R1 |
| ambience waits | `city_ambience_update` 0x738DC, `aqua_ambience_update` 0x740DD (`frame_cadence * 3..10` + `rand_float_crt`), the four timers -1 per update in `stage_ambience_update` 0x73643 | update counts written with `frame_cadence` | -1 per step with `frame_cadence` 60 | auto (presentation, CRT rand) |
| moon, crate glow billboards | `stage_render_lods` 0x6E538, `pickups_draw` 0x69CD2 | rebuilt each draw | per display frame from interpolated positions | none |

### Battle flow and the stage

| value | where | retail | unit | 60 Hz | rule |
|---|---|---|---|---|---|
| replacement clock | `battle_update` 0x46F01, `battle_replacements_update` 0x47C1E | `replace_clock` 300, `-= ticks` at the end of `battle_update`, newcomer when `ticks > clock`, wraps to 300 | ticks | same | auto, exact (150 updates, 300 steps) |
| replay cut | 0x47C1E | load not done with under 30 ticks left | ticks | same | auto |
| dead AI lies | `sv_motion_state_10` 0x8FA64 (`defeat_frames` +1 per update after the clip), read in 0x47C1E | `> 120` | updates | `> 241` | R12 E |
| gate wait | `scene_intro_update` 0x8920C (`door_wait`) | 120 | ticks | auto | R3 |
| intro | 0x8920C | Ready at 90, Go at 30, music at `frame_cadence * 6 * ticks` = 360; entering AIs play it at 3x | ticks | auto | R3 |
| door walk | `scene_door_update` 0x8A0E1 | at most 0.2 per update toward the gate, turn at most 0.10471976 per update | per update | 0.1, 0.05235988 | R8 |
| VS end | 0x46F01 (`vs_end_timer`) | 120 to the menu, rows 6 each | ticks | auto | R3 |
| end-of-battle pulse | 0x46F01 | `Battle.frame * ticks % 60` | ticks | `Battle.frame % 60` | auto |
| gate swing | `gate_opening_update` 0x73145, `gate_closing_update` 0x733B0 | 0.019333 rad per update to 0.58 | per update | 0.0096667 | R5 |
| gate timer | 0x73145, 0x733B0 (`Prop.timer` +1 per update) | swing while `<= 30`, passable (flag 0x200) from 20, closes at 150, closing 30 | updates | 60, 40, 300, 60 | R4 (fighters and shots pass the gate by these) |
| burning props | `stage_prop_alternate_transition` 0x7269E and the `prop_hit_*` set | `frame_cadence * 6`, -1 per update | updates | auto | R4 |
| result screen's demo camera | `demo_battle_enter_next` 0x4A70C | 600 ticks or `defeat_frames > 60`; `rand_table_next` every update until a slot is taken | | 600, `> 121`; draw on even steps | R11 (world RNG) |
| entrance walk | `scene_intro_update` 0x8920C, `scene_root_delta` 0xAE1E7 | the scene's root track now less 2 ticks back (an AI 6), turned by the heading, as the update's velocity; phase += length / walk rate from 0.01 moved | per update | 1 tick back (an AI 3), the step's velocity; from 0.005 | R1 (by the sample's span) |
| boss defeat's wait for the ranker's line | `scene_boss_defeat_update` 0x89B39 | until the script task ends (one interpreter step an update) | updates | the script's update count x 2 ticks, counted down a step | R3 |
| rail camera re-pick | `camera_view5_rails` 0x7F5C1 (`rail_repick_clock`) | every 30 calls, one an update | updates | 60 steps (the world's view 5, `FighterCamera::rail_repick_clock`; the scene cameras' own in presentation) | R4 |
| replay record | `replay_input_record` 0x49F9A | 20 bytes per update, 18,000 | updates | per step, 36,000 | R4 |
| play time | `play_time_tick` 0xBBA18 | `+= ticks` per frame | ticks | +1 per step | auto |

### AI

The AI stays a 30 Hz system: `ai_update` 0x57BF1 runs for each AI fighter on even steps only, in slot order, with
every constant below as retail has it and with `frame_cadence` 30 and `ticks` 2 inside it. Reasons:

- It is written almost entirely in update counts (`frame_cadence * seconds`, `delay--`, `age_frames`), many of its
  choices are per-update rolls with large chances (retaliation at level 60 is 73% an update), and it uses
  `age_frames & 1`, `% 5` and `% 15` as cheap random picks. At 60 Hz every one of those would need its own conversion
  and the per-update rolls cannot be turned into per-step ones without changing the number of `rand_table` draws.
- Its aiming is deadbeat: `ai_aim_at_target` 0x5C17B sets the turn and pitch axes to (error / largest turn per
  update), with the largest turn computed as `ticks * turn_deg` and 3 degrees of pitch. Held for two steps, each
  turning half as far, the fighter ends the pair exactly on the target as in retail.
- Retail's AI reads the HUD senses and shot threats from the previous update; on an even step it reads them from the
  previous step, which is the same thing.

What the port does around it:

- On odd steps the fighter reuses the AI's last control record (axes and buttons), with the edge bits cleared:
  4 (jump press, the AI sets 2 and 4 together), 8 (camouflage), 0x400, 0x800, 0x1000, 0x2000 and 0x4000 (recentre: the
  AI sets it every update it turns hard, and retail's `actor_camera_input` restarts the 8-update recentre on each, so
  a held bit would restart it every step and decay the pitch by 15/16 twice instead of 7/8 once). Held bits stay:
  1, 2, 0x10, 0x20 and the fire bits.
- `ai_sense_update` 0x58406 clears the arm `pressed` latches; that happens on even steps only, as in retail per update.
- Unit crossings at the boundary, the only fields the AI reads whose units change: `Actor.ground_velocity_x`
  (velocity per update; `ai_route_point_needs_jump` 0x5AD35 tests `< 0.1` for being stuck) is read as 2x the step value;
  `contact_flags` (wall and step bits for `wants_jump`) is read as the OR of the pair's two steps so a contact on the
  odd step isn't lost. `turn_deg`, `camera->pitch`, positions, `motion_state`, `dodge_ready`, shot velocities (per
  tick) need nothing.
- `DamageState.pending` (who hit this fighter, `ai_find_attacker`) is cleared by every step's `actor_settle_damage`,
  so an AI on an even step would see only the odd step's hits; what it reads must be the hits of both steps since its
  last update (`fighter_hit_pending` in `src/ai/ai_links.hpp`, for the damage code to fill).
- The port's AI (`src/ai`) does all of this: `ai_fighter_input` runs `ai_update` on even steps and holds the record
  over odd ones; `contact_flags_even` keeps the even step's contact bits for the OR.
- The fighter side of the AI's actions converts as for a human: its arm raise counts 10 steps, its reloads use R12 C,
  its melee lunge and turn clamps halve.

The AI's clocks, for reference. All unchanged under the above. If the AI is ever moved to every step, the update
counts double (R4, R12 E), `frame_cadence` becomes 60 (the code then doubles them itself), the per-update rolls stay on
even steps, and the `age_frames` picks keep counting AI updates.

| value | where | retail | unit |
|---|---|---|---|
| re-evaluation | `ai_change_state` 0x587C6, `ai_update` 0x57BF1 | `reeval_timer = frame_cadence << 2` (120), -1 per update | updates |
| state length | 0x587C6 | `ai_level_timing_profiles` x `frame_cadence` / 60; 18000 ticks for every level but -99, -40, 0, 10, 50, 99 | ticks turned into updates |
| sub timers | `ai_state_target_and_weapon` 0x588B8, 0x58A63, the ranker states 0x5E2E3 .. 0x5E9EF | `frame_cadence * 2`, `* 3`, `* 5 * (addr + 0x7F)/256 + 0.5 s` (retail bug) | updates |
| jump and back-up spacing | `ai_enter` 0x577E3, `ai_sense_update` 0x58406, `ai_route_steer` 0x5A55C | `frame_cadence * 3 + 1`, `frame_cadence + 1`, +1 per update to 0x1000 | updates |
| path cache | 0x58406 | cleared when `frames_ticking > frame_cadence * 5` | updates |
| target memory | `ai_pick_target` 0x5B941 | `(level+127)^3 / 100000`, -1 per update | updates |
| retaliation | `ai_retaliate` 0x5D23D | roll `level+127` of 255 per update; memory `frame_cadence * 2 + level * frame_cadence / 10` | per update; updates |
| fire decision | `ai_weapons_update` 0x5B2E0, `ai_fire_pacing_init` 0x5B16D | `delay` -1 per update; `refire_delay` (1440 - 14 level)/54 = 26 at level 0, x (1 + 0.2 r); fire chance (level+45) x 256/125 = 92 of 256 | updates (gameplay.md calls the 26 ticks; it counts updates, 0.87 s) |
| first-shot hold | 0x5B16D, `ai_weapon_slot_add` 0x5BEA2 | `burst_delay` = `frame_cadence * (70 - level) / 70` = 30, -1 per decision | AI decisions |
| rearm | 0x5B16D | `(level+99) * frame_cadence / 198` | updates |
| laser hold | `class5_fire_hold` set in `ai_ranker_setup` 0x5ECA1, counted in `ai_weapons_update` 0x5B2E0 | `frame_cadence * 5` or `* 0.5`, -1 per update | updates |
| firing gate | `ai_want_fire` 0x5B0B9 | fewer triggers held than `(level+128)^2/10000 + 1` | none |
| camouflage watch | `ai_camo_watch_tick` 0x5C7B7 | once every `frame_cadence` updates, roll `level+1` of 99 | updates |
| camouflage use | `ai_camo_update` 0x5C9AC, `ai_camo_setup` 0x5C885 | cooldown `(level+127) * frame_cadence / 20` x jitter; hold `camo_max x fraction` counted -1 per update | updates; the hold is ticks counted as updates, so it lasts twice its nominal time in retail. Keep that |
| close and firing camouflage | `ai_can_see` 0x5D09E | rolls `level+16`, `level+31` of 99 per sighting update | per update |
| ram | `ai_special_moves` 0x5941B, `ai_ram_start` 0x59719 | roll 15 of 255 per update with a target 5..20 away | per update |
| wall jump | 0x58406 | contact bit 2: roll 64 of 255 per update | per update |
| shot dodging | `ai_dodge_shots` 0x5D4B0 | `2 * level * (distance / 5)` of 255 per threat per update; beams only when `age_frames % 15 == 0`; `dodge_cooldown` -1 per update | per update; updates |
| band moves | `ai_evade_buttons` 0x5CD5D | band dodge, jump, button-1 rolls of 255 per update (0 for quest opponents) | per update |
| ranker enrage | `ai_ranker_update` 0x5DD8F | `age_frames > frame_cadence * 60 * 3` (3 minutes) with a target; `ranker_timer` `frame_cadence << 3` and others | updates |
| ranker strafe | `ai_ranker_attack_update` 0x5E6A7, `ai_ranker_strafe` 0x5E95B | `attack_frames` against `(frame_cadence << 1) * 0.3`, `* 0.8`; `strafe_chance` per update; `age_frames & 1` sides | updates |
| route waits | `ai_route_follow_step` 0x58B4A, `ai_route_steer` 0x5A55C, `ai_route_unstick` 0x5EB00 | `route_wait` against `frame_cadence`, modulo `frame_cadence << 2`; stuck after `frame_cadence << 2` | updates |
| pickup chase | `ai_fetch_pickup` 0x5999D | gives up after `frame_cadence * 5` | updates |
| player-hit tracking | `ai_track_player_hits` 0x5EE93, 0x5E6A7 | `frames_since_player_hit > frame_cadence * 3.5` | updates |
| camouflage reaction | `ai_camo_reaction_chance` 0x5F422 | only when `age_frames % (frame_cadence / 2) == 0` | updates |
| picks | `ai_state_combat_maneuver` 0x58CE9, `ai_ranker_mode_aim` 0x5E0D7, `ai_ranker_strafe` 0x5E95B and others | `age_frames % 5`, `age_frames & 1` | updates |
| stick scale | `ai_update` 0x57BF1 | axes x clamp((level+128)/99, 0.5, 1) | none |
| melee lunge cap, homing turn | `chassis_apply_performance` 0x759B6, `melee_home_step` 0x911C5 | run x 1.4 (x 2 for humans), 0.137445 rad | fighter side, converted in the melee table |

### Presentation, listed so nobody converts them as gameplay

None of these touch the fight; they are here so the port knows which way to take them. Run them per step with the
conversion given, or per display frame scaled by elapsed time.

| value | where | retail | 60 Hz |
|---|---|---|---|
| exhaust smoke | `actor_effects_update` 0x796BF | every 3rd update while running (`age_frames % 3`) | every 6th step |
| wheel spin | `actor_wheel_spin_update` 0x8893F | -distance / pi, held within 3.1677728; under 0.01 moved the turn stick adds 0.2 x |stick|; in the air the forward stick | auto for the distance; the cap, the 0.01 and the 0.2 and the air stick halve (R1). Done in the step (`src/mech/pose_weapons.cpp`) |
| body set phases | `actor_phase_fall_update` 0x91489, `actor_phase_start_stop_update` 0x91669, `actor_phase_run_update` 0x91794 | `ticks`, `0.5 * ticks`, wrap -30 | auto, in the step. Two don't come out the same: the run loop's wrap takes a step, not an update (a 31-tick cycle for 32), and a run's start or stop event lasts a step, so it pushes the start/stop clip half as far before it runs on. Both only move the body set's flagged nodes (fins, vents) |
| weapon open/close animation | `weapon_anim_update` 0x9B08B | `anim_turn += anim_rate` to 90 per update, players in ticks | `anim_rate / 2` |
| trails | `trail_push_default` 0x70425 | 33 points per update | push on even steps, or 66 points |
| particles, casings, lock-mark wobble | `effect.h`, `weapon_eject_casing` 0x998EE, `lock_mark_start` 0x52410 | CRT `rand`, per update | any rate; keep off the world RNG |
| the effects (src/effects): the particle pool, sprite effects, scorch marks, trails, decals, the static's scratches, the flare's fade, point lights, a fighter's emitters | decomp docs/port/effects.md | per 30 Hz update: curve key times, drag, spin, gust and paper constants, sprite frames, trail and decal lives | stepped as retail, once every second world step (after each even step), drawn between the last two updates; their own CRT-style generator per emitter |
| HUD bob | `hud_update` 0x4E62E | `bob += clamped delta` | per step |
| render-pass scrolls | 0xB26F0, 0xB65E0 | per frame | per step (architecture.md, "What ticks per frame") |

## Flagged

Each of these is a place where halving and quartering the numbers does not play the same. What goes wrong, what the
port does, and how to see it against retail. "Recording" means a capture of the original under xemu (the decomp's
runtime harness, `docs/findings.md` "Observed at runtime"): retail draws one frame per update, so frame n of a
recording is update n, and the port's capture is compared at every second step.

### Jump

Retail, per update, in `actor_step` 0x756F3 order: `actor_frame_reset` 0x88A5C zeroes `jump_step`; `actor_try_jump`
0x8D77A starts state 4 with phase 0; `sv_motion_state_4` 0x8E9DB adds `ticks` to the phase and lifts off at 10, setting
`vy = max(takeoff.y, 0) + jump_speed`, but takes its "airborne" flag before that, so no air control runs on the lift-off
update; later updates run `actor_air_control` 0x9052F (thrust ease `vy = 0.9 vy + 0.1 jump_speed`, `jump_step = g`
cancelling gravity while `jump_thrust_left` lasts, the last partial update getting `g - left`, the dive's `-6 g`);
then `sv_motion_integrate` 0x7D097 does `vy -= g; p += v`.

The vertical part of that, simulated on a flat floor at 30 Hz and at 60 Hz with four treatments, over jump heights
2.5 to 100 and button holds from a tap to the whole jump:

- re-derived: `jump_speed`, `jump_thrust` computed from the formula with g60, 0.9/0.1 as they are, no correction;
- halved: retail `jump_speed` and `jump_thrust` halved, g60, the ease as 0.948683/0.051317, no correction;
- halved + R10: the same plus `p += v + dv/2`;
- recipe: below.

| leg jump height, hold | retail apex, air time | re-derived | halved | recipe |
|---|---|---|---|---|
| 2.5, tap | 0.477, 22 ticks | -8.3% | +8.6% | 0.00% |
| 2.5, held 20 ticks | 1.306, 42 | -4.2% | +5.1% | +0.09% |
| 2.5, held | 2.670, 66 | -1.9% | -0.1% | +0.06% |
| 16.4, tap | 2.814, 56 | -3.4% | +3.6% | +0.07% |
| 16.4, held 20 ticks | 4.893, 76 | -2.7% | +1.6% | 0.00% |
| 16.4, held | 16.316, 164 | +2.2%, +5 ticks | +1.5%, +3 ticks | +0.07% |
| 100, tap | 16.469, 138 | -1.4% | +1.5% | 0.00% |
| 100, held | 101.251, 418 | -0.5% | -0.6% | +0.01% |

Over the whole sweep the recipe stays within 0.46% of retail's apex (the worst cases are small taps, where the 60 Hz
step samples the arc between two retail samples near the top; the positions at the end of every pair agree) and
within 2 ticks of retail's air time (a landing right on a pair boundary can tip to the next pair). Halved + R10 alone
is exact for taps but up to 2% off held jumps, from the pieces below.

The recipe, per fighter:

```
// constants: g60 = g30 / 4, js = jump_speed30 / 2, thrust budget kept in retail units
// crouch: lift off when phase >= 9 (retail 10, R12 A); the lift-off step and the next step are the
//         retail lift-off update: no air control on either
// pairs are counted from the lift-off step; air control runs from the second step after it,
// the first step of the next pair
air_control_step:
    // the second step thrusts when the pair's first step did (not when the budget ran out
    // before the pair: then neither step does, as retail's update doesn't)
    if holding jump and not cut and (first step of a pair ? thrust_left30 > 0 : pair thrust):
        vy = 0.948683 * vy + 0.051317 * js
        if first step of the pair:            // charge the retail way, once per pair
            if g30 < thrust_left30: step30 = g30;  thrust_left30 -= g30
            else:                   step30 = g30 - thrust_left30; thrust_left30 = 0   // retail bug, kept
        jump_step = step30 / 4                // the same on both steps of the pair
    if dive_used: jump_step = -6 * g60
    vy += jump_step
    ... steering, air push, drag (rows above)
integrate:
    dv = (vy after air control - vy before it) - g60      // forces only
    vy -= g60
    p.y += vy + dv / 2                                     // R10, horizontal the same with drag in dv
```

Why each piece: R10 fixes the semi-implicit Euler drift, which is what makes a halved tap 1.5..9% high. The crouch of
9 and the skipped air-control step put the 60 Hz lift-off on the first step of the pair that stands for retail's
lift-off update. Without the skip, air control starts half an update early with half of retail's gravity deficit in
the ease, and held jumps come out up to 1.7% high; with the skip but a crouch of 10 the whole arc runs a tick late and a
release at a given moment cuts half an update of thrust, 2..6% low on a 20-tick hold. The pair budget keeps retail's
`g - left` remainder: charged per step, the remainder falls on a different fraction and held jumps come out 0.5..3%
off. The dive needs only g60 and R10.

Check by eye: one leg, flat stage, a tap jump and a held jump standing still, recorded in retail and in the port.
Count frames from the press to the frame the feet leave the ground (retail lifts off on the 5th update counting the
press), to the top, and to touchdown; read the apex against a wall marking. The read code gives a 16.4 leg held a rise
of 16.3 units and 164 ticks (82 updates) of air on flat ground; `docs/findings.md` has a runtime note of about 18.9
for the player's 16.4 held, measured some other way. Measure retail first and match the port to the recording, not to
either number. The port's positions at every second step should agree to a frame.

### Integration

`sv_motion_integrate` 0x7D097 and the straight and lob shot movers do `v += force; p += v`; the guided movers and
debris do it the other way round (R10). At 60 Hz with forces quartered and velocities halved, each pair is off by
`dv/2` (gravity: a jump floats a little; a straight shot's acceleration: it falls a little short; a guided missile's:
it flies a little long). R10 (`p += v + dv/2`, `dv` the step's force) puts every pair exactly on retail's position for
constant forces (gravity, the dive, a shot's `accel`, lob gravity) and within 1e-5 per update for drag.

`dv` is forces only. Ground motion is rebuilt every update (`sv_motion_state_0_1` 0x8E636 and the others assign
x and z; `sv_contact_route` 0x7C740 zeroes the velocity on any floor contact), lift-off and the dodge impulse assign
the velocity, the floor projection in `sv_motion_dispatch` 0x88B7E rotates it: none of those go in `dv`. Air
control's ease, steering, push and drag do, as do gravity, the dive and the air-dodge 0.9.

Caps: retail caps the velocity, the push and the impulse at length 5 after gravity and moves by the capped values.
The port caps at 2.5 and caps `v + dv/2` as well, so a capped fall (the dive reaches the cap in under a second) moves
2.5 a step, 5 a pair, as retail.

Surface bit 0x40000 (`sv_contact_route`: the fighter stays airborne, this update's gravity is put back into the
velocity but the position has already moved by it): retail sinks `g30` per update at constant velocity. At 60 Hz the
same code sinks `g60` per step, half as fast. Move by `v - 2 * g60` per step on such contact and count
`airborne_time` down by 0.5 instead of 1. Traced 2026-11-11: `collision_surface_bits` 0x2056F8 gives types 0..10
the bits 1, 0x40, 0x80, 0x100 .. 0x8000 (one each), and the four stage grids use types 1, 2, 4, 5, 7, 8, 9 (and
1001 .. 1009, the same mod 1000), so no shipped surface sets 0x40000 and the port leaves the branch out. Type 8's
0x2000 only spawns a contact decal.

Check by eye: drop off a high ledge without jumping and count frames to the ground; fire a weapon with acceleration
at a wall a known distance away and count frames to impact.

### Air push

`actor_air_control` 0x9052F adds `stick * 1/256 * ticks` to the velocity every update. That is an acceleration (a
velocity change per update), so its 60 Hz value is a quarter of the per-update one, `stick * 0.5/256` per step; the
`ticks` factor alone only halves it and doubles the push in the air. Same for any other `ticks * x` that is added to a
velocity which carries over to the next update. The only other ones in battle code are the shot accelerations, and
there the velocity is per tick, so `ticks` is right (R9).

Check by eye: a standing jump holding forward from lift-off; compare the landing distance.

### Landing

`actor_land` 0x90AFD sets the recovery clip's start from `-(velocity_prev.y + sqrt(4 r g30)) * 2`. The port passes
`2 * velocity_prev.y` (per-update units) and g30. The landing velocity at 60 Hz is sampled up to half an update
earlier than retail's, a difference of at most `g30/2` against fall speeds of 0.1..0.5: a recovery a tick or two
shorter or longer on hard landings. Accept.

Check by eye: frames from touchdown to the first walk frame after a high fall.

### Decays

The skid (`actor_slide_step` 0x90CA1: `skid *= leg_bc`, then added) and the dodge slide (`sv_motion_state_6` 0x8EEF9:
`impulse *= 0.99`, then used) multiply and then move. At 60 Hz with `sqrt(f)` per step a pair moves `(1 + 1/r)/2` times
retail's update: 0.25% long for the dodge, 6% long for a skid with `leg_bc` 0.8. Starting the halved value at
`2r/(1+r)` of itself makes every pair exact; the stop threshold scales the same way. `leg_bc` is leg data (+0xBC);
check its range when the leg tables are converted, the lower it is the more this matters.

The ground dodge's ease-out uses a factor linear in the clip phase. Retail samples it at even ticks; a pair at 60 Hz
samples it at one odd and one even tick and averages half a tick early, 0.025 x impulse more per update. Evaluating it
at `phase + 0.5` fixes that exactly.

Check by eye: a full-speed run let go on flat ground, distance from release to stop; a dodge from standing, distance
to the end of the slide.

### Knockback

`damage_impact_result` 0x98861 adds the knockback to `DamageState.impulse`; the next update moves the fighter by the
impulse (capped at 5) and `actor_settle_damage` 0x766DE then multiplies it by 0.8. Total travel is 5x the hit. At 60 Hz
with half the hit and `sqrt(0.8)` per step the total is 4.74x, 5% short. Scaling the hit by `1/(1 + sqrt 0.8)`
(0.527864 of the retail impulse, not 0.5) and the cap to 2.63932 makes every pair match. Several hits in one update
stack as in retail.

Check by eye: a heavy-knockback weapon on a standing target; distance pushed.

### Reload

`weapon_fire_controller` 0x9B46D: a shot sets `reload_left = R` and `reload_left -= ticks` runs only on updates that
don't fire, so the gun fires every `ceil(R/2) + 1` updates. At 60 Hz with R as is, every `ceil(R) + 1` steps:

| R (ticks) | retail | R at 60 Hz | `2 ceil(R/2) + 1` |
|---|---|---|---|
| 0 | 30 shots a second | 60 a second | 1: 30 a second |
| 2 | 15 | 20 (+33%) | 3: 15 |
| 4 | 10 | 12 (+20%) | 5: 10 |
| 5 | 7.5 | 10 (+33%) | 7: 7.5 |
| 6 | 7.5 | 8.6 (+14%) | 7: 7.5 |
| 18 | 3 | 3.16 (+5%) | 19: 3 |

The fast guns are the ones it hits hardest. Set `reload_left = 2 * ceil(R / 2) + 1` on firing and keep the rest as
retail (count down 1 on steps that don't fire). R comes from the tuned weapon curve and can be fractional; the formula
covers that.

Check by eye: hold the trigger of the fastest arm gun for two seconds against a wall and count shots (or ammo used) in
both. Also a back weapon with a large R.

### Beams

`weapon_laser_controller` 0x9B97E keeps `reload_left` as the beam's intensity: up 2 per firing update to 8, down 2
after release, and the beam lives while it is above 0, three updates (6 ticks) after release. At 60 Hz stepping by 1
it would live 7 ticks; remove the beam when the intensity is 1 or less after the decrement and it lives 6 ticks after
any release, from a tap or a full beam. The beam damages on even steps (next section), so its lingering updates deal
the same three hits.

Recharge: `weapon_restore_resource` 0x97705 adds `resource_max / recharge_frames` per update; half per step. A laser
starts only from an exactly full gauge, so the add must clamp to the maximum as retail does (it does; fixed point
makes this exact).

Check by eye: Javelin (150 gauge) fires 75 updates (2.5 s) and refills in `recharge_frames` updates; time both.

### Contact damage

Three things in retail damage every update they touch, with no memory of who they already hit: a beam
(`shot_hit_fighters` 0x9C942 and `impact_volume_scan` 0x9D003 every pass of `battle_shots_update` 0x47E59), a
lingering blast (impact kind 2, shot state 3, `impact_damage` each pass for `impact_frames` ticks), and a melee blade
(`melee_active` is set again every update of the swing's active part by `sv_motion_state_9` 0x8FB9E, so a blade that
stays inside a fighter hits again). Each hit also rolls shield, crit and break from `rand_table`.

Run per step, all three deal double damage per second and draw double. They run on even steps (R11), with their
lifetimes in R12 B (`2 * ceil(L/2)`, so a run of `L'` steps holds exactly `L'/2` even steps whatever parity it starts
on). The beam sweep uses the tip from two steps ago so it covers the 30 Hz triangle. The melee blade's position is
updated every step for drawing; the hit test reads it on even steps.

Whether blasts and blades really hit every update in retail is still open in the decomp (gameplay.md, "Open"). The
even-step rule copies whatever retail does; if a runtime check shows a per-target memory, it applies on top.

Check by eye: stand in a lingering blast and read the durability lost; hold a beam on a target for a counted number
of frames and compare durability lost per second.

### Inherited velocity

At launch every shot adds the firer's velocity, a per-update value, to its own per-tick velocity
(`projectile_update_straight` 0x9781E, `projectile_update_rotating` 0x97A49, `projectile_update_guided` 0x97CC2). For
straight shots the next rebuild drops it before the first move, so nothing changes. For lobs it stays for the whole
flight and counts double; for guided shots it lasts one move. At 60 Hz the firer's velocity per step is half, so
adding it as is makes lobs inherit half of what they inherit in retail. Add `2 * owner->velocity`.

Check by eye: fire a lob running sideways at full speed and compare where it lands relative to a standing shot.

### Lobs

`projectile_update_rotating_next` 0x97BB0 takes `9.8/900 x 2.5` off a per-tick velocity once per update and moves
`2 * vel`. Per step that is `0.0136111` off and a move of `vel - 0.0068056` (R10). Without R10 a lob lands higher and
longer by half an update's drop per update of flight; with the plain per-update constant at 60 Hz it falls twice as
fast. Debris (`debris_update` 0x99798) is the same shape with 9.8/900 but in the explicit order (position first), so
its correction has the other sign (R10); it is cosmetic. Guided missiles also move first and accelerate after; a
missile with a long acceleration ramp flies `accel/2` per update too far without the explicit-order correction.

Check by eye: a lob fired level on flat ground, distance and flight frames.

### Reach

`weapon_reach_update` 0x9D3AF works out a lob's range from `g30` with a literal 2 for ticks, and the AI decides what
is in range from it. It is a distance and must come out the same; compute it with the retail constants (or g60 and
1, which agree), never g60 with the literal 2.

### Collision

The sweeps in `sv_contact_route` 0x7C740 work on this step's segment, half as long at 60 Hz, along the same path: the
throwback test against walls and props, the floor height reaching down by this step's speed, the shot and beam
segments. Two thresholds don't scale: `collision_segment_slide` runs only when the fighter moved a radius or more in
the step, which at 60 Hz needs twice the speed (only very fast movement used it anyway; the sphere query resolves the
rest), and the floor contact needing normal y over 0.75 is a shape test. Fighters can't tunnel more at 60 Hz than at
30. Leave as is; if a fighter scrapes along walls differently at top speed, the slide threshold is the place to look
(retail: moved >= radius per update; 60 Hz equivalent: >= radius/2 per step).

### Cosmetic draws from the world RNG

Retail draws `rand_table` values in code that only draws: `stage_props_render` 0x6F5ED (5 per update while the hit
static runs, 15 updates after a human at view 0 is hit), `hit_shake_jitter` 0x7E764 (3 per update for 15 updates),
`demo_battle_enter_next` 0x4A70C (the result screen's camera). The port keeps them in the step (AGENTS.md) and runs them
on even steps with their update counts as they are, so the gameplay rolls after them see the same table index as
retail. `stage_props_render` is the stage task's update, after the weapon tasks, and also runs each prop's callback (gates),
`supply_craft_update` 0x670FC and `pickups_draw` 0x69CD2: those are simulation and run every step with the converted
values; the static's 5 draws and the shake's 3 (only for a human in view 0) follow them in the same function, after every
fighter.

The shake's draws are gameplay too: `camera_view0_cockpit` moves the target by them and `camera_eye_update` tilts a human's
view-0 up by them, so the senses, the lock boxes and the tracking point see them (src/battle/camera_views.cpp,
`FighterCamera::shake_shift` and `up`). Retail's fighters read update N's draws in update N + 1; the port's draws on
even step 2k hold through steps 2k + 1 and 2k + 2, which both read them, so a hit settled in a pair reaches the look-at
for the next 15 pairs as retail's 15 updates. Nothing converts: the offsets are lengths, not rates.

### Odd and even updates, every Nth update

Retail's fight logic doesn't test update parity outside the AI. The AI's uses (`age_frames & 1`, `% 5`, `% 15`,
`% (frame_cadence / 2)`) stay as they are because the AI stays at 30 Hz. `Battle.frame * ticks % 60` and `% 120` drive
HUD pulses only (use `Battle.frame % 60` in steps). The exhaust smoke every 3rd update is cosmetic.

### Input

Retail reads the pad once per update, so a press and release inside 33 ms is lost and a held button is seen at 30 Hz.
The port reads per step: presses register up to a tick earlier and can't be lost. Everything downstream is per step:
edges in `sv_control_route` 0x7742D, the trigger states in `actor_triggers_update` 0x7645E, the double-flick window
(10 steps). The jump's hold time is read per step, so a release lands one tick finer than retail could; at the end of a
pair the arc agrees with retail's for any release retail could make.

### Thresholds

One-shot timings that only start or end an animation phase (clip loop points, fade states, scene timers) can come out
a tick early or late at 60 Hz. That is the same size as the input sampling difference; leave them at R3. The ones where
the length of the window is the game, and that the tables give exact values for: dodge intangibility (12.5 a step at a
time), the dodge's slide start (2), the crouch
(9), walk to run (31), reloads (`2 ceil(R/2) + 1`), shot and blast lives and lock times (`2 ceil(L/2)`), stagger
(`2 ceil(S/2)`), beam linger (removed at 1), arm raise (10), the double flick (10), dead AIs (`> 241`).

## Checking against the original

The plan's open item says "plays the same" is checked by eye against xemu recordings, not by a test suite. The
measurements that cover the flagged items, each a short scene that can be set up in free play or the test drive with
the same build in both:

1. Standing jump, tap and held, and a held jump released after 20 ticks: frames to lift-off, apex height, air time.
2. Walk off a high ledge: frames to the ground. A dive from the top of a held jump: frames to the ground.
3. Run at full speed for 60 updates: distance. Let go: skid distance and frames to standstill.
4. Hold the stick past 0.7 without the dash button: frames until the run starts (retail 16 updates after the stick
   passes 0.7 while walking).
5. Dodge from standing and from a run: slide distance; fire a straight gun at the dodging fighter from close range
   and count which frames pass through.
6. Fastest arm gun held for 2 s: shots fired. A missile volley: frames between missiles (5 updates). Arm raise: frames
   from press to the first arm shot (5 updates).
7. A lock-on weapon: frames from the target entering the box to the lock.
8. Laser held on a target: durability lost per second, gauge empty time, refill time, linger after release.
9. Lingering blast and a prop explosion on a standing fighter: durability lost.
10. Knockback from a heavy weapon: distance pushed. Stagger from it: frames until the victim can move.
11. A lob, standing and while strafing at full speed: landing point and flight frames.
12. Camera: from facing away, turn 180 degrees at full stick and count frames until the reticle settles; view 1
    chase lag while running past a pillar.
13. Camouflage: frames from the button to fully hidden, how long it lasts, how long until it can be used again.
14. Supply craft: frames between crate drops (50 updates), crate lifetime on the ground (901 updates), hatch open (27).
15. An AI fight with a fixed seed: in retail the replay is a resimulation, so a seed and the human's inputs
    reproduce a fight. With the RNG kept draw for draw (R11, the AI at 30 Hz), an idle human in front of a level 0
    AI should see the same AI decisions in the port for as long as no 60 Hz difference (a hit a tick earlier) has
    changed the fight; the first frame they part says what changed.

Where a measurement differs by more than a frame at a pair boundary, the row for that value is the first place to
look, then the rule it cites.

### Measured in the port against the read code

2026-11-11, the port's fighter step (`src/mech/fighter_*.cpp`) driven by `--script` with `--trace 1`, against a
30 Hz float simulation of the same decomp functions (no xemu yet). The Plain Holy (build 0: jump height 16.049, leg
0, root-motion walk and run), on Shinjuku and the flat test arena (`--stage 6`); build 42 (HL4Roller) for a
constant-speed skid. Scripts and the comparison scripts were scratch files; the port's numbers repeat with the same
script.

| scene | retail (read) | port |
|---|---|---|
| standing jump, tap | apex 2.757, lift-off tick 8, 56 ticks of air | 2.757, tick 8, 56 |
| standing jump, 20 ticks held | 4.812, 8, 76 | 4.811, 8, 76 |
| standing jump, held | 16.273, 8, 166 | 16.269 (-0.02%), 8, 166 |
| skid from a full run, leg_bc 0.94 / 0.875 (after the let-go step) | 15.002 / 5.094 | 14.995 / 5.098 (-0.05%, +0.08%) |
| ground dodge from standing, Plain Holy | 8.820, 28 ticks to idle | 8.767, 28 ticks |
| walk (stick 0.8 from standing) to run | 34 ticks | 33 ticks (the row above says why) |

The let-go step itself moves half of retail's let-go update: the port reads the release a tick earlier, which is
the input sampling difference, not a conversion.

Measured again 2026-11-11 with the pose in the step (`src/mech/pose_walk.cpp`; attachment 13 and the camera's head
live from it instead of measured once on the rest pose), the same scripts (`captures/movement/*.txt` and scratch
tap, 20-tick, dodge and walk-to-run scripts) with `--trace 1`, against the same runs before the change: every trace
is the same to the last printed digit (the jumps, both skids, the dodge, walk to run, Shinjuku with builds 0, 37 and
72), so the table holds. The one that moves is a held jump straight up from the test arena's first entry point,
under an overhang: in the air the jump pose lifts attachment 13 from 3.27 to 5.06 above the position, so the upper
sphere meets the overhang at a height of 5.14 instead of 6.74, as retail's would.

Again after merging the pose with the weapons (2026-11-11, the triggers raising the arms, `launch_pitch` from the
pose): `captures/movement/legs.txt`, `jump_held.txt` and `walk_run_jump.txt` with builds 0, 37 and 72, and the held
jump on the test arena, print the same movement trace as the pose branch did, line for line.

Again when humans took their button config's control type (2026-11-11, the default 3, Standard and View Change
Normal, instead of 0): `jump_held.txt`, `walk_run_jump.txt` (builds 0, 37, 72) and the held jump on the test arena
print the same trace line for line. `legs.txt` steers with the left stick's x, which under Standard is the strafe, so it
now slides sideways from step 91; with the turn moved to the right stick (Standard's turn) it prints the old trace line
for line for all three builds (docs/input.md, "Control type"). Under Standard a script turns with `rx`.

Camouflage and crates in the step (2026-11-11): the movement scripts above (`jump_held.txt`, `legs.txt`,
`walk_run_jump.txt` with builds 0, 37 and 72, and the held jump on the test arena) print the same trace line for line
as before, nobody cloaking or collecting. A cloak of the Plain Holy's OCG V B (1200 ticks, recharge 205), pressed on
step 1201: fading in to 1259, the hold to 1279, hidden until 2338 (1138 ticks, retail's 569 updates), faded out at
2399, recharging from 2420, ready again by 2604. On the test arena an AI's missile lock on the player, held from step 874
while it stood, is gone on step 882 when the player cloaks on 880.

The online groundwork (2026-11-12: the whole-world hash, the stage's fixed arrays, the events' steps, the fights built by
`viewer_world.cpp` and `battle_world.cpp`; docs/multiplayer.md "Status"): the movement scripts above with builds 0, 37
and 72, the held jump on the test arena, and two traced fights (`--trace 5 --trace-ai 5`: an AI-only `--demo` of six
on Shinjuku, 5400 steps; build 37 on `walk_run_jump.txt` against five AIs at level 20 on Shibuya by night, 4000 steps)
print the same as before line for line, and the front end's AI tournament fight (`--state 6`, 7000 steps) loads the
same replacements in the same order.

The presentation sweep of 2026-11-12 (the cockpit instruments, the ending's view 0 and carried machine, the weapon
clip's opening event, the stands' parts, the sticker, the dissolves, split-screen static): `jump_held.txt`, `legs.txt`
and `walk_run_jump.txt` with builds 0, 37 and 72 and `--trace 1` print the same trace line for line as before it.

The in-fight views (2026-11-12: the d-pad's view switch and views 2..5 in the world, `event_hit_rumble` in the settle;
src/battle/camera_views.cpp): `jump_held.txt`, `legs.txt` and `walk_run_jump.txt` with builds 0, 37 and 72 and
`--trace 1` print the same trace line for line as main before it (9e388ee), and as c2ebb26 before the merge.

The single-player polish (2026-11-12: the big screens' scene texture, the lamps by zone, the HUD's gate, `--route`'s
`every:`): `jump_held.txt`, `legs.txt` and `walk_run_jump.txt` with builds 0, 37 and 72 and `--trace 1` print the same
trace line for line as main e2fca17 before it.

The hit shake in the world and the fighters' sounds (2026-11-12): the same nine traces print the same step lines as main
ec235a1 (main's own logs lose about 1% of their lines to the audio thread's log lines splitting them; this build's
lose none). Build 37 in view 0 against three AIs with `PHANTOMCRASH_HURT=200:0:500,700:0:500`: the trace matches main's
to step 201 and differs for exactly 30 steps after each hit (steps 202..231 and 702..731: the player's tracking point
and the AIs' senses), then matches again. `pc_sim --rollback 50` on a six-fighter demo: 0 of 30 differ.

Genuine widescreen (2026-11-12: anchored 2D, extended backgrounds, the screens' 3D full width, the flare over the
drawn 3D; docs/screens.md "Widescreen"), merged with main 0eac0a1: `jump_held.txt`, `legs.txt` and `walk_run_jump.txt`
with builds 0, 37 and 72 and `--trace 1` print the same step lines as main ec235a1 before it (lines cut by the old
interleaved `audio:` and `run:` fragments compared up to the cut).

The feet's landing sets on footsteps and the prop sets' whole texture sets (2026-11-12, presentation only):
`jump_held.txt`, `legs.txt` and `walk_run_jump.txt` with builds 0, 37 and 72 and `--trace 1` print the same step lines as
eec732d before it.

The scenes' walk clips (2026-11-12): the gate walk, the door's and the ending's advance their clip by the distance over
`actor_leg_walk_speed` 0x76289, the leg class's root motion walk column (`Fighter::walk_scale`, units a clip tick), which
the port had read as `walk_rate`. A step moves half an update's distance, so the division gives half an update's advance
with no other conversion; the fallbacks stay retail's halved (0.5 a step at the gate and the door, 1 in the ending). The
muzzle event carries its barrel (`CombatEvent::effect`; the flashes, presentation). `jump_held.txt`, `legs.txt` and
`walk_run_jump.txt` with builds 0, 37 and 72 and `--trace 1` print the same step lines as a87035a before it (none runs a
scene). Build 0's entrance (`--intro`, a standing script): the same positions step for step, the walk clip's phase about
twice as fast (37.95 at step 300 against 18.41: 0.215 a step, the 0.072 units a step over 0.333), and the same lines again
from step 374, after the walk.

The screens' leftovers (2026-11-12: the screen adjust's 3D viewports, the spec page's places, the records' trophy pass,
the dodge lesson's count by `Fighter::motion_slot_prev`, a field the world hashes): `jump_held.txt`, `legs.txt` and
`walk_run_jump.txt` with builds 0, 37 and 72 and `--trace 1` print the same step lines as 3c3b402 before it. The dodge
lesson counts clip changes out of a dodge (`motion_set_animation` writes the slot it replaces, `actor_frame_reset`
clears it each step), at most one a step as retail's one an update, so the count needs no conversion.

The checks lines settled from the decomp (2026-11-12: the dying human's view 0 roll, the trails per split-screen view,
VS's end menu auto-repeat in the world, VS setup's rolling names and the area plate from the data): `jump_held.txt`,
`legs.txt` and `walk_run_jump.txt` with builds 0, 37 and 72 and `--trace 1` print the same step lines as a build of
3c3b402 before it (`captures/vs_checks/traces_base`, `traces_new`); the only world change is `VsBattle`'s menu pads,
read in VS alone. `pc_sim --rollback 50` on a six-fighter demo: 0 of 30 differ. Headless VS with
`PHANTOMCRASH_VS_KO=100:1` and pad 0's down held 100 frames: the end menu's cursor runs to EXIT and stops there
(`endmenu_held_down.png`; 3c3b402 stopped at SELECT AREA, `endmenu_held_down_base.png`).

### Weapons as the port runs them

`src/weapons` (2026-11-11), in `step_world`'s order: each fighter's step (triggers after the camera, the pose at
its end with `actor_back_weapons_place`, then `actor_settle_damage`), the shot pass, then per slot its HUD and its
four weapon tasks. What the conversions above come to in code:

- Every step: the triggers (arm raise 0..10), the fire controller (reload `2 ceil(R/2) + 1`, counted on idle steps;
  volley gap 10 at 1 a step), the laser (drain 1, intensity 1 a step to 8, removed at 1 after a release,
  recharge `max / (2 x recharge_frames)`), the melee swing and its blade's position, every projectile's move and
  hit test, the beam's stage and fighter cut, the senses and the tracking point, the threat lists.
- Even steps only, with retail's numbers: beam damage and its sweep (from the tip of the last even step), blade
  hits (from the blade's position on the last even step, so the knockback and rear test see the pair's motion),
  lingering blasts whole (radius, damage and `life -= 2`), the missile and melee locks (`timer -= 2`).
- Shot life `2 ceil(L/2)` steps; projectiles inherit `2 x` the firer's per-step velocity; lob gravity 0.0136111 a
  step with the move `v - 0.0068056`; straight shots accelerate and move `v + dv/2`; rockets move `v - dv/2` and then
  accelerate (the guided turn per step is `turn_rate`, auto). Knockback `x 0.527864`, the impulse `x 0.894427` a
  step; stagger `2 ceil(S/2)`; shield immunity 30 counted 1 a step; `defeat_frames` counts steps (`> 241`).
- The muzzles are the pose's (`fighter_muzzle`, since the pose merge): the weapon block's muzzle point `Weapon.muzzle`
  under its node of the weapon model the step placed, turned pi about y; the barrel steps on every call that asks.
  The arms point where the pose raised them (toward last step's reticle points, by `raise / 10`); the backs are
  pitched by `launch_pitch = spread_a + spread_b + elevation` with `spread_b` from the pose's body node, and lock
  rules 0 and 1 then re-aim at this step's point. Nothing here converts: the pose is built every step from the
  step's state.
- The screen tests use retail's projection (52 degrees vertical over 576 x 448) on the world's chase camera.

Measured with `--script`, `--trace` and `--dummy 0` on Shinjuku, Plain Holy against Plain Holy:

| weapon (tune 0) | retail (weapons.md) | port |
|---|---|---|
| Pistol/HM15, reload 18 | a shot every 10 updates, 450 a hit, 1350 from behind | every 20 steps, 450, 1350 (`--dummy-away`); the first shot 10 steps after the press |
| M2021 Gatling, reload 2, 2 pellets | a pull every 2 updates (15 a second), 4 draws a pull | every 3 steps; 4 draws a pull plus the rolls |
| L50/Javelin, gauge 150, recharge 150 | 75 updates lit, 150 to refill, 450 a pass | 150 steps lit, 300 to refill, 450 on even steps |
| M-24L/1 Aspide | lock, then a guided missile; 950; a kind-2 blast of 0 frames | locked in `ceil(L/2)` updates, guided hit 950, no blast damage |
| G24 Slingshot | 400 direct, then 120 a pass for 8 passes | 400, then 120 on 8 even steps |
| Penetrate Esturk (melee) | 1450, x3 on a frontal lunge (kept) | 4350 a hit on consecutive even steps of the swing while inside |
