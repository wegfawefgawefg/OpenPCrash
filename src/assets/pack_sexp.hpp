#pragma once
// A .sexp file the cook kept as text (tables in the core pack, layouts in screen packs, text and
// scripts in the text pack), parsed from the pack's memory. Names are data/-relative paths, e.g.
// "tables/legs.sexp" or "ui/layouts/title.sexp".
#include <gsexp/sexp.hpp>
#include <string>
#include <string_view>

struct Pack;

bool pack_sexp(const Pack& pack, std::string_view name, gsexp::ParseResult& out,
               std::string& error);
