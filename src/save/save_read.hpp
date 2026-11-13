#pragma once
// The save reader's small helpers (save_parse.cpp, save_parse_rumble.cpp): numbers, fields and
// lists out of the .sexp forms, with the first error kept.
#include "save/save.hpp"
#include "save/save_names.hpp"

#include <cstdlib>
#include <gsexp/sexp.hpp>
#include <string>

namespace save_read {

using gsexp::FormView;
using gsexp::Node;

struct In {
    std::string error;
};

inline bool fail(In& in, const std::string& what) {
    if (in.error.empty()) {
        in.error = what;
    }
    return false;
}

// an integer atom; false (and the error noted) for anything else
inline bool number(In& in, Node node, long long& out) {
    if (!node.valid() || node.is_list() || node.is_string()) {
        return fail(in, "expected a number");
    }
    const std::string text(node.text());
    char* end = nullptr;
    out = std::strtoll(text.c_str(), &end, 0);
    if (text.empty() || *end != '\0') {
        return fail(in, "not a number: " + text);
    }
    return true;
}
template <typename T> inline bool number_as(In& in, Node node, T& out) {
    long long value = 0;
    if (!number(in, node, value)) {
        return false;
    }
    out = static_cast<T>(value);
    return true;
}
// (name value) inside a form
template <typename T> inline bool field(In& in, FormView form, std::string_view name, T& out) {
    const Node node = form.find(name);
    if (!node.valid()) {
        return fail(in, "missing (" + std::string(name) + ")");
    }
    return number_as(in, node.child_at(1), out);
}
inline bool flag(In& in, FormView form, std::string_view name, bool& out) {
    const Node node = form.find(name).child_at(1);
    if (node.is_atom("true") || node.is_atom("false")) {
        out = node.is_atom("true");
        return true;
    }
    return fail(in, "missing (" + std::string(name) + " true|false)");
}
// (name a b c ...) into count values; a shorter list leaves the rest at fill
template <typename T>
inline bool list(In& in, FormView form, std::string_view name, T* out, int count, T fill, bool exact) {
    const Node node = form.find(name);
    if (!node.valid()) {
        return fail(in, "missing (" + std::string(name) + " ...)");
    }
    const int given = static_cast<int>(node.child_count()) - 1;
    if (given > count || (exact && given != count)) {
        return fail(in, "(" + std::string(name) + ") has " + std::to_string(given) + " values");
    }
    for (int i = 0; i < count; ++i) {
        out[i] = fill;
        if (i < given && !number_as(in, node.child_at(static_cast<std::size_t>(i) + 1), out[i])) {
            return false;
        }
    }
    return true;
}
inline bool glyphs(In& in, FormView form, std::string_view name, std::int16_t* out, int length) {
    return list(in, form, name, out, length, std::int16_t{-1}, false);
}
// a word from a table, or a number
template <std::size_t N>
inline bool word(In& in, Node node, const std::string_view (&words)[N], int& out) {
    if (!node.valid() || node.is_list() || !save_names::value(words, node.text(), out)) {
        return fail(in, "unknown word: " + std::string(node.valid() ? node.text() : ""));
    }
    return true;
}
inline bool index_in(In& in, long long index, int count, std::string_view what) {
    if (index < 0 || index >= count) {
        return fail(in, std::string(what) + " " + std::to_string(index) + " out of range");
    }
    return true;
}

// one RumbleContext form (save_parse_rumble.cpp)
bool read_rumble(In& in, FormView form, RumbleSlot* rumble, int& seen);

} // namespace save_read
