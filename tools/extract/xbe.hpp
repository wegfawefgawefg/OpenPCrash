#pragma once
// The retail XBE, read only: its sections and reads by virtual address. Bytes past a section's
// raw size read as zero (its .bss), as the loader would leave them.
#include <cstdint>
#include <string>
#include <vector>

struct XbeSection {
    std::string name;
    std::uint32_t va = 0;
    std::uint32_t vsize = 0;
    std::uint32_t raw = 0;
    std::uint32_t rawsize = 0;
};

struct Xbe {
    std::vector<std::uint8_t> bytes;
    std::uint32_t base = 0;
    std::vector<XbeSection> sections;
};

bool open_xbe(Xbe& xbe, const std::string& path, std::string& error);
// the same from bytes already read (the disc image's default.xbe); label is for the error
bool open_xbe_bytes(Xbe& xbe, std::vector<std::uint8_t> bytes, const std::string& label,
                    std::string& error);
// copies n bytes at va into out; false if va is in no section
bool xbe_read(const Xbe& xbe, std::uint32_t va, void* out, std::size_t n);
std::uint32_t xbe_u32(const Xbe& xbe, std::uint32_t va);
std::int32_t xbe_i32(const Xbe& xbe, std::uint32_t va);
std::int16_t xbe_i16(const Xbe& xbe, std::uint32_t va);
std::uint8_t xbe_u8(const Xbe& xbe, std::uint32_t va);
float xbe_f32(const Xbe& xbe, std::uint32_t va);
// a zero-ended byte string (Shift-JIS in most tables), raw
std::string xbe_cstring(const Xbe& xbe, std::uint32_t va);
