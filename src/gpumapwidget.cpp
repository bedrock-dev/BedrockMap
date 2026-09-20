#include "gpumapwidget.h"

#include <qcolor.h>
#include <qnamespace.h>
#include <qopengl.h>
#include <qpoint.h>
#include <qrgb.h>
#include <qtransform.h>

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QPainter>
#include <QTimer>
#include <algorithm>
#include <cmath>

#include "asynclevelloader.h"
#include "config.h"
#include "contextmenubuilder.h"
#include "loguru/loguru.hpp"
#include "mapview.h"

namespace {

    /// Floor division, so region indices stay correct west/north of the origin.
    int floorDiv(int value, int divisor) {
        const int quotient = value / divisor;
        return (value % divisor < 0) ? quotient - 1 : quotient;
    }

    int floorMod(int value, int divisor) {
        const int remainder = value % divisor;
        return remainder < 0 ? remainder + divisor : remainder;
    }

    bool isPowerOfTwo(int value) { return value > 0 && (value & (value - 1)) == 0; }

    /// Sun direction as one step of the shadow ray, matching MapTile's sunVector().
    void sunStep(int& sx, int& sy) {
        switch (constant::SUN_DIRECTION) {
            case constant::SunDir::NW:
                sx = -1;
                sy = -1;
                break;
            case constant::SunDir::NE:
                sx = 1;
                sy = -1;
                break;
            case constant::SunDir::SW:
                sx = -1;
                sy = 1;
                break;
            case constant::SunDir::SE:
                sx = 1;
                sy = 1;
                break;
        }
    }

}  // namespace

GpuMapWidget::GpuMapWidget(QWidget* parent, AsyncLevelLoader* loader, MapView* view, MapOverlays* overlays, ImportOverlay* import,
                           MapHost* host)
    : QOpenGLWidget(parent), level_loader_(loader), view_(view), overlays_(overlays), import_(import), host_(host) {
    QSurfaceFormat format;
    format.setVersion(3, 3);
    format.setProfile(QSurfaceFormat::CoreProfile);
    format.setDepthBufferSize(0);
    format.setStencilBufferSize(0);
    format.setSamples(0);  // the map is already axis aligned; MSAA buys nothing here
    setFormat(format);
    // Every frame redraws the whole viewport, so Qt's "keep what is already in
    // the FBO, repaint only the exposed region" path would only cost a blit and
    // leave stale pixels in the parts it skips.
    setUpdateBehavior(QOpenGLWidget::NoPartialUpdate);
    setFocusPolicy(Qt::StrongFocus);  // so the map can be panned/zoomed from this widget too
    // Without this the cursor readout and the import ghost would only follow a
    // drag: the CPU map enables it in its ctor too.
    setMouseTracking(true);
    // Shader tunables come from the settings; setAoStrength() overrides them for
    // the benchmarks, which need a fixed value regardless of the user's config.
    ao_strength_ = std::clamp(setting::current().GPU_AO_STRENGTH, 0.0f, 1.0f);
    bevel_strength_ = std::clamp(setting::current().GPU_BEVEL_STRENGTH, 0.0f, 1.0f);
    saturation_ = std::clamp(setting::current().GPU_SATURATION, 0.0f, 2.0f);
    brightness_ = std::clamp(setting::current().GPU_BRIGHTNESS, 0.0f, 2.0f);
    shadow_strength_ = std::clamp(setting::current().GPU_SHADOW_STRENGTH, 0.0f, 1.0f);
    uploads_.clear();
    syncSlots();

    if (level_loader_) {
        connect(level_loader_, &AsyncLevelLoader::regionReady, this, [this] {
            if (isVisible()) update();
        });
    }
    if (view_) {
        // No polling: the shared view tells us when anything about it changes.
        connect(view_, &MapView::viewChanged, this, qOverload<>(&QWidget::update));
    }
    // Input is handled here as well as on the CPU renderer; because the view is
    // shared, either widget can drive both.
    interaction_ = new MapInteraction(view_, this);
    // The status bar is driven from whichever renderer is on screen.
    connect(interaction_, &MapInteraction::cursorBlockChanged, this, &GpuMapWidget::mouseMove);

    // The debug window reports memory and per-frame counters, so it has to be
    // redrawn on a still view as well.
    debug_refresh_timer_ = new QTimer(this);
    connect(debug_refresh_timer_, &QTimer::timeout, this, [this] {
        if (overlays_ && overlays_->drawDebug()) update();
    });
    debug_refresh_timer_->start(2000);
}

