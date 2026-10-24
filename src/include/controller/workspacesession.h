#ifndef BEDROCKMAP_WORKSPACESESSION_H
#define BEDROCKMAP_WORKSPACESESSION_H

#include <QObject>
#include <QString>

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

   public:
    explicit WorkspaceSession(QString kind, QString path, QString title, QObject* parent = nullptr);
    ~WorkspaceSession() override = default;

    [[nodiscard]] QString kind() const { return kind_; }
    [[nodiscard]] QString path() const { return path_; }
    [[nodiscard]] QString title() const { return title_; }
    [[nodiscard]] bool isDirty() const { return dirty_; }
    [[nodiscard]] bool saving() const { return saving_; }
    [[nodiscard]] bool active() const { return active_; }

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
