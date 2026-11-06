#ifndef BEDROCKMAP_NAVIGATION_H
#define BEDROCKMAP_NAVIGATION_H

#include <QtQml/qqmlregistration.h>

#include <QObject>
#include <QVariantList>

/// Shared page identifiers and shell metadata. Keeping these in C++ avoids
/// duplicating page numbers, titles and icons in the main window, sidebar and
/// bottom navigation.
class Navigation : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QVariantList pages READ pages NOTIFY pagesChanged)
    Q_PROPERTY(int pageWorlds READ pageWorlds CONSTANT)
    Q_PROPERTY(int pageSessions READ pageSessions CONSTANT)
    Q_PROPERTY(int pageTools READ pageTools CONSTANT)
    Q_PROPERTY(int pageSettings READ pageSettings CONSTANT)
    Q_PROPERTY(int pageWorkspace READ pageWorkspace CONSTANT)

   public:
    enum Page { Worlds = 0, Sessions = 1, Tools = 2, Settings = 3, Workspace = 4 };
    Q_ENUM(Page)

    explicit Navigation(QObject* parent = nullptr);

    [[nodiscard]] QVariantList pages() const;
    [[nodiscard]] int pageWorlds() const { return Worlds; }
    [[nodiscard]] int pageSessions() const { return Sessions; }
    [[nodiscard]] int pageTools() const { return Tools; }
    [[nodiscard]] int pageSettings() const { return Settings; }
    [[nodiscard]] int pageWorkspace() const { return Workspace; }

    Q_INVOKABLE void retranslate();

   signals:
    void pagesChanged();
};

#endif  // BEDROCKMAP_NAVIGATION_H
