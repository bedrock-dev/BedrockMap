#include "mapoverlayquickitem.h"

#include <QColor>
#include <QMetaObject>
#include <QMutexLocker>
#include <QPainterPath>
#include <QQuickWindow>
#include <QRegion>
#include <QSGGeometry>
#include <QSGNode>
#include <QSGTextureMaterial>
#include <QSGVertexColorMaterial>
#include <cmath>
#include <vector>

#include "base/config.h"
#include "controller/globaldatacontroller.h"
#include "controller/mapcontroller.h"
#include "iconatlas.h"
#include "maprenderstate.h"
#include "render/resourcemanager.h"
#include "render/terraindataprovider.h"
#include "terrainquickitem.h"

namespace {
    constexpr qreal minimum_entity_scale = 4.0;

    const IconAtlas& markerIconAtlas() {
        static const IconAtlas atlas = [] {
            IconAtlas result;
            const auto images = MarkerIcons();
            result.build(std::vector<const QImage*>(images.cbegin(), images.cend()));
            return result;
        }();
        return atlas;
    }

    class SpriteAtlasMaterial final : public QSGTextureMaterial {
       public:
        explicit SpriteAtlasMaterial(QSGTexture* texture) : texture_(texture) {
            setTexture(texture);
            setFiltering(QSGTexture::Nearest);
            setMipmapFiltering(QSGTexture::None);
            setHorizontalWrapMode(QSGTexture::ClampToEdge);
            setVerticalWrapMode(QSGTexture::ClampToEdge);
            setFlag(Blending, true);
        }
        ~SpriteAtlasMaterial() override { delete texture_; }

       private:
        QSGTexture* texture_{nullptr};
    };
}  // namespace

MapOverlayQuickItem::MapOverlayQuickItem(QQuickItem* parent) : QQuickItem(parent) {
    setFlag(ItemHasContents, true);
    markerIconAtlas();
    entity_refresh_timer_.setSingleShot(true);
    entity_refresh_timer_.setInterval(33);
    connect(&entity_refresh_timer_, &QTimer::timeout, this, &MapOverlayQuickItem::refreshEntities);
    slime_chunk_refresh_timer_.setSingleShot(true);
    slime_chunk_refresh_timer_.setInterval(50);
    connect(&slime_chunk_refresh_timer_, &QTimer::timeout, this, &MapOverlayQuickItem::refreshSlimeChunks);
    rect_refresh_timer_.setSingleShot(true);
    rect_refresh_timer_.setInterval(50);
    connect(&rect_refresh_timer_, &QTimer::timeout, this, &MapOverlayQuickItem::refreshRects);
}

void MapOverlayQuickItem::setController(MapController* controller) {
    if (controller_ == controller) return;
    if (controller_) {
        disconnect(controller_, nullptr, this, nullptr);
        disconnect(controller_->regionImport(), nullptr, this, nullptr);
    }
    controller_ = controller;
    if (controller_) {
        connect(controller_, &MapController::stateChanged, this, [this] {
            syncViewState();
            syncSelectionState();
            update();
            scheduleEntityRefresh();
            scheduleSlimeChunkRefresh();
            scheduleRectRefresh();
        });
        connect(controller_, &MapController::cameraMoved, this, [this] {
            syncViewState();
            update();
            scheduleEntityRefresh();
            scheduleSlimeChunkRefresh();
            scheduleRectRefresh();
        });
        connect(controller_, &MapController::dimensionChanged, this, [this] {
            clearEntities();
            clearSlimeChunks();
            clearRects();
            syncSelectionState();
            update();
            scheduleEntityRefresh();
            scheduleSlimeChunkRefresh();
            scheduleRectRefresh();
        });
        connect(controller_, &MapController::selectionChanged, this, [this] {
            syncSelectionState();
            update();
        });
        connect(controller_->regionImport(), &RegionImportController::previewChanged, this, [this] {
            syncSelectionState();
            update();
        });
        syncViewState();
    }
    syncSelectionState();
    clearSlimeChunks();
    emit controllerChanged();
    scheduleSlimeChunkRefresh();
    update();
}

void MapOverlayQuickItem::setCameraSource(QObject* source) {
    if (camera_source_ == source) return;
    camera_source_ = source;
    auto* terrain = qobject_cast<TerrainQuickItem*>(source);
    camera_state_ = terrain ? terrain->renderState() : nullptr;
    published_camera_revision_ = 0;
    emit cameraSourceChanged();
    update();
}

void MapOverlayQuickItem::setProvider(TerrainDataProvider* provider) {
    if (provider_ == provider) return;
    if (provider_) disconnect(provider_, nullptr, this, nullptr);
    provider_ = provider;
    if (provider_) {
        connect(provider_, &TerrainDataProvider::tileReady, this, &MapOverlayQuickItem::scheduleEntityRefresh);
        connect(provider_, &TerrainDataProvider::tileReady, this, &MapOverlayQuickItem::scheduleRectRefresh);
        connect(provider_, &TerrainDataProvider::regionEmpty, this, &MapOverlayQuickItem::scheduleEntityRefresh);
        connect(provider_, &TerrainDataProvider::regionEmpty, this, &MapOverlayQuickItem::scheduleRectRefresh);
        connect(provider_, &TerrainDataProvider::windowRefilled, this, &MapOverlayQuickItem::scheduleEntityRefresh);
        connect(provider_, &TerrainDataProvider::windowRefilled, this, &MapOverlayQuickItem::scheduleRectRefresh);
    }
    clearEntities();
    clearSlimeChunks();
    emit providerChanged();
    scheduleEntityRefresh();
    scheduleSlimeChunkRefresh();
}