void GpuMapWidget::syncSlots() { slots_.clear(); }

GpuMapWidget::~GpuMapWidget() {
    if (!context() || !context()->isValid()) return;
    makeCurrent();
    if (vao_ != 0) glDeleteVertexArrays(1, &vao_);
    GLuint textures[] = {color_texture_, height_texture_};
    if (color_texture_ != 0 || height_texture_ != 0) glDeleteTextures(2, textures);
    doneCurrent();
    delete shader_;
    shader_ = nullptr;
}

void GpuMapWidget::setCaptureView(const QPointF& center_block, double px_per_block) {
    capture_view_ = true;
    capture_center_block_ = center_block;
    capture_px_per_block_ = std::max(0.01, px_per_block);
    update();
}

QImage GpuMapWidget::captureOffscreen(const QSize& size, const QPointF& center_block, double px_per_block) {
    resize(size);
    // A capture is a scoped override, not a mode: leaving capture_view_ set would
    // keep the widget on the synthetic view (and without overlays) for every
    // later frame, which is not what a caller asking for one frame expects.
    const bool was_capturing = capture_view_;
    const QPointF previous_center = capture_center_block_;
    const double previous_px = capture_px_per_block_;
    setCaptureView(center_block, px_per_block);
    // Keep painting until the currently visible upload queue is drained. Region
    // bakes can finish through queued Qt signals, so give those events a small
    // chance to run between frames while keeping a hard bound for callers.
    QImage image;
    for (int frame = 0; frame < 128; ++frame) {
        image = grabFramebuffer();
        if (uploads_.empty()) break;
        QCoreApplication::processEvents(QEventLoop::AllEvents, 1);
    }
    capture_view_ = was_capturing;
    capture_center_block_ = previous_center;
    capture_px_per_block_ = previous_px;
    return image;
}

void GpuMapWidget::mousePressEvent(QMouseEvent* event) {
    if (interaction_) interaction_->mousePress(event);
}

void GpuMapWidget::mouseMoveEvent(QMouseEvent* event) {
    // The import overlay owns the pointer while it is placing a structure, and
    // it resolves the cursor with this widget's own transform.
    if (import_ && import_->active() && view_ && !(event->buttons() & Qt::LeftButton)) {
        import_->handleMouseMove(view_->chunkPosAt(event->position(), size()));
        update();
        return;
    }
    if (interaction_) interaction_->mouseMove(event);
}

void GpuMapWidget::mouseReleaseEvent(QMouseEvent* event) {
    if (import_ && import_->active()) {
        if (event->button() == Qt::LeftButton && !import_->placed()) {
            import_->handleLeftClick();
            update();
            return;
        }
        if (event->button() == Qt::RightButton && import_->handleRightClick()) {
            update();
            return;
        }
    }
    // Right-click opens the same menu the CPU map shows. The two maps have
    // different pixel sizes, so the click is resolved with this widget's own
    // transform before being handed to the shared menu.
    if (event->button() == Qt::RightButton && host_ && view_ && !capture_view_) {
        const QPointF pos = event->position();
        const QPoint global_pos = mapToGlobal(pos.toPoint());
        const bl::chunk_pos chunk = view_->chunkPosAt(pos, size());
        const QPoint block = view_->blockPosAt(pos, size());
        ContextMenuBuilder::show(host_, MapMenuRequest{global_pos, chunk, bl::block_pos(block.x(), 0, block.y()), view_->dim(), this});
        event->accept();
        return;
    }
    if (interaction_) interaction_->mouseRelease(event);
}

void GpuMapWidget::wheelEvent(QWheelEvent* event) {
    if (interaction_) interaction_->wheel(event);
}

void GpuMapWidget::keyPressEvent(QKeyEvent* event) {
    if (import_ && import_->handleKeyPress(event->key())) {
        update();
        event->accept();
        return;
    }
    if (interaction_ && interaction_->keyPress(event)) return;
    QOpenGLWidget::keyPressEvent(event);
}

void GpuMapWidget::keyReleaseEvent(QKeyEvent* event) {
    if (interaction_ && interaction_->keyRelease(event)) return;
    QOpenGLWidget::keyReleaseEvent(event);
}

void GpuMapWidget::focusOutEvent(QFocusEvent* event) {
    if (interaction_) interaction_->reset();  // a pan key held while losing focus never gets its release
    QOpenGLWidget::focusOutEvent(event);
}

void GpuMapWidget::setShadowEnabled(bool enabled) {
    shadow_enabled_ = enabled;
    update();
}

