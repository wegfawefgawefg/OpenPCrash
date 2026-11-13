#pragma once
// The bark kinds a fighter asks for (enum Bark, decomp include/actor.h): one bit each in
// Fighter::barks, spoken by presentation (src/hud/hud_barks.cpp).

enum Bark {
    bark_enter_beaten_boss = 0x0,   // BARK_ENTER_BEATEN_BOSS: a beaten fixed opponent enters
    bark_killed_new_boss = 0x1,     // BARK_KILLED_NEW_BOSS: a boss down the first time, by a human
    bark_lost_to_boss = 0x2,        // BARK_LOST_TO_BOSS
    bark_armor_25_by_boss = 0x3,    // BARK_ARMOR_25_BY_BOSS
    bark_enter_new_boss = 0x4,      // BARK_ENTER_NEW_BOSS: an unbeaten fixed opponent enters
    bark_lost = 0x5,                // BARK_LOST
    bark_weapon_broken_last = 0x6,  // BARK_WEAPON_BROKEN_LAST
    bark_rear_hit = 0x7,            // BARK_REAR_HIT
    bark_kill_at_25 = 0x8,          // BARK_KILL_AT_25
    bark_door_at_25 = 0x9,          // BARK_DOOR_AT_25
    bark_shield = 0xa,              // BARK_SHIELD: the evade roll
    bark_armor_pickup_at_25 = 0xb,  // BARK_ARMOR_PICKUP_AT_25
    bark_weapon_broken_at_25 = 0xc, // BARK_WEAPON_BROKEN_AT_25
    bark_ammo_50_at_25 = 0xd,       // BARK_AMMO_50_AT_25
    bark_armor_25 = 0xe,            // BARK_ARMOR_25
    bark_weapon_broken = 0xf,       // BARK_WEAPON_BROKEN
    bark_camo_ending = 0x10,        // BARK_CAMO_ENDING
    bark_crit = 0x11,               // BARK_CRIT
    bark_enter = 0x12,              // BARK_ENTER: anyone else enters through a gate
    bark_ammo_pickup_low = 0x13,    // BARK_AMMO_PICKUP_LOW
    bark_ammo_50 = 0x14,            // BARK_AMMO_50
    bark_kill = 0x15,               // BARK_KILL
    bark_hit_flying = 0x16,         // BARK_HIT_FLYING
    bark_locked_on = 0x17,          // BARK_LOCKED_ON
    bark_armor_50 = 0x18,           // BARK_ARMOR_50
    bark_hit = 0x19,                // BARK_HIT: record class 6
    bark_laser_hit = 0x1a,          // BARK_LASER_HIT: record class 5
    bark_blast_taken = 0x1b,        // BARK_BLAST_TAKEN
    bark_door = 0x1c,               // BARK_DOOR
    bark_armor_pickup = 0x1d,       // BARK_ARMOR_PICKUP
    bark_ammo_pickup = 0x1e,        // BARK_AMMO_PICKUP
    bark_hit_flammable = 0x1f,      // BARK_HIT_FLAMMABLE
    bark_fired = 0x20,              // BARK_FIRED
    bark_idle = 0x21,               // BARK_IDLE
    bark_camo_on = 0x22,            // BARK_CAMO_ON
    bark_kinds = 0x23,              // 35 kinds; also "none" where a kind is kept
};
