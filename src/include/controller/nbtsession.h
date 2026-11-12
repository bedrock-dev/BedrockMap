#ifndef BEDROCKMAP_NBTSESSION_H
#define BEDROCKMAP_NBTSESSION_H

#include <QUrl>

#include "controller/nbtdocumentcontroller.h"
#include "controller/workspacesession.h"

/// One NBT file or untitled NBT document in the shared session list.
class NbtSession final : public WorkspaceSession {
    Q_OBJECT
    Q_PROPERTY(NbtDocumentController* document READ document CONSTANT)

   public:
    explicit NbtSession(QString path, QString title, BinaryClipboard* clipboard, QObject* parent = nullptr);

    [[nodiscard]] NbtDocumentController* document() const { return document_; }
    [[nodiscard]] bool commit() override;
    [[nodiscard]] bool titleBarSaveVisible() const override;
    [[nodiscard]] bool titleBarSaveEnabled() const override;

    bool loadFile(const QUrl& url);
    void createNew();

   private:
    void syncFromDocument();

    NbtDocumentController* document_{nullptr};
};

#endif  // BEDROCKMAP_NBTSESSION_H
