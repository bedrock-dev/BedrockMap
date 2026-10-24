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
#endif

#include "base/config.h"
#include "model/worldpath.h"

namespace {
    constexpr auto CallbackId = "bedrockmap-log-tab";

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
        const QByteArray message =
            (QStringLiteral("[Qt %1%2] ").arg(category, type == QtFatalMsg ? QStringLiteral(" FATAL") : QString()) + text).toUtf8();
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
}

LogModel::~LogModel() {
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
    if (role == TextRole || role == Qt::DisplayRole) return entry.text;
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
    const QString text = QString::fromUtf8(message.preamble) + QString::fromUtf8(message.indentation) + QString::fromUtf8(message.prefix) +
                         QString::fromUtf8(message.message);
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
    model->pending_.append({text.left(32768), severity});
}

bool LogModel::matches(const Entry& entry) const {
    return entry.severity >= minimum_level_ && (filter_.isEmpty() || entry.text.contains(filter_, Qt::CaseInsensitive));
}

QString LogModel::entryHtml(const Entry& entry) {
    QString text = entry.text.toHtmlEscaped();
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
    emit countChanged();
}

void LogModel::rebuild() {
    beginResetModel();
    visible_.clear();
    for (const auto& entry : entries_)
        if (matches(entry)) visible_.append(entry);
    rebuildDisplayHtml();
    endResetModel();
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

QString LogModel::snapshot() const {
    QStringList lines;
    lines.reserve(visible_.size());
    for (const auto& entry : visible_) lines.append(entry.text);
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
    if ((!destination.isLocalFile() && destination.scheme() != QStringLiteral("content")) || destination.isEmpty()) {
        error_ = tr("The selected log destination is not supported.");
    } else if (destination.isLocalFile() && !log_file_.isEmpty() &&
               world_path::identityKey(world_path::normalize(destination.toLocalFile())) ==
                   world_path::identityKey(world_path::normalize(log_file_))) {
        error_ = tr("Choose a different file from the active log file.");
    } else {
        // Android's QFile engine supports authorized content:// file URLs.
        QFile file(destination.isLocalFile() ? destination.toLocalFile() : destination.toString());
        loguru::flush();
        QFile source(log_file_);
        if (!log_file_.isEmpty() && !source.open(QIODevice::ReadOnly)) {
            error_ = tr("Could not save logs: %1").arg(source.errorString());
        } else if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            error_ = tr("Could not save logs: %1").arg(file.errorString());
        } else if (source.isOpen()) {
            // Freeze the byte count so concurrent logging cannot extend export
            // indefinitely. The full file includes entries evicted from the UI.
            qint64 remaining = source.size();
            while (remaining > 0) {
                const QByteArray bytes = source.read(std::min<qint64>(remaining, 256 * 1024));
                if (bytes.isEmpty()) {
                    error_ = tr("Could not save logs: %1").arg(source.errorString());
                    break;
                }
                if (file.write(bytes) != bytes.size()) {
                    error_ = tr("Could not save logs: %1").arg(file.errorString());
                    break;
                }
                remaining -= bytes.size();
            }
        } else {
            const QByteArray bytes = (snapshot() + QLatin1Char('\n')).toUtf8();
            if (file.write(bytes) != bytes.size()) error_ = tr("Could not save logs: %1").arg(file.errorString());
        }
        if (file.isOpen() && !file.flush() && error_.isEmpty()) error_ = tr("Could not save logs: %1").arg(file.errorString());
    }
    emit errorChanged();
    return error_.isEmpty();
}
