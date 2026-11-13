#include "report/report_fight.hpp"

#include "battle/battle_data.hpp"
#include "battle_replay.hpp"
#include "battle_world.hpp"
#include "world/game_state.hpp"
#include "world/world.hpp"
#include "world/world_hash.hpp"

#include <atomic>
#include <cstring>
#include <lz4.h>

namespace {

constexpr char head_magic[8] = {'P', 'C', 'F', 'I', 'G', 'H', 'T', '1'};
constexpr char tail_magic[4] = {'S', 'T', 'E', 'P'};
constexpr char pack_magic[8] = {'P', 'C', 'F', 'I', 'G', 'H', 'Z', '1'};
constexpr int hash_every = 60;  // a fight hash every second of steps
constexpr int weapons_each = 5; // the four mounts and the blast weapon

// the World's fields as the record keeps them, in order. The pointers (battle, the story's and
// the roster's tables) are left out; a field added to World and not here leaves the start world's
// hash unmatched when it is read back, which pc_sim says
#define FIGHT_FIELDS(X)                                                                            \
    X(step)                                                                                        \
    X(camera) X(fighters) X(combat) X(battle_phase) X(huds) X(lock_warnings) X(ai)                 \
        X(debug_weapon_reach) X(story) X(garages) X(profile) X(rumble) X(player_buttons) X(random) \
            X(vs) X(rules) X(stage) X(battle_state) X(player_controls)

struct FightRecord {
    std::vector<std::uint8_t> head;
    const BattleReplay* replay = nullptr;
    Inputs pending;                    // the step being stepped
    std::size_t pending_at = SIZE_MAX; // its index, while it runs
    std::atomic<bool> ready{false};    // head and replay may be read
};

// never destroyed: the crash handler and the watchdog may read it after main's statics
FightRecord& record() {
    static FightRecord* made = new FightRecord;
    return *made;
}

// the names the player typed (the pilot's, the machines', the chips' and the playlists') put back
// as a new game has them: a report carries no player names. Nothing the step reads; the fight
// hash leaves them out too
void scrub_names(World& world) {
    Profile fresh;
    profile_new(fresh);
    std::memcpy(world.profile.pilot_name, fresh.pilot_name, sizeof fresh.pilot_name);
    std::memcpy(world.profile.music.playlist_names, fresh.music.playlist_names,
                sizeof fresh.music.playlist_names);
    for (Garage& garage : world.garages.garages) {
        std::memcpy(garage.name, Garage{}.name, sizeof garage.name);
    }
    for (InventoryItem& item : world.garages.inventory) {
        std::memcpy(item.name, InventoryItem{}.name, sizeof item.name);
    }
}

Weapon* weapon_of(Fighter& f, int i) {
    return i < 4 ? &f.combat.weapons[i] : &f.combat.blast_weapon;
}
const Weapon* weapon_of(const Fighter& f, int i) {
    return i < 4 ? &f.combat.weapons[i] : &f.combat.blast_weapon;
}

// bytes in, little by little
struct Bytes {
    std::vector<std::uint8_t>& out;
    void raw(const void* data, std::size_t size) {
        const auto* p = static_cast<const std::uint8_t*>(data);
        out.insert(out.end(), p, p + size);
    }
    void u32(std::uint32_t v) {
        raw(&v, sizeof v);
    }
    void i32(std::int32_t v) {
        raw(&v, sizeof v);
    }
    void u64(std::uint64_t v) {
        raw(&v, sizeof v);
    }
    void ints(const std::vector<int>& v) {
        i32(static_cast<std::int32_t>(v.size()));
        raw(v.data(), v.size() * sizeof(int));
    }
};

// bytes out, checked
struct Reader {
    const std::string& in;
    std::size_t at = 0;
    bool ok = true;
    bool raw(void* data, std::size_t size) {
        if (!ok || size > in.size() - at) {
            ok = false;
            return false;
        }
        std::memcpy(data, in.data() + at, size);
        at += size;
        return true;
    }
    template <class T> T get() {
        T v{};
        raw(&v, sizeof v);
        return v;
    }
    std::vector<int> ints() {
        const std::int32_t n = get<std::int32_t>();
        if (n < 0 || n > 1 << 20) {
            ok = false;
            return {};
        }
        std::vector<int> v(static_cast<std::size_t>(n));
        raw(v.data(), v.size() * sizeof(int));
        return v;
    }
};

// the tail: the inputs so far (and the one being stepped), every 60th fight hash, the live hash
template <class Put>
void write_tail(const FightRecord& r, std::int64_t final_step, std::uint64_t final_hash,
                Put&& put) {
    const BattleReplay& replay = *r.replay;
    const std::size_t recorded = replay.inputs.size();
    const bool pending = r.pending_at == recorded;
    const std::uint32_t steps = static_cast<std::uint32_t>(recorded + (pending ? 1 : 0));
    const std::uint32_t input_size = sizeof(Inputs);
    put(tail_magic, sizeof tail_magic);
    put(&steps, sizeof steps);
    put(&input_size, sizeof input_size);
    put(replay.inputs.data(), recorded * sizeof(Inputs));
    if (pending) {
        put(&r.pending, sizeof(Inputs));
    }
    const std::size_t hashes = replay.live_hashes.size();
    std::uint32_t count = 0;
    for (std::size_t k = 0; k < hashes; ++k) {
        count += (k + 1) % hash_every == 0 || k + 1 == hashes ? 1 : 0;
    }
    put(&count, sizeof count);
    for (std::size_t k = 0; k < hashes; ++k) {
        if ((k + 1) % hash_every == 0 || k + 1 == hashes) {
            const std::uint32_t step = static_cast<std::uint32_t>(k);
            put(&step, sizeof step);
            put(&replay.live_hashes[k], sizeof(std::uint64_t));
        }
    }
    put(&final_step, sizeof final_step);
    put(&final_hash, sizeof final_hash);
}

// where the inputs sit in a record: their offset, count and size; false when it has none
bool inputs_span(const std::string& raw, std::size_t& at, std::uint32_t& count,
                 std::uint32_t& size) {
    std::uint32_t head_size = 0;
    if (raw.size() < 12 || std::memcmp(raw.data(), head_magic, 8) != 0) {
        return false;
    }
    std::memcpy(&head_size, raw.data() + 8, 4);
    if (head_size > raw.size() || raw.size() - head_size < 12 ||
        std::memcmp(raw.data() + head_size, tail_magic, 4) != 0) {
        return false;
    }
    std::memcpy(&count, raw.data() + head_size + 4, 4);
    std::memcpy(&size, raw.data() + head_size + 8, 4);
    at = head_size + 12;
    return size > 0 && count <= (raw.size() - at) / size;
}

// each input XORed with the one before, then byte columns (every input's first byte, then every
// second...): what stays the same from step to step becomes long runs of zeros
void inputs_to_columns(std::string& raw) {
    std::size_t at = 0;
    std::uint32_t count = 0, size = 0;
    if (!inputs_span(raw, at, count, size)) {
        return;
    }
    const std::string rows = raw.substr(at, static_cast<std::size_t>(count) * size);
    for (std::uint32_t k = 0; k < count; ++k) {
        for (std::uint32_t j = 0; j < size; ++j) {
            const char before = k == 0 ? 0 : rows[(k - 1) * size + j];
            raw[at + static_cast<std::size_t>(j) * count + k] =
                static_cast<char>(rows[k * size + j] ^ before);
        }
    }
}

void inputs_from_columns(std::string& raw) {
    std::size_t at = 0;
    std::uint32_t count = 0, size = 0;
    if (!inputs_span(raw, at, count, size)) {
        return;
    }
    const std::string columns = raw.substr(at, static_cast<std::size_t>(count) * size);
    for (std::uint32_t k = 0; k < count; ++k) {
        for (std::uint32_t j = 0; j < size; ++j) {
            const char before = k == 0 ? 0 : raw[at + (k - 1) * size + j];
            raw[at + k * size + j] =
                static_cast<char>(columns[static_cast<std::size_t>(j) * count + k] ^ before);
        }
    }
}

} // namespace

