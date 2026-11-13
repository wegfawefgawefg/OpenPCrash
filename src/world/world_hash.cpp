#include "world/world_hash.hpp"

#include "world/world.hpp"
#include "world/world_hash_put.hpp"

#include <cstdio>
#include <string>
#include <utility>
#include <vector>

// every struct hashed here, at the size its put() was written for (the game's 6 slots): a new
// field changes it, and wants hashing before the size is updated
#define PC_HASHED(T, size) static_assert(fighter_slots != 6 || sizeof(T) == (size), #T)
PC_HASHED(AiController, 376);
PC_HASHED(AiFirePacing, 16);
PC_HASHED(AiRoute, 36);
PC_HASHED(AiWeapon, 24);
PC_HASHED(BattleEvent, 24);
PC_HASHED(BattleResult, 52);
PC_HASHED(BattleSlot, 184);
PC_HASHED(Camo, 36);
PC_HASHED(CombatEvent, 64);
PC_HASHED(CombatWorld, 152768);
PC_HASHED(ControlBindings, 22);
PC_HASHED(ControlRecord, 20);
PC_HASHED(Crate, 40);
PC_HASHED(DamageState, 144);
PC_HASHED(Fighter, 11224);
PC_HASHED(FighterAttachment, 48);
PC_HASHED(FighterCamera, 120);
PC_HASHED(FighterCombat, 1608);
PC_HASHED(FighterHud, 252);
PC_HASHED(FighterPoseState, 8920);
PC_HASHED(FighterScene, 60);
PC_HASHED(FlyCamera, 20);
PC_HASHED(HudSense, 32);
PC_HASHED(LobbyRules, 3);
PC_HASHED(LockHud, 396);
PC_HASHED(LockMark, 12);
PC_HASHED(LockWarning, 8);
PC_HASHED(MotionTiming, 16);
PC_HASHED(MountTrigger, 12);
PC_HASHED(PcControls, 1);
PC_HASHED(Prop, 728);
PC_HASHED(Shot, 140);
PC_HASHED(ShotThreatLists, 200);
PC_HASHED(StageEvent, 48);
PC_HASHED(StageSetup, 16);
PC_HASHED(StageWorld, 77704);
PC_HASHED(StickFlick, 8);
PC_HASHED(SupplyCraft, 180);
PC_HASHED(VsBattle, 160);
PC_HASHED(Weapon, 168);
PC_HASHED(World, 335920);
PC_HASHED(BattleState, 1648);
#undef PC_HASHED

// The fight's part: every field of every struct the battle steps, in declaration order. The
// constant tables (Weapon::record, StageWorld::data, World::battle and the two table pointers)
// are left out; they come from the battle data, which the gameplay data hash covers.

// --- the fighter ---