void GpuMapWidget::setShadowSteps(int steps) {
    shadow_steps_ = std::clamp(steps, 1, 128);
    update();
}

void GpuMapWidget::setPenumbra(float penumbra) {
    penumbra_ = std::clamp(penumbra, 0.0f, 2.0f);
    update();
}

void GpuMapWidget::setShadowStrength(float strength) {
    shadow_strength_ = std::clamp(strength, 0.0f, 1.0f);
    update();
}

void GpuMapWidget::setAoStrength(float strength) {
    ao_strength_ = std::clamp(strength, 0.0f, 1.0f);
    update();
}

void GpuMapWidget::setBevelStrength(float strength) {
    bevel_strength_ = std::clamp(strength, 0.0f, 1.0f);
    update();
}

void GpuMapWidget::setSaturation(float saturation) {
    saturation_ = std::clamp(saturation, 0.0f, 2.0f);
    update();
}

void GpuMapWidget::setBrightness(float brightness) {
    brightness_ = std::clamp(brightness, 0.0f, 2.0f);
    update();
}

void GpuMapWidget::setAoMarch(int directions, int steps) {
    // The shader's loops are bounded by MAX_AO_DIRECTIONS / MAX_AO_STEPS.
    ao_directions_ = std::clamp(directions, 1, 16);
    ao_steps_ = std::clamp(steps, 1, 32);
    update();
}

void GpuMapWidget::setFlatShading(bool flat) {
    flat_shading_ = flat;
    update();
}

void GpuMapWidget::initializeGL() {
    initializeOpenGLFunctions();
    glClearColor(0.08f, 0.08f, 0.09f, 1.0f);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);

    shader_ = new QOpenGLShaderProgram(this);
    shader_->addShaderFromSourceFile(QOpenGLShader::Vertex, ":/res/shaders/map2d.vert");
    shader_->addShaderFromSourceFile(QOpenGLShader::Fragment, ":/res/shaders/map2d.frag");
    if (!shader_->link()) {
        LOG_F(ERROR, "Can not link map2d shader: %s", shader_->log().toStdString().c_str());
        return;
    }

    // Core profile requires a bound VAO even when the vertex stage reads nothing.
    glGenVertexArrays(1, &vao_);

    glGenTextures(1, &color_texture_);
    glBindTexture(GL_TEXTURE_2D, color_texture_);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, kAtlasTexels, kAtlasTexels, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

    glGenTextures(1, &height_texture_);
    glBindTexture(GL_TEXTURE_2D, height_texture_);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    // World heights are integral and fit exactly in half precision for all
    // supported Bedrock dimensions. This halves height-atlas bandwidth/memory.
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RG16F, kAtlasTexels, kAtlasTexels, 0, GL_RG, GL_FLOAT, nullptr);

    initAtlasTextures();
    LOG_F(INFO, "GpuMapWidget: atlas %dx%d texels, %d region slots at 1 texel/block", kAtlasTexels, kAtlasTexels,
          slotsPerSide() * slotsPerSide());
}
void GpuMapWidget::initAtlasTextures() {
    // Startup only: the texture contents are undefined until this runs, and the
    // per-slot fill is only affordable when there is nothing else to do.
    buildBlankTiles();
    const int side = slotsPerSide();
    const int texels = kRegionBlocks / blocks_per_texel_;
    for (int slot = 0; slot < side * side; ++slot) {
        const int slot_x = (slot % side) * texels;
        const int slot_z = (slot / side) * texels;

        glBindTexture(GL_TEXTURE_2D, color_texture_);
        glTexSubImage2D(GL_TEXTURE_2D, 0, slot_x, slot_z, texels, texels, GL_RGBA, GL_UNSIGNED_BYTE, blank_light_.data());
        height_buffer_.assign(static_cast<size_t>(texels) * texels * 2, kVoidHeight);
        glBindTexture(GL_TEXTURE_2D, height_texture_);
        glTexSubImage2D(GL_TEXTURE_2D, 0, slot_x, slot_z, texels, texels, GL_RG, GL_FLOAT, height_buffer_.data());
    }
    slots_.clear();
}

void GpuMapWidget::invalidateAtlas() {
    // The blank tiles are sized in texels for the current resolution, so they
    // have to be rebuilt before anything is uploaded at the new one.
    buildBlankTiles();
    // Bookkeeping only - deliberately no texture writes. The old contents stay
    // in place, but every slot is unassigned and all visible slots are uploaded
    // before the next draw, so stale pixels cannot survive a resolution change.
    slots_.clear();
    uploads_.clear();
}

