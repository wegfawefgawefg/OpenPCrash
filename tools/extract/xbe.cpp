#include "xbe.hpp"

#include <cstdio>
#include <cstring>
#include <utility>

namespace {

std::uint32_t read_u32(const std::vector<std::uint8_t>& bytes, std::size_t at) {
    std::uint32_t value = 0;
    if (at + 4 <= bytes.size()) {
        std::memcpy(&value, bytes.data() + at, 4);
    }
    return value;
}

} // namespace

bool open_xbe(Xbe& xbe, const std::string& path, std::string& error) {
    FILE* file = std::fopen(path.c_str(), "rb");
    if (!file) {
        error = "can't open the XBE " + path;
        return false;
    }
    std::fseek(file, 0, SEEK_END);
    const long size = std::ftell(file);
    std::fseek(file, 0, SEEK_SET);
    std::vector<std::uint8_t> bytes(size > 0 ? static_cast<std::size_t>(size) : 0);
    const std::size_t got = std::fread(bytes.data(), 1, bytes.size(), file);
    std::fclose(file);
    if (got != bytes.size()) {
        error = "can't read the XBE " + path;
        return false;
    }
    return open_xbe_bytes(xbe, std::move(bytes), path, error);
}

bool open_xbe_bytes(Xbe& xbe, std::vector<std::uint8_t> bytes, const std::string& label,
                    std::string& error) {
    xbe = Xbe{};
    xbe.bytes = std::move(bytes);
    if (xbe.bytes.size() < 0x200 || std::memcmp(xbe.bytes.data(), "XBEH", 4) != 0) {
        error = label + " is not an XBE";
        return false;
    }

    // image header: base at +0x104, section count at +0x11C, section headers at +0x120 (a VA)
    xbe.base = read_u32(xbe.bytes, 0x104);
    const std::uint32_t count = read_u32(xbe.bytes, 0x11C);
    const std::uint32_t table = read_u32(xbe.bytes, 0x120) - xbe.base;
    for (std::uint32_t i = 0; i < count; ++i) {
        const std::size_t at = table + i * 0x38;
        XbeSection section;
        section.va = read_u32(xbe.bytes, at + 4);
        section.vsize = read_u32(xbe.bytes, at + 8);
        section.raw = read_u32(xbe.bytes, at + 12);
        section.rawsize = read_u32(xbe.bytes, at + 16);
        const std::uint32_t name = read_u32(xbe.bytes, at + 20) - xbe.base;
        for (std::size_t c = name; c < xbe.bytes.size() && xbe.bytes[c] != 0; ++c) {
            section.name.push_back(static_cast<char>(xbe.bytes[c]));
        }
        xbe.sections.push_back(section);
    }
    return true;
}

bool xbe_read(const Xbe& xbe, std::uint32_t va, void* out, std::size_t n) {
    auto* dest = static_cast<std::uint8_t*>(out);
    std::memset(dest, 0, n);
    for (const XbeSection& section : xbe.sections) {
        if (va < section.va || va >= section.va + section.vsize) {
            continue;
        }
        const std::uint32_t offset = va - section.va;
        for (std::size_t i = 0; i < n; ++i) {
            const std::size_t within = offset + i;
            if (within < section.rawsize && section.raw + within < xbe.bytes.size()) {
                dest[i] = xbe.bytes[section.raw + within];
            }
        }
        return true;
    }
    return false;
}

std::uint32_t xbe_u32(const Xbe& xbe, std::uint32_t va) {
    std::uint32_t value = 0;
    xbe_read(xbe, va, &value, 4);
    return value;
}

std::int32_t xbe_i32(const Xbe& xbe, std::uint32_t va) {
    std::int32_t value = 0;
    xbe_read(xbe, va, &value, 4);
    return value;
}

std::int16_t xbe_i16(const Xbe& xbe, std::uint32_t va) {
    std::int16_t value = 0;
    xbe_read(xbe, va, &value, 2);
    return value;
}

std::uint8_t xbe_u8(const Xbe& xbe, std::uint32_t va) {
    std::uint8_t value = 0;
    xbe_read(xbe, va, &value, 1);
    return value;
}

float xbe_f32(const Xbe& xbe, std::uint32_t va) {
    float value = 0;
    xbe_read(xbe, va, &value, 4);
    return value;
}

std::string xbe_cstring(const Xbe& xbe, std::uint32_t va) {
    std::string text;
    for (std::uint32_t at = va; text.size() < 4096; ++at) {
        const std::uint8_t c = xbe_u8(xbe, at);
        if (c == 0) {
            break;
        }
        text.push_back(static_cast<char>(c));
    }
    return text;
}
