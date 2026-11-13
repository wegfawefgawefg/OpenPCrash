# Shaders

The original's GPU programs rewritten as GLSL 450 for Vulkan, in `assets/shaders/original/`. Each file's header says
which original program and bind row it is, its constants (hardware register to name), inputs and outputs, what it
computes and what changed. This page is the index, the vertex formats, the binding layout the renderer has to provide,
and the parts of the look that need passes or targets of their own. The spec is the decomp's `docs/port/render.md`
("Shaders", "Mesh drawing", "Xbox-only behaviour") and the constant handlers in `src/hand/u_0bee60.c`.

The 53 originals are in `Media/Shaders` on the disc. `python3 -I ../phantom-crash-decomp/tools/port/nv2a_shaders.py
<disc>/Media/Shaders` lists them (vertex microcode, texture modes, combiner equations). The listing is derived from game
files: write it to `data/shaders/` (gitignored) or anywhere outside the repo, never commit it.

## Conventions

`include/d3d.glsl` has them in full. Briefly:

- Matrices are uploaded as the C code builds them, before its transpose: `m * v` is D3D's `v * M`. Programs that were
  given an untransposed matrix (effopt's world and inverse world, multi_matrixPL's heading inverse) do `v * m` and lose
  the translation, as the original does.
- Vertex shaders write D3D clip coordinates. The renderer flips y with a negative viewport height and uses front face
  clockwise, so D3D `CULL_CCW` is `VK_CULL_MODE_BACK_BIT`. Every original program's NV2A viewport epilogue is dropped.
- Interpolants sit at fixed locations (0 oD0, 1 oD1, 2..5 oT0..oT3, 6 the fog factor) so vertex and fragment stages of
  different rows pair up. Vertex colour outputs are clamped to 0..1 as the NV2A does.
- Registers a program never writes are written as `NV2A_DEFAULT_TEXCOORD` (0, 0, 0, 1) and `NV2A_DEFAULT_FOG_COORD` 0.
  Both are assumptions; several effects read such registers (below).
- Combiner arithmetic (`include/nv2a.glsl`) clamps every register write to -1..1 and uses the NV2A input mappings.
  Combiner constants are 8 bits a channel: the renderer quantizes PS constants before upload.
- Linear render targets are addressed in texels on the Xbox. Every texel coordinate is divided by the original's size
  of the texture (640x480, 320x240, 160x120), never the port's, so offsets keep their fraction of the screen at any
  resolution.

## Program to port shader

Rows are `shader_binds` 0x280C30; the handler is the row of 0x280BB8 that sets its constants. "fixed" is no program:
D3D8's fixed-function pipeline, which the port also has to write as shaders.

