#include "crashhandler.h"

#include <backtrace.h>
#include <signal.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <windows.h>

#include <string>

#include "loguru/loguru.hpp"

namespace crashhandler {
    namespace {

        // Hardware faults (bad pointer deref, divide by zero, ...) do NOT become
        // C signals on Windows — they surface as SEH exceptions. So we install an
        // unhandled-exception filter and unwind the x64 stack by walking the RBP
        // chain captured in the exception CONTEXT, symbolizing each PC with
        // libbacktrace (reads the DWARF straight out of the exe, no PDB needed).
        // libbacktrace state is created once at startup and only queried afterwards.
        struct backtrace_state *g_state = nullptr;
        bool g_installed = false;

        // The report is accumulated in memory and handed to loguru in one go:
        // loguru dispatches it to stderr and every registered file sink. Doing
        // the formatting once keeps lock/alloc churn out of the fault handler.
        struct Sink {
            std::string text;
        };

        void sink_printf(Sink *s, const char *fmt, ...) {
            char buf[2048];
            va_list args;
            va_start(args, fmt);
            vsnprintf(buf, sizeof buf, fmt, args);
            va_end(args);
            s->text += buf;
        }

        void backtrace_error_cb(void *data, const char *msg, int errnum) {
            auto *sink = static_cast<Sink *>(data);
            sink_printf(sink, "  [backtrace] %s (errno %d)\n", msg, errnum);
        }

        int frame_cb(void *data, uintptr_t pc, const char *filename, int lineno, const char *function) {
            auto *sink = static_cast<Sink *>(data);
            if (function) {
                sink_printf(sink, "  %s\n      at %s:%d (0x%llx)\n", function, filename ? filename : "?", lineno,
                            static_cast<unsigned long long>(pc));
            } else if (filename) {
                sink_printf(sink, "  %s:%d (0x%llx)\n", filename, lineno, static_cast<unsigned long long>(pc));
            } else {
                sink_printf(sink, "  0x%llx\n", static_cast<unsigned long long>(pc));
            }
            return 0;  // keep walking
        }

        void symbol_pc(Sink *sink, uintptr_t pc) { backtrace_pcinfo(g_state, pc, frame_cb, backtrace_error_cb, sink); }

        bool is_readable(const void *p) {
            MEMORY_BASIC_INFORMATION mi;
            if (VirtualQuery(p, &mi, sizeof mi) == 0) return false;
            return mi.State == MEM_COMMIT && (mi.Protect & (PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READ |
                                                            PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)) != 0;
        }

        // x64: each frame stores its return address at [RBP+8] and the caller's RBP
        // at [RBP]. Walk that chain; stop early if a link points outside readable
        // memory (tail-call/leaf frames may be skipped, that is fine for diagnostics).
        void unwind_from_context(Sink *sink, DWORD64 rip, DWORD64 rbp) {
            for (int frame = 0; frame < 64 && rip != 0; ++frame) {
                symbol_pc(sink, static_cast<uintptr_t>(rip));
                if (rbp == 0) break;
                auto *ret = reinterpret_cast<const DWORD64 *>(rbp + 8);
                auto *prev = reinterpret_cast<const DWORD64 *>(rbp);
                if (!is_readable(ret) || !is_readable(prev)) break;
                rip = *ret;
                rbp = *prev;
            }
        }

        const char *exception_name(DWORD code) {
            switch (code) {
                case EXCEPTION_ACCESS_VIOLATION:
                    return "ACCESS_VIOLATION";
                case EXCEPTION_INT_DIVIDE_BY_ZERO:
                    return "INT_DIVIDE_BY_ZERO";
                case EXCEPTION_STACK_OVERFLOW:
                    return "STACK_OVERFLOW";
                case EXCEPTION_ILLEGAL_INSTRUCTION:
                    return "ILLEGAL_INSTRUCTION";
                case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:
                    return "ARRAY_BOUNDS_EXCEEDED";
                case EXCEPTION_DATATYPE_MISALIGNMENT:
                    return "DATATYPE_MISALIGNMENT";
                case EXCEPTION_FLT_DIVIDE_BY_ZERO:
                    return "FLT_DIVIDE_BY_ZERO";
                default:
                    return "UNKNOWN_EXCEPTION";
            }
        }