void report_fight_begin(const BattleReplay& replay, const BattleWorldPlan& plan) {
    FightRecord& r = record();
    r.ready = false;
    r.replay = nullptr;
    r.head.clear();
    if (!replay.start || !replay.start->battle) {
        return;
    }
    const World& start = *replay.start;
    const BattleData& data = *start.battle;
    Bytes out{r.head};
    out.raw(head_magic, sizeof head_magic);
    out.u32(0); // the head's size, filled in below
    out.u32(sizeof(World));
    out.u32(sizeof(Inputs));
    out.u32(fighter_slots);
    out.u32(max_pad_players);
    // what rebuilds the battle's data: the plan, and what presentation measured into it
    out.ints(plan.rigs);
    out.i32(plan.player_rig);
    out.u32(sizeof(BuildRecord));
    out.i32(static_cast<std::int32_t>(plan.extra.size()));
    out.raw(plan.extra.data(), plan.extra.size() * sizeof(BuildRecord));
    out.raw(data.ranker_line_ticks, sizeof data.ranker_line_ticks);
    out.ints(data.scene_text_scripts);
    // each weapon's record as its table and row
    const auto row_in = [](const std::vector<WeaponRecord>& rows, const WeaponRecord* p) {
        return !rows.empty() && p >= rows.data() && p < rows.data() + rows.size()
                   ? static_cast<int>(p - rows.data())
                   : -1;
    };
    for (const Fighter& f : start.fighters) {
        for (int i = 0; i < weapons_each; ++i) {
            const WeaponRecord* p = weapon_of(f, i)->record;
            const int arm = row_in(data.combat.arms, p), back = row_in(data.combat.backs, p);
            out.i32(arm >= 0 ? 0 : back >= 0 ? 1 : -1);
            out.i32(arm >= 0 ? arm : back);
        }
    }
    // the world's fields, the names scrubbed and its pointers cleared
    auto bare = std::make_unique<World>(start);
    scrub_names(*bare);
    out.u64(world_hash(*bare));
    out.u64(world_fight_hash(*bare));
    bare->battle.reset();
    bare->story_tables = nullptr;
    bare->roster_tables = nullptr;
    bare->stage.data = nullptr;
    for (Fighter& f : bare->fighters) {
        for (int i = 0; i < weapons_each; ++i) {
            weapon_of(f, i)->record = nullptr;
        }
    }
#define PUT_FIELD(name)                                                                            \
    out.u32(sizeof(bare->name));                                                                   \
    out.raw(&bare->name, sizeof(bare->name));
    FIGHT_FIELDS(PUT_FIELD)
#undef PUT_FIELD
    const std::uint32_t head_size = static_cast<std::uint32_t>(r.head.size());
    std::memcpy(r.head.data() + 8, &head_size, 4);
    r.pending_at = SIZE_MAX;
    r.replay = &replay;
    r.ready = true;
}

