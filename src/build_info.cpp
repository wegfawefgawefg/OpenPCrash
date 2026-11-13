#include "build_info.hpp"

#include "build_stamp.h" // written by cmake/release.cmake

const BuildInfo& build_info() {
#ifdef PHANTOMCRASH_RELEASE
    constexpr bool release = true;
#else
    constexpr bool release = false;
#endif
    static const BuildInfo info{PHANTOMCRASH_GAME_VERSION, PHANTOMCRASH_COMMIT,
                                PHANTOMCRASH_BUILD_DATE, release};
    return info;
}

std::string build_label() {
    const BuildInfo& info = build_info();
    std::string label = std::string("OpenPCrash ") + info.version;
    if (!info.release) {
        return label + " dev";
    }
    for (const char* part : {info.date, info.commit}) {
        if (*part != '\0') {
            label += std::string(" ") + part;
        }
    }
    return label;
}
