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
#include <utility>

#include "asynclevelloader.h"
#include "color.h"
#include "config.h"
#include "contextmenubuilder.h"
#include "loguru/loguru.hpp"
#include "mapview.h"

namespace {

    constexpr double ISO_HEIGHT_REFERENCE = 80.0;
    constexpr double ISO_RAY_TOP = 384.0;
    constexpr double ISO_RAY_BOTTOM = -128.0;
    constexpr double INV_SQRT2 = 0.7071067811865476;
    constexpr double SQRT_THREE_HALVES = 1.224744871391589;

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
    // Redraw the complete viewport every frame.
    setUpdateBehavior(QOpenGLWidget::NoPartialUpdate);
    setFocusPolicy(Qt::StrongFocus);  // so the map can be panned/zoomed from this widget too
    // Keep cursor and import overlays responsive without a drag.
    setMouseTracking(true);
    gpu_options_.orthographic_view = setting::current().GPU_ORTHOGRAPHIC_VIEW;
    // Initialize shader tunables from the runtime settings.
    gpu_options_.ao_strength = std::clamp(setting::current().GPU_AO_STRENGTH, 0.0f, 1.0f);
    gpu_options_.bevel_strength = std::clamp(setting::current().GPU_BEVEL_STRENGTH, 0.0f, 1.0f);
    gpu_options_.bevel_width = std::clamp(setting::current().GPU_BEVEL_WIDTH, 0.25f, 2.0f);
    gpu_options_.saturation = std::clamp(setting::current().GPU_SATURATION, 0.0f, 2.0f);
    gpu_options_.brightness = std::clamp(setting::current().GPU_BRIGHTNESS, 0.0f, 2.0f);
    gpu_options_.shadow_strength = std::clamp(setting::current().GPU_SHADOW_STRENGTH, 0.0f, 1.0f);
    uploads_.clear();
    syncSlots();

    if (level_loader_) {
        connect(level_loader_, &AsyncLevelLoader::regionReady, this, [this] {
            if (isVisible()) update();
        });
    }
    if (view_) {
        connect(view_, &MapView::viewChanged, this, qOverload<>(&QWidget::update));
    }
    interaction_ = new MapInteraction(view_, this);
    connect(interaction_, &MapInteraction::cursorBlockChanged, this, &GpuMapWidget::mouseMove);

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
    GLuint textures[] = {color_texture_, height_texture_, material_texture_, biome_palette_texture_};
    glDeleteTextures(4, textures);
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
    const QSize previous_size = this->size();
    resize(size);
    // Capture temporarily overrides the live view.
    const bool was_capturing = capture_view_;
    const QPointF previous_center = capture_center_block_;
    const double previous_px = capture_px_per_block_;
    setCaptureView(center_block, px_per_block);
    // Drain visible uploads while allowing queued region completions to run.
    QImage image;
    for (int frame = 0; frame < 128; ++frame) {
        image = grabFramebuffer();
        if (uploads_.empty() && !visible_region_data_pending_) break;
        QCoreApplication::processEvents(QEventLoop::AllEvents, 1);
    }
    capture_view_ = was_capturing;
    capture_center_block_ = previous_center;
    capture_px_per_block_ = previous_px;
    // Restore the live widget geometry after the offscreen capture.
    if (this->size() != previous_size) resize(previous_size);
    return image;
}

void GpuMapWidget::mousePressEvent(QMouseEvent* event) {
    if (gpu_options_.orthographic_view) {
        if (event->button() == Qt::LeftButton) {
            orthographic_drag_pos_ = event->position();
            orthographic_dragging_ = true;
            event->accept();
        }
        return;
    }
    if (interaction_) interaction_->mousePress(event);
}

void GpuMapWidget::mouseMoveEvent(QMouseEvent* event) {
    if (gpu_options_.orthographic_view) {
        if (orthographic_dragging_ && (event->buttons() & Qt::LeftButton) && view_) {
            const QPointF delta = event->position() - orthographic_drag_pos_;
            orthographic_drag_pos_ = event->position();
            const double scale = std::abs(view_->scale());
            if (scale > 0.0) {
                // Map screen-space drag axes back to world x/z.
                view_->translate(
                    QPointF(INV_SQRT2 * delta.x() + SQRT_THREE_HALVES * delta.y(), -INV_SQRT2 * delta.x() + SQRT_THREE_HALVES * delta.y()) /
                    scale);
            }
        }
        event->accept();
        return;
    }
    // Let the import overlay resolve the cursor while placing a structure.
    if (import_ && import_->active() && view_ && !(event->buttons() & Qt::LeftButton)) {
        import_->handleMouseMove(view_->chunkPosAt(event->position(), size()));
        update();
        return;
    }
    if (interaction_) interaction_->mouseMove(event);
}

