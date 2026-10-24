#include "model/notifications.h"

#include <QDateTime>
#include <algorithm>
#include <utility>

namespace {
    /// Housekeeping cadence: fine enough that a card leaves close to its
    /// deadline, coarse enough to stay off the CPU.
    constexpr int kExpiryCheckMs = 200;
    constexpr int kMaxEntriesLimit = 64;
}  // namespace

NotificationCenter::NotificationCenter(QObject* parent) : QAbstractListModel(parent) {
    expiry_timer_.setInterval(kExpiryCheckMs);
    connect(&expiry_timer_, &QTimer::timeout, this, &NotificationCenter::expire);
}

qint64 NotificationCenter::nowMs() { return QDateTime::currentMSecsSinceEpoch(); }

QString NotificationCenter::levelName(NotificationLevel level) {
    switch (level) {
        case NotificationLevel::Warning:
            return QStringLiteral("warning");
        case NotificationLevel::Error:
            return QStringLiteral("error");
        case NotificationLevel::Info:
            break;
    }
    return QStringLiteral("info");
}

int NotificationCenter::rowCount(const QModelIndex& parent) const { return parent.isValid() ? 0 : entries_.size(); }

QVariant NotificationCenter::data(const QModelIndex& index, int role) const {
    if (index.row() < 0 || index.row() >= entries_.size()) return {};
    const auto& entry = entries_.at(index.row());
    switch (role) {
        case Qt::DisplayRole:
        case TextRole:
            return entry.text;
        case IdRole:
            return entry.id;
        case LevelRole:
            return levelName(entry.level);
        case SourceRole:
            return entry.source;
        case DetailRole:
            return entry.detail;
        case StickyRole:
            return isSticky(entry.level);
        case RepeatRole:
            return entry.repeat;
        default:
            return {};
    }
}

QHash<int, QByteArray> NotificationCenter::roleNames() const {
    return {{IdRole, "notificationId"}, {LevelRole, "level"},   {TextRole, "text"},    {SourceRole, "source"},
            {DetailRole, "detail"},     {StickyRole, "sticky"}, {RepeatRole, "repeat"}};
}

int NotificationCenter::rowForId(qulonglong id) const {
    for (int row = 0; row < entries_.size(); ++row)
        if (entries_.at(row).id == id) return row;
    return -1;
}

void NotificationCenter::refreshRows() {
    if (entries_.isEmpty()) return;
    emit dataChanged(index(0), index(entries_.size() - 1));
}

void NotificationCenter::post(NotificationLevel level, const QString& text, const QString& source, const QString& detail) {
    if (text.isEmpty()) return;
    if (!entries_.isEmpty()) {
        auto& newest = entries_.last();
        if (newest.level == level && newest.text == text && newest.source == source) {
            ++newest.repeat;
            if (!isSticky(level)) newest.expires_at_ms = nowMs() + (level == NotificationLevel::Warning ? warning_ms_ : info_ms_);
            emit dataChanged(index(entries_.size() - 1), index(entries_.size() - 1), {RepeatRole});
            startExpiryTimer();
            return;
        }
    }

    Entry entry;
    entry.id = next_id_++;
    entry.level = level;
    entry.text = text;
    entry.source = source;
    entry.detail = detail;
    entry.expires_at_ms = isSticky(level) ? 0 : nowMs() + (level == NotificationLevel::Warning ? warning_ms_ : info_ms_);

    trimForInsert();
    beginInsertRows({}, entries_.size(), entries_.size());
    entries_.append(std::move(entry));
    endInsertRows();
    emit countChanged();
    startExpiryTimer();
}

void NotificationCenter::info(const QString& text, const QString& source, const QString& detail) {
    post(NotificationLevel::Info, text, source, detail);
}

void NotificationCenter::warning(const QString& text, const QString& source, const QString& detail) {
    post(NotificationLevel::Warning, text, source, detail);
}

void NotificationCenter::error(const QString& text, const QString& source, const QString& detail) {
    post(NotificationLevel::Error, text, source, detail);
}

void NotificationCenter::dismiss(qulonglong id) {
    const int row = rowForId(id);
    if (row < 0) return;
    beginRemoveRows({}, row, row);
    entries_.removeAt(row);
    endRemoveRows();
    refreshRows();
    emit countChanged();
    startExpiryTimer();
}

void NotificationCenter::dismissAll() {
    if (entries_.isEmpty()) return;
    beginResetModel();
    entries_.clear();
    endResetModel();
    emit countChanged();
    startExpiryTimer();
}

void NotificationCenter::setAutoDismissMs(int info_ms, int warning_ms) {
    info_ms_ = std::max(0, info_ms);
    warning_ms_ = std::max(0, warning_ms);
}

void NotificationCenter::setMaxEntries(int max_entries) {
    max_entries_ = std::clamp(max_entries, 1, kMaxEntriesLimit);
    while (entries_.size() > max_entries_) dropOldest();
}

void NotificationCenter::dropOldest() {
    // Prefer an expiring card over a sticky error, oldest first in both cases.
    int victim = -1;
    for (int row = 0; row < entries_.size(); ++row) {
        if (!isSticky(entries_.at(row).level)) {
            victim = row;
            break;
        }
    }
    if (victim < 0) victim = 0;
    beginRemoveRows({}, victim, victim);
    entries_.removeAt(victim);
    endRemoveRows();
    refreshRows();
    emit countChanged();
}

void NotificationCenter::trimForInsert() {
    // Leave room for exactly one more entry.
    while (entries_.size() >= max_entries_) dropOldest();
}

void NotificationCenter::startExpiryTimer() {
    const bool has_expiring = std::any_of(entries_.cbegin(), entries_.cend(), [](const Entry& entry) { return entry.expires_at_ms > 0; });
    if (has_expiring && !expiry_timer_.isActive()) {
        expiry_timer_.start();
    } else if (!has_expiring) {
        expiry_timer_.stop();
    }
}

void NotificationCenter::expire() {
    const auto now = nowMs();
    for (int row = 0; row < entries_.size();) {
        const auto& entry = entries_.at(row);
        if (entry.expires_at_ms <= 0 || entry.expires_at_ms > now) {
            ++row;
            continue;
        }
        beginRemoveRows({}, row, row);
        entries_.removeAt(row);
        endRemoveRows();
        refreshRows();
        emit countChanged();
    }
    startExpiryTimer();
}