| row | original vertex / pixel | port vertex / fragment | used by |
|---|---|---|---|
| 0 | effopt.xvu / effopt.xpu | effopt.vert / effopt.frag | pass 6 shockwave decals over the backdrop |
| 2 | bump.xvu / bump.xpu | bump.vert / bump.frag | meshes, layer[0] bit 2 with two textures |
| 3 | multi_matrix.xvu / multi_matrix.xpu | multi_matrix.vert / multi_matrix.frag | pass 4 fighters outside battle |
| 4 | multi_matrixPL.xvu / multi_matrixPL.xpu | multi_matrix_pl.vert / multi_matrix_pl.frag | pass 4 fighters in battle |
| 5 | multi_optical.xvu / Blend.xpu | multi_optical.vert / blend.frag | pass 5 camouflage |
| 6 | multi_shadow.xvu / fixed | multi_shadow.vert / none (colour writes off) | pass 3 fighter shadow volumes |
| 8 | dof.xvu / dof.xpu | screen_quad.vert (DOF_XVU) / dof.frag | pass 11 battle composite |
| 9 | blur.xvu / blur.xpu | screen_quad.vert / blur.frag | composite downsample 640 -> 320 -> 160 |
| 13 | shimmer.xvu / shimmer.xpu | screen_quad.vert / shimmer.frag | dead (`shimmer_on` never set) |
| 14 | particle.xvu / particle.xpu | particle.vert / premultiplied.frag | pass 10 billboards |
| 15 | spritemat.xvu / spritemat.xpu | spritemat.vert / premultiplied.frag | overlay passes 0, 12, 14, 15, 16 |
| 16 | point.xvu / point.xpu | point.vert / point.frag | battle pass 2, lit, translucent |
| 17 | point.xvu / pointalpha.xpu | point.vert / point.frag (ALPHA_FROM_VERTEX) | battle pass 2, lit, opaque |
| 18 | pointconst.xvu / point.xpu | point.vert (UNLIT) / point.frag | battle pass 2, unlit, translucent |
| 19 | pointconst.xvu / pointalpha.xpu | point.vert (UNLIT) / point.frag (ALPHA_FROM_VERTEX) | battle pass 2, unlit, opaque |
| 20 | seabump.xvu / seabump.xpu | seabump.vert / seabump.frag | water, layer[0] bit 0x800 |
| 22 | fixed 0x152 / vision_eff.xpu | ff_mesh.vert / vision_eff.frag | mesh 0x200000 |
| 23 | fog.xvu / fixed | fog.vert / ff_mesh.frag | default unlit meshes (height fog) |
| 24 | cockpitUV.xvu / fixed | cockpit_uv.vert / ff_mesh.frag | pass 11 instruments with a texture |
| 25 | rainbow.xvu / rainbow.xpu | rainbow.vert / rainbow.frag | mesh 0x100000, the shield bubble |
| 26 | reflect.xvu / reflect.xpu | reflect.vert / reflect.frag | mesh 0x1000000 or layer[1] bit 0x80 |
| 28 | dof.xvu (2nd copy) / dofblur.xpu | screen_quad.vert (DOF_XVU) / dofblur.frag | pass 11 composite with heat haze |
| - | fixed 0x152 / fixed | ff_mesh.vert / ff_mesh.frag | lit meshes outside battle, mesh 0x200, untextured pass 11, uv-override quads, result trophies |
| - | fixed 0x152 / none | ff_mesh.vert / none | pass 7 static shadow volumes |
| - | fixed 0x104 and 0x44 / fixed | screen_ff.vert / screen_ff.frag | fullscreen_quad copies and tint, shadow colour quad (SHADOW_QUAD) |
| - | none (video output) | screen_ff.vert / gamma.frag | the per-stage gamma ramp, the port's last pass |

Specialization constants: `DOF_XVU` (screen_quad.vert, id 0), `UNLIT` (point.vert, id 0), `ALPHA_FROM_VERTEX`
(point.frag, id 1), `DEPTH_FROM_DEFAULT_TEXCOORD` (dofblur.frag, id 1), `SHADOW_QUAD` (screen_ff.frag, id 0).

### Which mesh gets which

`pass_draw_meshes` 0xB31C0 takes the first match per sub-mesh: pass 11 (textured: row 0x18, else fixed), bump, rainbow,
reflect (two conditions), seabump, vision_eff, the point rows (battle, pass 2, textured, no texture factor), fixed if
lit, fixed if mesh 0x200, else fog (row 0x17). The fixed-function path is chosen by these render states:

- `LIGHTING` (DrawState `DS_LIGHTING`): on unless sub 0x100, pass 11 or wireframe; in pass 9 only with mesh 0x10000.
  Lit vertices get one directional light (light 0, light 1 in pass 13) with the D3D8 defaults the game leaves:
  NORMALIZENORMALS on, LOCALVIEWER on, diffuse from the vertex colour, ambient from the material (the sub-mesh colour),
  specular from the material (1), no global ambient.
