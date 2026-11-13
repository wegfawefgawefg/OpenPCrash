#include "cook/sexp_read.hpp"

#include <fstream>
#include <sstream>

bool parse_sexp_file(const std::string& path, gsexp::ParseResult& out, std::string& error) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        error = "can't open " + path;
        return false;
    }
    std::stringstream text;
    text << in.rdbuf();
    out = gsexp::parse_owned(text.str());
    if (!out.ok) {
        error = "can't parse " + path;
        if (!out.diagnostics.empty()) {
            error += ": " + out.diagnostics.front().message + " at line " +
                     std::to_string(out.diagnostics.front().line);
        }
        return false;
    }
    return true;
}