void GpuMapWidget::mouseReleaseEvent(QMouseEvent* event) {
    if (gpu_options_.orthographic_view) {
        if (event->button() == Qt::LeftButton) orthographic_dragging_ = false;
        event->accept();
        return;
    }
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
    // Resolve the click with this widget's transform before opening the shared menu.
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
    if (gpu_options_.orthographic_view) {
        if (view_ && event->angleDelta().y() != 0)
            view_->zoomToAdjacentLevel(event->angleDelta().y() > 0 ? 1 : -1, QPointF(width() / 2.0, height() / 2.0));
        event->accept();
        return;
    }
    if (interaction_) interaction_->wheel(event);
}

void GpuMapWidget::keyPressEvent(QKeyEvent* event) {
    if (gpu_options_.orthographic_view) {
        QOpenGLWidget::keyPressEvent(event);
        return;
    }
    if (import_ && import_->handleKeyPress(event->key())) {
        update();
        event->accept();
        return;
    }
    if (interaction_ && interaction_->keyPress(event)) return;
    QOpenGLWidget::keyPressEvent(event);
}

void GpuMapWidget::keyReleaseEvent(QKeyEvent* event) {
    if (gpu_options_.orthographic_view) {
        QOpenGLWidget::keyReleaseEvent(event);
        return;
    }
    if (interaction_ && interaction_->keyRelease(event)) return;
    QOpenGLWidget::keyReleaseEvent(event);
}

void GpuMapWidget::focusOutEvent(QFocusEvent* event) {
    orthographic_dragging_ = false;
    if (interaction_) interaction_->reset();  // a pan key held while losing focus never gets its release
    QOpenGLWidget::focusOutEvent(event);
}

void GpuMapWidget::setGpuOptions(const GpuRenderOptions& options) {
    gpu_options_ = options;
    gpu_options_.shadow_steps = std::clamp(gpu_options_.shadow_steps, 1, 128);
    gpu_options_.ao_strength = std::clamp(gpu_options_.ao_strength, 0.0f, 1.0f);
    gpu_options_.ao_directions = std::clamp(gpu_options_.ao_directions, 1, 16);
    gpu_options_.ao_steps = std::clamp(gpu_options_.ao_steps, 1, 32);
    gpu_options_.bevel_strength = std::clamp(gpu_options_.bevel_strength, 0.0f, 1.0f);
    gpu_options_.bevel_width = std::clamp(gpu_options_.bevel_width, 0.25f, 2.0f);
    gpu_options_.saturation = std::clamp(gpu_options_.saturation, 0.0f, 2.0f);
    gpu_options_.brightness = std::clamp(gpu_options_.brightness, 0.0f, 2.0f);
    gpu_options_.shadow_strength = std::clamp(gpu_options_.shadow_strength, 0.0f, 1.0f);
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
    shader_->addShaderFromSourceFile(QOpenGLShader::Fragment,
                                     gpu_options_.orthographic_view ? ":/res/shaders/map_orthographic.frag" : ":/res/shaders/map2d.frag");
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
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, ATLAS_TEXELS, ATLAS_TEXELS, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

    glGenTextures(1, &height_texture_);
    glBindTexture(GL_TEXTURE_2D, height_texture_);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    // World heights are integral and fit exactly in half precision for all
    // supported Bedrock dimensions. This halves height-atlas bandwidth/memory.
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RG16F, ATLAS_TEXELS, ATLAS_TEXELS, 0, GL_RG, GL_FLOAT, nullptr);

    glGenTextures(1, &material_texture_);
    glBindTexture(GL_TEXTURE_2D, material_texture_);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RG8, ATLAS_TEXELS, ATLAS_TEXELS, 0, GL_RG, GL_UNSIGNED_BYTE, nullptr);

    const auto water_base = bl::get_block_color("minecraft:water");
    gpu_options_.water_base_color = {water_base.r / 255.0f, water_base.g / 255.0f, water_base.b / 255.0f};
    const QColor configured_grass_color(setting::current().GPU_GRASS_HEIGHT_COLOR);
    const QColor grass_color = configured_grass_color.isValid() ? configured_grass_color : QColor(Qt::red);
    gpu_options_.grass_height_color = {grass_color.redF(), grass_color.greenF(), grass_color.blueF()};
    gpu_options_.grass_height_enabled = setting::current().GPU_GRASS_HEIGHT_ENABLED;
    gpu_options_.grass_height_base = setting::current().GPU_GRASS_HEIGHT_BASE;
    gpu_options_.grass_height_range = std::max(1.0f, setting::current().GPU_GRASS_HEIGHT_RANGE);
    initBiomePaletteTexture();

    initAtlasTextures();
    LOG_F(INFO, "GpuMapWidget: atlas %dx%d texels, %d region slots at 1 texel/block", ATLAS_TEXELS, ATLAS_TEXELS,
          slotsPerSide() * slotsPerSide());
}
void GpuMapWidget::initAtlasTextures() {
    // Nothing to write. Every texel the shader can sample belongs to a slot that
    // collectVisibleRegions() has just queued, and paintGL() drains that queue
    // before it draws (see the invariant there), so the undefined contents
    // glTexImage2D leaves behind are never read. Filling the atlas here would cost
    // 4 B + 4 B + 2 B per texel, i.e. ~160 MB of uploads at 4096.
    buildBlankTiles();
    slots_.clear();
}

