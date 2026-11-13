# Performance

How fast the port runs, how to see it in a run log, what was fixed and what is left. The first real-GPU measurements
(2026-11-13): an AMD RX 9070 XT on RADV (Mesa 25.2.8), a Ryzen 9 9950X.

## The run log's lines

Every run log (`~/.local/share/OpenPCrash/OpenPCrash/logs/`, src/run_log.cpp) now says, besides the sound's story:

- `render: Vulkan device NAME, DRIVER, Vulkan X.Y.Z` once, and `render: swapchain 1 in flight, fifo (v-sync), 4 images, WxH,
  targets WxH` each time the swapchain is made (start, resize, the v-sync or render scale settings).
- `load: ...` for the loads: `start to the first frame`, `boot to the title` (the logos and the attract movie are
  in it when they play), `battle` (a front-end fight, VS and replays included), `viewer battle`, and
  `stand machine KEY in X ms (its upload Y ms)` for each machine a model stand reads (the read is on a worker, the
  upload on the main thread).
- every 10 s a `perf:` line (src/perf_log.cpp):

```
perf: 1020 frames in 2.0s (510/s), frame p50 1.90 p99 3.55 ms, cpu p50 1.89 p99 3.54 ms; 1020 steps, world p50 132
p99 309 us; a frame ms: steps 0.36 menus 0.00 hud 0.00 ui 0.00 scene 0.30 record 0.20 present 0.79 other 0.32,
blocked fence 0.00 acquire 0.00 uploads 0.00; gpu p50 0.40 p99 0.48 ms (scene 0.30 composite 0.07 2d 0.00 gamma 0.03
blit 0.02); made 1 pipelines (0.1 ms) 5 sets, 0 uploads 0.0 KB; 1 in flight, fifo (v-sync), 4 images, 1920x1080,
targets 1920x1080
```

(one line in the log; a Shinjuku fight in an uncapped capture, `PHANTOMCRASH_PERF=2`)

| field | what |
|---|---|
| frame p50 p99 | the time from one frame's start to the next's: with v-sync, the display's period |
| cpu p50 p99 | a frame's own work: its time less what it spent blocked (the fence, the acquire, a capture) |
| steps, world | world steps run in the window; `world` is `step_world` alone, per step |
| steps (a frame ms) | the battle steps whole: the world, the presentation's steps (poses, cameras, effects, HUD events), the sounds |
| menus | the front end's updates (`front_end_update`: the screens' logic and their 2D) |
| hud | the fight's HUD 2D (`battle_run_hud`) |
| ui | `ui_present`: the 2D list handed to the renderer |
| scene | the 3D's draw list (`draw_scenery_views` up to the renderer: stage, machines, effects, billboards) |
| record | `renderer_draw_frame`'s own work: the per-draw blocks, the sprites' vertices, the commands |
| present | `vkQueueSubmit` and `vkQueuePresentKHR` |
| other | the rest of the frame's work: input, the gamepads, the sound device's following, the loop |
| fence | blocked on the last frame's fence (the GPU still drawing it) |
| acquire | blocked on the swapchain for an image (with v-sync, where the frame waits for the display) |
| uploads | the pending uploads (loads) submitted and waited for at the frame's start |
| gpu | the GPU's time a frame from timestamps, and per pass: `scene` (pass 0's 2D, passes 1..10, the backdrop copies), `composite` (pass 11: the big screens' copy, the blur and dof or the tinted copy, the cockpit), `2d` (passes 12..15, pass 13's meshes, the post pass), `gamma` (the capture copy, the gamma ramp), `blit` (to the window) |
| made | pipelines and texture descriptor sets made inside frames (first sight of a state or a texture), uploads |

`PHANTOMCRASH_PERF=N` says it every N seconds instead (0 never). `PHANTOMCRASH_VK_DEVICE=TEXT` takes the first Vulkan
device whose name has TEXT in it (`RADV`, `llvmpipe`), over the usual discrete-first pick.

## How it was measured

The captures, run logs, traces and tables are in `captures/perf/` (not in git).