void put(Hasher& h, const ControlBindings& b) {
    put_all(h, b.fire, b.jump, b.button1, b.dodge_a, b.dodge_b, b.camo, b.dpad_up, b.dpad_down);
}
void put(Hasher& h, const StickFlick& f) {
    put_all(h, f.state, f.window);
}
void put(Hasher& h, const ControlRecord& r) {
    put_all(h, r.control_axis, r.control_buttons);
}
void put(Hasher& h, const MotionTiming& t) {
    put_all(h, t.root_track, t.loop_start, t.loop_end, t.end);
}
void put(Hasher& h, const FighterCamera& c) {
    put_all(h, c.view, c.view_next, c.view_switch_frames, c.rail_repick_clock, c.rail_point,
            c.view_yaw, c.view_pitch, c.pitch, c.pitch_stick, c.recenter_frames,
            c.recenter_pitch, c.auto_turning, c.auto_turn_delta, c.quick_turn_lock,
            c.quick_turn_left, c.head_offset, c.eye, c.target, c.shake_shift, c.up);
}
void put(Hasher& h, const MountTrigger& t) {
    put_all(h, t.fire, t.raise, t.pressed, t.ranged);
}
void put(Hasher& h, const Weapon& w) {
    put_all(h, w.id, w.mount, w.fitted, w.present, w.weight, w.elevation, w.reload_frames,
            w.shot_life, w.speed, w.speed_max, w.accel, w.pellet_spread, w.turn_rate, w.shots,
            w.damage, w.blast_radius_end, w.blast_radius_start, w.impact_frames, w.impact_damage,
            w.resource_max, w.recharge_frames, w.stagger, w.knockback, w.break_chance);
    put_all(h, w.lock_target, w.volley_active, w.volley_index, w.volley_timer, w.beam_shot,
            w.reload_left, w.muzzle, w.resource, w.launch_pitch, w.reach_max, w.reach_min,
            w.clip_time, w.clip_start, w.clip_end, w.clip_state, w.clip_next, w.clip_turn);
    // w.record: the table row of w.id
}
void put(Hasher& h, const DamageState& d) {
    put_all(h, d.center, d.sphere_radius, d.pending, d.facing, d.stagger_peak, d.immune_frames,
            d.weapon_break, d.break_side, d.last_weapon, d.last_hit_step);
}
void put(Hasher& h, const LockMark& m) {
    put_all(h, m.target, m.timer, m.mount);
}
void put(Hasher& h, const LockHud& l) {
    put_all(h, l.track_x, l.track_y, l.track_inv_depth, l.box_a, l.box_b, l.locks, l.warning,
            l.melee_locked, l.mount_lock_target, l.mount_locks, l.mount_lock_max,
            l.lock_candidates, l.lock_candidate_count, l.lock_candidate_used, l.locked_on);
}
void put(Hasher& h, const FighterCombat& c) {
    put_all(h, c.alive, c.durability, c.durability_max, c.crit_chance, c.shield_chance,
            c.break_resist, c.laser_reduction, c.reticle_kind_a, c.reticle_kind_b, c.reticle_size_a,
            c.reticle_size_b, c.reticle_speed, c.lock_frames, c.triggers, c.weapons,
            c.blast_weapon, c.mount_free_aim, c.weapon_broken, c.melee_clip, c.damage, c.hud);
    put_all(h, c.aim_point, c.reticle_track_world, c.melee_active, c.melee_connected,
            c.melee_endpoint, c.melee_target, c.melee_target_stored, c.melee_tip_known,
            c.melee_target_known, c.melee_tip, c.melee_target_point, c.stagger_end,
            c.defeat_frames, c.score, c.kills, c.streak, c.streak_window, c.destruction_value,
            c.ammo_fraction);
}
void put(Hasher& h, const Camo& c) {
    put_all(h, c.state, c.timer, c.left, c.recharge, c.max, c.recharge_max, c.beginner,
            c.beginner_wait, c.chip_shader_param);
}
void put(Hasher& h, const FighterAttachment& a) {
    put_all(h, a.kind, a.position, a.previous, a.direction, a.part, a.matrix);
}
void put(Hasher& h, const FighterPoseState& p) {
    put_all(h, p.palette, p.stored, p.mount, p.mounted, p.head_position, p.attachments,
            p.foot_count, p.weapon_nodes, p.weapon_node_count, p.weapon_translate, p.weapon_euler,
            p.back_aim, p.back_launch_pitch, p.back_spin_node, p.blend_offset_x, p.blend_offset_z,
            p.stores);
}
void put(Hasher& h, const Fighter& f) {
    put_all(h, f.present, f.slot, f.controller, f.control_type, f.bindings, f.flick, f.control,
            f.restrict_input, f.pad_held, f.axis_zero, f.control_allow, f.tutorial_clamp);
    put_all(h, f.build, f.leg_class, f.body_class, f.leg_kind, f.radius, f.weight, f.walk_rate,
            f.run_rate, f.walk_root_motion, f.run_root_motion, f.walk_scale, f.jump_height,
            f.jump_speed_30, f.jump_thrust_30, f.jump_speed, f.turn_step, f.air_turn_step,
            f.dodge_speed_ground, f.dodge_speed_air, f.lunge_speed_max, f.leg_landing,
            f.leg_knockback_resist, f.slide_decay, f.skid_start, f.skid_stop, f.head_height);
    put_all(h, f.position, f.position_prev, f.velocity, f.velocity_prev, f.force_dv, f.push,
            f.impulse, f.takeoff_velocity, f.skid_velocity, f.ground_velocity,
            f.reticle_center_world, f.dodge_impulse, f.heading, f.heading_prev, f.aim_yaw,
            f.aim_yaw_recenter, f.turn_rate, f.run_lean);
    put_all(h, f.grounded, f.airborne, f.dodge_ready, f.dodge_sliding, f.dive_used, f.jump_cut,
            f.at_speed, f.reverse, f.skidding, f.landed, f.airborne_time, f.jump_thrust_left_30,
            f.jump_step, f.pair_jump_step, f.pair_thrust, f.liftoff_step, f.air_control_hold,
            f.intangible_frames, f.contact_flags);
    put_all(h, f.motion_state, f.motion_mode, f.motion_slot_a, f.motion_slot_prev, f.motion_phase,
            f.motion_phase_prev,
            f.timing, f.motion_time, f.camera, f.age_frames, f.scene_motion, f.combat, f.camo,
            f.crate_kind, f.crate_at, f.pose, f.pose_blend_steps, f.motion_events,
            f.node_frame_400, f.node_frame_800, f.node_frame_2000, f.wheel_angle, f.barks);
}

