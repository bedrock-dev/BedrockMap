#ifndef BEDROCKMAP_GPUMAPWIDGET_H
#define BEDROCKMAP_GPUMAPWIDGET_H

#include <qopenglfunctions_3_3_core.h>
#include <qopenglshaderprogram.h>
#include <qsize.h>
#include <qtmetamacros.h>
#include <qtransform.h>

#include <QOpenGLWidget>
#include <QString>
#include <QTimer>
#include <array>
#include <deque>
#include <string>
#include <unordered_map>
#include <vector>

#include "asynclevelloader.h"
#include "bedrock_key.h"
#include "chunk_task.h"
#include "importoverlay.h"
#include "mapinteraction.h"
#include "mapoverlays.h"

class AsyncLevelLoader;
class MapHost;
class MapView;

struct GpuRenderOptions {
    std::array<float, 3> water_base_color{0.64f, 0.64f, 0.64f};
    std::array<float, 3> grass_height_color{1.0f, 0.0f, 0.0f};
    bool grass_height_enabled{false};
    float grass_height_base{64.0f};
    float grass_height_range{128.0f};

    bool shadow_enabled{true};
    int shadow_steps{48};
    float shadow_strength{1.0f};

    float ao_strength{0.10f};
    int ao_directions{16};
    int ao_steps{16};
    float ao_step0{0.5f};
    float ao_radius{16.0f};

    float bevel_strength{1.0f};
    float bevel_width{1.0f};
    float saturation{1.0f};
    float brightness{1.0f};
    float biome_blend_blocks{8.0f};
    bool flat_shading{false};
    bool orthographic_view{false};
};

struct GpuRenderTimings {
    double last_frame_ms{0};
    double collect_ms{0};
    double upload_ms{0};
    double blank_upload_ms{0};
    double ready_upload_ms{0};
    double shade_ms{0};
    double overlay_ms{0};
};

/// GPU renderer for the 2D map, sharing state and region bakes with the CPU renderer.
class GpuMapWidget : public QOpenGLWidget, protected QOpenGLFunctions_3_3_Core {
    Q_OBJECT

   public:
    /// Texture edge of one atlas page, in texels.
    static constexpr int ATLAS_TEXELS = 8192;
    /// Side of one region tile in world blocks (8x8 chunks).
    static constexpr int REGION_BLOCKS = constant::RW * 16;

    static_assert(ATLAS_TEXELS % REGION_BLOCKS == 0, "the atlas must hold a whole number of region tiles");

    GpuMapWidget(QWidget* parent, AsyncLevelLoader* loader, MapView* view, MapOverlays* overlays, ImportOverlay* import, MapHost* host);

    ~GpuMapWidget() override;

    /// The view this renderer draws from (the same object the CPU renderer uses).
    [[nodiscard]] MapView* view() const { return view_; }

    /// World blocks per atlas texel at the current zoom (diagnostics).
    [[nodiscard]] int blocksPerTexel() const { return blocks_per_texel_; }
    /// Slots along one atlas edge at the current resolution.
    [[nodiscard]] int totalUploadCount() const { return total_uploads_; }

    /// Temporarily set the synthetic view used by offscreen captures.
    void setCaptureView(const QPointF& center_block, double px_per_block);

    /// Render offscreen until visible atlas uploads have drained.
    QImage captureOffscreen(const QSize& size, const QPointF& center_block, double px_per_block);

    void setGpuOptions(const GpuRenderOptions& options);
    [[nodiscard]] const GpuRenderOptions& gpuOptions() const { return gpu_options_; }

    [[nodiscard]] QSize sizeHint() const override { return {640, 480}; }

   signals:
    /// Cursor position for the status bar.
    void mouseMove(int x, int z, int dim);

   public:
    /// Select the atlas resolution for the current zoom.
    [[nodiscard]] int blocksPerTexelFor(double px_per_block) const;
    /// Same, for an explicit viewport (used by tests and offscreen captures).
    [[nodiscard]] int blocksPerTexelFor(const QSize& viewport, double px_per_block) const;

    /// Number of regions required by the viewport and shading margin.
    [[nodiscard]] int regionsSpannedBy(const QSize& viewport, double px_per_block) const;

    /// Diagnostics for the comparison overlay and the render benchmark.
    [[nodiscard]] int visibleRegionCount() const { return visible_regions_; }
    [[nodiscard]] int pendingUploadCount() const { return static_cast<int>(uploads_.size()); }
    [[nodiscard]] int residentRegionCount() const;
    [[nodiscard]] int atlasSlotsPerSide() const { return slotsPerSide(); }
    /// Time spent in paintGL. GL normally submits asynchronously, so this covers
    /// only the CPU side unless GPU timing is enabled, in which case it waits for
    /// the frame with glFinish() and includes the GPU.
    [[nodiscard]] double lastFrameMs() const { return gpu_timings_.last_frame_ms; }
    [[nodiscard]] bool gpuTimingEnabled() const { return gpu_timing_; }
    void setGpuTimingEnabled(bool enabled) { gpu_timing_ = enabled; }

