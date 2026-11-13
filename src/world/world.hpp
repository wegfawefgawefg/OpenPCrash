#pragma once
// The deterministic simulation: a copyable value changed only by step_world. Fixed point (src/fx),
// no SDL, floats, rendering or wall clock (AGENTS.md, "Simulation"). It holds the fighters of a
// battle (src/mech/fighter.hpp) over the battle's fixed data, and the first slice's free camera
// for when there is no battle.
#include "ai/ai.hpp"
#include "battle/battle_flow.hpp"
#include "battle/fighter_hud.hpp"
#include "battle/vs_battle.hpp"
#include "fx/angle.hpp"
#include "fx/vec.hpp"
#include "hud/hud_world.hpp"
#include "mech/fighter.hpp"
#include "stage/stage_world.hpp"

#include <array>
#include <memory>
#include "story/story.hpp"
#include "world/fighter_slots.hpp"
#include "world/game_state.hpp"
#include "world/inputs.hpp"
#include "world/world_random.hpp"

#include <cstdint>

// world steps a second; the original's battle ran at 30 (decomp docs/port/architecture.md)
inline constexpr int steps_per_second = 60;

struct RosterTables;

struct FlyCamera {
    FxVec3 position;
    Angle yaw;   // 0 looks along +z, a quarter turn along +x
    Angle pitch; // up positive, held within a quarter turn either way
};


// The lobby's rules for a battle: one small block set at its start, so online play doesn't grow
// flags through the battle code (docs/multiplayer.md "Change now", item 11). All off is retail.
// No mouse rule: mouse aim always turns at the mech's rate (docs/input.md, "Mouse aim"). No boss
// rule: a boss's entrance freezes everyone, however many humans (owner, 2026-11-11).
struct LobbyRules {
    bool online = false;   // seats on other machines (each machine's sound and views: presentation)
    bool team_end = false; // VS is decided when one pilot team or none is left in play
    bool ai_seats = false; // VS seats an AI may fill (a roster entry with a level)
};

struct World {
    std::int64_t step = 0;
    FlyCamera camera;
    // what the battle reads and never changes: shared, so a copy of the world copies a pointer
    std::shared_ptr<const BattleData> battle;
    // in slot order, which is the order they step in (the fighter tasks, all at priority 0x2000,
    // made in slot order: docs/port/gameplay.md "Update order in a battle")
    std::array<Fighter, fighter_slots> fighters;
    // the shot pool, the AI's threat lists and this step's combat events (src/weapons)
    CombatWorld combat;
    // GameState.battle_phase: 0 a fight, 1 the AI-only demo battles (ai_level 99). The battle's
    // roster is rumble[day_slot_of_selector(story)].roster, as retail's rumble context
    int battle_phase = 0;
    std::array<FighterHud, fighter_slots> huds; // what each fighter's HUD senses
    std::array<LockWarning, fighter_slots> lock_warnings; // each human's lock alarm (src/hud)
    std::array<AiController, fighter_slots> ai; // ai_controllers, by slot
    Fx debug_weapon_reach; // --ai-reach: every weapon's reach until weapons are in the world
    Story story; // progress the story reads and writes (src/story/story.hpp)
    // the rest of what a save holds (src/world/game_state.hpp, docs/saves.md)
    Garages garages;                 // the inventory, garages, colours and stickers owned
    Profile profile;                 // pilot name, records, music player, options, button config
    RumbleSlot rumble[rumble_count]; // today's queues and drawn rosters (tournaments: story.lineup)
    // each player's session copy of the button config (GameState +0x10FD4, 40 bytes a player; not
    // saved): player_config_commit copies the saved one to player 0
    ButtonConfig player_buttons[4];
    // PC (port-only): each seat's control settings the fight reads at spawn (not saved), filled
    // from the seats' PC CONFIG before a battle starts (pc_controls_commit)
    PcControls player_controls[4];
    WorldRandom random; // the one gameplay generator (world/world_random.hpp)
    VsBattle vs;        // split-screen VS: who is in play, the end menu, the wins (battle/vs_battle)
    LobbyRules rules;   // the lobby's, set at the battle's start (retail's: all off)
    StageWorld stage;   // props, supply craft, crates, hit static and shake (src/stage)
    // the battle around the fighters: scenes, replacements, the end and its result
    // (battle/battle_flow.hpp)
    BattleState battle_state;
    // the constant tables the story and the roster read (not world state: copied as pointers)
    const StoryTables* story_tables = nullptr;
    const RosterTables* roster_tables = nullptr;
};

void step_world(World& world, const Inputs& inputs);