void MapOverlayQuickItem::setGlobalData(GlobalDataController* global_data) {
    if (global_data_ == global_data) return;
    if (global_data_) disconnect(global_data_, nullptr, this, nullptr);
    global_data_ = global_data;
    if (global_data_) connect(global_data_, &GlobalDataController::sectionsChanged, this, &MapOverlayQuickItem::scheduleRectRefresh);
    emit globalDataChanged();
    scheduleRectRefresh();
}

void MapOverlayQuickItem::setEntitiesVisible(bool visible) {
    if (entities_visible_ == visible) return;
    entities_visible_ = visible;
    if (!visible)
        clearEntities();
    else
        scheduleEntityRefresh();
    emit entitiesVisibleChanged();
    update();
}

void MapOverlayQuickItem::setSlimeChunksVisible(bool visible) {
    if (slime_chunks_visible_ == visible) return;
    slime_chunks_visible_ = visible;
    if (!visible)
        clearSlimeChunks();
    else
        scheduleSlimeChunkRefresh();
    emit slimeChunksVisibleChanged();
    update();
}

void MapOverlayQuickItem::setActorRenderStyle(int style) {
    if (actor_render_style_ == style) return;
    actor_render_style_ = style;
    emit actorRenderStyleChanged();
    clearEntities();
    scheduleEntityRefresh();
}

void MapOverlayQuickItem::setGridVisible(bool visible) {
    if (grid_visible_ == visible) return;
    grid_visible_ = visible;
    emit gridVisibleChanged();
    update();
}

void MapOverlayQuickItem::setGridColor(const QColor& color) {
    if (grid_color_ == color) return;
    grid_color_ = color;
    emit gridColorChanged();
    update();
}

void MapOverlayQuickItem::setSelectionColor(const QColor& color) {
    if (!color.isValid() || selection_color_ == color) return;
    selection_color_ = color;
    emit selectionColorChanged();
    update();
}

void MapOverlayQuickItem::setVillagesVisible(bool visible) {
    if (villages_visible_ == visible) return;
    villages_visible_ = visible;
    emit villagesVisibleChanged();
    if (!visible && !hsa_visible_)
        clearRects();
    else
        scheduleRectRefresh();
    update();
}

void MapOverlayQuickItem::setHsaVisible(bool visible) {
    if (hsa_visible_ == visible) return;
    hsa_visible_ = visible;
    emit hsaVisibleChanged();
    if (!visible && !villages_visible_)
        clearRects();
    else
        scheduleRectRefresh();
    update();
}

void MapOverlayQuickItem::scheduleEntityRefresh() {
    if (entities_visible_ && provider_ && controller_ && !entity_refresh_timer_.isActive()) {
        const qreal scale = std::abs(controller_->cameraState().pixelsPerChunk);
        // A zoomed-out view spans many more regions. Refresh markers less often
        // there so collection stays off the input path; the cached icon list
        // still moves with the camera every rendered frame.
        entity_refresh_timer_.setInterval(scale < 8.0 ? 120 : (scale < 16.0 ? 66 : 33));
        entity_refresh_timer_.start();
    }
}

void MapOverlayQuickItem::clearEntities() {
    entity_refresh_timer_.stop();
    const auto empty = std::make_shared<QVector<ActorSprite>>();
    {
        const QMutexLocker lock(&entities_mutex_);
        entity_sprites_ = empty;
    }
    update();
}

void MapOverlayQuickItem::scheduleSlimeChunkRefresh() {
    if (!slime_chunks_visible_ || !provider_ || !controller_ || slime_chunk_refresh_timer_.isActive()) return;
    const qreal scale = std::abs(controller_->cameraState().pixelsPerChunk);
    // Match the entity overlay cutoff: dense chunk markers are both unreadable
    // and expensive below four pixels per chunk.
    if (scale < minimum_entity_scale) {
        clearSlimeChunks();
        return;
    }
    slime_chunk_refresh_timer_.setInterval(scale < 16.0 ? 100 : 50);
    slime_chunk_refresh_timer_.start();
}

void MapOverlayQuickItem::refreshSlimeChunks() {
    if (!slime_chunks_visible_ || !provider_ || !controller_ || width() <= 0 || height() <= 0) {
        clearSlimeChunks();
        return;
    }
    const MapCameraState camera = controller_->cameraState();
    const qreal scale = std::abs(camera.pixelsPerChunk);
    if (camera.dimension != 0 || scale < minimum_entity_scale) {
        clearSlimeChunks();
        return;
    }

    const qreal half_width = width() / (2.0 * scale) + constant::RW;
    const qreal half_height = height() / (2.0 * scale) + constant::RW;
    const int min_x = static_cast<int>(std::floor(camera.center.x() - half_width));
    const int max_x = static_cast<int>(std::ceil(camera.center.x() + half_width));
    const int min_z = static_cast<int>(std::floor(camera.center.y() - half_height));
    const int max_z = static_cast<int>(std::ceil(camera.center.y() + half_height));
    const int region_min_x = static_cast<int>(std::floor(static_cast<qreal>(min_x) / constant::RW)) * constant::RW;
    const int region_max_x = static_cast<int>(std::floor(static_cast<qreal>(max_x) / constant::RW)) * constant::RW;
    const int region_min_z = static_cast<int>(std::floor(static_cast<qreal>(min_z) / constant::RW)) * constant::RW;
    const int region_max_z = static_cast<int>(std::floor(static_cast<qreal>(max_z) / constant::RW)) * constant::RW;

    auto next = std::make_shared<QVector<QRect>>();
    for (int region_z = region_min_z; region_z <= region_max_z; region_z += constant::RW) {
        for (int region_x = region_min_x; region_x <= region_max_x; region_x += constant::RW) {
            const QImage image = provider_->slimeChunkImage(region_pos{region_x, region_z, 0});
            if (image.isNull()) continue;
            for (int chunk_z = 0; chunk_z < constant::RW; ++chunk_z) {
                for (int chunk_x = 0; chunk_x < constant::RW; ++chunk_x) {
                    const int pixel_x = (chunk_x << 4) + 8;
                    const int pixel_z = (chunk_z << 4) + 8;
                    if (pixel_x >= image.width() || pixel_z >= image.height()) continue;
                    if (qAlpha(image.pixel(pixel_x, pixel_z)) == 0) continue;
                    next->append(QRect(region_x + chunk_x, region_z + chunk_z, 1, 1));
                }
            }
        }
    }
    {
        const QMutexLocker lock(&slime_chunks_mutex_);
        slime_chunks_ = std::move(next);
    }
    update();
}

