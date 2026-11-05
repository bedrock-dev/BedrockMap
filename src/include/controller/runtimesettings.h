#ifndef BEDROCKMAP_RUNTIMESETTINGS_H
#define BEDROCKMAP_RUNTIMESETTINGS_H

#include <QObject>
#include <QString>

#include "base/config.h"

/// Read-only settings loaded at process startup and consumed by the running UI.
class RuntimeSettings final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool collapseSidebarOnStartup READ collapseSidebarOnStartup CONSTANT)
    Q_PROPERTY(bool showMapDebugButton READ showMapDebugButton CONSTANT)
    Q_PROPERTY(QString tileColorScheme READ tileColorScheme CONSTANT)
    Q_PROPERTY(double panInertia READ panInertia CONSTANT)
    Q_PROPERTY(QString gridLineColor READ gridLineColor CONSTANT)
    Q_PROPERTY(QString mapSelectionColor READ mapSelectionColor CONSTANT)
    Q_PROPERTY(int actorRenderStyle READ actorRenderStyle CONSTANT)
    Q_PROPERTY(QString voxelSelectionColor READ voxelSelectionColor CONSTANT)
    Q_PROPERTY(double gpuSaturation READ gpuSaturation CONSTANT)
    Q_PROPERTY(double gpuBrightness READ gpuBrightness CONSTANT)

   public:
    explicit RuntimeSettings(QObject* parent = nullptr);

    [[nodiscard]] bool collapseSidebarOnStartup() const { return values_.collapseSidebarOnStartup; }
    [[nodiscard]] bool showMapDebugButton() const { return values_.showMapDebugButton; }
    [[nodiscard]] QString tileColorScheme() const { return values_.tileColorScheme; }
    [[nodiscard]] double panInertia() const { return values_.panInertia; }
    [[nodiscard]] QString gridLineColor() const { return values_.gridLineColor; }
    [[nodiscard]] QString mapSelectionColor() const { return values_.mapSelectionColor; }
    [[nodiscard]] int actorRenderStyle() const { return values_.actorRenderStyle; }
    [[nodiscard]] QString voxelSelectionColor() const { return values_.voxelSelectionColor; }
    [[nodiscard]] double gpuSaturation() const { return values_.gpuSaturation; }
    [[nodiscard]] double gpuBrightness() const { return values_.gpuBrightness; }

   private:
    const setting::Settings& values_;
};

#endif  // BEDROCKMAP_RUNTIMESETTINGS_H
