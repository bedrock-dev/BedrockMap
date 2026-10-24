#ifndef BEDROCKMAP_NOTIFICATIONS_H
#define BEDROCKMAP_NOTIFICATIONS_H

#include <QAbstractListModel>
#include <QHash>
#include <QList>
#include <QString>
#include <QTimer>

/// How urgent a message is. The centre turns this into colour and into how long
/// the message stays on screen: only errors wait for the user.
enum class NotificationLevel { Info = 0, Warning = 1, Error = 2 };

/// Application-wide message centre.
///
/// One place decides how a message looks, how long it lives and how repeats are
/// folded together, so the same failure reads the same way on the worlds page
/// and inside an open world. The queue is a list model: QML renders it and
/// dismisses entries by id, and every entry can name the world it came from,
/// which is what keeps messages readable while several worlds are open.
class NotificationCenter final : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)

   public:
    enum Role {
        IdRole = Qt::UserRole + 1,
        /// "info", "warning" or "error"; the UI switches on this instead of on a
        /// numeric severity, so the mapping lives in one place.
        LevelRole,
        TextRole,
        /// World or item the message belongs to; empty when it is app-wide.
        SourceRole,
        /// Second line with the actionable bit (path, error code); may be empty.
        DetailRole,
        /// True for entries that never expire on their own.
        StickyRole,
        /// How often this exact message has been posted; 1 the first time.
        RepeatRole,
    };

    explicit NotificationCenter(QObject* parent = nullptr);

    [[nodiscard]] int count() const { return static_cast<int>(entries_.size()); }
    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

    /// Post a message. Repeating the newest message folds it into that entry
    /// instead of adding another card, so a retried action cannot stack three
    /// copies of the same complaint.
    void post(NotificationLevel level, const QString& text, const QString& source = {}, const QString& detail = {});

    Q_INVOKABLE void info(const QString& text, const QString& source = {}, const QString& detail = {});
    Q_INVOKABLE void warning(const QString& text, const QString& source = {}, const QString& detail = {});
    Q_INVOKABLE void error(const QString& text, const QString& source = {}, const QString& detail = {});

    /// Remove one entry. Entries are addressed by id rather than by row because
    /// rows shift whenever an older message expires.
    Q_INVOKABLE void dismiss(qulonglong id);
    Q_INVOKABLE void dismissAll();

    /// How long info and warning entries stay before they expire. Exposed so the
    /// unit test does not have to wait seconds.
    void setAutoDismissMs(int info_ms, int warning_ms);
    /// How many entries the queue keeps; the oldest expiring entry is dropped
    /// before the oldest error.
    void setMaxEntries(int max_entries);

    [[nodiscard]] static QString levelName(NotificationLevel level);

   signals:
    void countChanged();

   private:
    struct Entry {
        qulonglong id{0};
        NotificationLevel level{NotificationLevel::Info};
        QString text;
        QString source;
        QString detail;
        /// Absolute deadline in ms since epoch; 0 means "sticky".
        qint64 expires_at_ms{0};
        int repeat{1};
    };

    [[nodiscard]] static bool isSticky(NotificationLevel level) { return level == NotificationLevel::Error; }
    [[nodiscard]] static qint64 nowMs();
    [[nodiscard]] int rowForId(qulonglong id) const;
    /// Force the views to re-read the role values of the remaining rows. Rows
    /// shift on every insert and removal and the delegate model is not required
    /// to refresh the shifted entries by itself, so this is done explicitly.
    void refreshRows();
    /// Remove the entry the user can most afford to lose; used to make room for
    /// a new message and to honour a lowered cap.
    void dropOldest();
    void trimForInsert();
    void startExpiryTimer();
    void expire();

    /// Chronological, oldest first: the last row is the newest message. That is
    /// the order a bottom-anchored stack wants, and a view that shows newest
    /// first can read the rows backwards.
    QList<Entry> entries_;
    QTimer expiry_timer_;
    qulonglong next_id_{1};
    int info_ms_{4000};
    int warning_ms_{7000};
    int max_entries_{4};
};

#endif  // BEDROCKMAP_NOTIFICATIONS_H
