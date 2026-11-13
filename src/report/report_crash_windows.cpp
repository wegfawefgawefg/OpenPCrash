// Windows: an unhandled exception filter, abort() and the C runtime's own ends, after Mans In My
// House's. The stack is unwound from the fault's context with the unwind tables every x64 module
// carries (RtlLookupFunctionEntry, RtlVirtualUnwind): no dbghelp, no symbols on the player's
// machine. The file is written on a fresh thread (a stack overflow leaves the faulting one too
// little), then the process ends. The watchdog's sample of the main thread isn't done here.
#include "report/report_crash.hpp"
#include "report/report_fight.hpp"
#include "report/report_internal.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <windows.h>

namespace {

std::atomic<DWORD> writer{0};

void put(HANDLE file, const void* data, std::size_t size) {
    const char* text = static_cast<const char*>(data);
    while (size > 0) {
        DWORD sent = 0;
        if (!WriteFile(file, text, static_cast<DWORD>(size), &sent, nullptr) || sent == 0) {
            return;
        }
        text += sent;
        size -= sent;
    }
}

void put(HANDLE file, const char* text) {
    put(file, text, crash::text_size(text));
}

void put_hex(HANDLE file, std::uintptr_t value) {
    char text[24];
    crash::format_hex(text, value);
    put(file, text);
}

// "0x7ff6a4c01f3a OpenPCrash.exe+0x1f3a"
void put_frame(HANDLE file, std::uintptr_t address) {
    put_hex(file, address);
    HMODULE module = nullptr;
    char path[MAX_PATH];
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                               GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCSTR>(address), &module) &&
        module) {
        const DWORD length = GetModuleFileNameA(module, path, MAX_PATH);
        if (length > 0 && length < MAX_PATH) {
            const char* name = path;
            for (const char* at = path; *at; ++at) {
                if (*at == '\\' || *at == '/') {
                    name = at + 1;
                }
            }
            put(file, " ");
            put(file, name);
            put(file, "+");
            put_hex(file, address - reinterpret_cast<std::uintptr_t>(module));
        }
    }
    put(file, "\n");
}

bool read_word(std::uintptr_t address, std::uintptr_t& value) {
    SIZE_T read = 0;
    return ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<const void*>(address), &value,
                             sizeof value, &read) &&
           read == sizeof value;
}

int unwind(CONTEXT context, std::uintptr_t* frames, int most) {
    int count = 0;
#if defined(_M_X64) || defined(__x86_64__)
    while (count < most && context.Rip != 0) {
        frames[count++] = static_cast<std::uintptr_t>(context.Rip);
        DWORD64 image = 0;
        PRUNTIME_FUNCTION function = RtlLookupFunctionEntry(context.Rip, &image, nullptr);
        std::uintptr_t probe = 0;
        if (!read_word(static_cast<std::uintptr_t>(context.Rsp), probe)) {
            break;
        }
        if (!function) {
            // a leaf: its return address is on top of the stack
            context.Rip = probe;
            context.Rsp += 8;
            continue;
        }
        PVOID handler_data = nullptr;
        DWORD64 frame = 0;
        RtlVirtualUnwind(UNW_FLAG_NHANDLER, image, context.Rip, function, &context, &handler_data,
                         &frame, nullptr);
    }
#else
    (void)context;
    count = static_cast<int>(CaptureStackBackTrace(0, static_cast<DWORD>(most),
                                                   reinterpret_cast<void**>(frames), nullptr));
#endif
    return count;
}

const char* exception_name(DWORD code) {
    switch (code) {
    case EXCEPTION_ACCESS_VIOLATION:
        return "EXCEPTION_ACCESS_VIOLATION";
    case EXCEPTION_STACK_OVERFLOW:
        return "EXCEPTION_STACK_OVERFLOW";
    case EXCEPTION_ILLEGAL_INSTRUCTION:
        return "EXCEPTION_ILLEGAL_INSTRUCTION";
    case EXCEPTION_INT_DIVIDE_BY_ZERO:
        return "EXCEPTION_INT_DIVIDE_BY_ZERO";
    case EXCEPTION_IN_PAGE_ERROR:
        return "EXCEPTION_IN_PAGE_ERROR";
    case 0xC0000409:
        return "STATUS_STACK_BUFFER_OVERRUN (fail fast)";
    case 0xE06D7363:
    case 0x20474343:
        return "uncaught C++ exception";
    default:
        return "exception";
    }
}

struct CrashJob {
    const EXCEPTION_RECORD* record = nullptr; // null for abort()
    CONTEXT context{};
    const char* why = "abort()";
};