void report_fight_clear() {
    FightRecord& r = record();
    r.ready = false;
    r.replay = nullptr;
}

void report_fight_step(const Inputs& inputs) {
    FightRecord& r = record();
    if (!r.ready || !r.replay->recording ||
        static_cast<int>(r.replay->inputs.size()) >= replay_max_steps) {
        return;
    }
    r.pending = inputs;
    r.pending_at = r.replay->inputs.size();
}

std::string report_fight_bytes(const World* live) {
    const FightRecord& r = record();
    if (!r.ready || r.replay->inputs.empty()) {
        return {};
    }
    std::string bytes(r.head.begin(), r.head.end());
    std::int64_t final_step = -1;
    std::uint64_t final_hash = 0;
    // the live world's hash only while its fight is still recorded (after it, menus change it)
    if (live && r.replay->recording) {
        auto scrubbed = std::make_unique<World>(*live);
        scrub_names(*scrubbed);
        final_step = live->step;
        final_hash = world_hash(*scrubbed);
    }
    write_tail(r, final_step, final_hash, [&](const void* data, std::size_t size) {
        bytes.append(static_cast<const char*>(data), size);
    });
    return bytes;
}

void report_fight_write(void* to, FightPut put) {
    const FightRecord& r = record();
    if (!r.ready) {
        return;
    }
    put(to, r.head.data(), r.head.size());
    write_tail(r, -1, 0, [&](const void* data, std::size_t size) { put(to, data, size); });
}