On a private Xvfb with `MESA_VK_WSI_DEBUG=sw` (RADV draws, the presentation is a software copy into the X server) and
`PHANTOMCRASH_VK_DEVICE=RADV`; never on the owner's display. Each scene is a capture run (`--frames N`: one world step
a frame, no frame limit), 2400 frames, `PHANTOMCRASH_PERF=2`, the first window (the load) left out and the medians of
the rest taken. Runs went only when nothing else was compiling or running a game and the 1-minute load was under 4
(other agents' lavapipe captures share this machine); the load before and after each run is in the tables. The base
and the new build ran in turn, scene by scene.

Two things in these numbers are the setup's, not the game's:

- `present` (0.5-1.7 ms) and most of `other` are Mesa's software presentation copying the frame into Xvfb (below). On
  a real display RADV hands the image over in a few microseconds.
- Uncapped, `acquire` is near 0; with v-sync on a real display it is where the frame waits for the vertical blank.

The scenes, each with `--size WxH --capture out.png --frames 2400`:

| scene | how |
|---|---|
| Shinjuku, 6 fighters | `--viewer --stage 0 --opponents o19,o40,12,40,5 --demo` |
| Umihotaru, 6 fighters | `--viewer --stage 4 --opponents o19,o40,12,40,5 --demo` |
| hangar with its stands | `PHANTOMCRASH_DEV_BUILDS=37,72,0,5 --state e` |
| garage hub (wide: the blurred sides) | `--state d` |
| 4-way split-screen VS | `PHANTOMCRASH_PADS=3 --vs 4 --stage 0` |
| movie | `--state 2` (the attract movie) |

## Before and after

46d64d2 with the instrumentation only (before) against b560286 (after), in turn, scene by scene, 2026-11-13 05:55 to
06:22. Frame times are uncapped (one step a frame), so they are the whole frame's cost; at 60 Hz with v-sync the rest
of the 16.7 ms goes to `acquire`. The columns are a frame's milliseconds unless they say otherwise; `other` is new in
the after build. Load is the 1-minute load average before and after the run (nothing else compiling or running a
game). The table is the medians of the 2-second windows after the first.

### 1920x1080

| scene | build | frames/s | frame p50 / p99 ms | cpu p50 / p99 ms | world step p50 / p99 us | steps | 2D (menus+hud+ui) | scene | record | present | other | uploads | gpu p50 / p99 ms | load before / after |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| shinjuku | before | 387 | 2.40 / 3.25 | 2.40 / 3.25 | 137 / 243 | 0.36 | 0.00 | 0.38 | 0.21 | 0.76 | - | 0.15 | 0.40 / 0.45 | 1.99 / 1.83 |
|  | after | 426 | 1.80 / 3.52 | 1.79 / 3.52 | 115 / 250 | 0.33 | 0.00 | 0.29 | 0.20 | 0.77 | 0.28 | 0.00 | 0.39 / 0.46 | 1.83 / 1.79 |
| umihotaru | before | 368 | 2.52 / 3.54 | 2.52 / 3.54 | 224 / 450 | 0.39 | 0.00 | 0.35 | 0.25 | 0.85 | - | 0.17 | 0.45 / 0.52 | 1.79 / 1.89 |
|  | after | 392 | 2.15 / 3.50 | 2.15 / 3.49 | 220 / 380 | 0.40 | 0.00 | 0.30 | 0.25 | 0.86 | 0.35 | 0.00 | 0.47 / 0.53 | 1.89 / 1.75 |
| hangar | before | 1120 | 0.76 / 2.11 | 0.76 / 2.11 | 0 / 0 | 0.00 | 0.08 | 0.00 | 0.05 | 0.51 | - | 0.00 | 0.20 / 0.21 | 1.75 / 1.75 |
|  | after | 1161 | 0.75 / 1.96 | 0.75 / 1.96 | 0 / 0 | 0.00 | 0.08 | 0.00 | 0.05 | 0.51 | 0.23 | 0.00 | 0.20 / 0.22 | 1.75 / 1.61 |
| hub | before | 1165 | 0.76 / 1.75 | 0.76 / 1.74 | 0 / 0 | 0.00 | 0.02 | 0.00 | 0.03 | 0.49 | - | 0.00 | 0.20 / 0.22 | 1.61 / 1.64 |
|  | after | 1209 | 0.74 / 1.60 | 0.73 / 1.60 | 0 / 0 | 0.00 | 0.02 | 0.00 | 0.02 | 0.50 | 0.22 | 0.00 | 0.20 / 0.22 | 1.75 / 1.61 |
| vs4 | before | 270 | 3.80 / 4.55 | 3.79 / 4.54 | 160 / 260 | 0.21 | 0.05 | 0.68 | 0.80 | 1.31 | - | 0.15 | 0.84 / 0.86 | 1.61 / 1.44 |
|  | after | 310 | 2.89 / 4.09 | 2.89 / 4.08 | 149 / 252 | 0.19 | 0.04 | 0.54 | 0.76 | 1.29 | 0.33 | 0.00 | 0.84 / 0.87 | 1.44 / 1.41 |
| movie | before | 1173 | 0.75 / 2.88 | 0.75 / 2.88 | 0 / 0 | 0.00 | 0.06 | 0.00 | 0.02 | 0.60 | - | 0.01 | 0.21 / 0.30 | 1.41 / 1.29 |
|  | after | 1085 | 0.82 / 2.77 | 0.82 / 2.76 | 0 / 0 | 0.00 | 0.06 | 0.00 | 0.02 | 0.66 | 0.18 | 0.00 | 0.20 / 0.31 | 1.29 / 1.35 |

The GPU per pass (after), ms:

| scene | scene pass | composite | 2D | gamma | blit |
|---|---|---|---|---|---|
| shinjuku | 0.29 | 0.07 | 0.00 | 0.03 | 0.02 |
| umihotaru | 0.35 | 0.07 | 0.00 | 0.03 | 0.02 |
| hangar | 0.10 | 0.03 | 0.03 | 0.03 | 0.02 |
| hub | 0.12 | 0.02 | 0.01 | 0.03 | 0.02 |
| vs4 | 0.70 | 0.07 | 0.02 | 0.03 | 0.02 |
| movie | 0.03 | 0.01 | 0.14 | 0.02 | 0.01 |

### 2560x1440

| scene | build | frames/s | frame p50 / p99 ms | cpu p50 / p99 ms | world step p50 / p99 us | steps | 2D (menus+hud+ui) | scene | record | present | other | uploads | gpu p50 / p99 ms | load before / after |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| shinjuku | before | 276 | 3.46 / 5.08 | 3.46 / 5.06 | 173 / 333 | 0.45 | 0.00 | 0.45 | 0.25 | 1.08 | - | 0.19 | 0.55 / 0.64 | 2.18 / 2.15 |
|  | after | 321 | 3.09 / 4.96 | 3.08 / 4.95 | 161 / 324 | 0.44 | 0.00 | 0.33 | 0.23 | 1.07 | 0.84 | 0.00 | 0.55 / 0.61 | 2.15 / 2.14 |
| umihotaru | before | 279 | 3.70 / 4.79 | 3.70 / 4.78 | 283 / 461 | 0.51 | 0.00 | 0.43 | 0.30 | 1.19 | - | 0.19 | 0.65 / 0.69 | 2.14 / 2.12 |
|  | after | 329 | 3.13 / 4.91 | 3.12 / 4.90 | 274 / 458 | 0.48 | 0.00 | 0.34 | 0.27 | 1.23 | 0.68 | 0.00 | 0.67 / 0.76 | 2.12 / 2.10 |
| hangar | before | 565 | 1.59 / 3.94 | 1.58 / 3.94 | 0 / 0 | 0.00 | 0.12 | 0.00 | 0.07 | 0.76 | - | 0.01 | 0.31 / 0.33 | 2.10 / 2.09 |
|  | after | 588 | 1.53 / 3.34 | 1.53 / 3.33 | 0 / 0 | 0.00 | 0.11 | 0.00 | 0.06 | 0.76 | 0.69 | 0.00 | 0.31 / 0.33 | 2.09 / 2.16 |
| hub | before | 584 | 1.57 / 3.97 | 1.57 / 3.97 | 0 / 0 | 0.00 | 0.04 | 0.00 | 0.03 | 0.74 | - | 0.00 | 0.31 / 0.33 | 2.16 / 2.07 |
|  | after | 585 | 1.57 / 3.98 | 1.57 / 3.95 | 0 / 0 | 0.00 | 0.05 | 0.00 | 0.04 | 0.76 | 0.88 | 0.00 | 0.31 / 0.34 | 2.33 / 2.22 |
| vs4 | before | 208 | 5.02 / 6.09 | 5.02 / 6.08 | 208 / 331 | 0.28 | 0.07 | 0.77 | 0.85 | 1.64 | - | 0.17 | 1.00 / 1.03 | 2.22 / 1.96 |
|  | after | 238 | 4.46 / 5.75 | 4.45 / 5.75 | 200 / 316 | 0.26 | 0.06 | 0.59 | 0.84 | 1.71 | 0.70 | 0.00 | 1.00 / 1.02 | 1.96 / 1.91 |
| movie | before | 457 | 1.56 / 5.69 | 1.56 / 5.69 | 0 / 0 | 0.00 | 0.15 | 0.00 | 0.02 | 1.13 | - | 0.01 | 0.48 / 0.49 | 1.91 / 1.76 |
|  | after | 534 | 1.52 / 5.19 | 1.52 / 5.19 | 0 / 0 | 0.00 | 0.10 | 0.00 | 0.02 | 1.13 | 0.61 | 0.00 | 0.48 / 0.54 | 1.76 / 1.78 |

The GPU per pass (after), ms:

| scene | scene pass | composite | 2D | gamma | blit |
|---|---|---|---|---|---|
| shinjuku | 0.37 | 0.10 | 0.00 | 0.04 | 0.02 |
| umihotaru | 0.48 | 0.11 | 0.00 | 0.04 | 0.02 |
| hangar | 0.15 | 0.05 | 0.04 | 0.05 | 0.02 |
| hub | 0.21 | 0.02 | 0.01 | 0.05 | 0.03 |
| vs4 | 0.79 | 0.11 | 0.03 | 0.05 | 0.03 |
| movie | 0.05 | 0.03 | 0.34 | 0.05 | 0.03 |

Reading it:

- The fights lose 0.35-0.9 ms a frame (Shinjuku 2.40 to 1.80 ms at 1080p, the 4-way VS 3.80 to 2.89): the per-frame
  upload wait (`uploads` 0.15-0.19 to 0), the allocations and frees around it (in `scene` and `record`), and the
  draw lists' and the sort's allocations (`scene` 0.38 to 0.29, 0.68 to 0.54 for four views).
- The menus, the hangar and the movie had no per-frame uploads; they move within the noise. The movie's 1080p run is
  slower in `present`, the software presentation's copy; its picture upload costs the same memcpy as before.
- The GPU is idle most of the frame everywhere: 0.2-1.0 ms a frame at 1440p (the most for the four views), the pass 11
  composite 0.07-0.11 ms, the gamma pass 0.03-0.05 ms, the 2D under 0.05 ms except a movie's 0.14-0.34 ms (the
  picture and its 32-tap blurred sides over a wide window).