// --- shots and the HUDs ---

void put(Hasher& h, const Shot& s) {
    put_all(h, s.basis, s.origin, s.weapon, s.target, s.pos, s.prev_pos, s.hit_pos, s.velocity,
            s.prev_pos2, s.state, s.flags, s.life, s.radius, s.damage, s.mover, s.dodge_rolled);
}
void put(Hasher& h, const ShotThreatLists& t) {
    put_all(h, t.locked_on_me, t.locked_on_me_count, t.nearby, t.nearby_count, t.melee,
            t.melee_count, t.beams, t.beam_count);
}
void put(Hasher& h, const CombatEvent& e) {
    put_all(h, e.kind, e.step, e.slot, e.value, e.effect, e.position, e.dir, e.flags, e.surface);
}
void put(Hasher& h, const CombatWorld& c) {
    put_all(h, c.shots, c.shot_cursor, c.threats, c.events, c.event_count);
}
void put(Hasher& h, const HudSense& s) {
    put_all(h, s.distance, s.yaw, s.pitch, s.on_screen, s.screen_x, s.screen_y, s.screen_z,
            s.occluded);
}
void put(Hasher& h, const FighterHud& u) {
    put_all(h, u.visible, u.visible_count, u.sense, u.reticle_center_x, u.reticle_center_y,
            u.reticle_target, u.reticle_on_me, u.locked_on, u.locking_me, u.melee_locked);
}
void put(Hasher& h, const LockWarning& w) {
    put_all(h, w.timer, w.sounding);
}

// --- the AI ---

void put(Hasher& h, const AiWeapon& w) {
    put_all(h, w.bound, w.mount, w.want_fire, w.delay, w.burst_delay, w.armed, w.behavior,
            w.lock_rule);
}
void put(Hasher& h, const AiFirePacing& p) {
    put_all(h, p.firing, p.max_firing, p.fire_chance, p.refire_delay, p.rearm_delay,
            p.burst_delay);
}
void put(Hasher& h, const AiRoute& r) {
    put_all(h, r.placed, r.node, r.current, r.point_index, r.point_count, r.progress_steps, r.from);
}
void put(Hasher& h, const AiController& a) {
    put_all(h, a.flags, a.state, a.prev_state, a.mode, a.search_move, a.handler, a.path_cache,
            a.spare_1c, a.spec_level, a.profile, a.target, a.target_sense, a.target_slot,
            a.target_memory, a.camo_watch_timer, a.wanted_range, a.axis_forward, a.axis_leg_turn,
            a.axis_pitch, a.axis_twist, a.target_distance, a.target_distance_flat, a.wants_jump);
    put_all(h, a.target_aspect, a.durability_band, a.last_durability, a.view_yaw, a.view_pitch,
            a.weapon, a.fire, a.pacing, a.camo_hold, a.camo_cooldown, a.camo_cooldown_base,
            a.camo_chance, a.camo_fraction, a.out_of_ammo, a.band_button1_chance,
            a.band_jump_chance, a.band_dodge_chance, a.jump_start_y, a.node_kind, a.age_frames);
    put_all(h, a.state_timer, a.ranker_timer, a.reeval_timer, a.frames_ticking, a.sub_timer,
            a.route_wait, a.frames_since_jump, a.frames_since_backup, a.dodge_cooldown,
            a.route_stuck_frames, a.frames_since_player_hit, a.player_last_durability,
            a.move_flags, a.free_weapon_classes, a.pickup, a.pickup_frames, a.attack_frames,
            a.attack_jumps, a.class5_fire_frames, a.class5_fire_hold, a.strafe_chance);
    put_all(h, a.enraged, a.strafe_dir, a.use_camo, a.camo_when_shot, a.camo_held, a.route,
            a.record, a.contact_flags_even);
}

// --- the stage ---

