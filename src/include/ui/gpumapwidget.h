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

/// GPU renderer for the 2D map, sharing state and region bakes with the CPU renderer.
class GpuMapWidget : public QOpenGLWidget, protected QOpenGLFunctions_3_3_Core {
    Q_OBJECT

   public:
    /// Texture edge of one atlas page, in texels.
    static constexpr int kAtlasTexels = 8192;
    /// Side of one region tile in world blocks (8x8 chunks).
    static constexpr int kRegionBlocks = constant::RW * 16;

    static_assert(kAtlasTexels % kRegionBlocks == 0, "the atlas must hold a whole number of region tiles");

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

    void setShadowEnabled(bool enabled);

    [[nodiscard]] bool shadowEnabled() const { return shadow_enabled_; }

    /// Ray-march length in blocks.
    void setShadowSteps(int steps);

    [[nodiscard]] int shadowSteps() const { return shadow_steps_; }

    /// Retained for source compatibility; GPU shadows are binary.
    void setPenumbra(float penumbra);

    [[nodiscard]] float penumbra() const { return penumbra_; }

    /// Shadow darkness multiplier in the range 0..1.
    void setShadowStrength(float strength);

    [[nodiscard]] float shadowStrength() const { return shadow_strength_; }

    /// Ambient occlusion strength; zero disables it.
    void setAoStrength(float strength);

    [[nodiscard]] float aoStrength() const { return ao_strength_; }

    /// Bevel strength; zero removes the bevel.
    void setBevelStrength(float strength);

    [[nodiscard]] float bevelStrength() const { return bevel_strength_; }

    /// Width multiplier of the screen-space bevel. 1 preserves the automatic width.
    void setBevelWidth(float width);

    [[nodiscard]] float bevelWidth() const { return bevel_width_; }

    /// Saturation of the rendered image: 0 greyscale, 1 as stored, up to 2 boosted.
    void setSaturation(float saturation);

    [[nodiscard]] float saturation() const { return saturation_; }

    /// Brightness multiplier of the rendered image: 1 is neutral, 0 is black,
    /// and values above 1 lift the shaded colours.
    void setBrightness(float brightness);

    [[nodiscard]] float brightness() const { return brightness_; }

    /// Configure AO directions and samples per direction.
    void setAoMarch(int directions, int steps);

    [[nodiscard]] int aoDirections() const { return ao_directions_; }
    [[nodiscard]] int aoSteps() const { return ao_steps_; }

    /// Upload resident bakes without shader shading when `flat` is true.
    void setFlatShading(bool flat);

    [[nodiscard]] bool flatShading() const { return flat_shading_; }

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
    [[nodiscard]] double lastFrameMs() const { return last_frame_ms_; }
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
    static constexpr int kStatsBarHeight = 20;

    /// Height written for a column with no blocks (matches map2d.frag).
    static constexpr float kVoidHeight = -1000.0f;

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

    [[nodiscard]] int slotsPerSide() const { return kAtlasTexels / (kRegionBlocks / blocks_per_texel_); }

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
    std::array<float, 3> water_base_color_{0.64f, 0.64f, 0.64f};
    std::array<float, 3> grass_height_color_{1.0f, 0.0f, 0.0f};
    bool grass_height_enabled_{false};
    float grass_height_base_{64.0f};
    float grass_height_range_{128.0f};

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

    bool shadow_enabled_{true};
    int shadow_steps_{48};
    // Compatibility setting; hard shadows intentionally ignore this value.
    float penumbra_{0.0f};
    float ao_strength_{0.10f};
    int ao_directions_{16};
    int ao_steps_{16};
    // The march spans this radius in blocks; the near field is sampled finely and the
    // reach is bounded, so nothing beyond it can darken a pixel.
    static constexpr float ao_step0_{0.5f};
    static constexpr float ao_radius_{16.0f};
    float bevel_strength_{1.0f};
    float bevel_width_{1.0f};
    float saturation_{1.0f};
    float brightness_{1.0f};
    float shadow_strength_{1.0f};
    bool flat_shading_{false};
    bool orthographic_view_{true};

    bool capture_view_{false};
    QPointF capture_center_block_{0.0, 0.0};
    double capture_px_per_block_{4.0};
    QPointF orthographic_drag_pos_;
    bool orthographic_dragging_{false};
    ImportOverlay* import_{nullptr};
    MapInteraction* interaction_{nullptr};
    /// Refreshes debug counters on a still view.
    QTimer* debug_refresh_timer_{nullptr};

    // stats for the overlay
    double last_frame_ms_{0};
    // Per-stage frame timings.
    double collect_ms_{0};
    double upload_ms_{0};
    double blank_upload_ms_{0};
    double ready_upload_ms_{0};
    double shade_ms_{0};
    double overlay_ms_{0};
    bool gpu_timing_{false};
    int uploaded_last_frame_{0};
    int total_uploads_{0};
    int visible_regions_{0};
    /// True when collectVisibleRegions() found a region whose bake has not
    /// completed; offscreen capture waits for those bakes before returning.
    bool visible_region_data_pending_{false};
};

#endif  // BEDROCKMAP_GPUMAPWIDGET_H
