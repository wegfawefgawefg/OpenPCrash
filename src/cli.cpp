#include "cli.hpp"

#include "game_paths.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace {

void print_usage() {
    std::fprintf(stderr,
                 "phantomcrash [--data DIR] [--cooked DIR] [--stage 0..7] [--build N]\n"
                 "             [--size WxH] [--camera x,y,z,yaw,pitch] [--day N] [--safe-rect]\n"
                 "             [--capture out.png --frames N] [--script pad.txt] [--trace N]\n"
                 "             [--dummy BUILD [--dummy-distance D] [--dummy-away]]\n"
                 "             [--talk NAME [--talk-press N] [--talk-chord FRAME]]\n"
                 "             [--viewer] [--state HEX] [--press U:KEY+KEY,...]\n"
                 "             [--click U:X:Y,...] [--hover U:X:Y,...] [--drag U:X:Y:X2:Y2,...]\n"
                 "             [--no-dialogue | --dialogue]\n"
                 "             [--opponents N,oN,...] [--ai-level L] [--demo] [--follow N]\n"
                 "             [--ai-reach R] [--trace-ai N] [--pose-compare] [--intro]\n"
                 "             [--battle-script pad.txt] [--route route.txt]\n"
                 "             [--seed N] [--vs N] [--hash N]\n");
}

// "U:a+start,U:0x110": a pad word by update
bool parse_presses(const char* text, Options& options) {
    const char* at = text;
    while (*at) {
        char* end = nullptr;
        const int update = static_cast<int>(std::strtol(at, &end, 0));
        if (*end != ':') {
            return false;
        }
        at = end + 1;
        unsigned bits = 0;
        while (*at && *at != ',') {
            const char* stop = at;
            while (*stop && *stop != ',' && *stop != '+') {
                ++stop;
            }
            const std::string key(at, stop);
            static const struct {
                const char* name;
                unsigned bit;
            } names[] = {{"up", 0x1},    {"down", 0x2}, {"left", 0x4},  {"right", 0x8},
                         {"start", 0x10}, {"back", 0x20}, {"a", 0x100}, {"b", 0x200},
                         {"x", 0x400},   {"y", 0x800},
                         // PC: the stand's turn keys (Ui::pc_held, shifted up past the pad)
                         {"turnl", 0x10000}, {"turnr", 0x20000}};
            bool known = false;
            for (const auto& n : names) {
                if (key == n.name) {
                    bits |= n.bit;
                    known = true;
                }
            }
            if (!known) {
                bits |= static_cast<unsigned>(std::strtoul(key.c_str(), nullptr, 0));
            }
            at = *stop == '+' ? stop + 1 : stop;
        }
        options.presses.push_back({update, bits});
        if (*at == ',') {
            ++at;
        }
    }
    return true;
}

bool parse_clicks(const char* text, std::vector<std::pair<int, std::pair<float, float>>>& out) {
    const char* at = text;
    while (*at) {
        int update = 0;
        float x = 0, y = 0;
        int used = 0;
        if (std::sscanf(at, "%d:%f:%f%n", &update, &x, &y, &used) != 3) {
            return false;
        }
        out.push_back({update, {x, y}});
        at += used;
        if (*at == ',') {
            ++at;
        }
    }
    return true;
}

// --drag U:X:Y:X2:Y2,...: a left press at U, the pointer moved to X2, Y2 over 20 updates, let go
bool parse_drags(const char* text, std::vector<Drag>& out) {
    const char* at = text;
    while (*at) {
        Drag d;
        int used = 0;
        if (std::sscanf(at, "%d:%f:%f:%f:%f%n", &d.update, &d.from[0], &d.from[1], &d.to[0],
                        &d.to[1], &used) != 5) {
            return false;
        }
        out.push_back(d);
        at += used;
        if (*at == ',') {
            ++at;
        }
    }
    return true;
}

} // namespace

