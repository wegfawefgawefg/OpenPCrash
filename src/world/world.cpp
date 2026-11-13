#include "world/world.hpp"

#include "battle/battle_dev.hpp"
#include "battle/fighters_separate.hpp"
#include "stage/pickup_apply.hpp"
#include "weapons/weapons.hpp"

namespace {

// free camera speed: 0.25 units a step (15 a second), four times that with fast held
constexpr Fx fly_speed = fx_ratio(1, 4);
constexpr int fly_fast = 4;
// pitch held a little short of straight up or down
constexpr Angle pitch_limit{angle_quarter_raw - (1u << 22)};

void step_camera(FlyCamera& camera, const Inputs& inputs) {
    // turn
    camera.yaw += Angle{static_cast<std::uint32_t>(inputs.turn_yaw)};
    camera.pitch = angle_clamp(camera.pitch + Angle{static_cast<std::uint32_t>(inputs.turn_pitch)},
                               pitch_limit);

    // move along the look (forward), its flat right, and straight up
    const Fx flat = fx_cos(camera.pitch);
    const FxVec3 forward{fx_sin(camera.yaw) * flat, fx_sin(camera.pitch),
                         fx_cos(camera.yaw) * flat};
    // forward x up: the right hand of a camera in this right-handed, y-up frame
    const FxVec3 right{-fx_cos(camera.yaw), Fx{}, fx_sin(camera.yaw)};
    const FxVec3 up{Fx{}, fx(1), Fx{}};
    const Fx speed = inputs.fast ? fly_speed * fx(fly_fast) : fly_speed;
    camera.position += forward * (speed * fx(inputs.move_forward));
    camera.position += right * (speed * fx(inputs.move_right));
    camera.position += up * (speed * fx(inputs.move_up));
}

} // namespace

void step_world(World& world, const Inputs& inputs) {
    // this step's events (sounds, effects, impacts) start empty: the fighters and the stage add
    world.stage.event_count = 0;
    if (!world.battle) {
        step_camera(world.camera, inputs);
    } else {
        world.combat.event_count = 0;
        for (Fighter& f : world.fighters) {
            f.barks = 0;
        }
        // the battle task's own update first: fighters_separate, then the letterbox, the
        // replacements and the clocks (battle/battle_flow.hpp), then VS's who is in play and its
        // end menu (battle/vs_battle.hpp; a VS round's queues are empty, so nothing replaces)
        fighters_separate(world);
        battle_update(world);
        battle_scene_text_done(world, inputs.scene_text_done);
        vs_battle_update(world, inputs);
        battle_dev_apply(world, inputs.dev); // port-only: headless runs' kills and doors
        const bool decided = vs_round_decided(world);
        // the fighters, slot 0 to 5: a pad through the control route or the AI (not while
        // frozen by a scene, nor dead), the scene or the motion states, the step (whose end, the
        // back weapons' aim and the damage settle, is combat_fighter_late), then the door check.
        // A fighter parked by a boss's scene doesn't run
        for (int slot = 0; slot < fighter_slots; ++slot) {
            Fighter& fighter = world.fighters[static_cast<std::size_t>(slot)];
            const BattleSlot& run = world.battle_state.slots[slot];
            if (!fighter.present || run.parked) {
                continue;
            }
            if (fighter.controller == -1 && !run.ai_frozen && fighter.combat.durability > 0) {
                ai_fighter_input(world, slot);
            }
            // a human reads its seat's pad; a seat that isn't there reads a pad at rest, never
            // another seat's
            const bool seated = fighter.controller >= 0 && fighter.controller < max_pad_players;
            const auto seat = static_cast<std::size_t>(seated ? fighter.controller : 0);
            static const PadRecord no_pad{};
            static const MouseAim no_aim{};
            combat_fighter_early(world, fighter);
            const FighterScene& scene = world.battle_state.slots[slot].scene;
            const TutorialBefore before{fighter.heading, fighter.position, scene.state, scene.step};
            battle_scene_step(world, fighter);
            int clip_was[4];
            for (int m = 0; m < 4; ++m) {
                clip_was[m] = fighter.combat.weapons[m].clip_state;
            }
            fighter_step(fighter, *world.battle, seated ? inputs.pads[seat] : no_pad,
                         seated ? inputs.aims[seat] : no_aim, world.step, decided, &world.stage,
                         world.battle_phase == 5, world.battle_state.view_count <= 1);
            // a human's locked weapon that began opening in its pose (weapon_anim_update's
            // sound, presentation's): from rest only the opening (1, or 3 at once) leaves 0
            for (int m = 0; m < 4; ++m) {
                const int now = fighter.combat.weapons[m].clip_state;
                if (fighter.controller != -1 && clip_was[m] == 0 && (now == 1 || now == 3)) {
                    combat_event(world, event_weapon_open, slot, m, fighter.position);
                }
            }
            // the crates its sphere took (pickup_apply, inside sv_contact_route in retail)
            pickups_apply(world, fighter);
            if (scene_is_lesson(before.state)) {
                battle_tutorial_after(world, fighter, before); // free play's lessons
            }
            if (world.battle_phase == 4) {
                battle_free_play_refill(world, fighter);
            }
            combat_fighter_late(world, fighter);
            // its sounds' lock alarm (actor_sounds_update, after the step in the fighter task)
            lock_warning_update(world, fighter);
            battle_door_check(world, fighter);
            if (fighter.controller == -1) {
                ai_after_step(world, slot);
            }
        }
        // the shot pass, then per slot its HUD and its four weapon tasks (arm A, arm B, back A,
        // back B): docs/port/weapons.md "One battle update, as weapons see it". The HUD's senses
        // and reticle target for every slot first (they read only positions, which the weapons
        // don't move), then per slot the tracking point, the locks and the weapons
        battle_shots_update(world);
        huds_update(world);
        for (Fighter& fighter : world.fighters) {
            if (!fighter.present || world.battle_state.slots[fighter.slot].parked) {
                continue;
            }
            hud_update(world, fighter);
            for (int mount = 0; mount < 4; ++mount) {
                weapon_controller(world, fighter, mount);
            }
        }
    }
    // the stage task: after the fighters, the shot pool and the weapon tasks, as retail's tree
    // runs it (decomp docs/port/battle.md, "One update")
    step_stage(world.stage, world.random, world.step,
               world.battle ? &world.battle->stage : nullptr);
    for (int i = 0; i < world.stage.event_count; ++i) {
        world.stage.events[static_cast<std::size_t>(i)].step = world.step;
    }
    world.step += 1;
}