void GpuMapWidget::initBiomePaletteTexture() {
    std::vector<unsigned char> palette(256u * 3u * 3u, 255u);
    const bl::biome_tint_kind kinds[] = {bl::biome_tint_kind::water, bl::biome_tint_kind::leaves, bl::biome_tint_kind::grass};
    for (int row = 0; row < 3; ++row) {
        for (int id = 0; id < 256; ++id) {
            const auto c = bl::get_biome_tint_color(static_cast<bl::biome>(id), kinds[row]);
            const size_t offset = (static_cast<size_t>(row) * 256u + static_cast<size_t>(id)) * 3u;
            palette[offset + 0] = c.r;
            palette[offset + 1] = c.g;
            palette[offset + 2] = c.b;
        }
    }
    glGenTextures(1, &biome_palette_texture_);
    glBindTexture(GL_TEXTURE_2D, biome_palette_texture_);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, 256, 3, 0, GL_RGB, GL_UNSIGNED_BYTE, palette.data());
}

void GpuMapWidget::invalidateAtlas() {
    buildBlankTiles();
    slots_.clear();
    uploads_.clear();
    upload_index_.clear();
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
    // A pan/zoom can change the region mapped to a recycled slot while the old
    // request is waiting. Keep only the newest request, in place, so the queue
    // keeps the order the regions were discovered in.
    const auto existing = upload_index_.find(request.slot);
    if (existing != upload_index_.end()) {
        uploads_[existing->second] = request;
        return;
    }
    upload_index_.emplace(request.slot, uploads_.size());
    uploads_.push_back(request);
}

void GpuMapWidget::reindexUploads() {
    upload_index_.clear();
    for (size_t i = 0; i < uploads_.size(); ++i) upload_index_[uploads_[i].slot] = i;
}

