#include "controller/mcstructuresession.h"
#include <utility>

McstructureSession::McstructureSession(QString path, QString title, QObject* parent)
    : WorkspaceSession(QStringLiteral("mcstructure"), std::move(path), std::move(title), parent),
      controller_(new McstructureController(this)) {}

void McstructureSession::open() { controller_->load(path()); }
