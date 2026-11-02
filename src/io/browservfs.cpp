#include "io/browservfs.h"

#ifdef Q_OS_WASM
#include <QBuffer>
#include <QCoreApplication>
#include <QDir>
#include <QHash>
#include <QSet>
#include <QTemporaryFile>
#include <QThread>
#include <QUuid>
#include <emscripten.h>
#include <exception>
#include <type_traits>

namespace {
    const QString storageRoot = QStringLiteral("/tmp/bedrockmap-browser");
    struct Selection {
        QString root;
        io::BrowserVfsBackend::SelectionCallback callback;
    };
    QHash<int, Selection> selections;
    QSet<QUrl> writableFiles;
    int nextSelection = 0;

    template <typename Function>
    auto onBrowserThread(Function&& function) {
        if (QThread::currentThread() == QCoreApplication::instance()->thread()) return function();
        std::invoke_result_t<Function> result{};
        QMetaObject::invokeMethod(QCoreApplication::instance(), [&] { result = function(); }, Qt::BlockingQueuedConnection);
        return result;
    }

    QString nativeFileName(const QUrl& url) {
        // Qt registers unescaped file names in its browser file engine.
        return url.scheme() + QLatin1Char(':') + url.path(QUrl::FullyDecoded);
    }

    // Read one chunk at a time so importing a world does not duplicate it in JS memory.
    EM_JS(void, showBrowserPicker, (int request, const char* destination), {
        const root = UTF8ToString(destination);
        const input = document.createElement("input");
        input.type = "file";
        input.hidden = true;
        input.webkitdirectory = true;
        input.multiple = true;
        const finish = (path, error) => {
            input.remove();
            const pathBytes = lengthBytesUTF8(path) + 1;
            const errorBytes = lengthBytesUTF8(error) + 1;
            const pathPtr = _malloc(pathBytes);
            const errorPtr = _malloc(errorBytes);
            try {
                stringToUTF8(path, pathPtr, pathBytes);
                stringToUTF8(error, errorPtr, errorBytes);
                _bedrockmapBrowserSelection(request, pathPtr, errorPtr);
            } finally {
                _free(pathPtr);
                _free(errorPtr);
            }
        };
        input.addEventListener("cancel", () => finish("", ""), {once: true});
        input.addEventListener("change", async () => {
            if (!input.files.length) { finish("", ""); return; }
            let selectedPath = "";
            try {
                for (const file of input.files) {
                    const relative = file.webkitRelativePath;
                    const parts = relative.split("/");
                    if (parts.some(part => !part || part === "." || part === ".." || part.includes(String.fromCharCode(92)) || part.includes(String.fromCharCode(0))))
                        throw new Error("Invalid relative file path");
                    const selected = parts[0];
                    if (selectedPath && selectedPath !== selected)
                        throw new Error("Files must belong to one directory");
                    selectedPath = selected;
                    const path = root + "/" + relative;
                    FS.mkdirTree(path.substring(0, path.lastIndexOf("/")));
                    const stream = FS.open(path, "w");
                    try {
                        for (let offset = 0; offset < file.size; offset += 1048576) {
                            const bytes = new Uint8Array(await file.slice(offset, offset + 1048576).arrayBuffer());
                            if (FS.write(stream, bytes, 0, bytes.length, offset) !== bytes.length)
                                throw new Error("Incomplete file write");
                        }
                    } finally {
                        FS.close(stream);
                    }
                }
                finish(selectedPath, "");
            } catch (error) {
                finish("", String(error));
            }
        }, {once: true});
        document.body.appendChild(input);
        input.click();
    });
}  // namespace