void MapOverlayQuickItem::clearSlimeChunks() {
    slime_chunk_refresh_timer_.stop();
    const auto empty = std::make_shared<QVector<QRect>>();
    {
        const QMutexLocker lock(&slime_chunks_mutex_);
        slime_chunks_ = empty;
    }
    update();
}

void MapOverlayQuickItem::scheduleRectRefresh() {
    if ((!villages_visible_ && !hsa_visible_) || !provider_ || !controller_ || rect_refresh_timer_.isActive()) return;
    const qreal scale = std::abs(controller_->cameraState().pixelsPerChunk);
    rect_refresh_timer_.setInterval(scale < 16.0 ? 100 : 50);
    rect_refresh_timer_.start();
}

void MapOverlayQuickItem::clearRects() {
    rect_refresh_timer_.stop();
    const auto empty = std::make_shared<QVector<OverlayRect>>();
    {
        const QMutexLocker lock(&rects_mutex_);
        overlay_rects_ = empty;
    }
    update();
}

void MapOverlayQuickItem::refreshRects() {
    if ((!villages_visible_ && !hsa_visible_) || !provider_ || !controller_ || width() <= 0 || height() <= 0) {
        clearRects();
        return;
    }
    auto next = std::make_shared<QVector<OverlayRect>>();
    const MapCameraState camera = controller_->cameraState();
    const int dimension = camera.dimension;
    if (villages_visible_ && global_data_) {
        for (const auto& rect : global_data_->villageRects())
            if (rect.dimension == dimension) next->append(rect);
    }
    if (hsa_visible_) {
        const qreal scale = std::abs(camera.pixelsPerChunk);
        const auto center = camera.center;
        const qreal half_width = width() / (2.0 * scale) + 8.0;
        const qreal half_height = height() / (2.0 * scale) + 8.0;
        next->append(provider_->hsaRects(
            static_cast<int>(std::floor(center.x() - half_width)), static_cast<int>(std::floor(center.y() - half_height)),
            static_cast<int>(std::ceil(center.x() + half_width)), static_cast<int>(std::ceil(center.y() + half_height)), dimension));
    }
    {
        const QMutexLocker lock(&rects_mutex_);
        overlay_rects_ = std::move(next);
    }
    update();
}

void MapOverlayQuickItem::refreshEntities() {
    if (!entities_visible_ || !provider_ || !controller_ || width() <= 0 || height() <= 0) {
        clearEntities();
        return;
    }
    const MapCameraState camera = controller_->cameraState();
    const qreal scale = std::abs(camera.pixelsPerChunk);
    if (scale < minimum_entity_scale) {
        clearEntities();
        return;
    }

    const qreal center_x = camera.center.x();
    const qreal center_z = camera.center.y();
    const qreal half_width = width() / (2.0 * scale) + 8.0;
    const qreal half_height = height() / (2.0 * scale) + 8.0;
    // The provider and region cache are keyed in chunk coordinates. Block
    // coordinates are only introduced when the actor position is projected to
    // screen space below.
    const int min_x = static_cast<int>(std::floor(center_x - half_width));
    const int max_x = static_cast<int>(std::ceil(center_x + half_width));
    const int min_z = static_cast<int>(std::floor(center_z - half_height));
    const int max_z = static_cast<int>(std::ceil(center_z + half_height));

    auto next = std::make_shared<QVector<ActorSprite>>(
        provider_->actorSprites(min_x, min_z, max_x, max_z, camera.dimension, actor_render_style_));
    {
        const QMutexLocker lock(&entities_mutex_);
        entity_sprites_ = std::move(next);
    }
    update();
}

void MapOverlayQuickItem::syncViewState() {
    if (!controller_) return;
    const MapCameraState camera = controller_->cameraState();
    center_x_ = camera.center.x();
    center_z_ = camera.center.y();
    scale_ = camera.pixelsPerChunk;
}

void MapOverlayQuickItem::syncSelectionState() {
    auto next = std::make_shared<MapOverlaySelectionSnapshot>();
    if (controller_) {
        const auto* placement = controller_->regionImport();
        next->importing = placement->active() && placement->dimension() == controller_->dimension();
        if (next->importing) {
            QPainterPath outline;
            for (const auto& rect : placement->region()) {
                next->rects.append(rect);
                outline.addRect(QRectF(rect));
            }
            // Simplify the union so partial shared edges and holes retain the
            // exact same boundary used by placement hit-testing.
            for (const auto& polygon : outline.simplified().toSubpathPolygons()) {
                for (qsizetype i = 1; i < polygon.size(); ++i)
                    next->importEdges.append(QLineF(polygon[i - 1], polygon[i]));
            }
        } else {
            next->rects = controller_->selectionRects();
            next->dragRect = controller_->selectionDragRect();
        }
    }
    const QMutexLocker lock(&selection_mutex_);
    selection_snapshot_ = std::move(next);
}