- `SPECULARENABLE` (`DS_SPECULAR`): sub 0x40 unless mesh 0x8000000. Specular is added after the texture stages.
- `FOGENABLE` (`DS_FOG`) with `FOGTABLEMODE`, colour, start, end, density in the Frame block.
- Texture stage ops (DrawState `stages`, 8 bits a stage, encoded in ff_mesh.frag's header): modulate or add per
  `layer[s]` bit 1; alpha from texture, current or their product per layer bit 0x40, mesh 0x40 and 0x40000; the first
  untextured stage of a mesh with flag 0x20 multiplies by TEXTUREFACTOR.
- `ALPHAKILL` on stage 0 (`DS_ALPHA_KILL`) and alpha test GEQUAL with ref 0 or 0x80 (`DS_ALPHA_TEST`, ref in bits 8..15).

`pass_draw_fighters` always uses rows 3 or 4 and `pass_draw_camo` row 5, so their stage ops don't matter: a pixel
shader replaces them. Alpha kill and filters still apply.

## Not translated

- Bind rows with no caller (render.md): 1 (shadow.xvu, no pixel shader), 7 (zmap.xvu / zmap.xpu), 10 (multi_zmap.xvu /
  zmap.xpu), 11 (offset.xpu; its vertex handle is never loaded), 12 (RadBlur.xvu / RadBlur.xpu), 21 (barrier.xvu /
  barrier.xpu), 27 (fixed 0x152 with the second loaded copy of seabump.xpu).
- Loaded and never bound: pointshimmer.xvu. Not loaded at all: monitor.xvu, monitor.xpu, morph.xvu, optical.xvu,
  optical2.xvu, multi_matrixRainbow.xpu, bump2.xpu.
- fogshadow.xpu: loaded by `render_device_setup` for the shadow quad, used only while `shadow_shader` is set, which the
  code only ever clears. Its combiners (colour = v0, alpha x c0.a) are noted in screen_ff.frag.

## Vertex formats

All on one stream.

| name | stride | layout | used by |
|---|---|---|---|
| A, FVF 0x152 | 0x24 | pos float3 @0, normal float3 @12, colour D3DCOLOR @24 (`B8G8R8A8_UNORM`), uv float2 @28 | every mesh |
| B | 0x2C | A, then v4 float @0x24 = bone x 4 (the float at 0x28 is never read) | fighter part batches, rows 3..6 |
| screen quad | 0x30 | pos float4 (clip space) @0, four uv float2 @16 | rows 8, 9, 13, 28 |
| XYZRHW + TEX1 (0x104) | 0x18 | pos float4 (pixels, rhw) @0, uv float2 @16 | fullscreen_quad |
| XYZRHW + DIFFUSE (0x44) | 0x14 | pos float4 @0, colour D3DCOLOR @16 | shadow_screen_quad |
| billboard | 48 a billboard | storage buffer: pos (view space, w = flag), colour, cell (u, v, cos x size, sin x size) | particle.vert |
| sprite vertex | 48 a vertex | storage buffer: (x, y, u, v), colour, (k, 0, 0, 0); four a sprite | spritemat.vert |

Location 0 is position (read as vec4: the vertex fetch fills w = 1 like D3D), 1 normal, 2 colour, 3 uv, 4 bone
selector; the screen quad uses 1..4 for its four uvs. Particles and sprites have no vertex buffer: six vertices per
instance or sprite from `gl_VertexIndex`, replacing the original's 192-constant mode.

## Binding layout

The renderer (src/render/vk_pipeline.cpp) uses it as below. The per-draw blocks are built on the CPU by
src/render/mesh_constants.cpp (the handlers) and copied into one dynamic-offset ring per frame; set 1 binding 1 is
bound to a small placeholder until particles and sprites use it.


| set, binding | what | update |
|---|---|---|
| push constants | `DrawState`, 16 bytes: flags (fog, alpha kill, alpha test + ref, specular, lighting), fixed-function stage ops, TEXTUREFACTOR, which stages take texel coordinates | per draw |
| 0, 0 | `Frame` UBO, 64 bytes: fog colour, fog start/end/density/mode, the original's linear target size (640, 480, 1/640, 1/480), the pixel-centre offset | per frame (fog changes with the day cycle) |
| 1, 0 | the row's per-draw UBO, one dynamic-offset ring | per draw |
| 1, 1 | storage buffer of billboards (pass 10) or sprite vertices (overlay passes) | per frame |
| 2, 0..3 | the four texture stages as combined image samplers; filter and address modes are the stage's sampler | per draw |

Per-draw blocks (std140; sizes before rounding to `minUniformBufferOffsetAlignment`):

| block | size | holds (register order in each file's header) |
|---|---|---|
| FixedDraw | 288 | world, view, proj; light direction, diffuse, ambient, specular; material ambient; power |
| FogDraw | 144 | world x view x proj, world, (1/100, 0, 1, 0.5) |
| CockpitDraw | 80 | world x view x proj, (0, 1, u offset, v offset) |
| BumpDraw | 240 | world x view x proj, world^-1, world; k, object-space light, eye |
| RainbowDraw | 160 | world x view x proj; light, diffuse, ambient, (1, 0, scroll, 0.5), phases, strength |
| ReflectDraw | 192 | world x view x proj, world x view; light, diffuse, ambient, k |
| SeabumpDraw | 96 | world x view x proj, stage 1 bump matrix, du/dv signedness |
| PointDraw | 304 | world x view x proj, world; k, light, diffuse, ambient, 3 light positions, ranges, 3 light colours (PS c1..c3) |
| MultiMatrixDraw | 1344 | proj, 19 bone x view; light, k, diffuse, ambient |
| MultiMatrixPlDraw | 1552 | proj, 19 bone x view, view, heading inverse; light, k, diffuse, ambient, point and lamp positions, lamp direction, point and lamp colours (PS c1, c2) |
| MultiOpticalDraw | 1472 | proj, view, 19 bones; k, viewport size, diffuse, light, reveal sphere, fog k, viewport origin, ambient |
| MultiShadowDraw | 1312 | view x proj, 19 bones; (50, 0.5, 0, 1), light direction |
| EffoptDraw | 336 | world x view x proj, world^-1, world; k, viewport size, material, light, sphere, fog k, viewport origin, eye, glow (PS c0) |
| ScreenDraw | 128 | four stage texel scales, PS c0..c2, the heat-haze bump matrix |
| ParticleDraw | 64 | projection |
| SpriteDraw | 32 | (1, 0, 0.5, W/2 - 0.5), (0, 0, H/2 - 0.5, 0) |

The fighter blocks carry 19 bones (`part_batch_build`'s limit). For drawing at the display rate over the 30 Hz battle
the renderer keeps two bone palettes per fighter and writes interpolated bones into these blocks per frame; the
alternative is one storage buffer of palettes indexed by a push constant, which avoids copying 1.2 KB per part draw.
PS constants live in the same per-draw block as the vertex constants; the fragment stages read only their own fields.

## Effects that need passes or targets

These are the renderer's job; the shaders assume them.

- **Targets.** Scene 0x223 and backdrop 0x222 (colour, the original's 640x480 linear), one depth-stencil shared by all
  of them (D24S8; `D32_SFLOAT_S8_UINT` where D24 is missing), capture 0x225, and the 320x240 and 160x120 blur targets.
  The frame's first target and clear follow `render_frame_open` (backdrop if passes 5 or 6 have entries, else scene if
  any of 1..10 do, else the final target).
- **Camouflage and shockwave backdrop.** The first pass 5 opener in a frame copies 0x222 to 0x223 (screen_ff, factor
  white) and continues in 0x223; passes 5 and 6 sample 0x222 point-filtered at the refracted texel. The depth buffer
  carries over between the two targets. The port (src/render/vk_backdrop.cpp) keeps drawing into the scene target: a
  draw flagged `DrawItem::backdrop` (a view's first pass 5 or 6 draw) ends the scene pass, blits the 640x480 screen's
  rectangle of the scene over the backdrop (`texture_backdrop`) and resumes in a pass that loads colour, depth and
  stencil.
- **Scene composite at pass 11.** Outside battle: screen_ff with TEXTUREFACTOR = `scene_tint`. In battle (`dof_on`):
  blur.frag twice (scene -> 320x240 -> 160x120), then dof.frag, or dofblur.frag when the stage has `heat_wave_on`.
  Everything after pass 11 (cockpit, HUD, fades) is drawn on the final target untinted and unblurred. A frame with no
  3D (passes 1..10 empty, the front end) has no composite (`pass_open_effects` tests `scene_3d`): the scene target,
  its clear and pass 0, goes to the final target as it is (`vk_record_scene_copy`).
- **Overlay sprites.** For now the front end's own pair, `assets/shaders/sprite.vert` / `sprite.frag`, with the
  corners built on the CPU (`src/render/vk_sprites.cpp`, docs/screens.md), not spritemat.vert and the storage buffer
  below. Pass 0 draws into the scene target before the meshes; 12, 14 and 15 in a render pass of their own that loads
  the final target after the composite, before gamma.
- **Depth read as colour.** dof and dofblur sample the depth aspect of the shared depth buffer and rebuild the top byte
  of the 24-bit depth (`include/depth_byte.glsl`), bilinear like the original. The depth image must be in a read-only
  depth layout for the composite; nothing writes depth then (z off), and pass 13 clears it afterwards.
- **Heat haze.** Two 64x64 du/dv textures (`R8G8_SNORM`, the original's V8U8), one rewritten per composite
  (`wave_texture_update`: rows of a sine, fading at the bottom) and the two alternated. The bump matrix comes from
  `heat_bump_scale_set`.
- **Shadow volumes.** Pass 3: multi_shadow.vert, colour writes off, z test on, no z write, stencil always, front faces
  INCR_WRAP and back faces DECR_WRAP, then the same pair with z off and the operations swapped. Pass 4 to 6 pipelines
  set stencil pass ZERO so fighters clear the count they cover. Pass 7: static volumes through ff_mesh.vert the same
  way (the extra pair only when the eye is inside the sub-mesh's box), then the shadow quad (screen_ff SHADOW_QUAD)
  blended where stencil >= 1, replacing it with 1. Vulkan's two-sided stencil does each pair in one draw.
- **Height fog.** No extra pass: fog.vert and point.vert (UNLIT) scale the fog coordinate by height; point.vert (lit)
  and the fighter rows by light falloff and the reveal sphere. All go through D3D's fog table in `frame.glsl`.
- **Point lights.** Six per stage (`point_light_add`, cleared every battle update) plus the stage's lamps. The CPU
  picks the three nearest the mesh centre for the point rows and the nearest light and lamp for multi_matrixPL, and
  snapshots them per update with the meshes.
- **Sampling the target being drawn.** Mesh 0x80000 (big screens) samples 0x223 while drawing into it. The port gives
  it last frame's scene: the composite first copies the scene's 640x480 screen into `scene_last` (`texture_scene_last`,
  `vk_record_scene_last`), black until a frame with 3D. Retail's screens (pass 8) would see this frame's passes 0..7
  so far, or last frame's whole scene when the backdrop took passes 0..4 (UNKNOWN which a running Xbox shows).
  Retail's 0x223 holds the 3D in the title-safe rectangle (32, 16, 576x448) with black around it, and the screens'
  uvs run 0..640 by 0..520 texels clamped, so they show the picture in a black frame, 56 rows of it at the bottom.
  The port's stretched 3D (the safe-area option off) is copied into that rectangle of `scene_last`, the margins kept
  black; a straight copy clamped the picture's last row down the bottom tenth instead (playtest 2026-11-13).
- **Lens flare.** `lens_flare_occlusion_test` reads one depth texel on the CPU; use an occlusion query or a one-frame-late
  readback.
- **Gamma.** The per-stage ramp as the last pass (gamma.frag), over everything including the HUD; the identity ramp
  outside a battle (`FrameSetup::gamma` defaults to it).
- **The PC's settings** (`RenderOptions`, docs/settings.md): the player's brightness curves the ramp before it is
  written; soft focus off turns the frame's `dof` and `heat_wave` off (the tinted copy); the render scale sizes every
  target (the output is blitted to the window, linear when the sizes differ); 4:3 fits the 3D viewport in the 2D's
  box; the texture filter adds anisotropy to the filtered samplers; v-sync picks the present mode. No shader changes.

## Animation that the shaders' constants carry

These advance per draw call in the original's constant handlers or draw code, so their speed depends on how many
things are drawn: rainbow's scroll (-0.01) and band phase (-0.03), effopt's glow index, bump's unused wave phase,
`water_bump_angle` (+1/30 per water sub-mesh), the wave texture (per composite). The port advances them per update
(render.md, "Rendering at the display rate").

## What can't be checked until the renderer runs

The renderer runs every mesh row above except vision_eff, multi_matrix and spritemat (nothing queues
them yet; cockpitUV runs for the cockpit instruments, src/effects/cockpit_draw.cpp, after the composite with the
DrawItem::cockpit flag; multi_optical runs for camouflaged fighters, src/render/camo_draw.cpp; rainbow for the shield bubble and
effopt for the contact decals, src/effects/shield_bubble.cpp and contact_decals.cpp, validated on lavapipe with the
validation layer quiet for their draws), plus the composite, the shadow quad and gamma. Compared with
retail Shinjuku day under xemu (the decomp's runtime harness) by eye: the tint, fog, lighting, softness and the
dark vignette of the heat-haze composite match (captures in the renderer report). Still open:

1. The defaults for unwritten registers: oT components (0, 0, 0, 1) and oFog 0. dofblur's whole-screen depth tap
   reads the cleared depth in retail (the texel is outside the safe rectangle); the port binds white for it, which
   gives the same blend in both viewport modes and matches the retail screenshots' softness. Reflect's fog, Blend's
   t2 and the fixed-function stages 1..3 still rest on the assumption.
2. Fog per vertex versus per pixel, and the fixed-function fog coordinate (view depth assumed): looks right.
3. How the NV2A reads du/dv from yurayuraTest.bmp, an unsigned texture, for the water (`nv_dudv`): unchecked (no
   retail water capture yet).
4. 8-bit PS constant packing (round assumed) and the dof threshold that results; the combiners' fixed-point
   precision under dof's 32x gain.
5. Pixel centres: each moves the picture by under a pixel; not compared.
6. Stage 0's alpha kill during the composite and copies: the port turns it off there.
7. Fixed-function lighting details: specular only where N.L > 0, power 0 taken as 1, the material emissive 0.
8. Stage state left stale when a sub-mesh's texture index is >= 0x226 (treated as disabled).
9. Whether the bump offsets on linear targets are in texels (assumed, as xemu).
10. multi_matrix_pl.vert keeps the lamp term's RSQ of a zero direction finite (stages without lamps); the NV2A's
    result for it is unknown (docs/bugs.md).

## Compile check

`glslangValidator -V --target-env vulkan1.1` on every `.vert` and `.frag` from `assets/shaders/original/` (the
includes resolve relative to the file), then `spirv-val --target-env vulkan1.1`: 35 shaders, all compile and validate,
with each specialization constant at its default. The build compiles them all (cmake/renderer.cmake); the mesh,
composite and gamma rows run on lavapipe with the validation layer quiet. `glslc` is not installed here.
