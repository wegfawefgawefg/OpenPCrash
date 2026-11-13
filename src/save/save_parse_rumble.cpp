// The RumbleContexts in a save's text: queues of tickets and drawn roster entries (save_text.cpp
// writes them).
#include "save/save_read.hpp"

#include <cstring>

namespace save_read {

// --- rumble ---

bool read_ticket(In& in, Node node, RumbleTicket& ticket) {
    if (!node.head().is_atom("ticket") || !number_as(in, node.child_at(1), ticket.opponent) ||
        !word(in, node.child_at(2), save_names::ticket_roles, ticket.progress.role)) {
        return fail(in, "bad (ticket opponent role place)");
    }
    const FormView form(node);
    ticket.progress.pool = -1;
    ticket.progress.alternate = -1;
    ticket.progress.tournament = -1;
    if (form.find("tournament").valid()) {
        return field(in, form, "tournament", ticket.progress.tournament);
    }
    const Node pool = form.find("pool");
    return number_as(in, pool.child_at(1), ticket.progress.pool) &&
           (pool.child_count() < 3 || number_as(in, pool.child_at(2), ticket.progress.alternate));
}

bool read_queue(In& in, FormView form, OpponentQueue& queue) {
    if (!field(in, form, "drawable", queue.drawable) || !field(in, form, "count", queue.count) ||
        !field(in, form, "out", queue.out) || !field(in, form, "ready", queue.ready)) {
        return false;
    }
    int t = 0;
    for (std::size_t i = 1; i < form.node().child_count(); ++i) {
        const Node node = form.node().child_at(i);
        if (!node.head().is_atom("ticket")) {
            continue;
        }
        if (!index_in(in, t, queue_capacity, "ticket") || !read_ticket(in, node, queue.tickets[t])) {
            return false;
        }
        ++t;
    }
    return t == queue.count || fail(in, "a queue's tickets don't match its count");
}

bool read_roster(In& in, FormView form, RosterEntry& e) {
    const auto name = form.get_string("name");
    if (!name || name->size() >= sizeof e.name) {
        return fail(in, "bad roster (name)");
    }
    std::memset(e.name, 0, sizeof e.name);
    std::memcpy(e.name, name->data(), name->size());
    if (!field(in, form, "player", e.player) || !field(in, form, "level", e.level) ||
        !field(in, form, "ai-profile", e.ai_profile) ||
        !field(in, form, "value", e.destruction_value) || !field(in, form, "queue", e.queue) ||
        !read_ticket(in, form.find("ticket"), e.ticket)) {
        return false;
    }
    const FormView build(form.find("build"));
    RosterBuild& b = e.build;
    const Node weapons = build.find("weapons");
    if (!field(in, build, "f_00", b.unused_00) ||
        !number_as(in, build.find_arg("body", 0), b.body) ||
        !number_as(in, build.find_arg("body", 1), b.body_tune) ||
        !number_as(in, build.find_arg("leg", 0), b.leg) ||
        !number_as(in, build.find_arg("leg", 1), b.leg_tune) ||
        !number_as(in, build.find_arg("chip", 0), b.chip) ||
        !number_as(in, build.find_arg("chip", 1), b.chip_tune) ||
        !field(in, build, "color", b.color) || !field(in, build, "sticker", b.sticker) ||
        !list(in, build, "option", b.option, 10, std::int16_t{-1}, true) ||
        weapons.child_count() != 5) {
        return fail(in, "bad roster (build)");
    }
    for (int w = 0; w < 4; ++w) {
        const Node weapon = weapons.child_at(static_cast<std::size_t>(w) + 1);
        if (!number_as(in, weapon.child_at(0), b.weapon[w].id) ||
            !number_as(in, weapon.child_at(1), b.weapon[w].tune)) {
            return false;
        }
    }
    const FormView pilot(form.find("pilot"));
    RosterPilot& p = e.pilot;
    long long curve[5] = {};
    if (!field(in, pilot, "identity", p.identity) ||
        !field(in, pilot, "scoobee-identity", p.scoobee_identity) ||
        !field(in, pilot, "team", p.team) || !field(in, pilot, "f_06", p.unused_06) ||
        !list(in, pilot, "destruction-value", curve, 5, 0LL, true) ||
        !field(in, pilot, "reaction", p.reaction) || !field(in, pilot, "f_1a", p.unused_1a)) {
        return false;
    }
    p.value_base = static_cast<int>(curve[0]);
    p.value_at_mid = static_cast<int>(curve[1]);
    p.value_at_max = static_cast<int>(curve[2]);
    p.value_mid = static_cast<std::int8_t>(curve[3]);
    p.value_negative = static_cast<std::int8_t>(curve[4]);
    const Node start = form.find("start");
    std::int32_t raw[3] = {};
    if (start.child_count() != 5 ||
        !(start.child_at(1).is_atom("true") || start.child_at(1).is_atom("false")) ||
        !number_as(in, start.child_at(2), raw[0]) || !number_as(in, start.child_at(3), raw[1]) ||
        !number_as(in, start.child_at(4), raw[2])) {
        return fail(in, "bad roster (start set x y z)");
    }
    e.start_set = start.child_at(1).is_atom("true");
    e.start = {Fx::from_raw(raw[0]), Fx::from_raw(raw[1]), Fx::from_raw(raw[2])};
    return true;
}

bool read_rumble(In& in, FormView form, RumbleSlot* rumble, int& seen) {
    long long index = 0;
    if (!number(in, form.arg(0), index) || !index_in(in, index, rumble_count, "rumble")) {
        return false;
    }
    RumbleSlot& slot = rumble[index];
    ++seen;
    int queued[8] = {};
    if (!field(in, form, "regular-quota", slot.regular_quota) ||
        !field(in, form, "entrants", slot.entrants) ||
        !flag(in, form, "fixed-queued", slot.fixed_queued) ||
        !list(in, form, "pool-queued", queued, 8, 0, true)) {
        return false;
    }
    for (int k = 0; k < 8; ++k) {
        slot.pool_queued[k] = queued[k] != 0;
    }
    for (std::size_t i = 1; i < form.node().child_count(); ++i) {
        const FormView sub(form.node().child_at(i));
        const bool is_queue = sub.head().is_atom("queue");
        if (!is_queue && !sub.head().is_atom("roster")) {
            continue;
        }
        long long which = 0;
        if (!number(in, sub.arg(0), which) ||
            !index_in(in, which, is_queue ? 3 : roster_size, is_queue ? "queue" : "roster")) {
            return false;
        }
        const bool ok = is_queue ? read_queue(in, sub, slot.queues[which])
                                 : read_roster(in, sub, slot.roster[which]);
        if (!ok) {
            return false;
        }
    }
    return true;
}

} // namespace save_read
