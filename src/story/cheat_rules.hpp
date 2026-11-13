#pragma once
// The PC's cheats and price rules (docs/settings.md, "CHEATS"): story state like the money they act
// on, so the world holds them, the hash covers them and the save keeps them in its (pc-rules) form
// (docs/saves.md). Set only from PC CONFIG's CHEATS page between fights; the fight reads MONEY WON
// when it builds its result. All defaults are retail, and a save from before them loads as retail.
#include <cstdint>

// money's cap in units of 10 NY (money_add's 99,999,999: 999,999,990 NY)
inline constexpr int money_cap = 99999999;

struct CheatRules {
    bool cheated = false;          // a cheat or a non-retail rule was used in this game (kept)
    bool infinite_money = false;   // the money held at money_cap outside fights
    std::int16_t tune_divisor = 1; // Plus Tech's tune prices divided by it; 0 free
    std::int8_t shop_divisor = 1;  // the shops' prices (buying and selling) divided by it
    std::int8_t money_won = 1;     // a fight's prize multiplied by it
};

// the choices each rule steps through, retail first
inline constexpr int tune_divisors[] = {1, 10, 20, 100, 200, 0};
inline constexpr int shop_divisors[] = {1, 2, 4};
inline constexpr int money_won_factors[] = {1, 2, 5, 10};

// any rule off retail (the cheats themselves mark the game when used)
inline bool cheat_rules_retail(const CheatRules& r) {
    return r.tune_divisor == 1 && r.shop_divisor == 1 && r.money_won == 1 && !r.infinite_money;
}

// a price divided and rounded to whole units of 10 NY, at least 1 unit; the sign kept, so a tune
// down's refund shrinks with the tune up's price
inline int cheat_price_divided(int price, int divisor) {
    if (divisor == 1 || price == 0) {
        return price;
    }
    if (divisor <= 0) {
        return 0; // free
    }
    const long long size = price < 0 ? -static_cast<long long>(price) : price;
    long long out = (size + divisor / 2) / divisor;
    out = out < 1 ? 1 : out;
    return static_cast<int>(price < 0 ? -out : out);
}

// Plus Tech's price for a tune, a module's or a Chip's
inline int cheat_tune_price(const CheatRules& r, int price) {
    return cheat_price_divided(price, r.tune_divisor);
}

// a shop's price, bought or sold (Wild Machines, the dealers, Sonic Amp)
inline int cheat_shop_price(const CheatRules& r, int price) {
    return cheat_price_divided(price, r.shop_divisor);
}

// a fight's prize, held to the money's cap
inline int cheat_prize(const CheatRules& r, int prize) {
    if (prize <= 0) {
        return prize;
    }
    const long long out = static_cast<long long>(prize) * (r.money_won > 0 ? r.money_won : 1);
    return static_cast<int>(out > money_cap ? money_cap : out);
}
