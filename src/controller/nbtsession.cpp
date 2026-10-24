#include "controller/nbtsession.h"

#include <QFileInfo>
#include <utility>

NbtSession::NbtSession(QString path, QString title, QObject* parent)
    : WorkspaceSession(QStringLiteral("nbt"), std::move(path), std::move(title), parent),
      document_(new NbtDocumentController(this)) {
    connect(document_, &NbtDocumentController::dirtyChanged, this, [this] { setDirty(document_->dirty()); });
    connect(document_, &NbtDocumentController::documentTitleChanged, this, [this] { syncFromDocument(); });
    connect(document_, &NbtDocumentController::filePathChanged, this, [this] { syncFromDocument(); });
}

void NbtSession::syncFromDocument() {
    setPath(document_->filePath());
    setTitle(document_->documentTitle());
    setDirty(document_->dirty());
}

bool NbtSession::commit() { return document_->save(); }

bool NbtSession::loadFile(const QString& path) {
    if (!document_->loadFile(path)) return false;
    setPath(document_->filePath());
    setTitle(document_->documentTitle());
    setDirty(document_->dirty());
    return true;
}

void NbtSession::createNew() {
    document_->createNew();
    syncFromDocument();
}