        LONG WINAPI exception_filter(EXCEPTION_POINTERS *ep) {
            // Prefer the OS default handling so a debugger still breaks; this filter
            // only runs when nothing else handled the fault anyway.
            Sink sink;
            const auto code = ep->ExceptionRecord->ExceptionCode;
            sink_printf(&sink, "\n===== %s (0x%08lx) caught =====\n", exception_name(code), static_cast<unsigned long>(code));
#if defined(_WIN64)
            if (auto *access = ep->ExceptionRecord->ExceptionInformation;
                access && ep->ExceptionRecord->NumberParameters >= 2 && code == EXCEPTION_ACCESS_VIOLATION) {
                const char *kind = access[0] == 0 ? "read" : (access[0] == 1 ? "write" : "exec");
                sink_printf(&sink, "  %s of address 0x%llx\n", kind, static_cast<unsigned long long>(access[1]));
            }
            sink_printf(&sink, "Stack trace:\n");
            if (g_state) {
                // ep->ContextRecord->Rip is inside the faulting instruction; start the
                // frame chain from there so the crashing function appears first.
                unwind_from_context(&sink, ep->ContextRecord->Rip, ep->ContextRecord->Rbp);
            } else {
                sink_printf(&sink, "  (backtrace state not initialized)\n");
            }
#else
            sink_printf(&sink, "Stack trace:\n  (x86 unwind not supported)\n");
#endif
            sink_printf(&sink, "===== end =====\n");

            // Route through loguru: the report lands in the normal run log
            // (with the pre-crash context above it) and on stderr.
            LOG_F(ERROR, "%s", sink.text.c_str());

            // Let the system terminate the process with the normal crash semantics.
            return EXCEPTION_EXECUTE_HANDLER;
        }

        // CRT-originated fatal signals (abort/assert) still arrive as C signals.
        void on_abort(int sig) {
            signal(sig, SIG_DFL);

            Sink sink;
            sink_printf(&sink, "\n===== signal %d caught =====\n", sig);
            sink_printf(&sink, "Stack trace:\n");
            if (g_state) {
                // Synchronous call from the aborting code: walk the live stack.
                backtrace_full(g_state, 2, frame_cb, backtrace_error_cb, &sink);
            } else {
                sink_printf(&sink, "  (backtrace state not initialized)\n");
            }
            sink_printf(&sink, "===== end =====\n");

            // Same routing as the SEH handler: into the normal run log + stderr.
            LOG_F(ERROR, "%s", sink.text.c_str());

            raise(sig);
        }

    }  // namespace

    void install() {
        if (g_installed) return;
        g_installed = true;

        char exe[MAX_PATH];
        DWORD len = GetModuleFileNameA(nullptr, exe, MAX_PATH);
        if (len == 0 || len >= MAX_PATH) {
            strcpy_s(exe, "BedrockMap");
        }
        // Single-threaded access pattern (created here, read from crash handler).
        g_state = backtrace_create_state(exe, /*threaded=*/0, backtrace_error_cb, nullptr);

        // libbacktrace reads + parses the exe's DWARF lazily on the first lookup.
        // Trigger it now (normal context): the crash handler must not attempt a
        // multi-hundred-MB file read from inside the fault handler, where a
        // transient lock or slow read can make symbolization fail.
        if (g_state) {
            backtrace_pcinfo(
                g_state, reinterpret_cast<uintptr_t>(&install), [](void *, uintptr_t, const char *, int, const char *) { return 0; },
                [](void *, const char *, int) {}, nullptr);
        }

        // Catches hardware faults (access violation, divide by zero, ...).
        SetUnhandledExceptionFilter(exception_filter);
        // Catches CRT-originated aborts (assert failures, loguru FATAL, ...).
        signal(SIGABRT, on_abort);
        signal(SIGFPE, on_abort);
    }

}  // namespace crashhandler
