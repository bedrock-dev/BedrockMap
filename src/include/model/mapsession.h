#ifndef BEDROCKMAP_MAPSESSION_H
#define BEDROCKMAP_MAPSESSION_H

#include <QObject>

#include "model/mapview.h"

/// Renderer-independent state for one map view and its selection.
class MapSession final : public QObject {
    Q_OBJECT

   public:
    explicit MapSession(QObject* parent = nullptr);

    [[nodiscard]] MapView* view() { return &view_; }
    [[nodiscard]] const MapView* view() const { return &view_; }

    void setDim(int dim) { view_.setDim(dim); }
    void setLayer(RenderOption::LayerType layer) { view_.setLayer(layer); }
    void setOther(RenderOption::OtherType other, bool value) { view_.setOther(other, value); }
    [[nodiscard]] bool toggleOther(RenderOption::OtherType other);

   signals:
    void selectionChanged();

   private:
    MapView view_;
};

#endif  // BEDROCKMAP_MAPSESSION_H
