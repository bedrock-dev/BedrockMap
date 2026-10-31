#include "model/logmodel.h"

#include <QClipboard>
#include <QCoreApplication>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QMutexLocker>
#include <QStringList>
#include <algorithm>
#ifdef Q_OS_ANDROID
#include <android/log.h>

#include <QJniEnvironment>
#include <QJniObject>
#endif

#include "base/apppaths.h"
#include "base/platform.h"
#include "io/documentio.h"

namespace {
    constexpr auto CallbackId = "bedrockmap-log-tab";

#ifdef Q_OS_ANDROID
    void onAndroidLogMessage(JNIEnv*, jclass, jint priority, jstring tag, jstring message) {
        const auto verbosity = priority >= ANDROID_LOG_ERROR  ? loguru::Verbosity_ERROR
                               : priority >= ANDROID_LOG_WARN ? loguru::Verbosity_WARNING
                               : priority >= ANDROID_LOG_INFO ? loguru::Verbosity_INFO
                                                              : loguru::Verbosity{1};
        const auto nativeTag = QJniObject(tag).toString().toUtf8();
        const auto nativeMessage = QJniObject(message).toString().toUtf8();
        loguru::log(verbosity, "Android", 0, "[%s] %s", nativeTag.constData(), nativeMessage.constData());
    }
#endif

    void qtMessage(QtMsgType type, const QMessageLogContext& context, const QString& text) {
        loguru::Verbosity verbosity = loguru::Verbosity_INFO;
        switch (type) {
            case QtDebugMsg:
                verbosity = 1;
                break;
            case QtWarningMsg:
                verbosity = loguru::Verbosity_WARNING;
                break;
            case QtCriticalMsg:
                verbosity = loguru::Verbosity_ERROR;
                break;
            // Qt performs the abort after its handler returns.
            case QtFatalMsg:
                verbosity = loguru::Verbosity_ERROR;
                break;
            case QtInfoMsg:
                break;
        }
        const QString category = QString::fromUtf8(context.category ? context.category : "default");
        const QString prefix = category == QLatin1String("default") && type != QtFatalMsg
                                   ? QString()
                                   : QStringLiteral("[Qt %1%2] ").arg(category, type == QtFatalMsg ? QStringLiteral(" FATAL") : QString());
        const QByteArray message = (prefix + text).toUtf8();
        loguru::log(verbosity, context.file ? context.file : "Qt", std::max(context.line, 0), "%s", message.constData());
    }
}  // namespace

LogModel::LogModel(int& argc, char** argv, QObject* parent) : QAbstractListModel(parent) {
    loguru::g_flush_interval_ms = 250;
    loguru::add_callback(CallbackId, &LogModel::receive, this, loguru::Verbosity_MAX);
    previous_handler_ = qInstallMessageHandler(qtMessage);
    const QString file = app_paths::logDirectory() + QLatin1Char('/') +
                         QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss-zzz")) +
                         QStringLiteral("-%1.log").arg(QCoreApplication::applicationPid());
    loguru::init(argc, argv);
    const QByteArray nativeFile = QFile::encodeName(file);
    if (loguru::add_file(nativeFile.constData(), loguru::Truncate, loguru::Verbosity_MAX)) {
        log_file_ = file;
        LOG_F(INFO, "Log file: %s", nativeFile.constData());
    } else {
        LOG_F(WARNING, "Cannot create the application log file.");
    }
    connect(&timer_, &QTimer::timeout, this, &LogModel::drain);
    timer_.start(100);
#ifdef Q_OS_ANDROID
    QJniEnvironment env;
    if (env.registerNativeMethods("io/github/hhhxiao/bedrockmap/AppLog", {{"onMessage", "(ILjava/lang/String;Ljava/lang/String;)V",
                                                             reinterpret_cast<void*>(onAndroidLogMessage)}})) {
        QJniObject::callStaticMethod<void>("io/github/hhhxiao/bedrockmap/AppLog", "setNativeEnabled", "(Z)V", jboolean(true));
    }
    env.checkAndClearExceptions();
#endif
}

