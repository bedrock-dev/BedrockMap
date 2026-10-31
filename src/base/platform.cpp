#include "base/platform.h"

#include <QDir>
#include <QFileInfo>
#include <QUrl>

#if defined(Q_OS_ANDROID)
#include <unistd.h>

#include <QCoreApplication>
#include <QFile>
#include <QJniEnvironment>
#include <QJniObject>
#include <QScopeGuard>
#endif

#if defined(_WIN32)
// clang-format off
#include <Windows.h>
#include <Psapi.h>
// clang-format on
#endif

#if defined(_WIN32) && defined(BEDROCKMAP_ENABLE_CRASH_HANDLER)
// clang-format off
#include <backtrace.h>
#include <signal.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
// clang-format on

#include <string>

#include "loguru/loguru.hpp"
#endif

namespace platform {
    namespace {
#if defined(_WIN32) && defined(BEDROCKMAP_ENABLE_CRASH_HANDLER)
        // Hardware faults (bad pointer deref, divide by zero, ...) do NOT become
        // C signals on Windows; they surface as SEH exceptions. Install an
        // unhandled-exception filter and unwind the x64 stack by walking the RBP
        // chain captured in the exception CONTEXT, symbolizing each PC with
        // libbacktrace (reads the DWARF straight from the exe, no PDB needed).
        // libbacktrace state is created once at startup and only queried afterwards.
        struct backtrace_state* g_state = nullptr;
        bool g_installed = false;

        // The report is accumulated in memory and handed to loguru in one go:
        // loguru dispatches it to stderr and every registered file sink. Doing
        // the formatting once keeps lock/alloc churn out of the fault handler.
        struct Sink {
            std::string text;
        };

        void sink_printf(Sink* s, const char* fmt, ...) {
            char buf[2048];
            va_list args;
            va_start(args, fmt);
            vsnprintf(buf, sizeof buf, fmt, args);
            va_end(args);
            s->text += buf;
        }

        void backtrace_error_cb(void* data, const char* msg, int errnum) {
            auto* sink = static_cast<Sink*>(data);
            sink_printf(sink, "  [backtrace] %s (errno %d)\n", msg, errnum);
        }