std::string report_fight_pack(const std::string& raw) {
    std::string columns = raw;
    inputs_to_columns(columns);
    const int bound = LZ4_compressBound(static_cast<int>(columns.size()));
    std::string packed(sizeof pack_magic + 4 + static_cast<std::size_t>(bound), '\0');
    std::memcpy(packed.data(), pack_magic, sizeof pack_magic);
    const std::uint32_t raw_size = static_cast<std::uint32_t>(raw.size());
    std::memcpy(packed.data() + sizeof pack_magic, &raw_size, 4);
    const int size = LZ4_compress_default(columns.data(), packed.data() + sizeof pack_magic + 4,
                                          static_cast<int>(columns.size()), bound);
    if (size <= 0) {
        return {};
    }
    packed.resize(sizeof pack_magic + 4 + static_cast<std::size_t>(size));
    return packed;
}

bool report_fight_unpack(const std::string& packed, std::string& raw, std::string& error) {
    if (packed.size() >= 8 && std::memcmp(packed.data(), head_magic, 8) == 0) {
        raw = packed; // not packed (a crash file's own)
        return true;
    }
    std::uint32_t raw_size = 0;
    if (packed.size() < sizeof pack_magic + 4 ||
        std::memcmp(packed.data(), pack_magic, sizeof pack_magic) != 0) {
        error = "not a fight record";
        return false;
    }
    std::memcpy(&raw_size, packed.data() + sizeof pack_magic, 4);
    if (raw_size > 256u << 20) {
        error = "the record says it is bigger than any fight";
        return false;
    }
    raw.assign(raw_size, '\0');
    const int size = LZ4_decompress_safe(packed.data() + sizeof pack_magic + 4, raw.data(),
                                         static_cast<int>(packed.size() - sizeof pack_magic - 4),
                                         static_cast<int>(raw_size));
    if (size != static_cast<int>(raw_size)) {
        error = "the record doesn't unpack (LZ4)";
        return false;
    }
    inputs_from_columns(raw);
    return true;
}