void write_crash(const CrashJob& job) {
    HANDLE file = CreateFileA(crash::text_path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file != INVALID_HANDLE_VALUE) {
        put(file, "openpcrash-crash 1\nreason ");
        if (const EXCEPTION_RECORD* record = job.record) {
            put(file, exception_name(record->ExceptionCode));
            put(file, " ");
            put_hex(file, record->ExceptionCode);
            if (record->ExceptionCode == EXCEPTION_ACCESS_VIOLATION &&
                record->NumberParameters >= 2) {
                const ULONG_PTR how = record->ExceptionInformation[0];
                put(file, how == 0 ? " reading " : how == 1 ? " writing " : " executing ");
                put_hex(file, record->ExceptionInformation[1]);
            }
        } else {
            put(file, job.why);
        }
        if (crash::last_words[0]) {
            put(file, ": ");
            put(file, crash::last_words);
        }
        put(file, "\nlog ");
        put(file, crash::log_path);
        put(file, "\ncontext ");
        std::size_t size = 0;
        const char* context_json = crash::context_text(size);
        put(file, context_json, size);
        put(file, "\nstack\n");
        if (job.record) {
            put(file, "pc ");
            put_frame(file, reinterpret_cast<std::uintptr_t>(job.record->ExceptionAddress));
        }
        std::uintptr_t frames[crash::max_frames];
        const int count =
            job.record ? unwind(job.context, frames, crash::max_frames)
                       : static_cast<int>(CaptureStackBackTrace(
                             0, crash::max_frames, reinterpret_cast<void**>(frames), nullptr));
        for (int i = 0; i < count; ++i) {
            put_frame(file, frames[i]);
        }
        FlushFileBuffers(file);
        CloseHandle(file);
    }
    HANDLE fight = CreateFileA(crash::fight_path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                               FILE_ATTRIBUTE_NORMAL, nullptr);
    if (fight != INVALID_HANDLE_VALUE) {
        report_fight_write(fight, [](void* to, const void* data, std::size_t size) {
            put(static_cast<HANDLE>(to), data, size);
        });
        LARGE_INTEGER written{};
        GetFileSizeEx(fight, &written);
        CloseHandle(fight);
        if (written.QuadPart == 0) {
            DeleteFileA(crash::fight_path);
        }
    }
}

DWORD WINAPI crash_thread(LPVOID job) {
    write_crash(*static_cast<const CrashJob*>(job));
    return 0;
}

// on a thread with a fresh stack, the crashed one waiting
void write_crash_aside(CrashJob& job) {
    HANDLE thread = CreateThread(nullptr, 512 * 1024, crash_thread, &job, 0, nullptr);
    if (!thread) {
        write_crash(job);
        return;
    }
    WaitForSingleObject(thread, 15000);
    CloseHandle(thread);
}

// a second thread faulting while the first writes waits for the first, which ends the process
bool someone_else_writing() {
    if (!crash::crashing.exchange(true)) {
        writer = GetCurrentThreadId();
        return false;
    }
    if (writer.load() != GetCurrentThreadId()) {
        Sleep(20000);
    }
    return true;
}

LONG WINAPI on_exception(EXCEPTION_POINTERS* info) {
    if (!info || !info->ContextRecord || someone_else_writing()) {
        return EXCEPTION_CONTINUE_SEARCH;
    }
    static CrashJob job;
    job.record = info->ExceptionRecord;
    job.context = *info->ContextRecord;
    write_crash_aside(job);
    TerminateProcess(GetCurrentProcess(),
                     info->ExceptionRecord ? info->ExceptionRecord->ExceptionCode : 3);
    return EXCEPTION_EXECUTE_HANDLER;
}

void ended(const char* why) {
    if (someone_else_writing()) {
        return;
    }
    static CrashJob job;
    job.why = why;
    write_crash_aside(job);
    TerminateProcess(GetCurrentProcess(), 3);
}

void on_abort(int) {
    ended("abort()");
}

void on_invalid_parameter(const wchar_t*, const wchar_t*, const wchar_t*, unsigned, uintptr_t) {
    ended("an invalid parameter to the C runtime");
}

void on_purecall() {
    ended("a pure virtual call");
}

} // namespace

void crash_handlers_install(const std::string& text_path, const std::string& fight_path) {
    crash::set_paths(text_path.c_str(), fight_path.c_str());
    // no Windows Error Reporting box over the game: the crash file says it
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    SetUnhandledExceptionFilter(on_exception);
    crash_guard_thread();
    std::signal(SIGABRT, on_abort);
#if defined(_MSC_VER) || defined(_UCRT)
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#endif
    _set_invalid_parameter_handler(on_invalid_parameter);
    _set_purecall_handler(on_purecall);
    crash::set_terminate_handler();
}

void crash_guard_thread() {
    ULONG guarantee = 64 * 1024;
    SetThreadStackGuarantee(&guarantee);
}

std::vector<std::string> crash_main_stack() {
    return {};
}