   protected:
    void initializeGL() override;

    void paintGL() override;

    void mousePressEvent(QMouseEvent* event) override;

    void mouseMoveEvent(QMouseEvent* event) override;

    void mouseReleaseEvent(QMouseEvent* event) override;

    void wheelEvent(QWheelEvent* event) override;

    void keyPressEvent(QKeyEvent* event) override;

    void keyReleaseEvent(QKeyEvent* event) override;

    void focusOutEvent(QFocusEvent* event) override;

   private:
    /// Rebuild the slot table for the current blocks-per-texel.
    void syncSlots();
    /// Fill the atlas textures for the first time (startup).
    void initAtlasTextures();
    /// Upload the compact 256-entry water/leaves/grass tint palette.
    void initBiomePaletteTexture();
    /// Mark every slot stale after a resolution or dimension change.  Validity is
    /// cleared immediately; visible slots are refilled over later frames.
    void invalidateAtlas();
    /// Height of the stats strip drawn along the top. The shared overlays are
    /// told to keep clear of it.
    static constexpr int STATS_BAR_HEIGHT = 20;

    /// Height written for a column with no blocks (matches map2d.frag).
    static constexpr float VOID_HEIGHT = -1000.0f;

    struct SlotState {
        bl::chunk_pos region{};
        AsyncLevelLoader::RegionState state{AsyncLevelLoader::RegionState::Unloaded};
        const void* source{nullptr};
        int bp{0};  // atlas resolution (slot grid) the content was uploaded on
        bool assigned{false};
    };

    struct UploadRequest {
        bl::chunk_pos region;
        int slot{0};
    };

    /// Shadow reach shared by fitting and region collection.
    [[nodiscard]] int shadowReachBlocks() const;

    /// Whether the coordinate overview replaces terrain at this zoom.
    [[nodiscard]] bool overviewMode() const;

    [[nodiscard]] int slotsPerSide() const { return ATLAS_TEXELS / (REGION_BLOCKS / blocks_per_texel_); }

    void queueUpload(UploadRequest request);

    /// Rebuild the slot -> queue index after the queue was pruned.
    void reindexUploads();

    /// Upload one region tile and return whether it contains terrain.
    [[nodiscard]] bool uploadRegion(const UploadRequest& request);

    /// Build the dark and light empty-region tiles.
    void buildBlankTiles();

    void collectVisibleRegions(double px_per_block, const QPointF& world_origin);

    void drawStats(QPainter& painter, double frame_ms, double px_per_block);

    AsyncLevelLoader* level_loader_;
    MapView* view_{nullptr};
    /// Shared overlays; null for standalone captures.
    MapOverlays* overlays_{nullptr};
    /// Shared map host used by context-menu actions.
    MapHost* host_{nullptr};

    QOpenGLShaderProgram* shader_{nullptr};
    GLuint vao_{0};
    GLuint color_texture_{0};
    GLuint height_texture_{0};
    /// R = biome id, G = material flags (water, grass, leaves, water overlay, terrain sample).
    GLuint material_texture_{0};
    /// Three rows of RGB tint colours indexed by the biome id.
    GLuint biome_palette_texture_{0};
    int atlas_dim_{-1};        // dimension the atlas currently holds, -1 = nothing
    int blocks_per_texel_{1};  // atlas resolution, see blocksPerTexelFor()
    // Atlas contents depend on the selected base layer.
    bool atlas_biome_layer_{false};
    // Keep only visible slots; coarse LODs can span a large atlas.
    std::unordered_map<int, SlotState> slots_;
    /// Pending writes plus a slot-to-index map for replacement.
    std::deque<UploadRequest> uploads_;
    std::unordered_map<int, size_t> upload_index_;
    std::vector<unsigned char> color_buffer_;
    std::vector<float> height_buffer_;
    std::vector<unsigned char> material_buffer_;
    /// Prebuilt background tiles, uploaded as-is when a slot has no terrain.
    std::vector<unsigned char> blank_dark_;
    std::vector<unsigned char> blank_light_;

    GpuRenderOptions gpu_options_;

    bool capture_view_{false};
    QPointF capture_center_block_{0.0, 0.0};
    double capture_px_per_block_{4.0};
    QPointF orthographic_drag_pos_;
    bool orthographic_dragging_{false};
    ImportOverlay* import_{nullptr};
    MapInteraction* interaction_{nullptr};
    /// Refreshes debug counters on a still view.
    QTimer* debug_refresh_timer_{nullptr};

    GpuRenderTimings gpu_timings_;
    bool gpu_timing_{false};
    int uploaded_last_frame_{0};
    int total_uploads_{0};
    int visible_regions_{0};
    /// True when collectVisibleRegions() found a region whose bake has not
    /// completed; offscreen capture waits for those bakes before returning.
    bool visible_region_data_pending_{false};
};

#endif  // BEDROCKMAP_GPUMAPWIDGET_H