void put(Hasher& h, const StageSetup& s) {
    put_all(h, s.selector, s.free_play, s.craft_frozen, s.vs, s.gates_locked, s.crate_tournament,
            s.ending);
}
void put(Hasher& h, const Prop& p) {
    put_all(h, p.rest, p.transform, p.type, p.step, p.model, p.alt_model, p.timer, p.flags, p.cell,
            p.gate_second, p.sound_pos, p.tris, p.gate_angle);
}
void put(Hasher& h, const SupplyCraft& c) {
    put_all(h, c.state, c.saved_state, c.step, c.route, c.segment, c.last_segment, c.t, c.speed,
            c.pos, c.prev_pos, c.velocity, c.ingress, c.release_start, c.release_end, c.height,
            c.rotor, c.hatch, c.hatch_phase, c.crate_count, c.shootable, c.crash_timer, c.matrix,
            c.body, c.body_prev);
}
void put(Hasher& h, const Crate& c) {
    put_all(h, c.pos, c.drop_delay, c.life, c.state, c.kind, c.glow, c.shootable, c.claimed);
}
void put(Hasher& h, const StageEvent& e) {
    put_all(h, e.kind, e.step, e.a, e.b, e.flags, e.at, e.value, e.value2);
}
void put(Hasher& h, const StageWorld& s) {
    // s.data: the stage's constant tables
    put_all(h, s.setup, s.ready, s.prop_count, s.props, s.gates, s.craft, s.crates,
            s.hit_static_frames, s.hit_shake_frames, s.hit_seat, s.shake_tilt, s.shake_x,
            s.shake_y, s.event_count, s.events);
}

// --- the battle around the fighters ---

void put(Hasher& h, const FighterScene& s) {
    put_all(h, s.timer, s.focus, s.heading, s.door_heading, s.state, s.step, s.killer, s.door_wait,
            s.progress, s.text_left, s.hid_others);
}
void put(Hasher& h, const BattleResult& r) {
    put_all(h, r.won, r.rented, r.fixed_opponent_destroyed, r.destruction_count, r.elapsed,
            r.money_before, r.prize, r.weapon_repair, r.body_leg_repair, r.repair_total,
            r.damaged_weapons, r.tournament_won_at_start);
}
void put(Hasher& h, const BattleEvent& e) {
    put_all(h, e.kind, e.step, e.a, e.b);
}
void put(Hasher& h, const BattleSlot& s) {
    put_all(h, s.scene, s.ai_frozen, s.door_progress, s.killed_by_player, s.parked, s.retire_left,
            s.build, s.rig);
}
// the battle state but its result, which only a live tournament fight writes (battle phase 0),
// and the letterbox's clock, which a replay doesn't step (battle_update) and nothing else reads
void put_battle(Hasher& h, const BattleState& b) {
    put_all(h, b.live, b.menu_state, b.rumble, b.end_state, b.frame, b.replace_clock,
            b.replace_wait, b.boss_scene_done, b.view_count,
            b.loading_slot, b.player_gate, b.ranker_roll, b.free_play_lesson, b.scene_text,
            b.scene_text_serial, b.scene_text_alive);
    put_all(h, b.ending_requested, b.demo_slot, b.demo_clock, b.demo_followed, b.slots, b.events,
            b.event_count);
}
void put(Hasher& h, const VsBattle& v) {
    put_all(h, v.mode, v.in_play, v.end_state, v.end_timer, v.cursor, v.quit_open, v.quit_cursor,
            v.result, v.wins, v.losses);
    for (std::size_t i = 0; i < v.menu_buttons.size(); ++i) {
        for (const PadButtons* b : {&v.menu_buttons[i], &v.menu_stick[i]}) {
            put_all(h, b->prev, b->cur, b->rising, b->falling, b->repeat, b->timer);
        }
    }
}
void put(Hasher& h, const LobbyRules& r) {
    put_all(h, r.online, r.team_end, r.ai_seats);
}
void put(Hasher& h, const PcControls& c) {
    put_all(h, c.double_tap_step);
}
void put(Hasher& h, const FlyCamera& c) {
    put_all(h, c.position, c.yaw, c.pitch);
}

namespace {

void put_fight(Hasher& h, const World& w) {
    // w.battle, w.story_tables, w.roster_tables: constant tables
    put_all(h, w.step, w.camera, w.fighters, w.combat, w.huds, w.lock_warnings, w.ai,
            w.debug_weapon_reach, w.random.index, w.vs, w.rules, w.stage);
    put_battle(h, w.battle_state);
}

} // namespace

std::uint64_t world_fight_hash(const World& world) {
    Hasher h;
    put_fight(h, world);
    return h.h;
}

