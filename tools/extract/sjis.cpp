#include "sjis.hpp"

#include <SDL3/SDL.h>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

namespace {

// code page 932 to UTF-8, strict: an unmapped byte fails the whole string as iconv would
bool cp932_to_utf8(const char* bytes, std::size_t size, std::string& out) {
    if (size == 0) {
        out.clear();
        return true;
    }
    const int wide = MultiByteToWideChar(932, MB_ERR_INVALID_CHARS, bytes, static_cast<int>(size),
                                         nullptr, 0);
    if (wide <= 0) {
        return false;
    }
    std::vector<wchar_t> text(static_cast<std::size_t>(wide));
    MultiByteToWideChar(932, MB_ERR_INVALID_CHARS, bytes, static_cast<int>(size), text.data(), wide);
    const int narrow =
        WideCharToMultiByte(CP_UTF8, 0, text.data(), wide, nullptr, 0, nullptr, nullptr);
    out.assign(static_cast<std::size_t>(narrow), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), wide, out.data(), narrow, nullptr, nullptr);
    return true;
}

} // namespace

std::string sjis_to_utf8(const std::string& sjis) {
    std::string out;
    return cp932_to_utf8(sjis.data(), sjis.size(), out) ? out : sjis;
}

char* sjis_iconv_string(const char*, const char* bytes, std::size_t size) {
    std::string out;
    if (!cp932_to_utf8(bytes, size, out)) {
        return nullptr;
    }
    char* copy = static_cast<char*>(SDL_malloc(out.size() + 1));
    SDL_memcpy(copy, out.c_str(), out.size() + 1);
    return copy;
}

#else
#include <iconv.h>

namespace {

// from to UTF-8 through iconv, strict as code page 932 is on Windows: a byte iconv can't map fails
// the whole string (SDL_iconv_string would skip it and read on, so a bad lead byte left its trail
// byte as a character of its own)
bool iconv_to_utf8(const char* from, const char* bytes, std::size_t size, std::string& out) {
    iconv_t convert = iconv_open("UTF-8", from);
    if (convert == reinterpret_cast<iconv_t>(-1)) {
        return false;
    }
    std::vector<char> text(size * 4 + 16);
    char* in_at = const_cast<char*>(bytes);
    std::size_t in_left = size;
    char* out_at = text.data();
    std::size_t out_left = text.size();
    const std::size_t result = iconv(convert, &in_at, &in_left, &out_at, &out_left);
    iconv_close(convert);
    if (result == static_cast<std::size_t>(-1)) {
        return false;
    }
    out.assign(text.data(), text.size() - out_left);
    return true;
}

} // namespace

std::string sjis_to_utf8(const std::string& sjis) {
    std::string out;
    return iconv_to_utf8("CP932", sjis.data(), sjis.size(), out) ? out : sjis;
}

char* sjis_iconv_string(const char* from, const char* bytes, std::size_t size) {
    std::string out;
    if (!iconv_to_utf8(from, bytes, size, out)) {
        return nullptr;
    }
    char* copy = static_cast<char*>(SDL_malloc(out.size() + 1));
    SDL_memcpy(copy, out.c_str(), out.size() + 1);
    return copy;
}
#endif
