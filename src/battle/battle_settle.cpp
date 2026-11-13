// What a battle pays and records once it is over (decomp src/od/u_0933f0.c
// battle_result_settlement, battle_result_commit_stats, battle_result_clear;
// docs/port/battle.md "What the battle hands back").
#include "battle/battle_flow.hpp"

#include "world/game_state.hpp"

namespace {

constexpr int money_max = 99999999;
constexpr int count_cap = 999999;     // battles, wins
constexpr int weapons_cap = 9999;     // weapons_destroyed

} // namespace

// decomp: battle_result_settlement 0x96BC5
int battle_result_settlement(const BattleResult& r, const Story& story) {
    // the prize less every repair; a loss is capped at the money held
    int amount = r.prize - r.body_leg_repair;
    for (const int repair : r.weapon_repair) {
        amount -= repair;
    }
    if (amount < 0 && -amount > story.money) {
        amount = -story.money;
    }
    return amount;
}

void battle_result_commit(BattleResult& r, Profile& profile, Story& story, int settlement) {
    Records& rec = profile.records;
    const int take = settlement < 0 ? 0 : settlement;
    rec.total_earned += take;
    if (story.money > money_max) {
        story.money = money_max;
    }
    if (static_cast<unsigned>(rec.total_earned) > static_cast<unsigned>(money_max)) {
        rec.total_earned = money_max;
    }
    // best_payout is the best take, not the best prize
    if (static_cast<unsigned>(take) > static_cast<unsigned>(rec.best_payout)) {
        rec.best_payout = take;
    }
    rec.battles = rec.battles + 1 > count_cap ? count_cap : rec.battles + 1;
    if (r.won) {
        rec.wins = rec.wins + 1 > count_cap ? count_cap : rec.wins + 1;
    }
    if (static_cast<unsigned>(rec.longest_battle) < static_cast<unsigned>(r.elapsed)) {
        rec.longest_battle = r.elapsed > play_time_cap ? play_time_cap : r.elapsed;
    }
    if (static_cast<unsigned>(rec.shortest_battle) > static_cast<unsigned>(r.elapsed)) {
        rec.shortest_battle = r.elapsed > play_time_cap ? play_time_cap : r.elapsed;
    }
    rec.weapons_destroyed += r.damaged_weapons;
    if (static_cast<unsigned>(rec.weapons_destroyed) > static_cast<unsigned>(weapons_cap)) {
        rec.weapons_destroyed = weapons_cap;
    }
    // battle_result_clear
    r = BattleResult{};
}