void MapOverlayQuickItem::updateSlimeChunkNode(QSGNode* root) {
    if (!root) return;
    std::shared_ptr<const QVector<QRect>> chunks;
    {
        const QMutexLocker lock(&slime_chunks_mutex_);
        chunks = slime_chunks_;
    }
    if (!chunks || chunks->isEmpty() || !slime_chunks_visible_) {
        if (slime_node_ && slime_node_->parent()) root->removeChildNode(slime_node_);
        return;
    }

    if (!slime_node_) {
        auto* geometry = new QSGGeometry(QSGGeometry::defaultAttributes_ColoredPoint2D(), 0);
        geometry->setDrawingMode(QSGGeometry::DrawTriangles);
        geometry->setVertexDataPattern(QSGGeometry::DynamicPattern);
        slime_node_ = new QSGGeometryNode;
        slime_node_->setGeometry(geometry);
        slime_node_->setFlag(QSGNode::OwnsGeometry, true);
        auto* material = new QSGVertexColorMaterial;
        material->setFlag(QSGMaterial::Blending, true);
        slime_node_->setMaterial(material);
        slime_node_->setFlag(QSGNode::OwnsMaterial, true);
    }
    if (!slime_node_->parent()) {
        // Slime chunks sit above HSA/village fills and below the chunk grid.
        if (grid_node_ && grid_node_->parent())
            root->insertChildNodeBefore(slime_node_, grid_node_);
        else if (selection_node_ && selection_node_->parent())
            root->insertChildNodeBefore(slime_node_, selection_node_);
        else if (sprite_node_ && sprite_node_->parent())
            root->insertChildNodeBefore(slime_node_, sprite_node_);
        else
            root->appendChildNode(slime_node_);
    }

    const int required_vertices = static_cast<int>(chunks->size()) * 6;
    auto* geometry = slime_node_->geometry();
    if (slime_vertex_capacity_ < required_vertices) {
        geometry = new QSGGeometry(QSGGeometry::defaultAttributes_ColoredPoint2D(), required_vertices);
        geometry->setDrawingMode(QSGGeometry::DrawTriangles);
        geometry->setVertexDataPattern(QSGGeometry::DynamicPattern);
        slime_node_->setGeometry(geometry);
        slime_vertex_capacity_ = required_vertices;
    }

    auto* vertices = geometry->vertexDataAsColoredPoint2D();
    int vertex = 0;
    const QColor color(29, 145, 44, 190);
    const auto alpha = static_cast<uchar>(color.alpha());
    const auto red = static_cast<uchar>((color.red() * alpha + 127) / 255);
    const auto green = static_cast<uchar>((color.green() * alpha + 127) / 255);
    const auto blue = static_cast<uchar>((color.blue() * alpha + 127) / 255);
    const qreal scale = std::abs(scale_);
    const auto addQuad = [&](qreal left, qreal top, qreal right, qreal bottom) {
        if (right <= left || bottom <= top) return;
        const float l = static_cast<float>(left);
        const float t = static_cast<float>(top);
        const float r = static_cast<float>(right);
        const float b = static_cast<float>(bottom);
        vertices[vertex++].set(l, t, red, green, blue, alpha);
        vertices[vertex++].set(r, t, red, green, blue, alpha);
        vertices[vertex++].set(l, b, red, green, blue, alpha);
        vertices[vertex++].set(r, t, red, green, blue, alpha);
        vertices[vertex++].set(r, b, red, green, blue, alpha);
        vertices[vertex++].set(l, b, red, green, blue, alpha);
    };
    const qreal view_center_x = width() / 2.0;
    const qreal view_center_z = height() / 2.0;
    for (const QRect& chunk : *chunks) {
        const qreal left = view_center_x + (chunk.x() - center_x_) * scale;
        const qreal top = view_center_z + (chunk.y() - center_z_) * scale;
        const qreal right = left + chunk.width() * scale;
        const qreal bottom = top + chunk.height() * scale;
        if (right < 0.0 || left > width() || bottom < 0.0 || top > height()) continue;
        addQuad(left, top, right, bottom);
    }

    geometry->setVertexCount(vertex);
    geometry->markVertexDataDirty();
    slime_node_->markDirty(QSGNode::DirtyGeometry);
}

