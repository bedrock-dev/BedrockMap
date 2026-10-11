#pragma once

#include <QObject>
#include <QVariantList>

#include "levelpathmanager.h"

class WorldListModel final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList recentWorlds READ recentWorlds NOTIFY worldsChanged)
    Q_PROPERTY(QVariantList allWorlds READ allWorlds NOTIFY worldsChanged)

   public:
    explicit WorldListModel(QObject* parent = nullptr);

    [[nodiscard]] QVariantList recentWorlds() const { return recent_worlds_; }
    [[nodiscard]] QVariantList allWorlds() const { return all_worlds_; }

    Q_INVOKABLE void refresh();

   signals:
    void worldsChanged();

   private:
    static QVariantMap toMap(const LevelPathInfo& info);

    LevelPathManager manager_;
    QVariantList recent_worlds_;
    QVariantList all_worlds_;
};