bool parse_cli(int argc, char** argv, Options& options) {
    options.data_dir = default_data_dir(); // the checkout's, or a release's per-user folder
    options.cooked_dir = default_cooked_dir();
    for (int i = 1; i < argc; ++i) {
        const char* arg = argv[i];
        const char* value = i + 1 < argc ? argv[i + 1] : nullptr;
        if (std::strcmp(arg, "--pose-compare") == 0) {
            options.pose_compare = true;
            options.viewer = true;
            continue;
        }
        if (std::strcmp(arg, "--safe-rect") == 0) {
            options.safe_rect = true;
            options.viewer = true;
            continue;
        }
        if (std::strcmp(arg, "--no-dialogue") == 0 || std::strcmp(arg, "--dialogue") == 0) {
            options.no_dialogue = arg[2] == 'n' ? 1 : 0;
            continue;
        }
        if (std::strcmp(arg, "--intro") == 0) {
            options.intro = true;
            continue;
        }
        if (std::strcmp(arg, "--seed") == 0 && value) {
            options.seed = std::atoi(value);
            ++i;
            continue;
        }
        if (std::strcmp(arg, "--demo") == 0) {
            options.demo = true;
            options.viewer = true;
            continue;
        }
        if (std::strcmp(arg, "--opponents") == 0 && value) {
            std::string list = value;
            std::size_t at = 0;
            while (at <= list.size()) {
                const std::size_t comma = std::min(list.find(',', at), list.size());
                if (comma > at) {
                    options.opponents.push_back(list.substr(at, comma - at));
                }
                at = comma + 1;
            }
            options.viewer = true;
        } else if (std::strcmp(arg, "--ai-level") == 0 && value) {
            options.ai_level = std::atoi(value);
        } else if (std::strcmp(arg, "--vs") == 0 && value) {
            options.vs = std::atoi(value);
        } else if (std::strcmp(arg, "--follow") == 0 && value) {
            options.follow = std::atoi(value);
        } else if (std::strcmp(arg, "--ai-reach") == 0 && value) {
            options.ai_reach = static_cast<float>(std::atof(value));
        } else if (std::strcmp(arg, "--trace-ai") == 0 && value) {
            options.trace_ai = std::atoi(value);
            options.viewer = true;
        } else if (std::strcmp(arg, "--day") == 0 && value) {
            options.day = std::atoi(value);
            options.viewer = true;
        } else if (std::strcmp(arg, "--data") == 0 && value) {
            options.data_dir = value;
        } else if (std::strcmp(arg, "--cooked") == 0 && value) {
            options.cooked_dir = value;
        } else if (std::strcmp(arg, "--viewer") == 0) {
            options.viewer = true;
            continue;
        } else if (std::strcmp(arg, "--dummy-away") == 0) {
            options.dummy_away = true;
            continue;
        } else if (std::strcmp(arg, "--state") == 0 && value) {
            options.start_state = static_cast<int>(std::strtol(value, nullptr, 16));
        } else if (std::strcmp(arg, "--press") == 0 && value) {
            if (!parse_presses(value, options)) {
                print_usage();
                return false;
            }
        } else if (std::strcmp(arg, "--click") == 0 && value) {
            if (!parse_clicks(value, options.clicks)) {
                print_usage();
                return false;
            }
        } else if (std::strcmp(arg, "--drag") == 0 && value) {
            if (!parse_drags(value, options.drags)) {
                print_usage();
                return false;
            }
        } else if (std::strcmp(arg, "--hover") == 0 && value) {
            if (!parse_clicks(value, options.hovers)) {
                print_usage();
                return false;
            }
        } else if (std::strcmp(arg, "--stage") == 0 && value) {
            options.stage = std::atoi(value);
            options.viewer = true;
        } else if (std::strcmp(arg, "--build") == 0 && value) {
            options.build = std::atoi(value);
            options.viewer = true;
        } else if (std::strcmp(arg, "--talk") == 0 && value) {
            options.talk = value;
        } else if (std::strcmp(arg, "--talk-press") == 0 && value) {
            options.talk_press = std::atoi(value);
        } else if (std::strcmp(arg, "--talk-chord") == 0 && value) {
            options.talk_chord = std::atoi(value);
        } else if (std::strcmp(arg, "--script") == 0 && value) {
            options.script = value;
            options.viewer = true;
        } else if (std::strcmp(arg, "--route") == 0 && value) {
            options.route = value;
        } else if (std::strcmp(arg, "--battle-script") == 0 && value) {
            options.battle_script = value;
        } else if (std::strcmp(arg, "--hash") == 0 && value) {
            options.hash = std::atoi(value);
        } else if (std::strcmp(arg, "--trace") == 0 && value) {
            options.trace = std::atoi(value);
            options.viewer = true;
        } else if (std::strcmp(arg, "--dummy") == 0 && value) {
            options.dummy = std::atoi(value);
            options.viewer = true;
        } else if (std::strcmp(arg, "--dummy-distance") == 0 && value) {
            options.dummy_distance = static_cast<float>(std::atof(value));
        } else if (std::strcmp(arg, "--capture") == 0 && value) {
            options.capture = value;
        } else if (std::strcmp(arg, "--frames") == 0 && value) {
            options.frames = std::atoi(value);
        } else if (std::strcmp(arg, "--size") == 0 && value) {
            if (std::sscanf(value, "%dx%d", &options.width, &options.height) != 2) {
                print_usage();
                return false;
            }
        } else if (std::strcmp(arg, "--camera") == 0 && value) {
            float* c = options.camera;
            if (std::sscanf(value, "%f,%f,%f,%f,%f", &c[0], &c[1], &c[2], &c[3], &c[4]) != 5) {
                print_usage();
                return false;
            }
            options.camera_given = true;
            options.viewer = true;
        } else {
            print_usage();
            return false;
        }
        ++i;
    }
    // a script runs to its end unless told otherwise (main.cpp)
    if (!options.capture.empty() && options.frames <= 0 && options.script.empty()) {
        options.frames = 1;
    }
    return true;
}

std::string capture_path_for(const Options& options, int frame, bool last) {
    if (options.capture.empty() || options.frames <= 0) {
        return {};
    }
    if (last) {
        return options.capture;
    }
    const char* shots = std::getenv("PHANTOMCRASH_DEV_SHOTS");
    for (const char* at = shots ? shots : ""; *at;) {
        char* end = nullptr;
        const long shot = std::strtol(at, &end, 10);
        if (end == at) {
            break;
        }
        if (shot == frame) {
            std::string base = options.capture;
            const std::size_t dot = base.rfind(".png");
            base = dot == std::string::npos ? base : base.substr(0, dot);
            return base + "_" + std::to_string(frame) + ".png";
        }
        at = *end == ',' ? end + 1 : end;
    }
    return {};
}