int GpuMapWidget::residentRegionCount() const {
    int count = 0;
    for (const auto& [slot, state] : slots_) {
        (void)slot;
        if (state.assigned && state.source != nullptr) ++count;
    }
    return count;
}

void GpuMapWidget::queueUpload(UploadRequest request) {
    for (auto& queued : uploads_) {
        if (queued.slot == request.slot) {
            // A pan/zoom can change the region mapped to a recycled slot while
            // the old request is waiting. Keep only the newest request.
            queued = request;
            return;
        }
    }
    uploads_.push_back(request);
}

void GpuMapWidget::buildBlankTiles() {
    // Same 2x2 chessboard the CPU map uses for regions without terrain
    // (MapTile::NULL_REGION_TILE / UNLOADED_REGION_TILE). The cell is 64 blocks
    // and a region is 128, so the pattern lines up across region borders.
    const int texels = kRegionBlocks / blocks_per_texel_;
    const int cell = std::max(1, 64 / blocks_per_texel_);
    const auto fill = [texels, cell](std::vector<unsigned char>& out, int shade_even, int shade_odd) {
        out.resize(static_cast<size_t>(texels) * texels * 4);
        for (int z = 0; z < texels; ++z) {
            for (int x = 0; x < texels; ++x) {
                const int shade = ((x / cell + z / cell) % 2 == 0) ? shade_even : shade_odd;
                const size_t i = (static_cast<size_t>(z) * texels + x) * 4;
                out[i + 0] = out[i + 1] = out[i + 2] = static_cast<unsigned char>(shade);
                out[i + 3] = 255;
            }
        }
    };
    fill(blank_dark_, 20, 40);     // region loaded, but it holds no chunks
    fill(blank_light_, 128, 148);  // region not baked yet
}

