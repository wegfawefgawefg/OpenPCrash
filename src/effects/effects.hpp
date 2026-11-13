#pragma once
// The battle's effects as presentation (decomp docs/port/effects.md): the particle pool, sprite
// effects and scorch marks, trails, contact decals, the hit static's scratches and flickers, the
// lens flare and the point lights, driven by the world's events (StageWorld::events) and the
// world's fighters. Retail steps all of it once a 30 Hz update; so does this, on every second
// world step, and the draws fall between the last two updates. Its random numbers are its own
// (a CRT-style generator per effect), never the world's. It runs in the original's frame (z not
// negated): positions from the world come in with z negated and go out the same way.
#include "battle/battle_ending.hpp"
#include "effects/billboards.hpp"
#include "effects/effect_data.hpp"
#include "effects/particles.hpp"
#include "render/d3dx_math.hpp"
#include "render/scene_draw.hpp"
#include "render/stage_lighting.hpp"
#include "render/vk_sprites.hpp"
#include "stage/stage_view.hpp"
#include "stage/stage_world.hpp"
#include "talk/draw2d.hpp"
#include "weapons/combat.hpp"

#include <cstdint>
#include <string>
#include <vector>

struct World;
struct PackLibrary;
struct Scenery;

// a sprite effect (SpriteFx, sprite_fx_spawn)
struct SpriteFx {
    bool active = false;
    int kind = 0;
    int frame = 0;
    Mat4 matrix, before; // this update's and the last (the smoke rises)
    float scale = 1;
    std::uint32_t factor = 0xFFFFFFFF; // the alpha word the kind's update writes
    std::uint32_t seed = 1;
    bool visible = false; // drawn this update, at draw_frame
    int draw_frame = 0;
};

// a scorch mark (ScorchMark, scorch_mark_add)
struct ScorchMark {
    int kind = 0;
    Mat4 matrix;
};

// Trail (include/effect.h): 33 points, newest first
struct Trail {
    bool active = false;
    Vec3f points[33];
    Vec3f before_head; // the newest point before its last push, for the draw between updates
    int length = 0;
    int mode = 0; // 0 texture 0x16D, 1 `texture`, 2 untextured in `factor`
    std::uint32_t factor = 0;
    int texture = -1;
    float width_start = 0, width_end = 0, alpha_start = 0, alpha_end = 0;
    int segments = 32;
    bool released = false;
    int age = 0;
    int key = -1; // who pushes it: the shot's pool index, or -2 - lamp
};

// a contact decal (contact_decals): the shockwave's 30-model sequence
struct ContactDecal {
    Mat4 matrix;
    int life = 0;
    int shown_age = -1; // the model this update drew (0x3FD + age), -1 none
    float glow = 0;     // effopt's glow: the life left / 30
};

// a fighter's shield bubble (shield_bubble.cpp): its aim point after the last two steps, and
// what the last update left (glow_alpha, the spin and the rainbow's phases it drew with)
struct ShieldBubble {
    Vec3f at, before; // original frame
    float heading = 0, radius = 0;
    float glow = 0; // Actor.glow_alpha
    bool drawn = false;
    float spin = 0, scroll = 0, phase = 0;
};

// screen_scratch_add's two rings: 0 the scratches, 1 the flickers
struct Scratch {
    float y = 0;
    int life = 0;
    float speed = 0;
};

// one point light (point_light_add), original frame
struct EffectLight {
    Vec3f at{0, -1000, 0};
    std::uint32_t colour = 0;
    float size = 0.01f;
    bool set = false;
};

// lens_flare_draw's state
struct LensFlare {
    int state = 0, state_prev = 0; // 0 off, 1 on, 2 fading in, 3 fading out
    unsigned alpha = 0, alpha_full = 0;
    float sun_x = 0, sun_y = 0, center_distance = 9, z = 0;
    // where the sun falls in the 3D stretched over the 640x480 (the depth probe's place)
    float probe_x = -1, probe_y = -1;
    float corner_turn = 0;
    bool visible = false; // drawn this update
    float last_depth = 0; // lens_flare_occlusion_test's flare_last_pixel: the last read was sky
    bool last_sky = false;
};