- `present` and most of `other` are this setup's software presentation (MESA_VK_WSI_DEBUG=sw): both grow with the
  window (other 0.2-0.35 ms at 1080p, 0.6-0.9 ms at 1440p, in scenes whose own work doesn't change with size), which
  is the X server copying the frame while the next frame's event pump waits on it. With the GPU presenting to a real
  display neither is there. On this machine, then, a fight costs the CPU about 1.0-1.4 ms a frame of its own (steps,
  scene, record, the rest), a menu about 0.1 ms.
- The world step: 115-220 us at the median for six fighters (Umihotaru's long collision cells cost more), p99 250-460
  us; in line with `pc_sim --time` (docs/multiplayer.md). Untouched by this pass.

## Frames in flight and the GPU waits

One frame in flight, kept. `renderer_draw_frame` waits for the last frame's fence before it writes this frame's
buffers, but by then the GPU has long finished: the fence wait is 0.00 ms in every scene at both sizes, because a
whole frame takes the GPU 0.2-0.9 ms while the CPU spends 0.7-3 ms building the next one. A second frame in flight would
duplicate the per-frame buffers (the draw ring, the storage, the sprites' vertices, the wave and ramp staging, the
depth probe's readback) for nothing on this machine, and add a frame of latency and a frame of delay to the lens
flare's depth probe. Worth revisiting only if a GPU-bound machine shows `blocked fence` above zero.

Checked as well:

- No `vkQueueWaitIdle` or `vkDeviceWaitIdle` in the frame once a fight runs. Before this pass every frame had one: the
  stage's scrolling textures and fading lamps, the trails and the sky craft's screens rewrote their vertices through
  the load path, so each frame submitted the pending uploads and waited for the queue to go idle (`uploads` 0.15-0.19
  ms a frame, about 70 uploads a second). The remaining waits are on a resize, a settings change (v-sync, scale,
  texture filtering), a texture freed (a battle's end), loads, and `--capture`'s readback.
- The swapchain: minImageCount + 1 images (4 under Xvfb), FIFO with v-sync on, mailbox or immediate with it off. The
  present's semaphore was one for all images and could be signalled again while a present still waited on it; it is
  one an image now.
- Per-frame resource creation: none left. Pipelines and texture descriptor sets are made on first sight and cached
  (the perf line's `made`): about 30 pipelines and 110-120 sets at a fight's start, a few more as new effects first
  show, then none. With a warm shader cache the 30 take 1 ms all told; with Mesa's cache disabled
  (`MESA_SHADER_CACHE_DISABLE=true`, as a first run after a driver update) 2-3 ms each, 60 ms over the first second.
- The post pass (FXAA, brightness) runs only when asked for; at the defaults it is skipped.

## What was fixed

- **Per-frame vertex and picture updates** (85fc847, `src/render/vk_stream.cpp`). `renderer_update_mesh` and
  `renderer_update_sprite_texture` made a staging buffer (`vkAllocateMemory`) each call, recorded a copy into the load
  path's command buffer, and the next frame ran it with `vkQueueSubmit` + `vkQueueWaitIdle` and freed the buffers
  (`amdgpu_va_range_free` showed in the profile). They now write into a mapped ring of two 4 MB halves and the frame's
  own command buffer copies them before the scene pass: no allocation, no submit, no wait. The game fills one half
  while the GPU may still be copying out of the other; an update that doesn't fit takes the old path. A freed mesh or
  texture drops its pending copies.
- **Load staging** (85fc847). Every mesh and texture uploaded at a load had a staging buffer of its own (1,577 for a
  four-machine VS fight). They take room in one 32 MB mapped arena now; a full arena runs the uploads so far and starts
  again, and the rare upload bigger than it still gets its own buffer.
- **Descriptor set cache key** (85fc847). `vk_texture_set` built a `std::vector<int>` key for every draw of every
  frame to look up its cached set; the key is a `std::array<int, 8>`. The renderer's per-draw vectors (block offsets,
  pipelines, sets) keep their room between frames.
- **The scene's lists** (b560286). `draw_scenery_views` and `view_draws` built the frame's `FrameDraws`, the pass
  items, 17 pass queues and the billboards from empty vectors every frame (and again per split-screen view); they are
  kept in `Scenery` and cleared. `PHANTOMCRASH_POSE` is read once instead of every frame per fighter.
- **The billboards' sort** (b560286). `billboards_sort`'s merge sort read each key through the 48-byte billboard; it
  sorts packed (z, index) pairs, the same merge with the same tie rule.
- **Swapchain semaphores and a lost device** (85fc847): above; a `VK_ERROR_DEVICE_LOST` from the fence wait, the
  acquire or the present now ends the run instead of being ignored.

The world is untouched: the nine movement traces (`captures/movement/jump_held.txt`, `legs.txt`, `walk_run_jump.txt`
with builds 0, 37 and 72 on Shinjuku, `--trace 1`, compared on `^(step|t[0-9 ])`) print the same as 46d64d2, line for
line (`captures/perf/traces/`). Pictures: the Shinjuku fight, the hangar and the hub capture byte-identical to 46d64d2
on lavapipe with the validation layers on (no messages), and the 1080p and 1440p captures of the timed scenes are byte-identical between the two builds on RADV,
but two: the movie's frame depends on the wall clock (two runs of the same build differ too; checked by eye), and the
1080p Shinjuku fight differs in 13 pixels of one spark at (473..479, 456..463) by up to 5 levels, as two runs of the
base build differ from each other in the same 13 pixels (the new build matches the base's earlier run).

## The menus at the step rate, fights under a ceiling

(The menus' half of this was replaced the same day: they present at the display's refresh again, below.)

Before cd359d4 the front end's loop drew every pass: with v-sync off and no frame limit a menu redrew the same picture
2,000+ times a second (the picture only changes on a 60 Hz step) and a fight ran as fast as the GPU went. Now
(`src/front_end_run.cpp`, docs/settings.md FRAME LIMIT):

- a menu, a movie or a paused fight draws only on a pass where a step ran (or the window was resized or exposed) and
  otherwise sleeps (`SDL_DelayPrecise`) until the next step is due;
- a moving fight draws every pass with its interpolation alpha, as before, but FRAME LIMIT's MAX (0, which was OFF)
  holds it to 500 frames a second (`frame_ceiling`); v-sync on still holds it to the display. The stage viewer's loop
  shares the ceiling (`pc_frame_end`);
- a capture (`--frames`) draws every pass, one step each, as before; the captures and the frame counts are the same.

Measured 2026-11-13 11:53-12:01 the same way as above (RADV through a private Xvfb with `MESA_VK_WSI_DEBUG=sw`, 1280x960,
v-sync off so mailbox, FRAME LIMIT 0, `PHANTOMCRASH_PERF=2`), real time rather than a capture, 26 s each ended by
SIGTERM, CPU from `/usr/bin/time` over the whole run (the load included), one run at a time while no other game or
compiler ran; 1-minute load 2.2-5.7 (other agents' sessions):

| scene | build | frames/s | frame p50 / p99 ms | cpu a frame p50 ms | CPU over 26 s (user + sys) |
|---|---|---|---|---|---|
| garage hub (`--state d`) | 3e5c901 | 2,030-2,220 | 0.44 / 0.9-1.1 | 0.44 | 24.5 s (a core busy) |
| garage hub (`--state d`) | cd359d4 | 60 | 16.67 / 16.67 | 0.5 | 1.5 s |
| attract movie (`--state 2`) | cd359d4 | 60 | 16.67 / 16.67-17.7 | 1.2-2.2 | 2.5 s |
| tournament fight (`--state 6`) | 3e5c901 | 755-956 | 0.9-1.15 / 2.0-3.1 | 0.9-1.15 | 24.2 s |
| tournament fight (`--state 6`) | cd359d4 | 465-492 | 2.01-2.05 / 2.06-2.9 | 0.95-1.2 | 13.5 s |

The fight's frames come out a little under 500 because the wait runs from the frame's start and `SDL_DelayPrecise`
overshoots by some tens of microseconds. Most of the sys time is the software presentation into Xvfb (above). No
amdgpu messages in `journalctl -k` over the runs. The nine movement traces print the same as 3e5c901; the hub, a VS
round and a tournament fight capture byte-identical to 3e5c901 on lavapipe (the attract movie differs between any two
runs, as above).

## The HUD drawn twice

The owner, 2026-11-13, on a 144 Hz display with v-sync: the fight's barks and its MONEY and SHOOTDOWN counters
flickered badly. The cause is in `present_2d` (src/front_end_run.cpp), older than the frame pacing (3e5c901 has the same
lines in main.cpp): a fight's HUD is drawn each frame over `front_draw`, the front end's own 2D, and `front_draw` was
taken from `ui.draw` on every pass where a step ran. The battle state runs at 30 Hz (states.sexp, `cadence 30`), so
every other step skips the front end's update and leaves `ui.draw` as the last frame handed it: the front end's 2D with
that frame's HUD still on it. `front_draw` took that, and every frame until the next update drew the HUD over a copy of
itself: twice the barks' panel, twice the counters' and the reticle's translucent edges, for one step in two. At 60
frames a second that is every other frame; at 144, two or three frames on, two or three off, 30 times a second. The
fix takes `front_draw` only on a pass where the front end updated (`FrontEndRun::updated`, set in
`front_end_steps`) and trims `ui.draw` back to the front end's own quads after a fight's present.

`--state 6` (and `--vs`) enter the battle without `menu_set_state`'s cadence, so their front end updates every step and
never showed it (docs/backlog.md, Performance). The proof forced the 30 Hz cadence on `--state 6 --seed 1` with
`PHANTOMCRASH_DEV_ENTRY=0`, the walk_run_jump pad script and the new `PHANTOMCRASH_DEV_BETWEEN=2` (a capture draws two
frames a step, the second at alpha 0.5, docs/playthrough.md), 960x540 on lavapipe, frames 1100-1138 shot one by one:

- the 2D handed over: before the fix 109 quads (2 the front end's, 107 the HUD) on the frames after an update and 216
  on the frames after a skipped step, two frames of each in turn; after it 109 on every frame;
- the bark panel's mean brightness (its text box): before 39.1, 39.1, 33.5, 33.5, ... in turn; after 39.1 on every
  frame. The reticle's box moved by up to 4.5 levels a frame before (the doubled rings), at most 1.8 after (the 3D
  under it).

## Menus at the display's refresh

After the step-rate pacing the menus, the movies and a paused fight showed 60 frames a second on a 144 Hz display. They
present every pass again (src/front_end_run.cpp), and `pc_frame_end` waits out `pc_frame_seconds`
(src/screens/pc/pc_settings.cpp):

- v-sync on: FIFO paces them to the display; FRAME LIMIT, or at MAX the 500 ceiling, is the only wait, a backstop for a
  present that doesn't block (Mesa's software presentation on Xvfb doesn't: the hub ran 1,600-2,050 frames a second
  there without it);
- v-sync off: at least one refresh of the window's display (`pc_display_hz`: SDL's current mode, 60 when SDL says 0,
  as Xvfb does); FRAME LIMIT under that holds them to it;
- fights as before: every pass with the interpolation alpha, FRAME LIMIT or 500, v-sync on the display.

The picture still changes only on a 60 Hz step: between two, a menu presents the 2D the renderer kept and its 3D again.
SHOW FRAME RATE counts presented frames (`pc_frame_end`), so it reads the display's rate in the menus too.

Measured 2026-11-13 15:12-15:20 on RADV through a private Xvfb with `MESA_VK_WSI_DEBUG=sw`, 1280x960, 26 s each,
`PHANTOMCRASH_PERF=2`, CPU from `/usr/bin/time`, one run at a time; 1-minute load 4.3-7.0 (other agents). Xvfb reports
no refresh rate, so "the display" is 60 here and FIFO doesn't wait; on the owner's 144 Hz screen FIFO holds a fight to
144 (his run logs: 1,440 frames in 10 s, 6 ms a frame blocked in the acquire), and the menus now the same.

| scene | build | v-sync | FRAME LIMIT | frames/s | CPU over 26 s |
|---|---|---|---|---|---|
| garage hub (`--state d`) | 8f2667d | on | MAX | 60 | 1.5 s |
| garage hub | 8f2667d | off | MAX | 60 | 1.4 s |
| garage hub | this | on | MAX | 500 (FIFO not waiting on Xvfb: the ceiling) | 8.3 s |
| garage hub | this | off | MAX | 60 (the display's) | 1.3 s |
| garage hub | this | on | 144 | 144 | 2.3 s |
| garage hub | this | on, off | 30 | 30 | 0.9-1.0 s |
| attract movie (`--state 2`) | this | off | MAX | 60 | 3.4 s |
| paused fight (`--state 6`, `PHANTOMCRASH_DEV_PAUSE=120`) | 8f2667d | off | MAX | 60 | 3.5 s |
| paused fight | this | off | MAX | 60 | 2.8 s |
| paused fight | this | on | MAX | 495-499 (the ceiling, as above) | 12.0 s |
| tournament fight (`--state 6`) | 8f2667d | off | MAX | 475-485 | 17.8 s |
| tournament fight | this | off, on | MAX | 460-488 | 17.8 s, 16.7 s |

No amdgpu messages in `journalctl -k` over the runs. The nine movement traces print the same as merge5 (`^(step|t[0-9 ])`);
the title, hub, hangar, dealer, PLUS TECH, PC CONFIG, a VS round and a tournament fight capture byte-identical to
8f2667d on lavapipe (frames 20, 60, 119, 180, 239 and the last); the attract movie's frames 119 and 180 differ, as two
runs of 8f2667d differ at frame 119.

## Loads

From the after build's run logs (1080p; the same at 1440p within a few ms):

| load | time |
|---|---|
| start to the first frame (SDL, the Vulkan device, the packs, the sound banks, the front end) | 150-205 ms |
| a front-end battle (`--vs 4`: the stage, four machines, the effects) | 45-46 ms |
| the viewer's battle (the stage, six machines, the effects) | 65-67 ms |
| a stand's machine (read on the worker, uploaded on the main thread) | 3-5 ms, of it the upload 0.4-0.5 ms |
| boot to the title, the logos and the attract movie skipped as early as they allow | 8.9 s: the logos' own time |

Of the first frame's 150-205 ms, about 85 ms is SDL, the window and the Vulkan instance and device (the driver's), 15
ms the sound's banks and 10 ms `data_hash_print`. The battle and stand loads used to take one staging allocation per
mesh and texture (1,577 for the VS fight); they share the arena now. The base build said no load times, so there is no
before column; the loads were already short and nothing here waits on the disk after the packs are open.

With a cold shader cache (`MESA_SHADER_CACHE_DISABLE=true`) a fight's first second makes its 31 pipelines in 60 ms and
a few more appear later at 2-3 ms each, which a 144 Hz display can see as a dropped frame the first time an effect
shows. Mesa's on-disk cache makes that a first-run-after-a-driver-update thing.

## What is left

- **Pipeline warm-up.** A pipeline seen for the first time costs 2-3 ms on a cold driver cache (above). A
  `VkPipelineCache` saved to the user folder, or the battle's pipeline keys made during the load, would move that out
  of the fight. Small, and only on a first run; left.
- **The particles and the billboards.** `effects_step`'s particle update (`particle_step`, `motion_forces`) and
  `effects_queue`'s billboards (`billboards_prepare` and its sort, about 50 us a frame) are the biggest named CPU parts
  of a fight after the world step. They are ported from the original's code as it is; there was no waste to cut
  without changing what they compute.
- **The world step.** 115-220 us a step for six fighters, copied once a step for the presentation (`const World
  before = world`, 335 KB, 5-8 us). docs/multiplayer.md has the rollback budget.
- **One frame in flight.** Kept (above). If a slow GPU ever shows `blocked fence` in a run log, two frames in flight is
  the change, with the per-frame buffers doubled.
- **The GPU passes.** On this GPU none is worth caching: the widescreen blurred sides, the composite's copy, the
  `scene_last` copy for the big screens every 3D frame and the gamma pass with an identity ramp outside fights each
  cost 0.02-0.1 ms at 1440p. A weak integrated GPU might care about the gamma pass (skippable with an identity ramp)
  and the 32-tap blurred sides (cacheable while a menu's picture stays); measure there first, with the perf line.
- **Latency.** FIFO with minImageCount + 1 images lets the CPU queue up to three frames ahead of the display; fewer
  images would cut input latency, not cost.
- **The audio callback** (up to 1.1 ms, docs/multiplayer.md) runs on its own thread and is not in these numbers.
- **The setup.** These are one machine's numbers through software presentation. A run log from the owner's own play
  on his display gives the real `present` and `acquire`; anyone's slow machine gives the rest.