void GpuMapWidget::uploadRegion(const UploadRequest& request) {
    // RegionCacheManager owns the pointed-to object and may evict it between
    // frames. Re-resolve immediately before dereferencing it so delayed work
    // never reads a dangling or superseded ChunkRegion.
    const ChunkRegion* data = nullptr;
    const auto state = level_loader_->regionState(request.region, &data);
    // Each atlas texel covers blocks_per_texel_ x blocks_per_texel_ blocks; at
    // 1 it is the raw bake, above that the tile is reduced so a wider view still
    // fits in one atlas.
    const int bp = blocks_per_texel_;
    const int n = kRegionBlocks / bp;

    if (state != AsyncLevelLoader::RegionState::Ready || !data) {
        color_buffer_ = (state == AsyncLevelLoader::RegionState::Empty) ? blank_dark_ : blank_light_;
        height_buffer_.assign(static_cast<size_t>(n) * n * 2, kVoidHeight);
        // The upload below reads n*n texels from the front of the buffer, so a
        // tile built for a different resolution would be a garbled sub-rectangle
        // of the right pattern rather than a smaller checkerboard.
        Assert(color_buffer_.size() == static_cast<size_t>(n) * n * 4, "GpuMapWidget",
               "blank tile size does not match the atlas resolution (buildBlankTiles must run on every "
               "blocks_per_texel_ change)");
    } else {
        // Biome colours are baked by the loader just like the CPU map's biome
        // layer. They are already final display colours, so do not tint or mix
        // water into them again in this renderer.
        const bool biome_layer = view_ && view_->options().layer == RenderOption::Biome;
        const QImage& source = biome_layer ? data->biome_bake_image_ : data->flat_color_image_;
        const auto& tips = data->tips_info_;
        color_buffer_.resize(static_cast<size_t>(n) * n * 4);
        height_buffer_.resize(static_cast<size_t>(n) * n * 2);
        for (int tz = 0; tz < n; ++tz) {
            for (int tx = 0; tx < n; ++tx) {
                // Representative column of this texel: the tallest one, so a
                // structure is not hidden behind the flat ground next to it.
                int best_block_x = tx * bp;
                int best_block_z = tz * bp;
                int best_top = INT_MIN;
                for (int bz = 0; bz < bp; ++bz) {
                    for (int bx = 0; bx < bp; ++bx) {
                        const auto& candidate = tips[tx * bp + bx][tz * bp + bz];
                        if (candidate.height > best_top) {
                            best_top = candidate.height;
                            best_block_x = tx * bp + bx;
                            best_block_z = tz * bp + bz;
                        }
                    }
                }
                const auto& info = tips[best_block_x][best_block_z];
                const QRgb pixel = reinterpret_cast<const QRgb*>(source.constScanLine(best_block_z))[best_block_x];
                int r = qRed(pixel);
                int g = qGreen(pixel);
                int b = qBlue(pixel);
                const bool water = !biome_layer && info.water_surface_color != 0;
                if (water) {
                    // Same depth curve applyWaterOverlay() uses, but into the
                    // upload buffer so the shared region image stays untouched.
                    const float depth = static_cast<float>(info.height - info.solid_height);
                    const float opacity = waterSurfaceOpacity(depth);
                    r = static_cast<int>((1.0f - opacity) * r + opacity * qRed(info.water_surface_color));
                    g = static_cast<int>((1.0f - opacity) * g + opacity * qGreen(info.water_surface_color));
                    b = static_cast<int>((1.0f - opacity) * b + opacity * qBlue(info.water_surface_color));
                }
                const size_t color_index = (static_cast<size_t>(tz) * n + tx) * 4;
                color_buffer_[color_index + 0] = static_cast<unsigned char>(std::clamp(r, 0, 255));
                color_buffer_[color_index + 1] = static_cast<unsigned char>(std::clamp(g, 0, 255));
                color_buffer_[color_index + 2] = static_cast<unsigned char>(std::clamp(b, 0, 255));
                color_buffer_[color_index + 3] = water ? 255 : 0;

                // Heights keep their world values, so the shader's ray stays in
                // blocks; reducing the tile only coarsens the detail.
                const size_t height_index = (static_cast<size_t>(tz) * n + tx) * 2;
                height_buffer_[height_index + 0] = info.solid_height <= -128 ? kVoidHeight : static_cast<float>(info.solid_height);
                height_buffer_[height_index + 1] = info.height <= -128 ? kVoidHeight : static_cast<float>(info.height);
            }
        }
    }

    const int side = slotsPerSide();
    const int slot_x = (request.slot % side) * n;
    const int slot_z = (request.slot / side) * n;
    glBindTexture(GL_TEXTURE_2D, color_texture_);
    glTexSubImage2D(GL_TEXTURE_2D, 0, slot_x, slot_z, n, n, GL_RGBA, GL_UNSIGNED_BYTE, color_buffer_.data());
    glBindTexture(GL_TEXTURE_2D, height_texture_);
    glTexSubImage2D(GL_TEXTURE_2D, 0, slot_x, slot_z, n, n, GL_RG, GL_FLOAT, height_buffer_.data());

    slots_[request.slot] = SlotState{request.region, state, data, blocks_per_texel_, true};
    ++total_uploads_;
}