extern "C" EMSCRIPTEN_KEEPALIVE void bedrockmapBrowserSelection(int request, const char* path, const char* error) {
    const auto selection = selections.take(request);
    if (!selection.callback) return;
    const QString name = QString::fromUtf8(path);
    if (name.isEmpty()) QDir(selection.root).removeRecursively();
    const QUrl url = name.isEmpty() ? QUrl() : QUrl::fromLocalFile(selection.root + QLatin1Char('/') + name);
    const QString detail = QString::fromUtf8(error);
    selection.callback(url, detail.isEmpty() ? QString() : QCoreApplication::translate("BrowserVfsBackend", "Could not read selected files: %1").arg(detail));
}

namespace io {
    void BrowserVfsBackend::selectDirectory(SelectionCallback callback) {
        const int request = ++nextSelection;
        const QString root = storageRoot + QLatin1Char('/') + QUuid::createUuid().toString(QUuid::WithoutBraces);
        selections.insert(request, {root, std::move(callback)});
        showBrowserPicker(request, root.toUtf8().constData());
    }

    void BrowserVfsBackend::registerWritable(const QUrl& url) { writableFiles.insert(url); }
    bool BrowserVfsBackend::requiresSaveAs(const QUrl& url) const { return !writableFiles.contains(url); }
    bool BrowserVfsBackend::supports(const QUrl& url) const { return url.scheme() == QStringLiteral("weblocalfile"); }
    QString BrowserVfsBackend::displayName(const QUrl& url) const { return url.fileName(); }

    ReadResult BrowserVfsBackend::readAll(const QUrl& url, qint64 max_bytes) const {
        // Qt's browser handles and Promise suspension belong to the browser thread.
        return onBrowserThread([&]() -> ReadResult {
            QFile file(nativeFileName(url));
            if (!file.open(QIODevice::ReadOnly)) return {{}, file.errorString()};
            if (max_bytes >= 0 && file.size() > max_bytes) return {{}, QCoreApplication::translate("BrowserVfsBackend", "The file is too large.")};
            const QByteArray data = file.readAll();
            return file.error() == QFileDevice::NoError ? ReadResult{data, {}} : ReadResult{{}, file.errorString()};
        });
    }

    OpenReadResult BrowserVfsBackend::openRead(const QUrl& url) const {
        const auto read = readAll(url);
        if (!read.ok()) return {{}, read.error};
        // Workers such as archive import need a seekable QFile without JS thread affinity.
        auto file = std::make_unique<QTemporaryFile>();
        if (!file->open() || file->write(read.data) != read.data.size() || !file->seek(0)) return {{}, file->errorString()};
        return {std::move(file), {}};
    }

    QString BrowserVfsBackend::readStream(const QUrl& url, const StreamReader& reader) const {
        if (!reader) return QCoreApplication::translate("BrowserVfsBackend", "No file reader was provided.");
        auto opened = openRead(url);
        if (!opened.ok()) return opened.error;
        try {
            return reader(*opened.file);
        } catch (const std::exception& error) {
            return QString::fromUtf8(error.what());
        } catch (...) {
            return QCoreApplication::translate("BrowserVfsBackend", "The file reader failed.");
        }
    }

    QString BrowserVfsBackend::writeStream(const QUrl& url, const StreamWriter& writer, const WriteOptions&) const {
        if (!writer) return QCoreApplication::translate("BrowserVfsBackend", "No file writer was provided.");
        // Run serialization on the caller's thread, then hand bytes to Qt's native file engine.
        QBuffer buffer;
        buffer.open(QIODevice::WriteOnly);
        try {
            if (const QString error = writer(buffer); !error.isEmpty()) return error;
        } catch (const std::exception& error) {
            return QString::fromUtf8(error.what());
        } catch (...) {
            return QCoreApplication::translate("BrowserVfsBackend", "The file writer failed.");
        }
        return onBrowserThread([&]() -> QString {
            QFile file(nativeFileName(url));
            if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) return file.errorString();
            if (file.write(buffer.data()) != buffer.size() || !file.flush()) return file.errorString();
            file.close();
            return file.error() == QFileDevice::NoError ? QString() : file.errorString();
        });
    }
}  // namespace io
#endif
