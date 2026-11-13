# Numbers

The world's numbers: what type each is, how far the original's values reach, where Q16.16 holds, where it
needs 64-bit help and where it falls short. The code is `src/fx/` (`fx.hpp` scalars, `angle.hpp`, `vec.hpp`,
`mat.hpp`, `hit.hpp`/`hit.cpp`), built as the `phantomcrash_fx` library by `cmake/fx.cmake`. The trig tables
are written by `tools/fx_trig_tables.py`; `tools/fx_accuracy.cpp` measures everything below against doubles.

## Types

- `Fx`: gfxp's `Fixed16`, Q16.16 in an int32. Range +-32768, step 1/65536 (1.5e-5).
- Q32: the raw product of two `Fx` kept whole in an int64 (`dot_q32`, `length_q32`, `distance_q32`,
  `cross_q32`). Exact while every vector part is under 16384 units.
- `Angle`: a struct around a uint32, a turn is 2^32 units (1.5e-9 rad each). It wraps by itself, the way the
  original wraps its float radians to +-pi by hand. Its own type so a 16-bit angle or an `Fx` in radians can't
  slip in where one is meant.
- `FxVec2` (x, z: the floor plane), `FxVec3`, `FxMat3`, `FxMat4` (affine), `FxTri` (a collision triangle).
- Plain `int` for counts, timers in ticks, money, score, destruction values, durability where it's whole. Never
  `Fx` for money: 99,999,999 is far past 32768.

Floats appear only in two places: `fx_f(0.985f)` and `angle_f_radians`/`angle_f_degrees` are `consteval`, so
the float exists only inside the compiler; and `fx_bits` turns an IEEE single's bits into `Fx` with integer
operations only, for the loaders reading the disc's and the XBE's floats. Both round to nearest with halves
away from zero, so `fx_f(x) == fx_bits(bits of x)` for every float `x`.

## Conventions