bool report_fight_read(const std::string& raw, FightReplay& out, std::string& error) {
    Reader in{raw};
    char magic[8];
    in.raw(magic, sizeof magic);
    if (!in.ok || std::memcmp(magic, head_magic, sizeof magic) != 0) {
        error = "not a fight record";
        return false;
    }
    in.get<std::uint32_t>(); // the head's size
    const std::uint32_t world_size = in.get<std::uint32_t>();
    const std::uint32_t input_size = in.get<std::uint32_t>();
    const std::uint32_t slots = in.get<std::uint32_t>();
    const std::uint32_t pads = in.get<std::uint32_t>();
    if (world_size != sizeof(World) || input_size != sizeof(Inputs) || slots != fighter_slots ||
        pads != max_pad_players) {
        error = "the record is from a build whose world differs (World " +
                std::to_string(world_size) + " bytes, this build's " +
                std::to_string(sizeof(World)) + "): replay it on the build the report names";
        return false;
    }
    out.rigs = in.ints();
    out.player_rig = in.get<std::int32_t>();
    const std::uint32_t build_size = in.get<std::uint32_t>();
    const std::int32_t extras = in.get<std::int32_t>();
    if (build_size != sizeof(BuildRecord) || extras < 0 || extras > 64) {
        error = "the record's builds don't read";
        return false;
    }
    out.extra.resize(static_cast<std::size_t>(extras));
    in.raw(out.extra.data(), out.extra.size() * sizeof(BuildRecord));
    in.raw(out.ranker_line_ticks, sizeof out.ranker_line_ticks);
    out.scene_text_scripts = in.ints();
    for (std::uint32_t k = 0; k < fighter_slots * weapons_each; ++k) {
        const int table = in.get<std::int32_t>();
        out.weapon_rows.emplace_back(table, in.get<std::int32_t>());
    }
    out.start_hash = in.get<std::uint64_t>();
    out.start_fight_hash = in.get<std::uint64_t>();
    out.start = std::make_unique<World>();
    World& w = *out.start;
#define GET_FIELD(name)                                                                            \
    if (in.get<std::uint32_t>() != sizeof(w.name)) {                                               \
        error = "the record's " #name " doesn't fit this build's";                                 \
        return false;                                                                              \
    }                                                                                              \
    in.raw(&w.name, sizeof(w.name));
    FIGHT_FIELDS(GET_FIELD)
#undef GET_FIELD
    char tail[4];
    in.raw(tail, sizeof tail);
    if (!in.ok || std::memcmp(tail, tail_magic, sizeof tail) != 0) {
        error = "the record has no steps";
        return false;
    }
    const std::uint32_t steps = in.get<std::uint32_t>();
    in.get<std::uint32_t>(); // the input's size, checked above
    if (steps > (raw.size() - in.at) / sizeof(Inputs)) {
        error = "the record's steps are cut short";
        return false;
    }
    out.inputs.resize(steps);
    in.raw(out.inputs.data(), out.inputs.size() * sizeof(Inputs));
    const std::uint32_t hashes = in.get<std::uint32_t>();
    for (std::uint32_t k = 0; k < hashes && in.ok; ++k) {
        const int step = static_cast<int>(in.get<std::uint32_t>());
        out.fight_hashes.emplace_back(step, in.get<std::uint64_t>());
    }
    out.final_step = in.get<std::int64_t>();
    out.final_hash = in.get<std::uint64_t>();
    if (!in.ok) {
        error = "the record is cut short";
        return false;
    }
    return true;
}

bool report_fight_attach(World& world, const FightReplay& replay, PackLibrary& packs,
                         const StageData& stage, const StoryTables* story,
                         const RosterTables* roster, std::string& error) {
    BattleWorldPlan plan;
    plan.rigs = replay.rigs;
    plan.player_rig = replay.player_rig;
    plan.extra = replay.extra;
    const std::shared_ptr<BattleData> battle = battle_world_data(world, plan, packs, error);
    if (!battle) {
        return false;
    }
    std::memcpy(battle->ranker_line_ticks, replay.ranker_line_ticks,
                sizeof battle->ranker_line_ticks);
    battle->scene_text_scripts = replay.scene_text_scripts;
    world.stage.data = &stage;
    world.story_tables = story;
    world.roster_tables = roster;
    for (int slot = 0; slot < fighter_slots; ++slot) {
        for (int i = 0; i < weapons_each; ++i) {
            const auto [table, row] =
                replay.weapon_rows[static_cast<std::size_t>(slot * weapons_each + i)];
            const std::vector<WeaponRecord>& rows =
                table == 0 ? battle->combat.arms : battle->combat.backs;
            Weapon& w = *weapon_of(world.fighters[static_cast<std::size_t>(slot)], i);
            w.record = table < 0 || row < 0 || row >= static_cast<int>(rows.size())
                           ? nullptr
                           : &rows[static_cast<std::size_t>(row)];
        }
    }
    return true;
}
