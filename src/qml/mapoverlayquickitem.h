#ifndef BEDROCKMAP_MAPOVERLAYQUICKITEM_H
#define BEDROCKMAP_MAPOVERLAYQUICKITEM_H

#include <QtQml/qqmlregistration.h>

#include <QColor>
#include <QLineF>
#include <QMutex>
#include <QPointer>
#include <QQuickItem>
#include <QRect>
#include <QTimer>
#include <QVector>
#include <memory>

#include "controller/globaldatacontroller.h"
#include "controller/mapcontroller.h"
#include "render/overlay.h"
#include "render/terraindataprovider.h"

class QSGGeometryNode;
class QSGNode;
struct MapRenderState;

struct MapOverlaySelectionSnapshot {
    QVector<QRect> rects;
    QRect dragRect;
    bool importing{false};
    QVector<QLineF> importEdges;
};

/// Renderer-side overlay layer above the terrain atlas. It publishes the camera
/// actually applied by the terrain node; HSA, villages, slime masks, sprites and
/// selection can later be added as sibling scene-graph nodes without creating
/// QML items per feature.
class MapOverlayQuickItem : public QQuickItem {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(MapController* controller READ controller WRITE setController NOTIFY controllerChanged)
    Q_PROPERTY(QObject* cameraSource READ cameraSource WRITE setCameraSource NOTIFY cameraSourceChanged)
    Q_PROPERTY(TerrainDataProvider* provider READ provider WRITE setProvider NOTIFY providerChanged)
    Q_PROPERTY(GlobalDataController* globalData READ globalData WRITE setGlobalData NOTIFY globalDataChanged)
    Q_PROPERTY(bool entitiesVisible READ entitiesVisible WRITE setEntitiesVisible NOTIFY entitiesVisibleChanged)
    Q_PROPERTY(bool slimeChunksVisible READ slimeChunksVisible WRITE setSlimeChunksVisible NOTIFY slimeChunksVisibleChanged)
    Q_PROPERTY(bool villagesVisible READ villagesVisible WRITE setVillagesVisible NOTIFY villagesVisibleChanged)
    Q_PROPERTY(bool hsaVisible READ hsaVisible WRITE setHsaVisible NOTIFY hsaVisibleChanged)
    Q_PROPERTY(int actorRenderStyle READ actorRenderStyle WRITE setActorRenderStyle NOTIFY actorRenderStyleChanged)
    Q_PROPERTY(bool gridVisible READ gridVisible WRITE setGridVisible NOTIFY gridVisibleChanged)
    Q_PROPERTY(QColor gridColor READ gridColor WRITE setGridColor NOTIFY gridColorChanged)
    Q_PROPERTY(QColor selectionColor READ selectionColor WRITE setSelectionColor NOTIFY selectionColorChanged)
    Q_PROPERTY(double viewCenterX READ viewCenterX NOTIFY appliedCameraChanged)
    Q_PROPERTY(double viewCenterZ READ viewCenterZ NOTIFY appliedCameraChanged)
    Q_PROPERTY(double viewScale READ viewScale NOTIFY appliedCameraChanged)

   public:
    explicit MapOverlayQuickItem(QQuickItem* parent = nullptr);

