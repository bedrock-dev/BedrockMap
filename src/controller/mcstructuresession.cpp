#include "controller/mcstructuresession.h"

#include <utility>

#include "controller/mcstructurevolumesource.h"
#include "io/documentio.h"

McstructureSession::McstructureSession(const QUrl& url, QString title, QObject* parent)
    : WorkspaceSession(QStringLiteral("mcstructure"), io::DocumentIo::identifier(url), std::move(title), parent),
      url_(url),
      controller_(new BlockVolumeController(std::make_unique<McstructureVolumeSource>(), this)) {}

void McstructureSession::open() { controller_->load(url_); }