        int frame_cb(void* data, uintptr_t pc, const char* filename, int lineno, const char* function) {
            auto* sink = static_cast<Sink*>(data);
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

        void symbol_pc(Sink* sink, uintptr_t pc) { backtrace_pcinfo(g_state, pc, frame_cb, backtrace_error_cb, sink); }

        // x64 unwinding uses the per-function .pdata/.xdata unwind info via
        // RtlLookupFunctionEntry/RtlVirtualUnwind (the same metadata the OS uses for
        // SEH), not a hand-rolled RBP chain. MinGW GCC builds many frames with RBP as
        // a plain base register for locals (push regs; lea rbp,[rsp+N]), so walking
        // [RBP] -> caller RBP reads garbage and stops after the first frame, while the
        // unwind codes in .pdata describe the real frame layout for every function.
        void unwind_from_context(Sink* sink, PCONTEXT ctx, int skip_frames) {
            for (int frame = 0; frame < 64 && ctx->Rip != 0; ++frame) {
                if (frame >= skip_frames) symbol_pc(sink, static_cast<uintptr_t>(ctx->Rip));
                DWORD64 image_base = 0;
                auto* entry = RtlLookupFunctionEntry(ctx->Rip, &image_base, nullptr);
                if (entry == nullptr) break;  // leaf frame, no unwind metadata
                PVOID handler_data = nullptr;
                DWORD64 establisher_frame = 0;
                // Handler type 0 = UNW_FLAG_NHANDLER: never run registered handlers.
                RtlVirtualUnwind(0, image_base, ctx->Rip, entry, ctx, &handler_data, &establisher_frame, nullptr);
            }
        }

        const char* exception_name(DWORD code) {
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

        LONG WINAPI exception_filter(EXCEPTION_POINTERS* ep) {
            // Prefer the OS default handling so a debugger still breaks; this filter
            // only runs when nothing else handled the fault anyway.
            Sink sink;
            const auto code = ep->ExceptionRecord->ExceptionCode;
            sink_printf(&sink, "\n===== %s (0x%08lx) caught =====\n", exception_name(code), static_cast<unsigned long>(code));
#if defined(_WIN64)
            if (auto* access = ep->ExceptionRecord->ExceptionInformation;
                access && ep->ExceptionRecord->NumberParameters >= 2 && code == EXCEPTION_ACCESS_VIOLATION) {
                const char* kind = access[0] == 0 ? "read" : (access[0] == 1 ? "write" : "exec");
                sink_printf(&sink, "  %s of address 0x%llx\n", kind, static_cast<unsigned long long>(access[1]));
            }
            sink_printf(&sink, "Stack trace:\n");
            if (g_state) {
                // Start from the faulting instruction's register context so the crashing
                // function is printed first.
                CONTEXT ctx = *ep->ContextRecord;
                unwind_from_context(&sink, &ctx, 0);
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
                // Synchronous call from the aborting code: snapshot the live stack and
                // walk it with the same metadata-driven unwinder as the SEH path.
                // libbacktrace cannot walk a MinGW PE stack on its own.
                CONTEXT ctx;
                RtlCaptureContext(&ctx);
                unwind_from_context(&sink, &ctx, 1);  // skip this handler's own frame
            } else {
                sink_printf(&sink, "  (backtrace state not initialized)\n");
            }
            sink_printf(&sink, "===== end =====\n");

            // Same routing as the SEH handler: into the normal run log + stderr.
            LOG_F(ERROR, "%s", sink.text.c_str());

            raise(sig);
        }
#endif
    }  // namespace

    bool isContentUri(const QUrl& url) { return url.scheme() == QStringLiteral("content"); }

    QString openDocumentFile(QFile& file, QIODevice::OpenMode mode) {
#ifdef Q_OS_ANDROID
        const QUrl url(file.fileName());
        if (isContentUri(url)) {
            const QJniObject context(QNativeInterface::QAndroidApplication::context());
            const auto resolver = context.callObjectMethod("getContentResolver", "()Landroid/content/ContentResolver;");
            const auto source = QJniObject::fromString(url.toString(QUrl::FullyEncoded));
            const auto uri = QJniObject::callStaticObjectMethod("org/bedrockmap/DocumentMetadata", "grantedUri",
                                                                "(Landroid/content/Context;Ljava/lang/String;)Landroid/net/Uri;",
                                                                context.object(), source.object<jstring>());
            QString access = QStringLiteral("r");
            if (mode & QIODevice::WriteOnly) {
                access = mode & QIODevice::ReadOnly ? QStringLiteral("rw") : QStringLiteral("w");
                if (mode & QIODevice::Append)
                    access += QLatin1Char('a');
                else if (mode & QIODevice::Truncate)
                    access += QLatin1Char('t');
            }
            const auto accessString = QJniObject::fromString(access);
            QJniEnvironment env;
            const auto exceptionMessage = [&env]() -> QString {
                if (!env->ExceptionCheck()) return {};
                const auto exception = env->ExceptionOccurred();
                env->ExceptionClear();
                return QJniObject::fromLocalRef(exception).callObjectMethod<jstring>("toString").toString();
            };
            // Qt 6.11 rebuilds document IDs before checking permissions, which
            // changes e.g. literal parentheses to %28/%29 and loses the grant.
            // ContentResolver must receive the original provider URI instead.
            const auto openMethod = env->GetMethodID(resolver.objectClass(), "openFileDescriptor",
                                                     "(Landroid/net/Uri;Ljava/lang/String;)Landroid/os/ParcelFileDescriptor;");
            if (const QString error = exceptionMessage(); !error.isEmpty()) return error;
            const auto descriptorObject = env->CallObjectMethod(resolver.object(), openMethod, uri.object(), accessString.object());
            if (const QString error = exceptionMessage(); !error.isEmpty()) return error;
            const auto descriptor = QJniObject::fromLocalRef(descriptorObject);
            const auto missingDescriptor = [] {
                return QCoreApplication::translate("platform", "The document provider returned no file descriptor.");
            };
            if (!descriptor.isValid()) return missingDescriptor();
            const auto closeDescriptor = qScopeGuard([&descriptor] { descriptor.callMethod<void>("close"); });
            const auto detachMethod = env->GetMethodID(descriptor.objectClass(), "detachFd", "()I");
            if (const QString error = exceptionMessage(); !error.isEmpty()) return error;
            const int fd = env->CallIntMethod(descriptor.object(), detachMethod);
            if (const QString error = exceptionMessage(); !error.isEmpty()) return error;
            if (fd < 0) return missingDescriptor();
            if (file.open(fd, mode, QFileDevice::AutoCloseHandle)) return {};
            const QString error = file.errorString();
            ::close(fd);
            return error;
        }
#endif
        return file.open(mode) ? QString() : file.errorString();
    }

    QString normalizeLocalPath(const QString& input) {
        if (input.isEmpty()) return {};
        QString path = input;
#ifdef Q_OS_WIN
        const bool drivePath = input.size() >= 2 && input.at(0).isLetter() && input.at(1) == QLatin1Char(':');
#else
        const bool drivePath = false;
#endif
        if (!drivePath) {
            const QUrl url(input);
            if (!url.scheme().isEmpty()) {
                if (!url.isValid() || !url.isLocalFile() || url.hasQuery() || url.hasFragment()) return {};
                path = url.toLocalFile();
            }
        }
        if (path.isEmpty()) return {};
        const QFileInfo info(QDir::fromNativeSeparators(path));
        const QString canonical = info.canonicalFilePath();
        return QDir::cleanPath(canonical.isEmpty() ? info.absoluteFilePath() : canonical);
    }

    QString fileDisplayName(const QString& input) {
        if (input.isEmpty()) return {};
        const QUrl url(input);
        const QString path = url.isLocalFile() ? url.toLocalFile() : input;
#ifdef Q_OS_ANDROID
        if (isContentUri(url)) {
            const auto context = QNativeInterface::QAndroidApplication::context();
            const auto source = QJniObject::fromString(url.toString(QUrl::FullyEncoded));
            const auto result = QJniObject::callStaticObjectMethod("org/bedrockmap/DocumentMetadata", "displayName",
                                                                   "(Landroid/content/Context;Ljava/lang/String;)Ljava/lang/String;",
                                                                   context.object(), source.object<jstring>());
            QJniEnvironment env;
            if (!env.checkAndClearExceptions() && result.isValid()) {
                const QString name = result.toString();
                if (!name.isEmpty()) return name;
            }
        }
#endif
        const QString name = QFileInfo(path).fileName();
        if (!name.isEmpty()) return name;
        if (isContentUri(url)) {
            const QString uriName = QFileInfo(url.fileName()).fileName();
            if (!uriName.isEmpty()) return uriName;
        }
        return input;
    }

    QString resolveContentDirectory(const QUrl& url) {
#ifdef Q_OS_ANDROID
        if (!isContentUri(url) || !hasAllFilesPermission()) return {};
        const auto context = QNativeInterface::QAndroidApplication::context();
        const auto source = QJniObject::fromString(url.toString(QUrl::FullyEncoded));
        const auto result = QJniObject::callStaticObjectMethod("org/bedrockmap/SafTreeIo", "resolveTreePath",
                                                               "(Landroid/content/Context;Ljava/lang/String;)Ljava/lang/String;",
                                                               context.object(), source.object<jstring>());
        QJniEnvironment env;
        if (env.checkAndClearExceptions() || !result.isValid()) return {};
        return result.toString();
#else
        Q_UNUSED(url);
        return {};
#endif
    }

    void installCrashHandler() {
#if defined(_WIN32) && defined(BEDROCKMAP_ENABLE_CRASH_HANDLER)
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
                g_state, reinterpret_cast<uintptr_t>(&installCrashHandler),
                [](void*, uintptr_t, const char*, int, const char*) { return 0; }, [](void*, const char*, int) {}, nullptr);
        }