LogModel::~LogModel() {
#ifdef Q_OS_ANDROID
    QJniObject::callStaticMethod<void>("io/github/hhhxiao/bedrockmap/AppLog", "setNativeEnabled", "(Z)V", jboolean(false));
    QJniEnvironment env;
    env.checkAndClearExceptions();
#endif
    timer_.stop();
    qInstallMessageHandler(previous_handler_);
    loguru::remove_callback(CallbackId);
    if (!log_file_.isEmpty()) {
        const QByteArray path = QFile::encodeName(log_file_);
        loguru::remove_callback(path.constData());
    }
}

int LogModel::rowCount(const QModelIndex& parent) const { return parent.isValid() ? 0 : visible_.size(); }

QVariant LogModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= visible_.size()) return {};
    const auto& entry = visible_.at(index.row());
    if (role == TextRole || role == Qt::DisplayRole) return displayText(entry);
    if (role == SeverityRole) return entry.severity;
    return {};
}

QHash<int, QByteArray> LogModel::roleNames() const { return {{TextRole, "logText"}, {SeverityRole, "severity"}}; }

void LogModel::receive(void* userData, const loguru::Message& message) {
    auto* model = static_cast<LogModel*>(userData);
    const int severity = message.verbosity <= loguru::Verbosity_ERROR     ? 3
                         : message.verbosity == loguru::Verbosity_WARNING ? 2
                         : message.verbosity == loguru::Verbosity_INFO    ? 1
                                                                          : 0;
    const QString prefix = QString::fromUtf8(message.preamble);
    const QString text =
        prefix + QString::fromUtf8(message.indentation) + QString::fromUtf8(message.prefix) + QString::fromUtf8(message.message);
#ifdef Q_OS_ANDROID
    const int priority = severity >= 3   ? ANDROID_LOG_ERROR
                         : severity == 2 ? ANDROID_LOG_WARN
                         : severity == 1 ? ANDROID_LOG_INFO
                                         : ANDROID_LOG_DEBUG;
    __android_log_write(priority, "BedrockMap", text.toUtf8().constData());
#endif
    // Callbacks run on logging threads. Batch delivery so worker bursts never
    // enqueue one GUI event per message or touch the model from those threads.
    QMutexLocker lock(&model->pending_mutex_);
    if (model->pending_.size() == MaxEntries) {
        model->pending_.removeFirst();
        ++model->pending_omitted_;
    }
    model->pending_.append({text.left(32768), prefix.size(), severity});
}

bool LogModel::matches(const Entry& entry) const {
    return entry.severity >= minimum_level_ && (filter_.isEmpty() || entry.text.contains(filter_, Qt::CaseInsensitive));
}

QString LogModel::displayText(const Entry& entry) const { return show_prefix_ ? entry.text : entry.text.mid(entry.prefixLength); }

QString LogModel::entryHtml(const Entry& entry) const {
    QString text = displayText(entry).toHtmlEscaped();
    text.replace(QLatin1String("\r\n"), QLatin1String("<br/>"));
    text.replace(QLatin1Char('\r'), QLatin1String("<br/>"));
    text.replace(QLatin1Char('\n'), QLatin1String("<br/>"));
    if (entry.severity >= 3)
        return QStringLiteral("<div style=\"white-space:pre-wrap;\"><span style=\"color:#ed7785;\">%1</span></div>").arg(text);
    if (entry.severity == 2)
        return QStringLiteral("<div style=\"white-space:pre-wrap;\"><span style=\"color:#e6b455;\">%1</span></div>").arg(text);
    return QStringLiteral("<div style=\"white-space:pre-wrap;\">%1</div>").arg(text);
}

void LogModel::rebuildDisplayHtml() {
    display_html_.clear();
    for (const auto& entry : visible_) display_html_ += entryHtml(entry);
}

