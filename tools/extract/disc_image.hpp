#pragma once
// Where the disc's files come from: an extracted folder (Media/, and default.xbe beside it), an
// Xbox disc image (XISO: the XDVDFS volume from byte 0), or a full Redump-style image with the
// video partition ahead of the game's. The files are read by their disc path ("Media/sound/x.wav"),
// case-insensitively in an image as the Xbox's own file system does. No keys and no decryption:
// an XDVDFS volume is plain.
#include <cstdint>
#include <map>
#include <string>
#include <vector>

// why a disc couldn't be opened, for a message a player understands
enum class DiscProblem {
    none,
    missing,           // nothing at that path
    archive,           // a .zip, .7z or .rar: unpack it first
    not_xbox,          // a file or folder, but no Xbox disc in it
    not_phantom_crash, // an Xbox disc of another game
    other_release,     // Phantom Crash, but not the release the port reads
    damaged,           // an image cut short or with a broken file table
};

struct DiscEntry {
    std::uint64_t offset = 0; // from the start of the image file
    std::uint32_t size = 0;
    bool directory = false;
};

struct DiscSource {
    std::string path;            // the folder holding Media/, or the image file
    bool image = false;
    std::uint64_t partition = 0; // where the XDVDFS volume starts in the image
    std::uint64_t image_size = 0;
    // images: every file and folder by its upper-cased disc path ("MEDIA/SOUND/12SENTOU.WAV"),
    // and its name as stored
    std::map<std::string, DiscEntry> entries;
    std::map<std::string, std::string> stored_names;
};

// a folder or an image; the problem says why not
bool open_disc_source(DiscSource& source, const std::string& path, DiscProblem& problem,
                      std::string& error);
// size bytes of a disc file from offset; false if it's missing or the read came up short
bool disc_read(const DiscSource& source, const std::string& disc_path, std::uint64_t offset,
               std::uint64_t size, std::vector<std::uint8_t>& out);
// the whole file; empty if missing
std::vector<std::uint8_t> disc_file(const DiscSource& source, const std::string& disc_path);
bool disc_exists(const DiscSource& source, const std::string& disc_path);
// the file names in a disc folder ("Media/sound"), as stored on the disc
std::vector<std::string> disc_list(const DiscSource& source, const std::string& folder);
// "image" or "folder", with the partition: for the log
std::string disc_source_label(const DiscSource& source);