// a mount's break arcs (ActorEffects.arc_emission, arc_delay, arc_offset): side 0 the aim frame's
// (an arm's frame point, or the break pair), side 1 the hang's, four arcs each
struct BreakArcs {
    EffectHandle arc[2][4];
    int delay[2][4] = {{10, 10, 10, 10}, {10, 10, 10, 10}};
    Vec3f offset[2][4];
};

// a lamp's night trail and glow (ActorDecal, lamp_trails.cpp): the trail, and the glow this
// update drew (model, ARGB, its node matrix now and the update before, original frame)
struct LampTrail {
    int trail = -1;
    bool glow = false, glow_seen = false;
    int model = -1;
    std::uint32_t colour = 0;
    Mat4 matrix, before;
};

// what a fighter's effects remember between updates (ActorEffects, Actor +0x40E8)
struct FighterEffects {
    bool seen = false;
    int motion_state = -1;
    bool airborne = false;
    // the feet that stepped since the last update, bit k foot k (view/footsteps.hpp): retail's
    // motion events 0x400 << foot, raised in the step and read by the 30 Hz update
    std::uint32_t footsteps = 0;
    bool dive_used = false;
    Vec3f attach_before[14];
    bool seen_foot[6] = {};
    Vec3f foot_before[6];
    int spark_window = -1, spark_hold = -1;
    EffectHandle spark[6];
    int dust_timer[6] = {-1, -1, -1, -1, -1, -1};
    EffectHandle dust_feet[6];
    EffectHandle muffler[5][3];
    int muffle_timer[5] = {-1, -1, -1, -1, -1};
    EffectHandle vernier[2];
    int age = 0;
    float position[3] = {};
    float heading = 0;
    EffectHandle jump_fire[2];
    EffectHandle dive[2];
    EffectHandle hover[4];
    EffectHandle gun_smoke[4];
    EffectHandle burnout[3];
    bool wrecked = false;
    // Actor.weapon_state and weapon_break_timer by mount (weapon_break.cpp)
    int weapon_state[4] = {};
    int break_timer[4] = {};
    BreakArcs arcs[4];
    LampTrail lamps[4];
};

// a shot as one world step left it, for its look (shot_looks.cpp): the transform in the
// original's frame, the life and radius (ticks, units) and what draws
struct ShotSnap {
    enum Kind : std::uint8_t { none, flying, blast, beam };
    Kind kind = none;
    int weapon = -1;
    int look = -1; // a flying shot's shot_model row
    Mat4 m;
    float life = 0, radius = 0;
    int intensity = 0; // a beam's (Weapon.reload_left)
};

// a spent casing (Debris, 0x60: a matrix, a velocity a tick, a life in ticks), original frame
struct Debris {
    Mat4 matrix;
    Vec3f velocity;
    Vec3f before; // the place before the last update, for the draw between updates
    float life = 0;
};

// the models, textures and meshes the effects draw with
struct EffectAssets {
    std::vector<ViewModelSet> sets;
    std::vector<std::pair<int, std::pair<int, int>>> slots; // slot -> (set, object)
    ModelObject grid[2]; // grid_mdl 0x4DB as textured_quad_draw_full sets it: 0 blended, 1 additive
    ModelObject quad2;   // 0x3F8 as textured_quad2_draw_full sets it
    int grid_mesh = -1, quad2_mesh = -1;
    int atlas = -1;               // texture slot 0x190
    int flare = -1, flicker = -1; // slots 0x125, 0x140
    int flare_size[2] = {128, 128};
    ModelObject trail_objects[3]; // the trail model by mode: its section as trail_draw sets it
    // one mesh a trail for each split-screen view: trail_draw rewrites the vertices for the eye,
    // and every upload of a frame runs before any of its views draws, so views sharing one mesh
    // would all draw the last view's
    int trail_mesh[4][50] = {};
    std::vector<MeshVertex> trail_vertices; // the model's, uvs kept
    bool loaded = false;
};

