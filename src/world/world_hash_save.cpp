// The world hash's save part (world/game_state.hpp, story/story.hpp): every field, in declaration
// order. world_hash covers it; world_fight_hash, the replay's, leaves it out.
#include "world/world.hpp"
#include "world/world_hash_put.hpp"

// every struct hashed here, at the size its put() was written for (the game's 6 slots): a new
// field changes it, and wants hashing before the size is updated
#define PC_HASHED(T, size) static_assert(fighter_slots != 6 || sizeof(T) == (size), #T)
PC_HASHED(BuildRecord, 104);
PC_HASHED(ButtonConfig, 44);
PC_HASHED(EndingCarry, 32);
PC_HASHED(GameOptions, 48);
PC_HASHED(Garage, 54);
PC_HASHED(Garages, 6932);
PC_HASHED(InventoryItem, 24);
PC_HASHED(Music, 556);
PC_HASHED(OpponentQueue, 1456);
PC_HASHED(OpponentTicket, 16);
PC_HASHED(PoolProgress, 9);
PC_HASHED(Profile, 676);
PC_HASHED(Records, 52);
PC_HASHED(RosterBuild, 54);
PC_HASHED(RosterEntry, 176);
PC_HASHED(RosterPilot, 28);
PC_HASHED(RosterWeapon, 4);
PC_HASHED(RumbleSlot, 5444);
PC_HASHED(RumbleTicket, 20);
PC_HASHED(Story, 2824);
#undef PC_HASHED

void put(Hasher& h, const PoolProgress& p) {
    put_all(h, p.primary, p.alternates);
}
void put(Hasher& h, const EndingCarry& c) {
    put_all(h, c.carry, c.durability, c.resource, c.fitted, c.broken);
}
void put(Hasher& h, const Story& s) {
    put_all(h, s.pools, s.tournament_won, s.day, s.money, s.game_beaten, s.music_owned,
            s.events_done, s.lineup, s.completed_cycles, s.days_played, s.map_spin_money,
            s.rumble_selected, s.class_wins_at_entry, s.ranker_wins_at_entry, s.talk_src,
            s.talk_dst, s.ending_carry);
}
void put(Hasher& h, const InventoryItem& i) {
    put_all(h, i.garage, i.tune, i.kind, i.part, i.name);
}
void put(Hasher& h, const Garage& g) {
    put_all(h, g.body, g.leg, g.arm, g.back, g.chip, g.option, g.name, g.color, g.sticker);
}
void put(Hasher& h, const Garages& g) {
    put_all(h, g.inventory, g.garages, g.unlocked, g.selected, g.colors_owned, g.stickers_owned);
}
void put(Hasher& h, const Records& r) {
    put_all(h, r.total_earned, r.best_payout, r.battles, r.wins, r.kills, r.drones_shot_down,
            r.play_time, r.longest_battle, r.shortest_battle, r.save_count,
            r.enemy_weapons_destroyed, r.weapons_destroyed, r.play_clock);
}
void put(Hasher& h, const Music& m) {
    put_all(h, m.playlists, m.playlist_random, m.playlist_selected, m.playlist_names,
            m.sonic_amp_stock);
}
void put(Hasher& h, const ButtonConfig& b) {
    put_all(h, b.control, b.view_change, b.beginner, b.keys);
}
void put(Hasher& h, const GameOptions& o) {
    put_all(h, o.camera_view, o.buttons);
}
void put(Hasher& h, const Profile& p) {
    put_all(h, p.pilot_name, p.records, p.music, p.options);
}
void put(Hasher& h, const OpponentTicket& t) {
    put_all(h, t.role, t.pool, t.alternate, t.tournament);
}
void put(Hasher& h, const RumbleTicket& t) {
    put_all(h, t.opponent, t.progress);
}
void put(Hasher& h, const OpponentQueue& q) {
    put_all(h, q.tickets, q.drawable, q.count, q.out, q.ready);
}
void put(Hasher& h, const RosterWeapon& w) {
    put_all(h, w.id, w.tune);
}
void put(Hasher& h, const RosterBuild& b) {
    put_all(h, b.unused_00, b.body, b.body_tune, b.leg, b.leg_tune, b.weapon, b.chip, b.chip_tune,
            b.color, b.sticker, b.option);
}
void put(Hasher& h, const RosterPilot& p) {
    put_all(h, p.identity, p.scoobee_identity, p.team, p.unused_06, p.value_base, p.value_at_mid,
            p.value_at_max, p.value_mid, p.value_negative, p.reaction, p.unused_1a);
}
void put(Hasher& h, const RosterEntry& e) {
    put_all(h, e.build, e.pilot, e.level, e.ai_profile, e.player, e.destruction_value, e.ticket,
            e.queue, e.start, e.start_set, e.name);
}
void put(Hasher& h, const RumbleSlot& r) {
    put_all(h, r.queues, r.regular_quota, r.entrants, r.roster, r.fixed_queued, r.pool_queued);
}
void put(Hasher& h, const BuildRecord& b) {
    put_all(h, b.body, b.body_tune, b.leg, b.leg_tune, b.chip, b.chip_tune, b.weapon,
            b.weapon_tune, b.colour, b.sticker, b.option);
}
