#pragma once
// Shift-JIS (cp932, the tables' text) to UTF-8: through iconv, and on Windows (which has no iconv,
// and whose SDL_iconv knows no cp932) through the system's own code page 932.
#include <cstddef>
#include <string>

std::string sjis_to_utf8(const std::string& sjis);
// bytes in from ("CP932" or "SHIFT_JIS") to UTF-8, strict on every system: null when any byte
// doesn't map (cp932 through Windows' code page there); SDL_free the result
char* sjis_iconv_string(const char* from, const char* bytes, std::size_t size);