struct Effects {
    EffectData data;
    ParticlePool pool;
    EffectAssets assets;
    std::uint32_t seed = 1; // the effects' own generator (the CRT draws outside the pool)

    SpriteFx sprites[128];
    float pulse_phase = 0; // sprite_fx_pulse_phase
    ScorchMark marks[100];
    int mark_next = 0, mark_count = 0;
    float mark_angle = 0;
    Trail trails[50];
    ContactDecal decals[20];
    Scratch scratches[2][10];
    LensFlare flare;
    EffectLight lights[6];
    int light_cursor = 0;

    // the supply craft's emitters (craft_effects): 0, 1 jets, 2..4 fires, 5 the crash
    EffectHandle craft[6];
    Mat4 craft_matrix, craft_prev_matrix; // original frame
    int craft_state = 0;
    // the ending's paper storm (craft_effects 6..22, supply_craft_paper_storm): five at the craft,
    // five at each of the scene's points A and B, two at C; the anchors the shot set last
    EffectHandle paper[17];
    EndingPaper paper_anchors;
    // crate pickups (pickup_get_effects): the emitter, the collector, updates left to finish
    struct GetItem {
        EffectHandle handle;
        int slot = -1;
        int finish = 0;
    } get_items[3];

    FighterEffects fighters[6];
    // a flash model shows for exactly one update (weapon_flash_model_*): slot, matrix
    struct Flash {
        int slot = -1;
        Mat4 matrix; // original frame
        bool billboard = false;
    };
    std::vector<Flash> flashes;
    // a shot's trail (shot_model_draw's trail_id, trail_started, trail_tick), by pool index
    struct ShotTrail {
        int trail = -1;
        int weapon = -1;
        bool started = false;
        bool tick = true;
    };
    std::vector<ShotTrail> shot_trails = std::vector<ShotTrail>(shot_pool_size);
    // the shots' looks after the last world step and the one before, by pool index
    std::vector<ShotSnap> shot_now = std::vector<ShotSnap>(shot_pool_size);
    std::vector<ShotSnap> shot_before = std::vector<ShotSnap>(shot_pool_size);
    // Battle.debris: 256 casings, the allocation cursor, and the pulls this update threw one
    Debris debris[256];
    int debris_cursor = 0;
    bool casing_due[6][4] = {};
    // the shield bubbles, shield_bubble_spin and the rainbow program's scroll and band phase
    // (D_00281740, D_00A69B24)
    ShieldBubble bubbles[6];
    float bubble_spin = 0, rainbow_scroll = 0, rainbow_phase = 1;
    std::vector<StageEvent> pending; // the world's events since the last update
    std::vector<CombatEvent> pending_combat;
    std::int64_t updates = 0;
    int phase = 0;              // world steps since the last update
    bool battle = false;        // a battle's effects (single view: the flare draws)
    int day_mode = 0;           // the stage selector: the flare on day modes 0 and 2
    int replay_view = -1;       // replay_view 0..5 in a replay (battle phase 2), -1 none
    float sun[3] = {0, 1, 0};   // sun_direction (the stage light's reverse), port frame
    Mat4 view_projection;       // view 0's, port frame, as the last frame drew it
    float sun_depth = 0;        // the depth at the sun's pixel last frame (1 the cleared far)
    // the one view's drawn 3D in the sprite layer's screen pixels as the last frame drew it: its
    // middle and half width and height (the port's 3D fills the window; retail's flare placed the
    // sun in the 576 x 448 safe area)
    float drawn_3d[4] = {320, 240, 320, 240};
    std::vector<Quad2D> screen; // this update's 2D: scratches, flickers, flare
    // PC: FLASHES (docs/settings.md), how bright the flashes are, 0..1: the effects' point lights,
    // the muzzle flashes, the lens flare and the hit static's flicker; presentation only
    float flash_scale = 1.0f;
    int screen_static = 0;      // how many of them are the static (scratches and flickers)
    int hit_seat = -1;          // StageWorld::hit_seat as the last step left it
};

