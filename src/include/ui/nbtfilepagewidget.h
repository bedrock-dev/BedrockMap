#ifndef BEDROCKMAP_NBTFILEPAGEWIDGET_H
#define BEDROCKMAP_NBTFILEPAGEWIDGET_H

#include <QString>
#include <QWidget>

#include "nbtwidget.h"
#include "tabpagewidget.h"

// A tab page holding a single NbtWidget for editing .nbt / .nbts files that
// were opened from disk (File mode: load, in-place save and save-as available).
class NbtFilePageWidget : public TabPageWidget {
    Q_OBJECT

   public:
    explicit NbtFilePageWidget(QWidget *parent = nullptr);
    ~NbtFilePageWidget() override;

    bool loadFile(const QString &path);
    /// Start an empty, unsaved document (no source file; dirty by default).
    void createNew();
    [[nodiscard]] QString getFileName() const { return file_name_; }

    bool isDirty() const override;
    bool commit() override;

   signals:
    void dirtyChanged(bool dirty);
    /// Emitted after a successful save, with the path written to.
    void saved(const QString &path);

   private:
    NbtWidget *nbt_editor_{nullptr};
    QString file_name_;
    bool dirty_{false};
};

#endif  // BEDROCKMAP_NBTFILEPAGEWIDGET_H
