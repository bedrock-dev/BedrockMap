#pragma once

#include <QAbstractListModel>
#include <QList>
#include <QMutex>
#include <QTimer>
#include <QUrl>
#include <QtLogging>

#include "loguru/loguru.hpp"

class LogModel final : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(QString filter READ filter WRITE setFilter NOTIFY filterChanged)
    Q_PROPERTY(int minimumLevel READ minimumLevel WRITE setMinimumLevel NOTIFY filterChanged)
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(int storedCount READ storedCount NOTIFY countChanged)
    Q_PROPERTY(qulonglong omittedCount READ omittedCount NOTIFY countChanged)
    Q_PROPERTY(QString logFile READ logFile CONSTANT)
    Q_PROPERTY(QString error READ error NOTIFY errorChanged)
    Q_PROPERTY(QString displayHtml READ displayHtml NOTIFY countChanged)

   public:
    enum Role { TextRole = Qt::UserRole + 1, SeverityRole };
    explicit LogModel(int& argc, char** argv, QObject* parent = nullptr);
    ~LogModel() override;
    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    QString filter() const { return filter_; }
    int minimumLevel() const { return minimum_level_; }
    int count() const { return visible_.size(); }
    int storedCount() const { return entries_.size(); }
    qulonglong omittedCount() const { return omitted_count_; }
    QString logFile() const { return log_file_; }
    QString error() const { return error_; }
    QString displayHtml() const { return display_html_; }
    void setFilter(const QString& text);
    void setMinimumLevel(int level);
    Q_INVOKABLE void clear();
    Q_INVOKABLE void copyAll();
    Q_INVOKABLE void copyText(const QString& text);
    Q_INVOKABLE bool save(const QUrl& destination);

   signals:
    void filterChanged();
    void countChanged();
    void errorChanged();

   private:
    struct Entry {
        QString text;
        int severity;
    };
    static constexpr int MaxEntries = 10000;
    static void receive(void* userData, const loguru::Message& message);
    void drain();
    bool matches(const Entry& entry) const;
    void rebuild();
    void rebuildDisplayHtml();
    static QString entryHtml(const Entry& entry);
    QString snapshot() const;
    QMutex pending_mutex_;
    QList<Entry> pending_;
    qulonglong pending_omitted_{0};
    QList<Entry> entries_;
    QList<Entry> visible_;
    QTimer timer_;
    QString filter_;
    int minimum_level_{0};
    qulonglong omitted_count_{0};
    QString log_file_;
    QString error_;
    QString display_html_;
    QtMessageHandler previous_handler_{nullptr};
};