// a colour's red, green and blue times the FLASHES scale (alpha kept)
std::uint32_t flash_colour(const Effects& fx, std::uint32_t colour);

// once per battle: the data from the core pack, the models and textures from theirs;
// effect_pool_clear and the rest cleared
bool effects_load(Effects& fx, PackLibrary& packs, Scenery& scenery, Renderer& renderer,
                  std::string& error);
void effects_reset(Effects& fx, int selector, std::uint32_t seed);
// after every world step (before it, world after): its events and footsteps gathered; on every
// second step, the 30 Hz update
void effects_step(Effects& fx, const World& before, const World& world);
// the frame between the last two updates (alpha: how far past the last world step): queued
// meshes (sprite effects, scorch marks, trails, flashes), the billboards of pass 10, the point
// lights; view is the port's view matrix of the drawn view, eye its eye (port frame), and
// view_index which split-screen view it is (0..3; the trails keep their meshes per view)
void effects_queue(Effects& fx, Renderer& renderer, float alpha, const Mat4& view,
                   const Mat4& projection, const float eye[3], int view_index,
                   std::vector<PassItem>& items, std::vector<Billboard>& billboards,
                   PointLight lights[6]);
// a split-screen view the effects' 2D goes into: its index (Quad2D::view), retail's rectangle for
// it, where its 3D is drawn, and whether the hit static shows there
struct ScreenView {
    int view = -1;
    const float* rect = nullptr;  // x, y, w, h of the 640x480 screen
    const float* drawn = nullptr; // the same
    bool static_shown = true;
};
// the 2D over the 3D: the hit static's scratches and flickers, the lens flare; over the one view
// (view null), or into a split-screen view (the static fitted to it, no flare)
void effects_screen(Effects& fx, Draw2D& draw, std::vector<SlotTexture>& slot_textures,
                    const ScreenView* view);

// stage_effects.cpp: the stage's events and its craft, props and crates
void stage_effects_event(Effects& fx, const World& world, const StageEvent& event);
void stage_effects_update(Effects& fx, const World& world);
// combat_effects.cpp: impacts, muzzles and wrecks from world.combat.events, the shots' trails,
// the stage's Light and GetItem events
void combat_effects_event(Effects& fx, const World& world, const CombatEvent& event);
void stage_light_event(Effects& fx, const StageEvent& event);
void combat_effects_update(Effects& fx, const World& world);
void combat_effects_queue(Effects& fx, const Mat4& view, const Vec3f& eye,
                          std::vector<PassItem>& items);
// fighter_effects.cpp: actor_effects_update's emitters, from the world's fighters and
// their live pose (FighterPoseState attachments 0..13, feet 14..19)
void fighter_effects_update(Effects& fx, const World& world);
// matrix_from_direction, no translation (kind 0 and 2 alike; 1 flattens y to a tenth)
Mat4 from_direction(Vec3f dir, int kind);

// sprite_fx.cpp
void sprite_fx_spawn(Effects& fx, int kind, float scale, const Vec3f& at);
void sprite_fx_update(Effects& fx);
void scorch_mark_add(Effects& fx, int kind, float scale, const Mat4& top);
void sprite_fx_queue(Effects& fx, float t, const Mat4& view, const Vec3f& eye,
                     std::vector<PassItem>& items);
// trails.cpp
int trail_start(Effects& fx, const Vec3f& at, int segments, int key);
void trail_push(Effects& fx, int trail, const Vec3f& at, int texture, int mode,
                std::uint32_t colour, float width_start, float width_end, float alpha_start,
                float alpha_end);
void trail_push_default(Effects& fx, int trail, const Vec3f& at);
void trail_release(Effects& fx, int trail);
void trails_update(Effects& fx);
void trails_queue(Effects& fx, Renderer& renderer, float t, const Mat4& view, const Vec3f& eye,
                  int view_index, std::vector<PassItem>& items);
