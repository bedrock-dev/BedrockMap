#include "controller/nbtsession.h"

#include <utility>

NbtSession::NbtSession(QString path, QString title, QObject* parent)
    : WorkspaceSession(QStringLiteral("nbt"), std::move(path), std::move(title), parent), document_(new NbtDocumentController(this)) {
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

bool NbtSession::loadFile(const QUrl& url) {
    if (!document_->loadFile(url)) return false;
    setPath(document_->filePath());
    setTitle(document_->documentTitle());
    setDirty(document_->dirty());
    return true;
}

void NbtSession::createNew() {
    document_->createNew();
    syncFromDocument();
}