void LogModel::drain() {
    QList<Entry> batch;
    qulonglong omitted;
    {
        QMutexLocker lock(&pending_mutex_);
        batch.swap(pending_);
        omitted = pending_omitted_;
        pending_omitted_ = 0;
    }
    if (batch.isEmpty() && omitted == 0) return;
    omitted_count_ += omitted;
    const int overflow = std::max(0, int(entries_.size() + batch.size()) - MaxEntries);
    if (overflow > 0) {
        int removedVisible = 0;
        for (int i = 0; i < overflow; ++i)
            if (matches(entries_.at(i))) ++removedVisible;
        if (removedVisible > 0) {
            beginRemoveRows({}, 0, removedVisible - 1);
            visible_.remove(0, removedVisible);
            endRemoveRows();
        }
        entries_.remove(0, overflow);
        omitted_count_ += overflow;
    }
    entries_.append(batch);
    QList<Entry> additions;
    for (const auto& entry : batch)
        if (matches(entry)) additions.append(entry);
    if (!additions.isEmpty()) {
        const int first = visible_.size();
        beginInsertRows({}, first, first + additions.size() - 1);
        visible_.append(additions);
        endInsertRows();
    }
    if (overflow > 0)
        rebuildDisplayHtml();
    else
        for (const auto& entry : additions) display_html_ += entryHtml(entry);
    emit displayChanged();
    emit countChanged();
}

void LogModel::rebuild() {
    beginResetModel();
    visible_.clear();
    for (const auto& entry : entries_)
        if (matches(entry)) visible_.append(entry);
    rebuildDisplayHtml();
    endResetModel();
    emit displayChanged();
}

void LogModel::setFilter(const QString& text) {
    if (filter_ == text) return;
    filter_ = text;
    rebuild();
    emit filterChanged();
    emit countChanged();
}

void LogModel::setMinimumLevel(int level) {
    level = std::clamp(level, 0, 3);
    if (minimum_level_ == level) return;
    minimum_level_ = level;
    rebuild();
    emit filterChanged();
    emit countChanged();
}

void LogModel::setShowPrefix(bool show) {
    if (show_prefix_ == show) return;
    show_prefix_ = show;
    rebuildDisplayHtml();
    if (!visible_.isEmpty()) emit dataChanged(index(0), index(visible_.size() - 1), {TextRole, Qt::DisplayRole});
    emit showPrefixChanged();
    emit displayChanged();
}

void LogModel::clear() {
    {
        QMutexLocker lock(&pending_mutex_);
        pending_.clear();
        pending_omitted_ = 0;
    }
    entries_.clear();
    omitted_count_ = 0;
    rebuild();
    emit countChanged();
}

QString LogModel::snapshot(bool complete) const {
    QStringList lines;
    const auto& entries = complete ? entries_ : visible_;
    lines.reserve(entries.size());
    for (const auto& entry : entries) lines.append(complete ? entry.text : displayText(entry));
    return lines.join(QLatin1Char('\n'));
}

void LogModel::copyText(const QString& text) { QGuiApplication::clipboard()->setText(text); }
void LogModel::copyAll() {
    drain();
    copyText(snapshot());
}

bool LogModel::save(const QUrl& destination) {
    drain();
    error_.clear();
    if ((!destination.isLocalFile() && !platform::isContentUri(destination)) || destination.isEmpty()) {
        error_ = tr("The selected log destination is not supported.");
    } else if (destination.isLocalFile() && !log_file_.isEmpty() &&
               platform::normalizeLocalPath(destination.toLocalFile()) == platform::normalizeLocalPath(log_file_)) {
        error_ = tr("Choose a different file from the active log file.");
    } else {
        loguru::flush();
        QFile source(log_file_);
        const QString writeError = io::DocumentIo::writeStream(destination, [this, &source](QIODevice& file) {
            if (!log_file_.isEmpty() && !source.open(QIODevice::ReadOnly)) return source.errorString();
            if (!source.isOpen()) {
                const QByteArray bytes = (snapshot(true) + QLatin1Char('\n')).toUtf8();
                return file.write(bytes) == bytes.size() ? QString() : file.errorString();
            }
            // Freeze the byte count so concurrent logging cannot extend export
            // indefinitely. The full file includes entries evicted from the UI.
            qint64 remaining = source.size();
            while (remaining > 0) {
                const QByteArray bytes = source.read(std::min<qint64>(remaining, 256 * 1024));
                if (bytes.isEmpty()) return source.errorString();
                if (file.write(bytes) != bytes.size()) return file.errorString();
                remaining -= bytes.size();
            }
            return QString();
        });
        if (!writeError.isEmpty()) error_ = tr("Could not save logs: %1").arg(writeError);
    }
    emit errorChanged();
    return error_.isEmpty();
}