// shot_looks.cpp: every world step, then each frame between the last two steps (alpha)
void shot_looks_step(Effects& fx, const World& world);
void shot_looks_queue(Effects& fx, float alpha, const Mat4& view, const Vec3f& eye,
                      std::vector<PassItem>& items);
// contact_decals.cpp: contact_decal_spawn, the fighters' contacts and the lives, the draw
void contact_decal_spawn(Effects& fx, const Mat4& matrix);
void contact_decals_update(Effects& fx, const World& world);
void contact_decals_queue(const Effects& fx, const Mat4& view, std::vector<PassItem>& items);
// shield_bubble.cpp: every world step the aim points, every update glow_alpha and the spin
void shield_bubble_step(Effects& fx, const World& world);
void shield_bubble_update(Effects& fx, const World& world);
void shield_bubble_queue(const Effects& fx, float alpha, const Mat4& view,
                         std::vector<PassItem>& items);
// weapon_break.cpp: a mount broke (event_weapon_break); the states' sparks and arcs each update
void weapon_break_event(Effects& fx, int slot, int mount);
void weapon_break_update(Effects& fx, const World& world);
// lamp_trails.cpp: the lamps' trails and glows at night (actor_decals_update)
void lamp_trails_update(Effects& fx, const World& world);
void lamp_glows_queue(const Effects& fx, float t, const Mat4& view, std::vector<PassItem>& items);
// casings.cpp: weapon_eject_casing for a mount's pull, debris_update, the draw
void casing_eject(Effects& fx, const World& world, int slot, int mount);
void casings_update(Effects& fx);
void casings_queue(const Effects& fx, float t, const Mat4& view, std::vector<PassItem>& items);
// cockpit_draw.cpp: the HUD's cockpit instruments this frame into pass 11
struct HudCockpit;
// and the in-world readout's quads on quad (textured_quad_draw's grid_mdl, the stage view's)
void effects_queue_cockpit(Effects& fx, const HudCockpit& cockpit, const ModelObject& quad,
                           int quad_mesh, std::vector<PassItem>& items);
// screen_effects.cpp
void screen_scratch_add(Effects& fx, float y, int life, float speed, int kind);
void screen_effects_update(Effects& fx, const World& world);
// after every step: whose view the hit static shows in (StageWorld::hit_seat; the FX test's seat)
void screen_effects_seat(Effects& fx, const World& world);

// helpers shared by the files
// effect_set_play: the set's emitters and its sprite effects (at pos, scale)
EffectHandle effects_play(Effects& fx, int set, const Vec3f& pos, const Vec3f& prev,
                          float scale = 1);
void effects_light(Effects& fx, const Vec3f& at, std::uint32_t colour, float size);
// a model slot the effects loaded, at a matrix of the original's frame (model_render_submit)
void effects_queue_slot(const Effects& fx, int slot, const Mat4& world, int pass,
                        std::uint32_t flags, std::uint32_t factor, const Mat4& view,
                        std::vector<PassItem>& items);
// textured_quad_draw_full (grid, by base slot) and textured_quad2_draw_full
// (quad2, billboarded by the mesh's 0x200 the way pass_draw_meshes turns it)
void effects_queue_quad(const Effects& fx, int base, const Mat4& world, std::uint32_t flags,
                        std::uint32_t factor, int pass, const Mat4& view,
                        std::vector<PassItem>& items);
void effects_queue_quad2(const Effects& fx, int base, const Mat4& world, std::uint32_t flags,
                         std::uint32_t factor, int pass, const Mat4& view, const Vec3f& eye,
                         std::vector<PassItem>& items);
// the world's port-frame fixed point as the original's frame in floats
Vec3f original_of(const FxVec3& v);
inline Vec3f mirror(const Vec3f& v) {
    return {v.x, v.y, -v.z};
}
// the original's matrix_billboard_at at a point for an eye (original frame)
Mat4 billboard_matrix(const Vec3f& at, const Vec3f& eye, float off_x, float off_y, float off_z);
// a matrix of the original's frame as the port's (M W M with M the z flip)
Mat4 port_matrix(const Mat4& m);