void GpuMapWidget::buildBlankTiles() {
    // Match the CPU renderer's 64-block checkerboard across 128-block regions.
    const int texels = REGION_BLOCKS / blocks_per_texel_;
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

bool GpuMapWidget::uploadRegion(const UploadRequest& request) {
    // Re-resolve the cache entry before dereferencing it; entries may be evicted.
    const ChunkRegion* data = nullptr;
    const auto state = level_loader_->regionState(request.region, &data);
    // Sample each bake at blocks_per_texel_ spacing, matching the CPU renderer.
    const int bp = blocks_per_texel_;
    const int n = REGION_BLOCKS / bp;
    const size_t texels = static_cast<size_t>(n) * n;
    const bool biome_layer = view_ && view_->options().layer == RenderOption::Biome;

    color_buffer_.resize(texels * 4);
    height_buffer_.resize(texels * 2);
    material_buffer_.assign(texels * 2, 0);
    bool terrain = false;

    if (state == AsyncLevelLoader::RegionState::Ready && data) {
        terrain = true;
        const bool blend_water = setting::current().TRANSPARENT_WATER;
        const QImage& source = biome_layer ? data->biome_bake_image_ : data->flat_color_image_;
        const auto& tips = data->tips_info_;
        for (int tz = 0; tz < n; ++tz) {
            const int bz = tz * bp;
            const auto* line = reinterpret_cast<const QRgb*>(source.constScanLine(bz));
            for (int tx = 0; tx < n; ++tx) {
                const int bx = tx * bp;
                const auto& info = tips[bx][bz];
                // Biome mode remains a pre-baked categorical visualisation. In
                // terrain mode, however, the raw palette colour is uploaded and
                // its biome tint is reconstructed in the fragment shader.
                const QRgb pixel = biome_layer ? line[bx] : (info.gpu_base_color != 0 ? info.gpu_base_color : line[bx]);
                const size_t color_index = (static_cast<size_t>(tz) * n + tx) * 4;
                color_buffer_[color_index + 0] = static_cast<unsigned char>(qRed(pixel));
                color_buffer_[color_index + 1] = static_cast<unsigned char>(qGreen(pixel));
                color_buffer_[color_index + 2] = static_cast<unsigned char>(qBlue(pixel));
                // Alpha is the shader's "this column is under water" flag, not
                // opacity: it fades the bevel and AO out over water.
                color_buffer_[color_index + 3] = (!biome_layer && blend_water && info.gpu_water_overlay) ? 255 : 0;

                if (!biome_layer) {
                    constexpr unsigned char WATER_TINT = 1u << 0;
                    constexpr unsigned char GRASS_TINT = 1u << 1;
                    constexpr unsigned char LEAVES_TINT = 1u << 2;
                    constexpr unsigned char WATER_OVERLAY = 1u << 3;
                    constexpr unsigned char TERRAIN_SAMPLE = 1u << 4;
                    unsigned char flags = 0;
                    // A ready atlas slot can still contain a region background
                    // where no chunk exists. Keep its zero biome ID out of the
                    // shader's interpolation neighbourhood.
                    if (info.height > -128) flags |= TERRAIN_SAMPLE;
                    switch (static_cast<bl::biome_tint_kind>(info.gpu_tint_kind)) {
                        case bl::biome_tint_kind::water:
                            flags |= WATER_TINT;
                            break;
                        case bl::biome_tint_kind::grass:
                            flags |= GRASS_TINT;
                            break;
                        case bl::biome_tint_kind::leaves:
                            flags |= LEAVES_TINT;
                            break;
                        default:
                            break;
                    }
                    if (blend_water && info.gpu_water_overlay) flags |= WATER_OVERLAY;
                    const size_t material_index = (static_cast<size_t>(tz) * n + tx) * 2;
                    material_buffer_[material_index + 0] = static_cast<unsigned char>(info.biome);
                    material_buffer_[material_index + 1] = flags;
                }

                // Heights keep their world values, so the shader's ray stays in
                // blocks; only the sentinel moves to the shader's own void height.
                const size_t height_index = (static_cast<size_t>(tz) * n + tx) * 2;
                height_buffer_[height_index + 0] = info.solid_height <= -128 ? VOID_HEIGHT : static_cast<float>(info.solid_height);
                height_buffer_[height_index + 1] = info.height <= -128 ? VOID_HEIGHT : static_cast<float>(info.height);
            }
        }
    } else {
        color_buffer_ = (state == AsyncLevelLoader::RegionState::Empty) ? blank_dark_ : blank_light_;
        height_buffer_.assign(texels * 2, VOID_HEIGHT);
        // The upload below reads n*n texels from the front of the buffer, so a
        // tile built for a different resolution would be a garbled sub-rectangle
        // of the right pattern rather than a smaller checkerboard.
        Assert(color_buffer_.size() == texels * 4, "GpuMapWidget",
               "blank tile size does not match the atlas resolution (buildBlankTiles must run on every "
               "blocks_per_texel_ change)");
    }

    const int side = slotsPerSide();
    const int slot_x = (request.slot % side) * n;
    const int slot_z = (request.slot / side) * n;
    glBindTexture(GL_TEXTURE_2D, color_texture_);
    glTexSubImage2D(GL_TEXTURE_2D, 0, slot_x, slot_z, n, n, GL_RGBA, GL_UNSIGNED_BYTE, color_buffer_.data());
    glBindTexture(GL_TEXTURE_2D, height_texture_);
    glTexSubImage2D(GL_TEXTURE_2D, 0, slot_x, slot_z, n, n, GL_RG, GL_FLOAT, height_buffer_.data());
    glBindTexture(GL_TEXTURE_2D, material_texture_);
    glTexSubImage2D(GL_TEXTURE_2D, 0, slot_x, slot_z, n, n, GL_RG, GL_UNSIGNED_BYTE, material_buffer_.data());

    slots_[request.slot] = SlotState{request.region, state, data, blocks_per_texel_, true};
    ++total_uploads_;
    return terrain;
}

void GpuMapWidget::collectVisibleRegions(double px_per_block, const QPointF& world_origin) {
    const double world_width = width() / px_per_block;
    const double world_height = height() / px_per_block;
    // Keep shadow, bevel, AO, and biome samples resident around the viewport.
    const double shading_margin = gpu_options_.flat_shading ? 0.0 : static_cast<double>(std::max(blocks_per_texel_, 16));
    const double margin = std::max<double>(shadowReachBlocks(), shading_margin);

    double min_x = world_origin.x() - margin;
    double max_x = world_origin.x() + world_width + margin;
    double min_z = world_origin.y() - world_height - margin;
    double max_z = world_origin.y() + margin;
    if (gpu_options_.orthographic_view) {
        const QPointF center = capture_view_ ? capture_center_block_
                                             : (view_ ? view_->worldToView().inverted().map(QPointF(view_->viewportSize().width() / 2.0,
                                                                                                    view_->viewportSize().height() / 2.0)) *
                                                            16.0
                                                      : QPointF());
        const double half_u = width() / 2.0 / px_per_block;
        const double half_v = height() / 2.0 / px_per_block;
        const double max_dy = std::max(std::abs(ISO_RAY_BOTTOM - ISO_HEIGHT_REFERENCE), std::abs(ISO_RAY_TOP - ISO_HEIGHT_REFERENCE));
        // A pixel's world offset picks up half its screen x through the horizontal
        // axis and half its screen y through the vertical one, and the march adds
        // the whole ray depth on top.
        const double half_axis = INV_SQRT2 * half_u + SQRT_THREE_HALVES * half_v + max_dy;
        min_x = center.x() - half_axis - margin;
        max_x = center.x() + half_axis + margin;
        min_z = center.y() - half_axis - margin;
        max_z = center.y() + half_axis + margin;
    }

    const int rx0 = floorDiv(static_cast<int>(std::floor(min_x)), REGION_BLOCKS);
    const int rx1 = floorDiv(static_cast<int>(std::floor(max_x)), REGION_BLOCKS);
    const int rz0 = floorDiv(static_cast<int>(std::floor(min_z)), REGION_BLOCKS);
    const int rz1 = floorDiv(static_cast<int>(std::floor(max_z)), REGION_BLOCKS);

    visible_regions_ = 0;
    visible_region_data_pending_ = false;
    const int dim = view_ ? view_->dim() : 0;
    const int side = slotsPerSide();
    // Discard uploads made irrelevant by a pan or zoom.
    uploads_.erase(std::remove_if(uploads_.begin(), uploads_.end(),
                                  [&](const UploadRequest& request) {
                                      return request.region.dim != dim || request.region.x < rx0 * constant::RW ||
                                             request.region.x > rx1 * constant::RW || request.region.z < rz0 * constant::RW ||
                                             request.region.z > rz1 * constant::RW;
                                  }),
                   uploads_.end());
    reindexUploads();
    level_loader_->setRenderViewport({rx0 * constant::RW, rz0 * constant::RW, dim}, {rx1 * constant::RW, rz1 * constant::RW, dim});
    Assert(rx1 - rx0 < side && rz1 - rz0 < side, "GpuMapWidget",
           "visible region range exceeds the atlas; blocksPerTexelFor returned a level that does not fit");
    for (int rz = rz0; rz <= rz1; ++rz) {
        for (int rx = rx0; rx <= rx1; ++rx) {
            ++visible_regions_;
            const bl::chunk_pos region{rx * constant::RW, rz * constant::RW, dim};
            const int slot = floorMod(rz, side) * side + floorMod(rx, side);
            const ChunkRegion* data = nullptr;
            const auto now = level_loader_->regionState(region, &data);
            if (now == AsyncLevelLoader::RegionState::Unloaded) visible_region_data_pending_ = true;
            const auto state_it = slots_.find(slot);
            const SlotState* state = state_it == slots_.end() ? nullptr : &state_it->second;
            // A slot showing a different region, content uploaded at a different
            // atlas resolution, or an older state for this one has to be
            // re-uploaded - including when a bake finishes and replaces the
            // background, and when an edit re-bakes the region (new object).
            const bool matches_request = state && state->bp == blocks_per_texel_ && state->region == region && state->state == now &&
                                         (now != AsyncLevelLoader::RegionState::Ready || state->source == data);
            const bool up_to_date = matches_request && state->assigned;
            if (up_to_date) continue;

            queueUpload({region, slot});
        }
    }
}

int GpuMapWidget::shadowReachBlocks() const {
    // The shader marches in half-texel steps within a 256-iteration bound, so it
    // covers this many blocks at every resolution the atlas uses.
    return gpu_options_.shadow_enabled && gpu_options_.shadow_strength > 0.0f ? gpu_options_.shadow_steps : 0;
}

bool GpuMapWidget::overviewMode() const { return !capture_view_ && overlays_ && overlays_->coordsOverviewMode(); }

// Slots along one atlas edge at a given resolution: the atlas is a fixed number
// of texels and a region covers REGION_BLOCKS blocks, so the slot count grows
// with blocks-per-texel.
static int slotsPerSideFor(int blocks_per_texel) {
    const int texels_per_region = GpuMapWidget::REGION_BLOCKS / std::max(1, blocks_per_texel);
    return GpuMapWidget::ATLAS_TEXELS / std::max(1, texels_per_region);
}

int GpuMapWidget::regionsSpannedBy(const QSize& viewport, double px_per_block) const {
    // Worst case across both axes: the span in blocks plus the one extra region
    // that floor()-ing the two edge regions can add. Deriving the requirement
    // from the region count (rather than from a block-span proxy) is what keeps
    // this exactly consistent with the range collectVisibleRegions() walks.
    const double shading_margin = gpu_options_.flat_shading ? 0.0 : static_cast<double>(blocks_per_texel_);
    const double reach = std::max<double>(shadowReachBlocks(), shading_margin);
    double span = std::max(viewport.width() / px_per_block, viewport.height() / px_per_block) + 2.0 * reach;
    if (gpu_options_.orthographic_view) {
        // The fixed camera projects a vertical ray depth into both horizontal
        // world axes. Size the atlas for that projected footprint, not just the
        // screen rectangle used by the top-down renderer.
        const double half_u = viewport.width() / 2.0 / px_per_block;
        const double half_v = viewport.height() / 2.0 / px_per_block;
        const double max_dy = std::max(std::abs(ISO_RAY_BOTTOM - ISO_HEIGHT_REFERENCE), std::abs(ISO_RAY_TOP - ISO_HEIGHT_REFERENCE));
        const double half_axis = INV_SQRT2 * half_u + SQRT_THREE_HALVES * half_v + max_dy;
        span = 2.0 * half_axis + 2.0 * reach;
    }
    return static_cast<int>(std::ceil(span / REGION_BLOCKS)) + 1;
}

int GpuMapWidget::blocksPerTexelFor(const QSize& viewport, double px_per_block) const {
    // 128 blocks per texel is the coarsest possible level: a region tile would
    // otherwise be smaller than one texel.
    constexpr int MAX_BLOCKS_PER_TEXEL = REGION_BLOCKS;
    const int needed_slots = regionsSpannedBy(viewport, px_per_block);

    int minimal = 1;
    while (minimal < MAX_BLOCKS_PER_TEXEL && slotsPerSideFor(minimal) < needed_slots) minimal *= 2;

    if (blocks_per_texel_ < minimal) return minimal;

    // Going back to a finer level rebuilds the atlas, so only do it with room to
    // spare. Without the margin, scrolling in and out across a band boundary
    // rebuilds on every wheel step; the floor above is what keeps this from
    // overshooting into a level that no longer covers the view.
    constexpr double SHRINK_MARGIN = 1.4;
    const int finer = blocks_per_texel_ / 2;
    if (finer >= minimal && static_cast<double>(slotsPerSideFor(finer)) >= needed_slots * SHRINK_MARGIN) return finer;
    return blocks_per_texel_;
}

int GpuMapWidget::blocksPerTexelFor(double px_per_block) const { return blocksPerTexelFor(size(), px_per_block); }

void GpuMapWidget::paintGL() {
    QElapsedTimer frame_timer;
    frame_timer.start();

    if (!capture_view_ && view_) {
        view_->setViewportSize(size());
        // The zoom floor depends on whether the chunk coordinate index is loaded,
        // which only the loader knows.
        view_->applyConfiguredZoomLimits(level_loader_ && level_loader_->preloadAllChunkCoords());
    }

    QPainter painter(this);
    painter.beginNativePainting();

    glClearColor(0.08f, 0.08f, 0.09f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    // A far-out view spans thousands of region tiles, so the atlas walk, the
    // bakes it asks for and the uploads all grow with the view instead of with
    // the world - which is what makes zooming out hitch. Those zooms draw the
    // coordinate overview instead, at the same threshold the CPU renderer uses.
    const bool overview = !gpu_options_.orthographic_view && overviewMode();
    if (overview) {
        // No region work in this mode; without this the stats strip would keep
        // reporting the last terrain frame's counters.
        visible_regions_ = 0;
        uploaded_last_frame_ = 0;
        visible_region_data_pending_ = false;
    }

    bool ready = level_loader_ && level_loader_->isOpen() && shader_ && shader_->isLinked() && color_texture_ != 0;
    double px_per_block = 0.0;
    QPointF world_origin;

    // Per-stage frame cost, so a hitch can be attributed to one of them instead
    // of guessed at (see drawStats).
    QElapsedTimer stage_timer;
    stage_timer.start();
    const auto stage_ms = [&stage_timer](double& out) {
        out = stage_timer.nsecsElapsed() / 1.0e6;
        stage_timer.restart();
    };
    gpu_timings_ = {};

    // Same place and scale as every other renderer on this view. Also what the
    // stats strip reports, so it is resolved even while the overview is drawn.
    double map_px_per_block = capture_px_per_block_;
    QPointF world_center = capture_center_block_;
    if (!capture_view_ && view_) {
        map_px_per_block = view_->scaleLevel() / 16.0;
        const QSize viewport = view_->viewportSize();
        const QPointF center = view_->worldToView().inverted().map(QPointF(viewport.width() / 2.0, viewport.height() / 2.0));
        world_center = center * 16.0;  // chunk units -> blocks
    }
    px_per_block = std::max(0.01, map_px_per_block);

    if (ready && !overview) {
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

        // Reduce atlas resolution as the view widens instead of refusing to zoom out.
        const int wanted_bp = blocksPerTexelFor(px_per_block);
        if (wanted_bp != blocks_per_texel_) {
            blocks_per_texel_ = wanted_bp;
            syncSlots();
            invalidateAtlas();
        }

        if (gpu_options_.orthographic_view) {
            // The isometric shader can see the full ray depth for every pixel.
            // Convert that projected footprint into a conservative axis-aligned
            // atlas range so every sampled column is resident.
            const double half_u = width() / 2.0 / px_per_block;
            const double half_v = height() / 2.0 / px_per_block;
            const double max_dy = std::max(std::abs(ISO_RAY_BOTTOM - ISO_HEIGHT_REFERENCE), std::abs(ISO_RAY_TOP - ISO_HEIGHT_REFERENCE));
            const double half_axis = INV_SQRT2 * half_u + SQRT_THREE_HALVES * half_v + max_dy;
            world_origin = QPointF(world_center.x() - half_axis, world_center.y() + half_axis);
        } else {
            const double half_w = width() / 2.0 / px_per_block;
            const double half_h = height() / 2.0 / px_per_block;
            // gl_FragCoord (0,0) is the bottom-left of the widget, and world z grows
            // downwards on screen, so the bottom edge carries the larger z.
            world_origin = QPointF(world_center.x() - half_w, world_center.y() + half_h);
        }

        collectVisibleRegions(px_per_block, world_origin);
        stage_ms(gpu_timings_.collect_ms);

        // Upload queued slots before drawing so the atlas has no stale visible tiles.
        uploaded_last_frame_ = 0;
        for (auto it = uploads_.begin(); it != uploads_.end();) {
            QElapsedTimer slot_timer;
            slot_timer.start();
            const bool terrain = uploadRegion(*it);
            (terrain ? gpu_timings_.ready_upload_ms : gpu_timings_.blank_upload_ms) += slot_timer.nsecsElapsed() / 1.0e6;
            ++uploaded_last_frame_;
            it = uploads_.erase(it);
        }
        reindexUploads();
        stage_ms(gpu_timings_.upload_ms);
    }

    if (ready && !overview) {
        int sx = -1;
        int sy = -1;
        sunStep(sx, sy);
        const double dpr = devicePixelRatioF();
        const double device_px_per_block = px_per_block * dpr;
        const double texel_px = device_px_per_block * blocks_per_texel_;

        shader_->bind();
        shader_->setUniformValue("uColor", 0);
        shader_->setUniformValue("uHeight", 1);
        shader_->setUniformValue("uMaterial", 2);
        shader_->setUniformValue("uBiomePalette", 3);
        shader_->setUniformValue("uViewOrigin", static_cast<float>(world_origin.x()), static_cast<float>(world_origin.y()));
        shader_->setUniformValue("uPxPerBlock", static_cast<float>(device_px_per_block));
        shader_->setUniformValue("uAtlasTexels", static_cast<float>(ATLAS_TEXELS));
        shader_->setUniformValue("uBlocksPerTexel", static_cast<float>(blocks_per_texel_));
        shader_->setUniformValue("uSunStep", static_cast<float>(sx), static_cast<float>(sy));
        shader_->setUniformValue("uShadowDarkness", 1.0f - std::clamp(setting::current().SHADOW_LEVEL, 0, 255) / 255.0f * 0.75f);
        shader_->setUniformValue("uShadowStrength", gpu_options_.shadow_strength);
        shader_->setUniformValue("uShadowReach", static_cast<float>(shadowReachBlocks()));
        const double base_edge_width = std::clamp(std::max(0.25, 1.0 / texel_px), 0.25, 0.5);
        shader_->setUniformValue("uEdgeWidth", static_cast<float>(std::clamp(base_edge_width * gpu_options_.bevel_width, 0.0625, 0.75)));
        shader_->setUniformValue("uAoStrength", gpu_options_.ao_strength);
        shader_->setUniformValue("uAoDirections", gpu_options_.ao_directions);
        shader_->setUniformValue("uAoSteps", gpu_options_.ao_steps);
        shader_->setUniformValue("uAoStep0", gpu_options_.ao_step0);
        shader_->setUniformValue("uAoRadius", gpu_options_.ao_radius);
        shader_->setUniformValue("uBevelStrength", gpu_options_.bevel_strength);
        shader_->setUniformValue("uSaturation", gpu_options_.saturation);
        shader_->setUniformValue("uBrightness", gpu_options_.brightness);
        shader_->setUniformValue("uGrassHeightEnabled", gpu_options_.grass_height_enabled ? 1.0f : 0.0f);
        shader_->setUniformValue("uGrassHeightBase", gpu_options_.grass_height_base);
        shader_->setUniformValue("uGrassHeightRange", gpu_options_.grass_height_range);
        shader_->setUniformValue("uGrassHeightColor", gpu_options_.grass_height_color[0], gpu_options_.grass_height_color[1],
                                 gpu_options_.grass_height_color[2]);
        // Biome bake is a categorical visualisation, not terrain material:
        // height-driven bevel, AO, shadows, and water treatment must not alter it.
        const bool biome_layer = view_ && view_->options().layer == RenderOption::Biome;
        shader_->setUniformValue("uFlatShading", (gpu_options_.flat_shading || biome_layer) ? 1.0f : 0.0f);
        shader_->setUniformValue("uWaterBaseColor", gpu_options_.water_base_color[0], gpu_options_.water_base_color[1],
                                 gpu_options_.water_base_color[2]);
        if (gpu_options_.orthographic_view) {
            shader_->setUniformValue("uIsoCenter", static_cast<float>(world_center.x()), static_cast<float>(world_center.y()));
            shader_->setUniformValue("uIsoReferenceHeight", static_cast<float>(ISO_HEIGHT_REFERENCE));
            shader_->setUniformValue("uIsoViewport", static_cast<float>(width() * dpr), static_cast<float>(height() * dpr));
            shader_->setUniformValue("uIsoRayTop", static_cast<float>(ISO_RAY_TOP));
            shader_->setUniformValue("uIsoRayBottom", static_cast<float>(ISO_RAY_BOTTOM));
        }

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, color_texture_);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, height_texture_);
        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_2D, material_texture_);
        glActiveTexture(GL_TEXTURE3);
        glBindTexture(GL_TEXTURE_2D, biome_palette_texture_);
        glActiveTexture(GL_TEXTURE0);

        glBindVertexArray(vao_);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        glBindVertexArray(0);
        if (gpu_timing_) glFinish();
        shader_->release();
    }
    stage_ms(gpu_timings_.shade_ms);

    painter.endNativePainting();

    // --- overlays ---
    // Draw shared overlays in this widget's viewport transform.
    if (overlays_ && view_ && !capture_view_ && !gpu_options_.orthographic_view) {
        const QTransform world_to_view = view_->transformForViewport(size());
        overlays_->setTransform(world_to_view);
        overlays_->setScreenInset(STATS_BAR_HEIGHT);

        painter.setTransform(world_to_view);
        const RenderOption& options = view_->options();
        if (overview) overlays_->drawCoordsOverview(&painter);
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

    gpu_timings_.last_frame_ms = frame_timer.nsecsElapsed() / 1.0e6;
    stage_ms(gpu_timings_.overlay_ms);
    if (!gpu_options_.orthographic_view) drawStats(painter, gpu_timings_.last_frame_ms, px_per_block);
    painter.end();
}

