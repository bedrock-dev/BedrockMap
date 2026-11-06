#ifndef BEDROCKMAP_WORKSPACESESSION_H
#define BEDROCKMAP_WORKSPACESESSION_H

#include <QObject>
#include <QString>
#include <QVariantList>

/// Common lifecycle and presentation contract for every document tab in the
/// application. Worlds, NBT files and future structures differ in their
/// payloads, but the shell only needs this shared state.
class WorkspaceSession : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString kind READ kind CONSTANT)
    Q_PROPERTY(QString path READ path NOTIFY pathChanged)
    Q_PROPERTY(QString title READ title WRITE setTitle NOTIFY titleChanged)
    Q_PROPERTY(bool dirty READ isDirty NOTIFY dirtyChanged)
    Q_PROPERTY(bool saving READ saving NOTIFY savingChanged)
    Q_PROPERTY(bool active READ active NOTIFY activeChanged)
    Q_PROPERTY(bool titleBarSaveVisible READ titleBarSaveVisible NOTIFY titleBarActionsChanged)
    Q_PROPERTY(bool titleBarSaveEnabled READ titleBarSaveEnabled NOTIFY titleBarActionsChanged)
    Q_PROPERTY(bool titleBarNavigationVisible READ titleBarNavigationVisible NOTIFY titleBarActionsChanged)
    Q_PROPERTY(QVariantList titleBarNavigationSections READ titleBarNavigationSections NOTIFY titleBarActionsChanged)
    Q_PROPERTY(int titleBarNavigationIndex READ titleBarNavigationIndex WRITE setTitleBarNavigationIndex NOTIFY titleBarActionsChanged)
    Q_PROPERTY(bool saveCompletesAsynchronously READ saveCompletesAsynchronously CONSTANT)

   public:
    explicit WorkspaceSession(QString kind, QString path, QString title, QObject* parent = nullptr);
    ~WorkspaceSession() override = default;

    [[nodiscard]] QString kind() const { return kind_; }
    [[nodiscard]] QString path() const { return path_; }
    [[nodiscard]] QString title() const { return title_; }
    [[nodiscard]] bool isDirty() const { return dirty_; }
    [[nodiscard]] bool saving() const { return saving_; }
    [[nodiscard]] bool active() const { return active_; }
    [[nodiscard]] virtual bool titleBarSaveVisible() const { return false; }
    [[nodiscard]] virtual bool titleBarSaveEnabled() const { return false; }
    [[nodiscard]] virtual bool titleBarNavigationVisible() const { return false; }
    [[nodiscard]] virtual QVariantList titleBarNavigationSections() const { return {}; }
    [[nodiscard]] virtual int titleBarNavigationIndex() const { return 0; }
    [[nodiscard]] virtual bool saveCompletesAsynchronously() const { return false; }
    virtual void setTitleBarNavigationIndex(int) {}

    Q_INVOKABLE virtual void open() {}
    Q_INVOKABLE virtual void close() {}
    Q_INVOKABLE [[nodiscard]] virtual bool commit() = 0;
    virtual void setActive(bool active);
    void setTitle(const QString& title);

   signals:
    void pathChanged();
    void titleChanged();
    void dirtyChanged();
    void savingChanged();
    void activeChanged();
    void titleBarActionsChanged();
    void saved();
    void saveFailed(const QString& message);

   protected:
    void setPath(const QString& path);
    void setDirty(bool dirty);
    void setSaving(bool saving);

   private:
    QString kind_;
    QString path_;
    QString title_;
    bool dirty_{false};
    bool saving_{false};
    bool active_{false};
};

#endif  // BEDROCKMAP_WORKSPACESESSION_H