void MapOverlayQuickItem::updateGridNode(QSGNode* root) {
    if (!root) return;

    const qreal scale = std::abs(scale_);
    const bool draw_minor = grid_visible_ && scale > 64.0;
    const bool draw_major = grid_visible_ && scale > 1.0;
    if ((!draw_minor && !draw_major) || width() <= 0.0 || height() <= 0.0) {
        if (grid_node_ && grid_node_->parent()) root->removeChildNode(grid_node_);
        return;
    }

    const qreal half_width = width() / (2.0 * scale);
    const qreal half_height = height() / (2.0 * scale);
    const int min_x = static_cast<int>(std::floor(center_x_ - half_width)) - 1;
    const int max_x = static_cast<int>(std::ceil(center_x_ + half_width)) + 1;
    const int min_z = static_cast<int>(std::floor(center_z_ - half_height)) - 1;
    const int max_z = static_cast<int>(std::ceil(center_z_ + half_height)) + 1;
    constexpr int major_step = constant::GRID_WIDTH;
    const int major_min_x = static_cast<int>(std::floor(static_cast<qreal>(min_x) / major_step)) * major_step;
    const int major_max_x = static_cast<int>(std::ceil(static_cast<qreal>(max_x) / major_step)) * major_step;
    const int major_min_z = static_cast<int>(std::floor(static_cast<qreal>(min_z) / major_step)) * major_step;
    const int major_max_z = static_cast<int>(std::ceil(static_cast<qreal>(max_z) / major_step)) * major_step;

    const int minor_vertical = draw_minor ? max_x - min_x + 2 : 0;
    const int minor_horizontal = draw_minor ? max_z - min_z + 2 : 0;
    const int major_vertical = draw_major ? (major_max_x - major_min_x) / major_step + 1 : 0;
    const int major_horizontal = draw_major ? (major_max_z - major_min_z) / major_step + 1 : 0;
    const int line_count = minor_vertical + minor_horizontal + major_vertical + major_horizontal;
    if (line_count <= 0) {
        if (grid_node_ && grid_node_->parent()) root->removeChildNode(grid_node_);
        return;
    }

    if (!grid_node_) {
        auto* geometry = new QSGGeometry(QSGGeometry::defaultAttributes_ColoredPoint2D(), 0);
        geometry->setDrawingMode(QSGGeometry::DrawTriangles);
        geometry->setVertexDataPattern(QSGGeometry::DynamicPattern);
        grid_node_ = new QSGGeometryNode;
        grid_node_->setGeometry(geometry);
        grid_node_->setFlag(QSGNode::OwnsGeometry, true);
        auto* material = new QSGVertexColorMaterial;
        material->setFlag(QSGMaterial::Blending, true);
        grid_node_->setMaterial(material);
        grid_node_->setFlag(QSGNode::OwnsMaterial, true);
    }
    if (!grid_node_->parent()) {
        // Grid sits above terrain overlays but below selections and actors.
        if (selection_node_ && selection_node_->parent())
            root->insertChildNodeBefore(grid_node_, selection_node_);
        else if (sprite_node_ && sprite_node_->parent())
            root->insertChildNodeBefore(grid_node_, sprite_node_);
        else
            root->appendChildNode(grid_node_);
    }

    const int required_vertices = line_count * 6;
    auto* geometry = grid_node_->geometry();
    if (grid_vertex_capacity_ < required_vertices) {
        geometry = new QSGGeometry(QSGGeometry::defaultAttributes_ColoredPoint2D(), required_vertices);
        geometry->setDrawingMode(QSGGeometry::DrawTriangles);
        geometry->setVertexDataPattern(QSGGeometry::DynamicPattern);
        grid_node_->setGeometry(geometry);
        grid_vertex_capacity_ = required_vertices;
    }

    auto* vertices = geometry->vertexDataAsColoredPoint2D();
    int vertex = 0;
    const QColor color = grid_color_.isValid() ? grid_color_ : QColor(QStringLiteral("#bbbbbb"));
    const auto alpha = static_cast<uchar>(color.alpha());
    const auto premultiply = [alpha](int channel) {
        return static_cast<uchar>((channel * alpha + 127) / 255);
    };
    const auto red = premultiply(color.red());
    const auto green = premultiply(color.green());
    const auto blue = premultiply(color.blue());
    const auto addQuad = [&](qreal left, qreal top, qreal right, qreal bottom) {
        const float l = static_cast<float>(left);
        const float t = static_cast<float>(top);
        const float r = static_cast<float>(right);
        const float b = static_cast<float>(bottom);
        vertices[vertex++].set(l, t, red, green, blue, alpha);
        vertices[vertex++].set(r, t, red, green, blue, alpha);
        vertices[vertex++].set(l, b, red, green, blue, alpha);
        vertices[vertex++].set(r, t, red, green, blue, alpha);
        vertices[vertex++].set(r, b, red, green, blue, alpha);
        vertices[vertex++].set(l, b, red, green, blue, alpha);
    };
    const auto addVertical = [&](int chunk_x, qreal line_width) {
        const qreal x = width() / 2.0 + (chunk_x - center_x_) * scale;
        const qreal half_line = line_width / 2.0;
        addQuad(x - half_line, 0.0, x + half_line, height());
    };
    const auto addHorizontal = [&](int chunk_z, qreal line_width) {
        const qreal y = height() / 2.0 + (chunk_z - center_z_) * scale;
        const qreal half_line = line_width / 2.0;
        addQuad(0.0, y - half_line, width(), y + half_line);
    };

    if (draw_minor) {
        for (int x = min_x; x <= max_x + 1; ++x) addVertical(x, 1.0);
        for (int z = min_z; z <= max_z + 1; ++z) addHorizontal(z, 1.0);
    }
    if (draw_major) {
        for (int x = major_min_x; x <= major_max_x; x += major_step) addVertical(x, 3.0);
        for (int z = major_min_z; z <= major_max_z; z += major_step) addHorizontal(z, 3.0);
    }

    geometry->setVertexCount(vertex);
    geometry->markVertexDataDirty();
    grid_node_->markDirty(QSGNode::DirtyGeometry);
}

void MapOverlayQuickItem::geometryChange(const QRectF& newGeometry, const QRectF& oldGeometry) {
    QQuickItem::geometryChange(newGeometry, oldGeometry);
    if (newGeometry.size() != oldGeometry.size()) {
        update();
        scheduleEntityRefresh();
    }
}