void GpuMapWidget::drawStats(QPainter& painter, double frame_ms, double px_per_block) {
    if (width() < 160 || height() < 60) return;
    const QString stats =
        tr("GPU map  %1 ms  %2 px/block  texel %3 blk  regions %4  uploads %5  ao %6")
            .arg(QString::number(frame_ms, 'f', 1), QString::number(px_per_block, 'f', 2))
            .arg(blocks_per_texel_)
            .arg(visible_regions_)
            .arg(uploaded_last_frame_)
            .arg(QString::number(static_cast<double>(gpu_options_.ao_strength), 'f', 2)) +
        tr("  |  collect %1  upload %2 (background %3 / terrain %4)  shade %5  overlay %6")
            .arg(QString::number(gpu_timings_.collect_ms, 'f', 1), QString::number(gpu_timings_.upload_ms, 'f', 1),
                 QString::number(gpu_timings_.blank_upload_ms, 'f', 1), QString::number(gpu_timings_.ready_upload_ms, 'f', 1),
                 QString::number(gpu_timings_.shade_ms, 'f', 1), QString::number(gpu_timings_.overlay_ms, 'f', 1));
    painter.setPen(QColor(235, 235, 235));
    const int bar_top = height() - STATS_BAR_HEIGHT;
    painter.fillRect(QRect(0, bar_top, width(), STATS_BAR_HEIGHT), QColor(22, 22, 22, 170));
    painter.drawText(QRect(4, bar_top, width() - 8, STATS_BAR_HEIGHT), Qt::AlignVCenter | Qt::AlignLeft, stats);
}