void GpuMapWidget::collectVisibleRegions(double px_per_block, const QPointF& world_origin) {
    const double world_width = width() / px_per_block;
    const double world_height = height() / px_per_block;
    // The ray has to read occluders outside the viewport, so keep a margin
    // resident: it is what keeps shadows correct right up to the screen edge.
    // Bevel and AO sample one neighbouring atlas texel even with shadows off.
    // Keep that neighbourhood resident too; otherwise a recycled slot can leak
    // stale terrain into the viewport edge.
    const double shading_margin = flat_shading_ ? 0.0 : static_cast<double>(blocks_per_texel_);
    const double margin = std::max<double>(shadowReachBlocks(), shading_margin);

    const double min_x = world_origin.x() - margin;
    const double max_x = world_origin.x() + world_width + margin;
    const double min_z = world_origin.y() - world_height - margin;
    const double max_z = world_origin.y() + margin;

    const int rx0 = floorDiv(static_cast<int>(std::floor(min_x)), kRegionBlocks);
    const int rx1 = floorDiv(static_cast<int>(std::floor(max_x)), kRegionBlocks);
    const int rz0 = floorDiv(static_cast<int>(std::floor(min_z)), kRegionBlocks);
    const int rz1 = floorDiv(static_cast<int>(std::floor(max_z)), kRegionBlocks);

    visible_regions_ = 0;
    const int dim = view_ ? view_->dim() : 0;
    const int side = slotsPerSide();
    // Discard uploads made irrelevant by a pan/zoom before spending any GPU
    // bandwidth on them. Without this, rapid movement can accumulate a queue
    // of off-screen regions and stall the next frame.
    uploads_.erase(std::remove_if(uploads_.begin(), uploads_.end(),
                                  [&](const UploadRequest& request) {
                                      return request.region.dim != dim || request.region.x < rx0 * constant::RW ||
                                             request.region.x > rx1 * constant::RW || request.region.z < rz0 * constant::RW ||
                                             request.region.z > rz1 * constant::RW;
                                  }),
                   uploads_.end());
    // Same as the CPU map: without this the scheduler has no viewport, so it
    // cannot prioritise - or prune to - what is on screen.
    level_loader_->setRenderViewport({rx0 * constant::RW, rz0 * constant::RW, dim}, {rx1 * constant::RW, rz1 * constant::RW, dim});
    // The slot mapping is modulo the atlas, so it only works while the requested
    // range fits in it. That holds by construction now (see blocksPerTexelFor);
    // this assertion is here to make a regression loud rather than silent, since
    // the symptom is otherwise just flickering edges.
    Assert(rx1 - rx0 < side && rz1 - rz0 < side, "GpuMapWidget",
           "visible region range exceeds the atlas; blocksPerTexelFor returned a level that does not fit");
    for (int rz = rz0; rz <= rz1; ++rz) {
        for (int rx = rx0; rx <= rx1; ++rx) {
            ++visible_regions_;
            const bl::chunk_pos region{rx * constant::RW, rz * constant::RW, dim};
            const int slot = floorMod(rz, side) * side + floorMod(rx, side);
            const ChunkRegion* data = nullptr;
            const auto now = level_loader_->regionState(region, &data);
            const auto state_it = slots_.find(slot);
            const SlotState* state = state_it == slots_.end() ? nullptr : &state_it->second;
            // A slot showing a different region, content uploaded at a different
            // atlas resolution, or an older state for this one has to be
            // re-uploaded - including when a bake finishes and replaces the
            // background, and when an edit re-bakes the region (new object).
            const bool up_to_date = state && state->assigned && state->bp == blocks_per_texel_ && state->region == region &&
                                    state->state == now && (now != AsyncLevelLoader::RegionState::Ready || state->source == data);
            if (up_to_date) continue;
            queueUpload({region, slot});
        }
    }
}

int GpuMapWidget::shadowReachBlocks() const {
    // The shader marches in half-texel steps within a 256-iteration bound, so it
    // covers this many blocks at every resolution the atlas uses.
    return shadow_enabled_ ? shadow_steps_ : 0;
}

// Slots along one atlas edge at a given resolution: the atlas is a fixed number
// of texels and a region covers kRegionBlocks blocks, so the slot count grows
// with blocks-per-texel.
static int slotsPerSideFor(int blocks_per_texel) {
    const int texels_per_region = GpuMapWidget::kRegionBlocks / std::max(1, blocks_per_texel);
    return GpuMapWidget::kAtlasTexels / std::max(1, texels_per_region);
}

int GpuMapWidget::regionsSpannedBy(const QSize& viewport, double px_per_block) const {
    // Worst case across both axes: the span in blocks plus the one extra region
    // that floor()-ing the two edge regions can add. Deriving the requirement
    // from the region count (rather than from a block-span proxy) is what keeps
    // this exactly consistent with the range collectVisibleRegions() walks.
    const double shading_margin = flat_shading_ ? 0.0 : static_cast<double>(blocks_per_texel_);
    const double reach = std::max<double>(shadowReachBlocks(), shading_margin);
    const double span = std::max(viewport.width() / px_per_block, viewport.height() / px_per_block) + 2.0 * reach;
    return static_cast<int>(std::ceil(span / kRegionBlocks)) + 1;
}

int GpuMapWidget::blocksPerTexelFor(const QSize& viewport, double px_per_block) const {
    // 128 blocks per texel is the coarsest possible level: a region tile would
    // otherwise be smaller than one texel.
    constexpr int kMaxBlocksPerTexel = kRegionBlocks;
    const int needed_slots = regionsSpannedBy(viewport, px_per_block);

    int minimal = 1;
    while (minimal < kMaxBlocksPerTexel && slotsPerSideFor(minimal) < needed_slots) minimal *= 2;

    if (blocks_per_texel_ < minimal) return minimal;

    // Going back to a finer level rebuilds the atlas, so only do it with room to
    // spare. Without the margin, scrolling in and out across a band boundary
    // rebuilds on every wheel step; the floor above is what keeps this from
    // overshooting into a level that no longer covers the view.
    constexpr double kShrinkMargin = 1.4;
    const int finer = blocks_per_texel_ / 2;
    if (finer >= minimal && static_cast<double>(slotsPerSideFor(finer)) >= needed_slots * kShrinkMargin) return finer;
    return blocks_per_texel_;
}

