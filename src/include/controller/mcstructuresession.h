#ifndef BEDROCKMAP_MCSTRUCTURESESSION_H
#define BEDROCKMAP_MCSTRUCTURESESSION_H

#include <QUrl>

#include "controller/blockvolumecontroller.h"
#include "controller/nbtdocumentcontroller.h"
#include "controller/workspacesession.h"

class AppContext;

/// Standalone structures are browse-only and never participate in saving.
class McstructureSession final : public WorkspaceSession {
    Q_OBJECT
    Q_PROPERTY(BlockVolumeController* controller READ controller CONSTANT)
    Q_PROPERTY(NbtDocumentController* document READ document CONSTANT)
   public:
    explicit McstructureSession(const QUrl& url, QString title, AppContext& context, QObject* parent = nullptr);
    [[nodiscard]] BlockVolumeController* controller() const { return controller_; }
    [[nodiscard]] NbtDocumentController* document() const { return document_; }
    void open() override;
    [[nodiscard]] bool commit() override { return true; }

   private:
    void syncDocument();

    QUrl url_;
    BlockVolumeController* controller_;
    NbtDocumentController* document_;
};
#endif  // BEDROCKMAP_MCSTRUCTURESESSION_H