QSGNode* MapOverlayQuickItem::updatePaintNode(QSGNode* oldNode, UpdatePaintNodeData*) {
    auto* root = oldNode ? oldNode : new QSGNode;
    if (!oldNode) {
        rect_node_ = nullptr;
        rect_vertex_capacity_ = 0;
        grid_node_ = nullptr;
        grid_vertex_capacity_ = 0;
        slime_node_ = nullptr;
        slime_vertex_capacity_ = 0;
        selection_node_ = nullptr;
        selection_vertex_capacity_ = 0;
        sprite_node_ = nullptr;
        sprite_vertex_capacity_ = 0;
    }
    if (camera_state_) {
        quint64 revision = 0;
        int dimension = 0;
        camera_state_->read(center_x_, center_z_, scale_, dimension, revision);
        if (revision != published_camera_revision_) {
            published_camera_revision_ = revision;
            const double center_x = center_x_;
            const double center_z = center_z_;
            const double scale = scale_;
            QMetaObject::invokeMethod(
                this,
                [this, center_x, center_z, scale] {
                    applied_center_x_ = center_x;
                    applied_center_z_ = center_z;
                    applied_scale_ = scale;
                    emit appliedCameraChanged();
                },
                Qt::QueuedConnection);
        }
    } else {
        syncViewState();
    }

    std::shared_ptr<const QVector<OverlayRect>> rects;
    {
        const QMutexLocker lock(&rects_mutex_);
        rects = overlay_rects_;
    }
    if ((!rects || rects->isEmpty()) && rect_node_ && rect_node_->parent()) root->removeChildNode(rect_node_);
    if (rects && !rects->isEmpty()) {
        if (!rect_node_) {
            auto* geometry = new QSGGeometry(QSGGeometry::defaultAttributes_ColoredPoint2D(), 0);
            geometry->setDrawingMode(QSGGeometry::DrawTriangles);
            geometry->setVertexDataPattern(QSGGeometry::DynamicPattern);
            rect_node_ = new QSGGeometryNode;
            rect_node_->setGeometry(geometry);
            rect_node_->setFlag(QSGNode::OwnsGeometry, true);
            auto* material = new QSGVertexColorMaterial;
            material->setFlag(QSGMaterial::Blending, true);
            rect_node_->setMaterial(material);
            rect_node_->setFlag(QSGNode::OwnsMaterial, true);
            root->prependChildNode(rect_node_);
        } else if (!rect_node_->parent()) {
            root->prependChildNode(rect_node_);
        }

        const int capacity = static_cast<int>(rects->size()) * 30;
        auto* geometry = rect_node_->geometry();
        if (rect_vertex_capacity_ < capacity) {
            geometry = new QSGGeometry(QSGGeometry::defaultAttributes_ColoredPoint2D(), capacity);
            geometry->setDrawingMode(QSGGeometry::DrawTriangles);
            geometry->setVertexDataPattern(QSGGeometry::DynamicPattern);
            rect_node_->setGeometry(geometry);
            rect_vertex_capacity_ = capacity;
        }
        auto* vertices = geometry->vertexDataAsColoredPoint2D();
        int vertex = 0;
        const qreal view_center_x = width() / 2.0;
        const qreal view_center_z = height() / 2.0;
        const auto addQuad = [&](qreal left, qreal top, qreal right, qreal bottom, const QColor& color) {
            if (right <= left || bottom <= top || color.alpha() == 0) return;
            const auto a = static_cast<uchar>(color.alpha());
            // QSG uses premultiplied-alpha blending for vertex colors.
            const auto premultiply = [a](int channel) { return static_cast<uchar>((channel * a + 127) / 255); };
            const auto r = premultiply(color.red());
            const auto g = premultiply(color.green());
            const auto b = premultiply(color.blue());
            const float l = static_cast<float>(left);
            const float t = static_cast<float>(top);
            const float rr = static_cast<float>(right);
            const float bb = static_cast<float>(bottom);
            vertices[vertex++].set(l, t, r, g, b, a);
            vertices[vertex++].set(rr, t, r, g, b, a);
            vertices[vertex++].set(l, bb, r, g, b, a);
            vertices[vertex++].set(rr, t, r, g, b, a);
            vertices[vertex++].set(rr, bb, r, g, b, a);
            vertices[vertex++].set(l, bb, r, g, b, a);
        };
        for (const auto& rect : *rects) {
            qreal left = view_center_x + (rect.min_x / 16.0 - center_x_) * scale_;
            qreal right = view_center_x + (rect.max_x / 16.0 - center_x_) * scale_;
            qreal top = view_center_z + (rect.min_z / 16.0 - center_z_) * scale_;
            qreal bottom = view_center_z + (rect.max_z / 16.0 - center_z_) * scale_;
            if (right < left) std::swap(left, right);
            if (bottom < top) std::swap(top, bottom);
            if (right < 0.0 || left > width() || bottom < 0.0 || top > height()) continue;
            addQuad(left, top, right, bottom, rect.fill);
            const qreal border = 2.5;
            addQuad(left, top, right, std::min(bottom, top + border), rect.outline);
            addQuad(left, std::max(top, bottom - border), right, bottom, rect.outline);
            addQuad(left, std::min(bottom, top + border), std::min(right, left + border), std::max(top, bottom - border), rect.outline);
            addQuad(std::max(left, right - border), std::min(bottom, top + border), right, std::max(top, bottom - border), rect.outline);
        }
        geometry->setVertexCount(vertex);
        geometry->markVertexDataDirty();
        rect_node_->markDirty(QSGNode::DirtyGeometry);
    }

    updateSlimeChunkNode(root);
    updateGridNode(root);

    std::shared_ptr<const MapOverlaySelectionSnapshot> selection;
    {
        const QMutexLocker lock(&selection_mutex_);
        selection = selection_snapshot_;
    }
    const QVector<QRect> selection_rects = selection ? selection->rects : QVector<QRect>();
    const QRect drag_rect = selection ? selection->dragRect : QRect();
    const bool has_selection = !selection_rects.isEmpty() || drag_rect.isValid();
    if (!has_selection) {
        if (selection_node_ && selection_node_->parent()) root->removeChildNode(selection_node_);
    } else {
        if (!selection_node_) {
            auto* geometry = new QSGGeometry(QSGGeometry::defaultAttributes_ColoredPoint2D(), 0);
            geometry->setDrawingMode(QSGGeometry::DrawTriangles);
            geometry->setVertexDataPattern(QSGGeometry::DynamicPattern);
            selection_node_ = new QSGGeometryNode;
            selection_node_->setGeometry(geometry);
            selection_node_->setFlag(QSGNode::OwnsGeometry, true);
            auto* material = new QSGVertexColorMaterial;
            material->setFlag(QSGMaterial::Blending, true);
            selection_node_->setMaterial(material);
            selection_node_->setFlag(QSGNode::OwnsMaterial, true);
        }
        if (!selection_node_->parent()) root->appendChildNode(selection_node_);

        const bool importing = selection && selection->importing;
        const int capacity = (static_cast<int>(selection_rects.size()) * (importing ? 1 : 5) +
                              (importing ? static_cast<int>(selection->importEdges.size()) : 0) +
                              (drag_rect.isValid() ? 5 : 0)) * 6;
        auto* geometry = selection_node_->geometry();
        if (selection_vertex_capacity_ < capacity) {
            geometry = new QSGGeometry(QSGGeometry::defaultAttributes_ColoredPoint2D(), capacity);
            geometry->setDrawingMode(QSGGeometry::DrawTriangles);
            geometry->setVertexDataPattern(QSGGeometry::DynamicPattern);
            selection_node_->setGeometry(geometry);
            selection_vertex_capacity_ = capacity;
        }
        auto* vertices = geometry->vertexDataAsColoredPoint2D();
        int vertex = 0;
        const qreal view_center_x = width() / 2.0;
        const qreal view_center_z = height() / 2.0;
        const auto addQuad = [&](qreal left, qreal top, qreal right, qreal bottom, const QColor& color) {
            if (right <= left || bottom <= top || color.alpha() == 0) return;
            const auto a = static_cast<uchar>(color.alpha());
            const auto premultiply = [a](int channel) { return static_cast<uchar>((channel * a + 127) / 255); };
            const auto r = premultiply(color.red());
            const auto g = premultiply(color.green());
            const auto b = premultiply(color.blue());
            const float l = static_cast<float>(left);
            const float t = static_cast<float>(top);
            const float rr = static_cast<float>(right);
            const float bb = static_cast<float>(bottom);
            vertices[vertex++].set(l, t, r, g, b, a);
            vertices[vertex++].set(rr, t, r, g, b, a);
            vertices[vertex++].set(l, bb, r, g, b, a);
            vertices[vertex++].set(rr, t, r, g, b, a);
            vertices[vertex++].set(rr, bb, r, g, b, a);
            vertices[vertex++].set(l, bb, r, g, b, a);
        };
        const auto screenRect = [&](const QRect& rect) {
            return QRectF(view_center_x + (rect.x() - center_x_) * scale_, view_center_z + (rect.y() - center_z_) * scale_,
                          rect.width() * scale_, rect.height() * scale_);
        };
        const QColor fill = selection && selection->importing ? QColor(255, 193, 7, 80) : selection_color_;
        QColor border = fill;
        border.setAlpha(255);
        QRegion selection_region;
        for (const auto& rect : selection_rects) selection_region += rect;
        constexpr qreal border_width = 2.0;
        for (const auto& rect : selection_rects) {
            const QRectF screen = screenRect(rect);
            addQuad(screen.left(), screen.top(), screen.right(), screen.bottom(), fill);
            if (importing) continue;
            if (!selection_region.intersects(QRect(rect.x(), rect.y() - 1, rect.width(), 1)))
                addQuad(screen.left(), screen.top(), screen.right(), screen.top() + border_width, border);
            if (!selection_region.intersects(QRect(rect.x(), rect.y() + rect.height(), rect.width(), 1)))
                addQuad(screen.left(), screen.bottom() - border_width, screen.right(), screen.bottom(), border);
            if (!selection_region.intersects(QRect(rect.x() - 1, rect.y(), 1, rect.height())))
                addQuad(screen.left(), screen.top(), screen.left() + border_width, screen.bottom(), border);
            if (!selection_region.intersects(QRect(rect.x() + rect.width(), rect.y(), 1, rect.height())))
                addQuad(screen.right() - border_width, screen.top(), screen.right(), screen.bottom(), border);
        }
        if (importing) {
            for (const auto& edge : selection->importEdges) {
                const qreal x1 = view_center_x + (edge.x1() - center_x_) * scale_;
                const qreal y1 = view_center_z + (edge.y1() - center_z_) * scale_;
                const qreal x2 = view_center_x + (edge.x2() - center_x_) * scale_;
                const qreal y2 = view_center_z + (edge.y2() - center_z_) * scale_;
                const qreal half = border_width / 2;
                addQuad(std::min(x1, x2) - half, std::min(y1, y2) - half,
                        std::max(x1, x2) + half, std::max(y1, y2) + half, border);
            }
        }
        if (drag_rect.isValid()) {
            const QRectF screen = screenRect(drag_rect);
            addQuad(screen.left(), screen.top(), screen.right(), screen.bottom(), fill);
            addQuad(screen.left(), screen.top(), screen.right(), screen.top() + border_width, border);
            addQuad(screen.left(), screen.bottom() - border_width, screen.right(), screen.bottom(), border);
            addQuad(screen.left(), screen.top(), screen.left() + border_width, screen.bottom(), border);
            addQuad(screen.right() - border_width, screen.top(), screen.right(), screen.bottom(), border);
        }
        geometry->setVertexCount(vertex);
        geometry->markVertexDataDirty();
        selection_node_->markDirty(QSGNode::DirtyGeometry);
    }

    std::shared_ptr<const QVector<ActorSprite>> sprites;
    {
        const QMutexLocker lock(&entities_mutex_);
        sprites = entity_sprites_;
    }
    if (!sprites || sprites->isEmpty() || !entities_visible_) {
        if (sprite_node_ && sprite_node_->parent()) root->removeChildNode(sprite_node_);
        return root;
    }

    const IconAtlas& atlas = markerIconAtlas();
    if (atlas.isEmpty()) return root;
    if (!sprite_node_) {
        auto* geometry = new QSGGeometry(QSGGeometry::defaultAttributes_TexturedPoint2D(), 0);
        geometry->setDrawingMode(QSGGeometry::DrawTriangles);
        geometry->setVertexDataPattern(QSGGeometry::DynamicPattern);
        sprite_node_ = new QSGGeometryNode;
        sprite_node_->setGeometry(geometry);
        sprite_node_->setFlag(QSGNode::OwnsGeometry, true);
        QSGTexture* texture = window() ? window()->createTextureFromImage(atlas.image()) : nullptr;
        if (!texture) {
            delete sprite_node_;
            sprite_node_ = nullptr;
            return root;
        }
        sprite_node_->setMaterial(new SpriteAtlasMaterial(texture));
        sprite_node_->setFlag(QSGNode::OwnsMaterial, true);
        root->appendChildNode(sprite_node_);
    }

    const qreal px_per_block = scale_ / 16.0;
    const qreal view_center_x = width() / 2.0;
    const qreal view_center_z = height() / 2.0;
    int visible_count = 0;
    for (const auto& sprite : *sprites) {
        const auto* entry = atlas.entry(sprite.icon);
        if (!entry) continue;
        const qreal x = view_center_x + (sprite.block_x / 16.0 - center_x_) * scale_;
        const qreal y = view_center_z + (sprite.block_z / 16.0 - center_z_) * scale_;
        const qreal half_w = entry->sourceSize.width() * sprite.scale / 2.0;
        const qreal half_h = entry->sourceSize.height() * sprite.scale / 2.0;
        if (x + half_w < 0.0 || x - half_w > width() || y + half_h < 0.0 || y - half_h > height()) continue;
        ++visible_count;
    }
    if (!visible_count) {
        if (sprite_node_->parent()) root->removeChildNode(sprite_node_);
        return root;
    }
    if (!sprite_node_->parent()) root->appendChildNode(sprite_node_);

    auto* geometry = sprite_node_->geometry();
    const int required_vertices = visible_count * 6;
    if (sprite_vertex_capacity_ < required_vertices) {
        geometry = new QSGGeometry(QSGGeometry::defaultAttributes_TexturedPoint2D(), required_vertices);
        geometry->setDrawingMode(QSGGeometry::DrawTriangles);
        geometry->setVertexDataPattern(QSGGeometry::DynamicPattern);
        // QSGGeometryNode owns the previous geometry and releases it itself.
        sprite_node_->setGeometry(geometry);
        sprite_vertex_capacity_ = required_vertices;
    } else {
        geometry->setVertexCount(required_vertices);
    }
    auto* vertices = geometry->vertexDataAsTexturedPoint2D();
    int vertex = 0;
    for (const auto& sprite : *sprites) {
        const auto* entry = atlas.entry(sprite.icon);
        if (!entry) continue;
        const qreal x = view_center_x + (sprite.block_x / 16.0 - center_x_) * scale_;
        const qreal y = view_center_z + (sprite.block_z / 16.0 - center_z_) * scale_;
        const qreal w = entry->sourceSize.width() * sprite.scale;
        const qreal h = entry->sourceSize.height() * sprite.scale;
        const qreal left = x - w / 2.0;
        const qreal top = y - h / 2.0;
        if (left + w < 0.0 || left > width() || top + h < 0.0 || top > height()) continue;
        const float u0 = static_cast<float>(entry->uv.left());
        const float v0 = static_cast<float>(entry->uv.top());
        const float u1 = static_cast<float>(entry->uv.right());
        const float v1 = static_cast<float>(entry->uv.bottom());
        const float l = static_cast<float>(left);
        const float t = static_cast<float>(top);
        const float r = static_cast<float>(left + w);
        const float b = static_cast<float>(top + h);
        vertices[vertex++].set(l, t, u0, v0);
        vertices[vertex++].set(r, t, u1, v0);
        vertices[vertex++].set(l, b, u0, v1);
        vertices[vertex++].set(r, t, u1, v0);
        vertices[vertex++].set(r, b, u1, v1);
        vertices[vertex++].set(l, b, u0, v1);
    }
    geometry->markVertexDataDirty();
    sprite_node_->markDirty(QSGNode::DirtyGeometry);
    return root;
}