std::uint64_t world_hash(const World& world) {
    Hasher h;
    put_fight(h, world);
    put_all(h, world.battle_phase, world.battle_state.result, world.battle_state.letterbox_state,
            world.battle_state.letterbox_time, world.story, world.garages, world.profile,
            world.rumble, world.player_buttons, world.player_controls);
    return h.h;
}

namespace {

template <class T> std::uint64_t hash_of(const T& part) {
    Hasher h;
    put(h, part);
    return h.h;
}

// the world in named parts, for saying where two worlds part (a desync's first look)
void parts_of(const World& w, std::vector<std::pair<std::string, std::uint64_t>>& out) {
    out.emplace_back("step", hash_of(w.step));
    out.emplace_back("camera", hash_of(w.camera));
    for (int i = 0; i < fighter_slots; ++i) {
        const Fighter& f = w.fighters[static_cast<std::size_t>(i)];
        const std::string at = "fighters[" + std::to_string(i) + "]";
        out.emplace_back(at + ".combat", hash_of(f.combat));
        out.emplace_back(at + ".camo", hash_of(f.camo));
        out.emplace_back(at + ".pose", hash_of(f.pose));
        out.emplace_back(at + ".camera", hash_of(f.camera));
        out.emplace_back(at, hash_of(f));
        out.emplace_back("ai[" + std::to_string(i) + "]", hash_of(w.ai[static_cast<std::size_t>(i)]));
        out.emplace_back("huds[" + std::to_string(i) + "]",
                         hash_of(w.huds[static_cast<std::size_t>(i)]));
        out.emplace_back("battle_state.slots[" + std::to_string(i) + "]",
                         hash_of(w.battle_state.slots[i]));
    }
    out.emplace_back("combat.shots", hash_of(w.combat.shots));
    out.emplace_back("combat.threats", hash_of(w.combat.threats));
    out.emplace_back("combat.events", hash_of(w.combat.events));
    out.emplace_back("lock_warnings", hash_of(w.lock_warnings));
    out.emplace_back("random", hash_of(w.random.index));
    out.emplace_back("vs", hash_of(w.vs));
    out.emplace_back("rules", hash_of(w.rules));
    out.emplace_back("stage.props", hash_of(w.stage.props));
    out.emplace_back("stage.craft", hash_of(w.stage.craft));
    out.emplace_back("stage.crates", hash_of(w.stage.crates));
    out.emplace_back("stage.events", hash_of(w.stage.events));
    out.emplace_back("stage", hash_of(w.stage));
    Hasher b;
    put_battle(b, w.battle_state);
    out.emplace_back("battle_state", b.h);
    out.emplace_back("battle_state.events", hash_of(w.battle_state.events));
    out.emplace_back("battle_phase", hash_of(w.battle_phase));
    out.emplace_back("battle_state.result", hash_of(w.battle_state.result));
    out.emplace_back("battle_state.letterbox",
                     hash_of(w.battle_state.letterbox_state) ^ hash_of(w.battle_state.letterbox_time));
    out.emplace_back("story", hash_of(w.story));
    out.emplace_back("garages", hash_of(w.garages));
    out.emplace_back("profile", hash_of(w.profile));
    out.emplace_back("rumble", hash_of(w.rumble));
    out.emplace_back("player_buttons", hash_of(w.player_buttons));
    out.emplace_back("player_controls", hash_of(w.player_controls));
}

} // namespace

std::string world_hash_diff(const World& a, const World& b) {
    std::vector<std::pair<std::string, std::uint64_t>> pa, pb;
    parts_of(a, pa);
    parts_of(b, pb);
    std::string differ;
    for (std::size_t i = 0; i < pa.size(); ++i) {
        if (pa[i].second != pb[i].second) {
            differ += (differ.empty() ? "" : " ") + pa[i].first;
        }
    }
    return differ;
}

void world_hash_parts_print(const World& world) {
    std::vector<std::pair<std::string, std::uint64_t>> parts;
    parts_of(world, parts);
    for (const auto& [name, hash] : parts) {
        std::printf("part %lld %s %016llx\n", static_cast<long long>(world.step), name.c_str(),
                    static_cast<unsigned long long>(hash));
    }
}

void world_hash_print(const World& world, int every) {
    if (every > 0 && world.step % every == 0) {
        std::printf("hash %lld %016llx %016llx\n", static_cast<long long>(world.step),
                    static_cast<unsigned long long>(world_hash(world)),
                    static_cast<unsigned long long>(world_fight_hash(world)));
    }
}