int GpuMapWidget::blocksPerTexelFor(double px_per_block) const { return blocksPerTexelFor(size(), px_per_block); }

void GpuMapWidget::paintGL() {
    QElapsedTimer frame_timer;
    frame_timer.start();

    if (!capture_view_ && view_) {
        // Re-asserted here as well as on resize, because a widget painted before
        // it was ever shown has no resize event yet - the CPU renderer does the
        // same, and with one renderer per view there is no ambiguity about which
        // size the camera belongs to.
        view_->setViewportSize(size());
        // The zoom floor depends on whether the chunk coordinate index is loaded,
        // which only the loader knows.
        view_->applyConfiguredZoomLimits(level_loader_ && level_loader_->preloadAllChunkCoords());
    }

    QPainter painter(this);
    painter.beginNativePainting();

    glClearColor(0.08f, 0.08f, 0.09f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    bool ready = level_loader_ && level_loader_->isOpen() && shader_ && shader_->isLinked() && color_texture_ != 0;
    double px_per_block = 0.0;
    QPointF world_origin;

    if (ready) {
        const int dim = view_ ? view_->dim() : 0;
        if (dim != atlas_dim_) {
            blocks_per_texel_ = 1;
            syncSlots();
            invalidateAtlas();
            atlas_dim_ = dim;
        }

        // A region is the same object in both layers, so source-pointer based
        // slot validation alone cannot see this change. Drop the resident
        // texels and refill the visible slots with the selected base layer.
        const bool biome_layer = view_ && view_->options().layer == RenderOption::Biome;
        if (biome_layer != atlas_biome_layer_) {
            atlas_biome_layer_ = biome_layer;
            invalidateAtlas();
        }

        // Same place and scale as every other renderer on this view.
        double map_px_per_block = capture_px_per_block_;
        QPointF world_center = capture_center_block_;
        if (!capture_view_ && view_) {
            map_px_per_block = view_->scaleLevel() / 16.0;
            const QSize viewport = view_->viewportSize();
            const QPointF center = view_->worldToView().inverted().map(QPointF(viewport.width() / 2.0, viewport.height() / 2.0));
            world_center = center * 16.0;  // chunk units -> blocks
        }
        px_per_block = std::max(0.01, map_px_per_block);

        // Widening the view past one texel per block reduces the atlas
        // resolution instead of refusing to zoom out, which is what keeps this
        // renderer usable across the whole zoom range.
        const int wanted_bp = blocksPerTexelFor(px_per_block);
        if (wanted_bp != blocks_per_texel_) {
            blocks_per_texel_ = wanted_bp;
            syncSlots();
            invalidateAtlas();
        }

        const double half_w = width() / 2.0 / px_per_block;
        const double half_h = height() / 2.0 / px_per_block;
        // gl_FragCoord (0,0) is the bottom-left of the widget, and world z grows
        // downwards on screen, so the bottom edge carries the larger z.
        world_origin = QPointF(world_center.x() - half_w, world_center.y() + half_h);

        collectVisibleRegions(px_per_block, world_origin);

        // Every queued request describes a visible slot whose previous contents
        // belong to another region (or another LOD). Upload all of them before
        // drawing; throttling background tiles would display stale terrain.
        uploaded_last_frame_ = 0;
        for (auto it = uploads_.begin(); it != uploads_.end();) {
            uploadRegion(*it);
            ++uploaded_last_frame_;
            it = uploads_.erase(it);
        }
    }

    if (ready) {
        int sx = -1;
        int sy = -1;
        sunStep(sx, sy);
        const double dpr = devicePixelRatioF();
        const double device_px_per_block = px_per_block * dpr;
        const double texel_px = device_px_per_block * blocks_per_texel_;

        shader_->bind();
        shader_->setUniformValue("uColor", 0);
        shader_->setUniformValue("uHeight", 1);
        shader_->setUniformValue("uViewOrigin", static_cast<float>(world_origin.x()), static_cast<float>(world_origin.y()));
        shader_->setUniformValue("uPxPerBlock", static_cast<float>(device_px_per_block));
        shader_->setUniformValue("uAtlasTexels", static_cast<float>(kAtlasTexels));
        shader_->setUniformValue("uBlocksPerTexel", static_cast<float>(blocks_per_texel_));
        shader_->setUniformValue("uSunStep", static_cast<float>(sx), static_cast<float>(sy));
        shader_->setUniformValue("uShadowDarkness", 1.0f - std::clamp(setting::current().SHADOW_LEVEL, 0, 255) / 255.0f * 0.75f);
        shader_->setUniformValue("uShadowStrength", shadow_strength_);
        shader_->setUniformValue("uShadowReach", static_cast<float>(shadowReachBlocks()));
        shader_->setUniformValue("uEdgeWidth", static_cast<float>(std::clamp(std::max(0.25, 1.0 / texel_px), 0.25, 0.5)));
        shader_->setUniformValue("uPenumbra", penumbra_);
        shader_->setUniformValue("uAoStrength", ao_strength_);
        shader_->setUniformValue("uAoDirections", ao_directions_);
        shader_->setUniformValue("uAoSteps", ao_steps_);
        shader_->setUniformValue("uAoStep0", ao_step0_);
        shader_->setUniformValue("uAoRadius", ao_radius_);
        shader_->setUniformValue("uBevelStrength", bevel_strength_);
        shader_->setUniformValue("uSaturation", saturation_);
        shader_->setUniformValue("uBrightness", brightness_);
        // Biome bake is a categorical visualisation, not terrain material:
        // height-driven bevel, AO, shadows, and water treatment must not alter it.
        const bool biome_layer = view_ && view_->options().layer == RenderOption::Biome;
        shader_->setUniformValue("uFlatShading", (flat_shading_ || biome_layer) ? 1.0f : 0.0f);

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, color_texture_);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, height_texture_);

        glBindVertexArray(vao_);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        glBindVertexArray(0);
        // Command submission is asynchronous, so without this the timing only
        // covers the CPU side of the frame.
        if (gpu_timing_) glFinish();
        shader_->release();
    }

    painter.endNativePainting();

    // --- overlays ---
    // The same MapOverlays the CPU map uses, so the two renderers cannot drift
    // apart. They are QPainter work on top of the GL pass, which is why they need
    // the widget's own transform rather than the view's: this widget may be a
    // different size from the one the view was sized for.
    if (overlays_ && view_ && !capture_view_) {
        const QTransform world_to_view = view_->transformForViewport(size());
        overlays_->setTransform(world_to_view);
        overlays_->setScreenInset(kStatsBarHeight);

        painter.setTransform(world_to_view);
        const RenderOption& options = view_->options();
        if (options.getOther(RenderOption::HSA)) overlays_->drawHSAs(&painter);
        if (options.getOther(RenderOption::Village)) overlays_->drawVillages(&painter);
        if (options.getOther(RenderOption::SlimeChunk)) overlays_->drawSlimeChunks(&painter);
        if (options.getOther(RenderOption::Grid)) overlays_->drawGrid(&painter);
        if (view_->selectionVisible()) overlays_->drawSelection(&painter);
        overlays_->drawOpenedChunk(&painter);
        // The placement ghost is in the same world space as those layers.
        if (import_ && import_->active()) import_->draw(&painter, view_->scale());
        painter.resetTransform();
        if (options.getOther(RenderOption::Actors)) overlays_->drawActors(&painter);
        if (options.getOther(RenderOption::Coords)) overlays_->drawChunkPosText(&painter);
        overlays_->drawCoordsMiniMap(&painter);
        overlays_->drawDebugWindow(&painter);
    }

    last_frame_ms_ = frame_timer.nsecsElapsed() / 1.0e6;
    drawStats(painter, last_frame_ms_, px_per_block);
    painter.end();

    // Draining the upload queue over several frames keeps a newly revealed area
    // from stalling a single frame.
    if (ready && !uploads_.empty()) update();
}

void GpuMapWidget::drawStats(QPainter& painter, double frame_ms, double px_per_block) {
    if (width() < 160 || height() < 60) return;
    const QString stats = tr("GPU map  %1 ms  %2 px/block  texel %3 blk  regions %4  uploads %5  ao %6")
                              .arg(QString::number(frame_ms, 'f', 1), QString::number(px_per_block, 'f', 2))
                              .arg(blocks_per_texel_)
                              .arg(visible_regions_)
                              .arg(uploaded_last_frame_)
                              .arg(QString::number(static_cast<double>(ao_strength_), 'f', 2));
    painter.setPen(QColor(235, 235, 235));
    painter.fillRect(QRect(0, 0, width(), kStatsBarHeight), QColor(22, 22, 22, 170));
    painter.drawText(QRect(4, 0, width() - 8, kStatsBarHeight), Qt::AlignVCenter | Qt::AlignLeft, stats);
}
