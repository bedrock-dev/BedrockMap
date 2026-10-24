#ifndef BEDROCKMAP_MCSTRUCTURESESSION_H
#define BEDROCKMAP_MCSTRUCTURESESSION_H

#include "controller/mcstructurecontroller.h"
#include "controller/workspacesession.h"

/// Standalone structures are browse-only and never participate in saving.
class McstructureSession final : public WorkspaceSession {
    Q_OBJECT
    Q_PROPERTY(McstructureController* controller READ controller CONSTANT)
public:
    explicit McstructureSession(QString path, QString title, QObject* parent = nullptr);
    [[nodiscard]] McstructureController* controller() const { return controller_; }
    void open() override;
    [[nodiscard]] bool commit() override { return true; }
private:
    McstructureController* controller_;
};
#endif  // BEDROCKMAP_MCSTRUCTURESESSION_H
