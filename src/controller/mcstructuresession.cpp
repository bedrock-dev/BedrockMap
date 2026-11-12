#include "controller/mcstructuresession.h"

#include <memory>
#include <utility>
#include <vector>

#include "base/appcontext.h"
#include "controller/mcstructurevolumesource.h"
#include "io/documentio.h"
#include "nbt.h"

McstructureSession::McstructureSession(const QUrl& url, QString title, AppContext& context, QObject* parent)
    : WorkspaceSession(QStringLiteral("mcstructure"), io::DocumentIo::identifier(url), std::move(title), parent),
      url_(url),
      controller_(new BlockVolumeController(std::make_unique<McstructureVolumeSource>(), context, this)),
      document_(new NbtDocumentController(this)) {
    document_->setReadOnly(true);
    document_->setClipboard(&context.binaryClipboard());
    connect(controller_, &VoxelSceneController::voxelChanged, this, &McstructureSession::syncDocument);
}

void McstructureSession::open() { controller_->load(url_); }

void McstructureSession::syncDocument() {
    std::vector<std::unique_ptr<bl::nbt::compound_tag>> roots;
    if (const auto volume = controller_->volume(); volume && volume->root)
        roots.emplace_back(static_cast<bl::nbt::compound_tag*>(volume->root->copy()));
    document_->loadMemory(std::move(roots), {}, title());
}