        // Catches hardware faults (access violation, divide by zero, ...).
        SetUnhandledExceptionFilter(exception_filter);
        // Catches CRT-originated aborts (assert failures, loguru FATAL, ...).
        signal(SIGABRT, on_abort);
        signal(SIGFPE, on_abort);
#endif
    }

    double memoryUsageMiB() {
#if defined(_WIN32)
        PROCESS_MEMORY_COUNTERS_EX pmc{};
        if (!GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pmc), sizeof(pmc))) return 0.0;
        return static_cast<double>(pmc.WorkingSetSize) / (1024.0 * 1024.0);
#elif defined(Q_OS_ANDROID)
        QFile statm(QStringLiteral("/proc/self/statm"));
        if (!statm.open(QIODevice::ReadOnly)) return 0.0;
        const auto fields = statm.readAll().simplified().split(' ');
        if (fields.size() < 2) return 0.0;
        bool ok = false;
        const auto residentPages = fields[1].toULongLong(&ok);
        const long pageSize = sysconf(_SC_PAGESIZE);
        if (!ok || pageSize <= 0) return 0.0;
        return static_cast<double>(residentPages) * static_cast<double>(pageSize) / (1024.0 * 1024.0);
#else
        return 0.0;
#endif
    }

    bool hasAllFilesPermission() {
#if defined(Q_OS_ANDROID)
        const auto context = QNativeInterface::QAndroidApplication::context();
        if (!context.isValid()) return false;
        return QJniObject::callStaticMethod<jboolean>("org/bedrockmap/StoragePermission", "hasAccess", "(Landroid/content/Context;)Z",
                                                      context.object());
#else
        return true;
#endif
    }

    void requestAllFilesPermission() {
#if defined(Q_OS_ANDROID)
        const auto context = QNativeInterface::QAndroidApplication::context();
        if (!context.isValid()) return;
        QJniObject::callStaticMethod<void>("org/bedrockmap/StoragePermission", "request", "(Landroid/content/Context;)V", context.object());
#endif
    }
    QString shizukuPermission() {
#if defined(Q_OS_ANDROID)
        const auto status = QJniObject::callStaticObjectMethod("org/bedrockmap/ShizukuPermission", "status", "()Ljava/lang/String;");
        QJniEnvironment env;
        if (!env.checkAndClearExceptions() && status.isValid()) return status.toString();
#endif
        return QStringLiteral("Unavailable");
    }

    void requestShizukuPermission() {
#if defined(Q_OS_ANDROID)
        QJniObject::callStaticMethod<void>("org/bedrockmap/ShizukuPermission", "request", "()V");
        QJniEnvironment env;
        env.checkAndClearExceptions();
#endif
    }

    QString rootPermission() {
#if defined(Q_OS_ANDROID)
        const auto status = QJniObject::callStaticObjectMethod("org/bedrockmap/RootPermission", "status", "()Ljava/lang/String;");
        QJniEnvironment env;
        if (!env.checkAndClearExceptions() && status.isValid()) return status.toString();
#endif
        return QStringLiteral("Unavailable");
    }

    void requestRootPermission() {
#if defined(Q_OS_ANDROID)
        const auto context = QNativeInterface::QAndroidApplication::context();
        if (!context.isValid()) return;
        QJniObject::callStaticMethod<void>("org/bedrockmap/RootPermission", "request", "(Landroid/content/Context;)V", context.object());
        QJniEnvironment env;
        env.checkAndClearExceptions();
#endif
    }

}  // namespace platform
