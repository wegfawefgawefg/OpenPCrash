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

std::string sjis_to_utf8(const std::string& sjis) {
    iconv_t convert = iconv_open("UTF-8", "CP932");
    if (convert == reinterpret_cast<iconv_t>(-1)) {
        return sjis;
    }
    std::vector<char> out(sjis.size() * 4 + 16);
    char* in_at = const_cast<char*>(sjis.data());
    std::size_t in_left = sjis.size();
    char* out_at = out.data();
    std::size_t out_left = out.size();
    const std::size_t result = iconv(convert, &in_at, &in_left, &out_at, &out_left);
    iconv_close(convert);
    if (result == static_cast<std::size_t>(-1)) {
        return sjis;
    }
    return std::string(out.data(), out.size() - out_left);
}

char* sjis_iconv_string(const char* from, const char* bytes, std::size_t size) {
    return SDL_iconv_string("UTF-8", from, bytes, size);
}
#endif