- Matrices are D3DX's, as the original's code uses them: a point is a row on the left (p' = p * M), the rows
  are where the x, y and z axes go, `FxMat4`'s fourth row is the translation, `mat4_mul(a, b)` applies a
  first. `mat3_rotation_x/y/z` are `D3DXMatrixRotationX/Y/Z`; `mat4_look_at_lh` is `D3DXMatrixLookAtLH`.
- A fighter's forward is -z turned by its heading: `forward_of_heading(h)` = (-sin h, 0, -cos h).
- `angle_atan2(y, x)` is C's `atan2(y, x)`: 0 along +x, a quarter turn along +y. The original's calls
  (`atan2_f(v.x, -v.z)` for a yaw) port one to one.

## How far the original's values reach

Game units; gravity is 9.8 of them a second squared, so they read as metres (the sound code halves them for
DirectSound, `docs/findings.md`; that doesn't change any range here).

The four stage collision grids, read from the files (`choco_col.bin` and the three cabinet segments the archive
split out): the triangles' bounds, the longest edge, the grid's cell size (the grid is 32 x 4 x 32 in all four).

| stage | x | y | z | longest edge | cell (x, y, z) |
|---|---|---|---|---|---|
| choco (test arena) | -106.6 .. 106.6 | 0 .. 110 | -106.6 .. 106.6 | 269.2 | 6.8, 28.5, 6.8 |
| Shinjuku | -47.2 .. 195.6 | -4.7 .. 150 | -158.9 .. 85.6 | 339.4 | 7.7, 39.7, 7.7 |
| Shibuya | -112.6 .. 69.8 | -75 .. 150 | -89.9 .. 98.9 | 151.7 | 5.8, 57.3, 6.0 |
| Umihotaru | -264.0 .. 265.1 | -23.4 .. 150 | -85.0 .. 85.0 | 434.6 | 16.7, 44.4, 5.4 |

So a coordinate is under 266, two points on one stage are under 560 apart, and an edge is under 435 long. The
shortest edges are under a millimetre (Shinjuku 0.0008).

From `docs/port/gameplay.md` (30 Hz updates; a 60 Hz step halves velocities and quarters accelerations):

| value | 30 Hz | 60 Hz step | raw | rounding |
|---|---|---|---|---|
| gravity, 9.8/900 x 2.5 | 0.027222 | 0.0068056 | 446 | 2e-5 |
| the dive's extra 6 g | 0.065333 | 0.016333 | 1070 | 4e-4 |
| air push, 2/256 a full stick | 0.0078125 | 0.0019531 | 128 | exact |
| velocity, push and impulse caps | 5 | 2.5 | 163840 | exact |
| slowest walk, 10/216 a tick | | 0.046296 | 3034 | 2e-5 |
| air drag 0.985 an update | | sqrt: 0.99247 | 65043 | 6e-6 |
| knockback decay 0.8 | | sqrt: 0.89443 | 58617 | 3e-6 |
| dodge decay 0.99 | | sqrt: 0.99499 | 65207 | 8e-6 |
| smallest threshold, 0.001 | | | 66 | 0.7% |

The rest: AI sight 100, melee lock 25 (20 AI), pickups 25, rams 5..20, fighter radius 0..2, shot radius 0.3,
blast radius 3..15, the segment walks give up past 10000, missing floors read -1000, nearest searches start at
1e4. Turn rates 0.1..90 degrees a tick, the AI's turn clamp 0.137 rad and the homing one pi/16 an update, the view
cone pi/6, the rear-hit cosine 0.7071.

## Where Q16.16 is enough

Positions, velocities, distances, radii, every per-step constant above, plane heights and every value a
fighter's state holds. The largest coordinate is 266 of 32768. The step is 1.5e-5 units: finer than the
original's float positions anywhere past 128 units from the origin (a float's step there is 3e-5 and more), and
far under anything visible. A jump at 60 Hz with Q16 gravity peaks 0.00013 units off the same jump in doubles.

Angles are not Q16: a 16-bit binary angle (Mans In My House's) steps 0.0055 degrees, and the slowest turn,
0.1 degrees a tick at a 1% stick, is 0.001 degrees a tick, under one 16-bit unit. A turn in 2^32 units makes it
11,930 units.

## Where 64-bit intermediates are needed

- Any squared length or dot product of a vector over 181 units (181^2 is 32768): two points across a stage
  are 560 apart, 313,600 squared. Use `length_q32`, `dot_q32`, `distance_q32`, `longer_than`, `shorter_than`,
  `within`; `dot` saturates. `length`, `normalized`, `scaled_to` and `clamp_length` go through Q32 already.
- Cross products of triangle edges: 435 x 435 is 189,000. `cross` saturates past 32768; `cross_parts`,
  `unit_cross` and everything in `hit.cpp` keep them whole. Take a triangle's normal from the file, never from
  `cross` of its edges.
- A point's place in a triangle: the original inverts a 3x3 frame per triangle; its determinant is past 64
  bits for stage-sized rows. `tri_frame` gives the same numbers as exact area ratios (numerators and a
  denominator up to 2^53, compared, not divided), `frame_u`/`frame_w` divide only when a margin needs them.
- Where a segment crosses a plane or passes nearest a point: the fraction is kept in Q30 (`point_along` in
  `hit.cpp`), so the point is right to a raw unit on a 435-unit edge.
- a * b / c with one rounding: `fx_mul_div`.
- Distance from a line, |m x d| / |d|: each part of the cross divided by the length before squaring
  (`segment_sphere_entry`), so a graze is as good as a hit square on.

## Where it falls short, and what to do

- **Turning by multiplying small rotations.** sin of a 0.037 degree step is 42 raw, so each step turns
  up to 1% wrong: ten minutes of such steps multiplied into a matrix ended 0.17 rad off. Keep a heading as an
  `Angle`, add to it, and build the matrix from it each step (`mat3_rotation_y(heading)`), as the original
  keeps a float heading. Never carry a pose matrix from step to step.
- **Turn amounts as Fx degrees.** 0.1 degree is 6554 raw, 6e-5 off; ten minutes of turning at it drift 0.0016
  rad. Scale in the angle domain: `angle_scaled(turn_as_angle, stick)` drifts 0.0003 rad in the same test,
  which is the stick's own Q16 rounding (the pad itself gives 1/32767 steps).
- **Matrix entries are Q16.** A point d units from a matrix's origin comes out about d x 1e-5 off: 4 mm at 300
  units. Transform local offsets (parts, muzzles, the camera's offset, all within a few units: well under
  0.1 mm) and add world positions after, which is how the original's attachments work anyway.
- **Normals are Q16.** Heights over a triangle and the in-plane position are off by about the distance from
  the triangle's first corner x 1e-5: 3 mm on the largest ground triangles. Floor heights and contacts use the
  same rounded normal, so the world agrees with itself; only against the drawn mesh is it a few mm. If that
  ever shows, keep normals in Q30 (an int32 holds a unit at 2^30) and take heights as sum(n * d) >> 30.
- **A view near straight up or down.** A unit vector's small parts are coarse: a direction 0.3 degrees off
  vertical has horizontal parts of 300 raw, so a side axis or yaw worked out from it is 1e-3 rad off.
  `mat4_look_at_lh` and `mat3_from_dir_up` take their crosses from the exact unnormalized offset
  (`unit_cross`); do the same for yaws (`angle_atan2(v.x, -v.z)` of the offset, not of a normalized one).
- **Decays.** gfxp's `a * b` floors, so a negative value scaled by 0.8 every step sticks a few raw units below
  zero (-1000 raw ended at -5 for good) while a positive one reaches 0: a knockback that never quite ends,
  pushing one way only. Use `fx_mul_trunc` / `scaled_toward_zero` for every decay and drag (knockback 0.8, air
  drag 0.985, dodge 0.99, the skid's slide factor).
- **acos and asin near +-1.** A cosine 1 raw under 1 is already acos 0.0055 rad. `angle_between` takes
  atan2(|a x b|, a . b) on exact products instead (8e-8 rad); use it for cones and aspect checks.
- **The overweight cliff**, (capacity / weight)^10, is 0.001 at a ratio of 0.5: 64 raw, 1.5% off. It is worked
  out once at spawn and the rates it feeds are clamped (run speed at least 20/216), so it only matters near the
  clamp. Work the spawn-time rates (both weight factors, their square roots and the ^10) in Q32 if the clamps'
  edges ever need to match.
- **Overflow is silent in gfxp.** Its `+`, `-` and `*` on `Fx` wrap (and `+`/`-` are signed int overflow, which
  is undefined), `/` truncates and returns 0 for x / 0, `Fx * int` is a plain int multiply. `src/fx` saturates
  where results can run past the range (`fx_div_near`, `fx_mul_div`, `fx_from_q32`, `fx_saturate`); sums of
  values that may be saturated go to int64 first (`segment_tri_hit`'s margin test does).
- **`Fx{n}` is a raw value**, n / 65536. Write `fx(n)`, `fx_raw(n)` or `fx_f(x)`.

Not a numbers question but next to one: integration order and caps applied per step change at 60 Hz whatever
the number type (`docs/sixty.md`).

## Rounding

gfxp floors `a * b` and truncates `a / b`. Everything in `src/fx` rounds to nearest with halves away from zero,
once per result (products summed whole in 64 bits first), so mirrored inputs give mirrored results: a mech
strafing left moves exactly as far as one strafing right. Use `fx_mul_near` for a scalar product where that
matters, `fx_mul_trunc` for decays. (Looked at 2026-11-11 over 200,000 random cases each: `normalized`, vector
scaling both ways, `angle_atan2`, `fx_sin`, `segment_sphere_entry`, `closest_on_segment`, `unit_cross` and
`angle_between` all give exactly the mirrored answer for mirrored inputs.)

`grid_cell` truncates toward zero like the original's float cast (so -0.5 cells is cell 0). At an exact cell
boundary a fixed-point position can land in the cell next to the one the float did; the port isn't bit-exact
with retail anyway (`docs/port/architecture.md`, "What the port has to reproduce exactly").

## Determinism

The rules are Mans In My House's (`docs/architecture.md`, "Determinism across platforms"): no floats or libm
in the step (only `consteval` conversions), no `long`, integers only, one random draw a statement (the order a
call's arguments are worked out in is the compiler's: `fx_accuracy`'s first version gave different samples
under GCC and Clang for exactly that). C++20 defines shifts of negative numbers and narrowing conversions as
two's complement, which `src/fx` relies on.

The check is `fx_accuracy`'s last line, a hash of 20000 rounds of every function on integer-made inputs.
2026-11-11, `2afe577aee973664` from all of:

- GCC 13.3, libstdc++, x86-64;
- Clang 18.1, libc++, x86-64;
- Clang 18.1, `--target=aarch64-linux-gnu` against the arm64 cross glibc and libstdc++ 14, run under
  qemu-aarch64 8.2 (the double-precision lines matched too). Built by hand: `clang++-18
  --target=aarch64-linux-gnu --sysroot=/ -fuse-ld=lld -std=c++20 -O2 -isystem
  /usr/aarch64-linux-gnu/include/c++/14 -isystem /usr/aarch64-linux-gnu/include/c++/14/aarch64-linux-gnu
  -isystem /usr/aarch64-linux-gnu/include -B/usr/lib/gcc-cross/aarch64-linux-gnu/14
  -L/usr/lib/gcc-cross/aarch64-linux-gnu/14 -L/usr/aarch64-linux-gnu/lib -Isrc -I../gfxp/include
  tools/fx_accuracy.cpp src/fx/hit.cpp`, run with `qemu-aarch64 -L /usr/aarch64-linux-gnu`;
- Clang 18.1 with `-fsanitize=undefined,integer` (unsigned wrap and implicit conversions left out): no signed
  overflow, bad shift or division by zero. The first run caught one, the margin test summing saturated values,
  now in int64.

### The whole step

2026-11-12. `pc_sim` (`cmake/sim.cmake`, `src/sim_main.cpp`) builds the world alone, with no window, GPU or sound, from
the packs the game cooked, and steps a fight: the stage viewer's (`--viewer` and its flags, built by
`src/viewer_world.cpp` exactly as the game's viewer builds it) or the front end's tournament fight (`--state 6`, built
by `src/battle_world.cpp` exactly as `battle_run_start` builds it, with today's line-up, the replacements from the
queues and, in phase 0, the result; `PHANTOMCRASH_DEV_ENTRY=N` enters loadout row N as the player's machine).
`--hash 1` prints `hash STEP WORLD FIGHT` after every step, `world_hash` and `world_fight_hash`
(`src/world/world_hash.hpp`), the same line the game's `--hash 1` prints. The game and `pc_sim`, both GCC, printed the
same stream for a 5400-step viewer demo (the world hash) and for a 2115-step tournament fight to the human's death (the
fight hash; the game rolls Sonic Amp's stock into the profile with CRT `rand` at boot, which `pc_sim` doesn't).

The builds, all `-O3`:

- GCC 13.3, libstdc++ 13, x86-64;
- Clang 18.1, libstdc++ 13, x86-64;
- Clang 18.1, libc++ 18, x86-64 (`-DPHANTOMCRASH_SIM_ONLY=ON -DCMAKE_CXX_FLAGS=-stdlib=libc++`);
- Clang 18.1 for `aarch64-linux-gnu` against the cross glibc and libstdc++ 14, run under qemu-aarch64 8.2, built by
  CMake with `-DPHANTOMCRASH_SIM_ONLY=ON` (gfxp and gsexp only, no SDL) and a toolchain file setting
  `CMAKE_CXX_COMPILER_TARGET aarch64-linux-gnu`, `--sysroot=/`, the `-isystem`, `-B` and `-L` paths of the fx check
  above and `-fuse-ld=lld`. It reads the packs cooked on x86-64; a real ARM64 machine cooks its own, which the
  gameplay data hash compares.

The fights, 8000 steps each, every step's two hashes compared with GCC's:

| fight | what happens in it |
|---|---|
| eight AI-only demos, one on each stage selector 0..7, opponent entries from all over the table | 400-2900 shots, 150-540 hits, 3-5 kills, 12-24 cloaks, 0-6 crates taken each; the training stage's random props |
| three AIs on Shibuya | cloaks and crates, no shots |
| three humans on scripts (`walk_run_jump.txt`, `legs.txt`, `jump_held.txt`) against 3-5 AIs at levels 20, 40 and 99 | a death scene, a door, hits both ways |
| four tournament fights from `--state 6` (seeds 1, 4, 11, 21; a human entered) | replacements drawn from the queues (up to 9 spawns), deaths with the result built, a door, 1-8 crates |

Every stream matched GCC's at every step, in every build. The checks that ran with it, on GCC's and Clang's builds:

- `--rollback N`: every N steps a copy of the world N steps back, stepped again on the same inputs, reaches the live
  hash. Nothing the step reads lives outside the world. 0 misses in 150 checks of 40 steps and 120 of 50.
- `--replay`: the first world in battle phase 2 on the recorded inputs matches the live fight's hash at every step, as
  the game's replay checks it. The game's own replay of the same tournament fight matched at all 2115 steps.
- valgrind memcheck with `--hash 1`, so an uninitialized field would reach the printed hash: no errors.
- Clang with `-fsanitize=address,undefined`, 6 slots and 12: no errors after one fix (below).

What it found and fixed:

- **Fused multiply-adds.** Clang fuses `a * b + c` within an expression by default (`-ffp-contract=on`), and on ARM64
  it did: 9 in `battle_flow.cpp` (the result's repair costs, `0.2 x base + 0.01 x tuned` in float as retail), 20 in
  `roster_tables.cpp` (the curves behind prices, destruction values and weights) and 5 in `motion_sets.cpp`. Evaluated
  over every pilot's pay at every level, every part's price and repair at every tune and every build's weight (530,530
  values), the fused ARM64 build gave 16 prices one off from x86-64; unfused, none. The fights above happened not to
  meet those 16. CMakeLists.txt now builds everything with `-ffp-contract=off` (GCC and Clang; MSVC's `/fp:precise`
  doesn't fuse); `llvm-objdump` finds no fused instruction left in the ARM64 objects. x86-64 has none to fuse without
  `-mfma`.
- **The hash itself.** `char` is signed on x86-64 and unsigned on ARM64, so the roster names' UTF-8 bytes hashed
  differently though they were the same; `put` hashes a `char` as its byte.
- **Builds.** Two weapons files used `std::clamp` without `<algorithm>` (libstdc++ 14 doesn't bring it in);
  `rig_data.hpp` gave a default argument of a `std::vector` of an incomplete type, which Clang rejects.
- **UB.** `BlobReader::raw` passed a null pointer to `memcpy` reading an empty list from a pack (harmless, now skipped).

Kept as they are: the floats in the repair costs and curves (retail's numbers, now rounded alike everywhere); CRT `rand`
in menus only (multiplayer.md). Not under the step's control yet: `Inputs::scene_text_done` and
`BattleData::ranker_line_ticks`, which presentation supplies (docs/backlog.md).

## Accuracy

`fx_accuracy` (configure with `-DPHANTOMCRASH_FX_ACCURACY=ON`), 2026-11-11, worst error over a few thousand
samples each. A raw unit is 1.5e-5.

| function | worst error |
|---|---|
| `fx_bits` against exact rounding | 0 |
| `fx_sqrt`, `length` (parts to 600 units) | 0.5 raw |
| `fx_sin`, `fx_cos` | 0.52 raw |
| `angle_atan2` (inputs 1 raw .. 32768) | 7.8e-8 rad |
| `angle_acos`, `angle_asin` | 7.9e-8 rad (against the rounded input) |
| `angle_between` (many under 0.01 rad) | 7.9e-8 rad |
| `angle_from_radians` (under 16 rad), `angle_radians` | 0.5 raw |
| `angle_from_degrees` (under 1024 degrees) | 8 angle units (1.2e-8 rad) |
| `normalized` (parts 1 raw .. 600 units) | 0.5 raw a part; its length within 0.77 raw of 1 |
| `mat3_rotation_y` on a point 300 units out | 252 raw (0.004 units) |
| rigid inverse round trip, 300 units | 823 raw (0.013 units) |
| `mat4_look_at_lh`, points up to 900 units from the eye | 480 raw (0.007 units, 1e-5 rad) |
| `tri_frame` height (exact normal as reference) | 45 raw |
| `tri_frame` u, w as a point on the plane | 178 raw (0.003 units) |
| `segment_tri_hit` crossing point | 26 raw |
| `segment_sphere_entry` entry point | 3.5 raw |
| `tri_height_at` (floors, normal.y over 0.2) | 110 raw |

The matrix and triangle rows are the Q16 rounding of the matrix entries and normals (above), not of the
arithmetic, which loses at most a raw unit per result.

## Measured from the data

`tools/measure_numbers.py` reads the converted `data/` (after `pc_extract`) and prints these; numbers from
2026-11-11. They are in the port's frame (the original's with z negated, `docs/assets.md`), which is why the z
columns here are the table above's mirrored.

| stage | collision x | y | z | stage model x | y | z |
|---|---|---|---|---|---|---|
| Shinjuku (379, 186/187) | -47.2 .. 195.6 | -4.7 .. 150 | -85.6 .. 158.9 | +-2206.9 | -341.6 .. 652.1 | +-2228.9 |
| Shibuya (380, 188/189) | -112.6 .. 69.8 | -75 .. 150 | -98.9 .. 89.9 | +-2206.9 | -341.6 .. 681.1 | +-2228.2 |
| Umihotaru (381, 190/191) | -264.0 .. 265.1 | -23.4 .. 150 | -85.0 .. 85.0 | +-2060.6 | -93.0 .. 681.1 | +-2060.6 |
| test arena (382, 192/193) | -106.6 .. 106.6 | 0 .. 110 | -106.6 .. 106.6 | +-2166.4 | -356.6 .. 681.1 | +-2166.4 |

- The stage models reach 2229 units (the sky dome and far scenery around each arena). That is inside Q16.16 too, but
  nothing simulated goes there: they stay floats on the drawing side.
- Entry points (`stage_entry_points`) all sit inside the collision bounds (Shinjuku's first is at 111.2, 0.3, 152.3).
- The widest diagonal over everything, models included, is 6351 units; its square in Q32.32 is 1.7e17, inside int64
  with 50x to spare, so even a squared distance taken against the sky stays exact in 64 bits.
- A fighter's pose (the SV graph walk with its IK chains) is gameplay as well as drawing:
  muzzles, the contact spheres `fighters_separate` uses, the feet's ground clamp and the camera head come from it.
  It runs in the step since 2026-11-11 ("The pose in the step" below). Its numbers all fit Q16.16 the same way: a fighter's nodes lie
  within 3 units of its position (the Plain Holy's matrices, idle clip: 0.45 .. 2.9 above its position, under 1.2 to the sides),
  motion curve values are radians and offsets under 3, and the IK needs only square roots and ratios of link lengths
  (0.76 .. 1.1 for the Holy legs), no angles. Ported into the step it wants `FxMat` rows (`fx/mat.hpp`) rebuilt each
  step from `Angle`s, as the rules above say.

## The pose in the step

`src/mech/pose_walk.cpp` (the walk, the IK chains, the arms' raise, the blend), `pose_ik.cpp` (the solvers and
quaternions), `pose_weapons.cpp` (the weapon models, the wheels, the attachment points), on the data of
`src/mech/rig_data.hpp` (the graphs, every clip of the build's leg, arm and body sets, the weapon models' node trees,
converted by their float bits at load). What it leaves on the fighter is `FighterPoseState` (`fighter_pose.hpp`).

- Each node's matrix is built every step from `Angle`s: the curve's radians to an `Angle`, the three turns as one
  matrix with every entry summed whole and rounded once (`mat3_turns`), times the parent. Nothing is carried from step
  to step but the palette the pose blend reads, which lets go of it within 20 steps.
- The IK has no angles: the aim is the target's local offset over its lengths (`scaled_parts_to`), the bends the law
  of cosines from exact Q32 squares, their sines from the exact other side, sqrt((2ac)^2 - (a^2 + c^2 - b^2)^2),
  brought down 12 bits so it fits 64 bits.
- The blend's slerp takes its angle from the chord, 2 asin(|b - a| / 2), which stays good for small turns, and lerps
  and normalizes under 0.05 rad.
- Points are put under a world matrix as offset times basis plus place, and the arm's frame against its mount is
  taken from the difference of the two places before turning, so nothing is multiplied at stage size.

Measured 2026-11-11 with `--pose-compare` (`src/mech/pose_compare.cpp`) against the float walk (`mech_pose.cpp`, the
same code in floats) over the idle, walk, run and jump clips at every quarter tick, at the origin, 190 and 270
units out with three headings, standing still and with the upper body turned 0.3, pitched 0.25 and leaning 0.2, for
15 builds (one of every leg class with each body class: 0, 12, 7, 15, 20, 23, 22, 10, 16, 25, 11, 9, 8, 6, 1):

| | largest node error | largest rotation entry error |
|---|---|---|
| every node off the IK chains, and the mounts (body, arms' mounts and aim frames) | 0.00038 units | 0.0002 |
| nodes under an IK chain (shins, feet, the legs' armour) | 0.0107 units | 0.012 |
| the three-link IK alone against doubles, Holy legs, reach under 2.1 / 2.1 to straight | 0.00009 / 0.00022 units | |

Both walks store the same nodes everywhere. The IK chains' larger error isn't the solver (the last row): it is the
knee near a straight leg, where the bend is acos of a cosine within a few 1e-5 of 1, so the bend moves as the
square root of the slack and a change of reach of a raw unit moves the knee millimetres. The worst case, build 0's
jump at tick 31.25, has the second chain's reach 2.13158 against 2.13163 straight, and its foot target 4e-5 off
the float walk's (the curves' Q16 rounding). Carrying the curves and the chain in more bits would close it. Of what
gameplay reads, only the foot points (attachments 14 on, the footstep nodes under the feet: dust and footstep
sounds) hang there, up to a centimetre off near a straight leg; muzzles, mounts and attachment 13 hang off the body.
So it is left.