    [[nodiscard]] MapController* controller() const { return controller_; }
    [[nodiscard]] QObject* cameraSource() const { return camera_source_; }
    [[nodiscard]] TerrainDataProvider* provider() const { return provider_; }
    [[nodiscard]] GlobalDataController* globalData() const { return global_data_; }
    [[nodiscard]] bool entitiesVisible() const { return entities_visible_; }
    [[nodiscard]] bool slimeChunksVisible() const { return slime_chunks_visible_; }
    [[nodiscard]] bool villagesVisible() const { return villages_visible_; }
    [[nodiscard]] bool hsaVisible() const { return hsa_visible_; }
    [[nodiscard]] int actorRenderStyle() const { return actor_render_style_; }
    [[nodiscard]] bool gridVisible() const { return grid_visible_; }
    [[nodiscard]] QColor gridColor() const { return grid_color_; }
    [[nodiscard]] QColor selectionColor() const { return selection_color_; }
    [[nodiscard]] double viewCenterX() const { return applied_center_x_; }
    [[nodiscard]] double viewCenterZ() const { return applied_center_z_; }
    [[nodiscard]] double viewScale() const { return applied_scale_; }
    void setController(MapController* controller);
    void setCameraSource(QObject* source);
    void setProvider(TerrainDataProvider* provider);
    void setGlobalData(GlobalDataController* global_data);
    void setEntitiesVisible(bool visible);
    void setSlimeChunksVisible(bool visible);
    void setVillagesVisible(bool visible);
    void setHsaVisible(bool visible);
    void setActorRenderStyle(int style);
    void setGridVisible(bool visible);
    void setGridColor(const QColor& color);
    void setSelectionColor(const QColor& color);

   signals:
    void controllerChanged();
    void cameraSourceChanged();
    void providerChanged();
    void globalDataChanged();
    void entitiesVisibleChanged();
    void slimeChunksVisibleChanged();
    void villagesVisibleChanged();
    void hsaVisibleChanged();
    void actorRenderStyleChanged();
    void gridVisibleChanged();
    void gridColorChanged();
    void selectionColorChanged();
    void appliedCameraChanged();

   protected:
    QSGNode* updatePaintNode(QSGNode* oldNode, UpdatePaintNodeData*) override;
    void geometryChange(const QRectF& newGeometry, const QRectF& oldGeometry) override;

   private:
    void syncViewState();
    void syncSelectionState();
    void scheduleEntityRefresh();
    void refreshEntities();
    void clearEntities();
    void scheduleSlimeChunkRefresh();
    void refreshSlimeChunks();
    void clearSlimeChunks();
    void scheduleRectRefresh();
    void refreshRects();
    void clearRects();
    void updateSlimeChunkNode(QSGNode* root);
    void updateGridNode(QSGNode* root);

    QPointer<MapController> controller_;
    QPointer<QObject> camera_source_;
    QPointer<TerrainDataProvider> provider_;
    QPointer<GlobalDataController> global_data_;
    std::shared_ptr<MapRenderState> camera_state_;
    QTimer entity_refresh_timer_;
    bool entities_visible_{true};
    QTimer slime_chunk_refresh_timer_;
    bool slime_chunks_visible_{false};
    bool villages_visible_{false};
    bool hsa_visible_{false};
    int actor_render_style_{0};
    bool grid_visible_{true};
    QColor grid_color_{QStringLiteral("#bbbbbb")};
    QColor selection_color_{218, 255, 251, 80};
    mutable QMutex entities_mutex_;
    std::shared_ptr<const QVector<ActorSprite>> entity_sprites_;
    QSGGeometryNode* sprite_node_{nullptr};
    int sprite_vertex_capacity_{0};
    mutable QMutex slime_chunks_mutex_;
    std::shared_ptr<const QVector<QRect>> slime_chunks_;
    QSGGeometryNode* slime_node_{nullptr};
    int slime_vertex_capacity_{0};
    QTimer rect_refresh_timer_;
    mutable QMutex rects_mutex_;
    std::shared_ptr<const QVector<OverlayRect>> overlay_rects_;
    mutable QMutex selection_mutex_;
    std::shared_ptr<const MapOverlaySelectionSnapshot> selection_snapshot_;
    QSGGeometryNode* rect_node_{nullptr};
    int rect_vertex_capacity_{0};
    QSGGeometryNode* grid_node_{nullptr};
    int grid_vertex_capacity_{0};
    QSGGeometryNode* selection_node_{nullptr};
    int selection_vertex_capacity_{0};
    qreal center_x_{0.0};
    qreal center_z_{0.0};
    qreal scale_{64.0};
    double applied_center_x_{0.0};
    double applied_center_z_{0.0};
    double applied_scale_{64.0};
    quint64 published_camera_revision_{0};
};

#endif  // BEDROCKMAP_MAPOVERLAYQUICKITEM_H
