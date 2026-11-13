#pragma once
// Bug reports to the owner's server (docs/release.md, "Bug reports"), sent only after the player
// says yes: the first-run screen asks once (an install that never saw the question is asked once
// over the title), and PC CONFIG's GAME page changes it. Kinds: a crash (a signal or an exception,
// written to a file by the handler and sent on the next start), a hang (the watchdog), the renderer
// failing or its device lost, a start that fails, and the player's own (REPORT A BUG in the pause
// and in PC CONFIG: a note and, from the pause, a small JPEG of the picture). Each carries the run
// log's tail with the home folder taken out, the build stamp, the OS, the GPU and its driver, the
// PC settings and, from a fight, its replay (report_fight.hpp), which reproduces it exactly.
//
// The address is PHANTOMCRASH_REPORT_URL (a CMake cache option, empty in the public source; with
// none, nothing here shows or sends). Reports go as JSON over HTTPS from a worker thread; one that
// can't (offline, the server down) waits in <per-user folder>/reports/outbox/ for a later start,
// tried a few times at most. No player names, no paths with the home folder in them, and the
// server keeps no addresses.
#include <string>
#include <utility>
#include <vector>

// first thing in main after the run log: the consent read, and with a yes the crash handler, the
// worker (with last time's crash files and the outbox) and the watchdog. quiet: a headless capture
// or a scripted run (--frames): no automatic reports and no question
void reports_start(int argc, char** argv, bool quiet);
// at the end of main: the watchdog stopped, what's queued sent for a moment and the rest kept
void reports_finish();

// an address was built in (or PHANTOMCRASH_REPORT_URL given): the question and the menu items show
bool reports_available();
// the question shows over the title and in the setup: available, unanswered, not a quiet run
bool reports_question_due();
enum class ReportConsent { unasked, yes, no };
ReportConsent report_consent();
// the player's answer, kept in the per-user folder; yes arms the crash handler and the worker, no
// drops what waits in the outbox
void report_consent_set(bool yes);

// the context every report carries, as it changes
void report_set_settings(std::vector<std::pair<std::string, std::string>> settings);
void report_set_gpu(const std::string& device);
// each front end update: the AppState, whether a fight is stepping, and the game's cheats and price
// rules ("retail" without any; PC CONFIG's CHEATS)
void report_note_game(int app_state, bool fighting, const std::string& rules);

// the watchdog: each frame alive; before something long on the main thread (a battle's load, the
// cook), how long the next frame may take
void report_frame();
void report_busy(double seconds);

// an automatic report of a kind (hang, renderer, startup) with what went wrong, from any thread;
// once a kind a session, a few at most, ten seconds apart. fight: the fight's record as bytes
// (report_fight_bytes), "" none; stack: the hang's. False when nothing was queued
bool report_send(const char* kind, const std::string& message, const std::string& fight = {},
                 const std::vector<std::string>& stack = {});
// the player's own: their note, the picture (a PNG file the worker turns into a small JPEG and then
// removes; "" none) and the fight's record. "" when queued, else why not
std::string report_send_manual(const std::string& note, const std::string& shot_png,
                               const std::string& fight);
// where the picture for a manual report is written (in the reports folder)
std::string report_shot_path();

// port-only, headless checks: PHANTOMCRASH_DEV_CRASH=U crashes on purpose at front end update U
void report_crash_on_purpose();
