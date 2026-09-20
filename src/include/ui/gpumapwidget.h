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

/// GPU renderer for the 2D map.
///
/// It consumes the same region bakes as the CPU renderer (one texel per block,
/// see ChunkRegion::flat_color_image_ / tips_info_) but uploads them into a
/// single world-aligned atlas and evaluates the shading per screen pixel instead
/// of per baked texel. Per-pixel AO and shadow therefore follow the zoom level
/// instead of a fixed tile resolution.
///
/// It is one of two interchangeable renderers of a single MapView, the other
/// being CpuMapWidget, so it owns nothing but its own painting: it points at the
/// same MapView, draws the same MapOverlays, draws and drives the same
/// ImportOverlay, and routes its context menu through the same MapHost, so
/// only the pixels differ. Both take exactly the same set, which is what keeps
/// them from drifting apart.
class GpuMapWidget : public QOpenGLWidget, protected QOpenGLFunctions_3_3_Core {
    Q_OBJECT

   public:
    /// Texture edge of one atlas page, in texels.
    static constexpr int kAtlasTexels = 2048;
    /// Side of one region tile in world blocks (8x8 chunks).
    static constexpr int kRegionBlocks = constant::RW * 16;

    GpuMapWidget(QWidget* parent, AsyncLevelLoader* loader, MapView* view, MapOverlays* overlays, ImportOverlay* import, MapHost* host);

    ~GpuMapWidget() override;

    /// The view this renderer draws from (the same object the CPU renderer uses).
    [[nodiscard]] MapView* view() const { return view_; }

    /// World blocks per atlas texel at the current zoom (diagnostics).
    [[nodiscard]] int blocksPerTexel() const { return blocks_per_texel_; }
    /// Slots along one atlas edge at the current resolution (diagnostics: the
    /// invariant is that this covers regionsSpannedBy()).
    /// Total uploads since construction (diagnostics: a settled view must stop).
    [[nodiscard]] int totalUploadCount() const { return total_uploads_; }

    /// Offscreen capture helper (benchmarks/tests): point a standalone view at a
    /// world position and scale. The widget then ignores its MapView and uses
    /// these instead, so a capture does not need a widget tree.
    void setCaptureView(const QPointF& center_block, double px_per_block);

    /// Render offscreen without showing a window and return the frame. Repeats
    /// the paint until the atlas upload queue has drained, so the result is not
    /// a half-populated atlas.
    QImage captureOffscreen(const QSize& size, const QPointF& center_block, double px_per_block);

    void setShadowEnabled(bool enabled);

    [[nodiscard]] bool shadowEnabled() const { return shadow_enabled_; }

    /// Ray march length in blocks. Longer reaches further but costs one texture
    /// fetch per half texel per pixel.
    void setShadowSteps(int steps);

    [[nodiscard]] int shadowSteps() const { return shadow_steps_; }

    /// 0 = hard shadow edge, larger = wider penumbra.
    void setPenumbra(float penumbra);

    [[nodiscard]] float penumbra() const { return penumbra_; }

    /// How much of the shadow's darkness is applied: 0 disables the ray march's
    /// effect, 1 is the full shadow. Scales the occlusion, so the shadow keeps its
    /// shape (and its penumbra) at any strength.
    void setShadowStrength(float strength);

    [[nodiscard]] float shadowStrength() const { return shadow_strength_; }

    /// Depth of the ambient occlusion in concave corners. 0 disables it, which
    /// is also the switch the benchmark uses to isolate the effect. Seeded from
    /// the settings when the widget is created.
    void setAoStrength(float strength);

    [[nodiscard]] float aoStrength() const { return ao_strength_; }

    /// Depth of the bevel. 0 removes it, which is also the switch the benchmark
    /// uses to isolate the effect. Seeded from the settings when the widget is
    /// created.
    void setBevelStrength(float strength);

    [[nodiscard]] float bevelStrength() const { return bevel_strength_; }

    /// Saturation of the rendered image: 0 greyscale, 1 as stored, up to 2 boosted.
    void setSaturation(float saturation);

    [[nodiscard]] float saturation() const { return saturation_; }

    /// Brightness multiplier of the rendered image: 1 is neutral, 0 is black,
    /// and values above 1 lift the shaded colours.
    void setBrightness(float brightness);

    [[nodiscard]] float brightness() const { return brightness_; }

    /// Resolution of the ambient occlusion march: how many azimuths, how many samples
    /// along each, and the distance to the first sample in blocks. Cost is the product
    /// of the first two, so this is the quality/performance knob.
    void setAoMarch(int directions, int steps);

    [[nodiscard]] int aoDirections() const { return ao_directions_; }
    [[nodiscard]] int aoSteps() const { return ao_steps_; }

    /// Upload the region bakes already resident; false disables shading to show
    /// what the CPU styles produce.
    void setFlatShading(bool flat);

    [[nodiscard]] bool flatShading() const { return flat_shading_; }

    [[nodiscard]] QSize sizeHint() const override { return {640, 480}; }

   signals:
    /// Cursor moved over the map: what the status bar shows. Same signal the CPU
    /// renderer emits, so the page wires either one the same way.
    void mouseMove(int x, int z, int dim);

   public:
    /// World blocks per atlas texel for the current zoom: 1 when zoomed in
    /// enough for one texel per block, doubling as the view widens so that the
    /// whole visible area always fits in one atlas. Changing it changes the slot
    /// layout, so the atlas is rebuilt when it changes.
    [[nodiscard]] int blocksPerTexelFor(double px_per_block) const;
    /// Same, for an explicit viewport (used by tests and offscreen captures).
    [[nodiscard]] int blocksPerTexelFor(const QSize& viewport, double px_per_block) const;

    /// Number of regions the view spans, including the shadow margin. Every
    /// visible region must get its own slot, so this is what sets the coarsest
    /// resolution the atlas has to be able to hold.
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
    /// Mark every slot stale after a resolution or dimension change. No texture
    /// writes: the visible slots are refilled in the frame that follows.
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
        int bp{0};  // atlas resolution the slot's content was uploaded at
        bool assigned{false};
    };

    struct UploadRequest {
        bl::chunk_pos region;
        int slot{0};
    };

    /// The shadow ray's reach in world blocks, shared by the shader, the fit
    /// calculation and the region collection.
    [[nodiscard]] int shadowReachBlocks() const;

    [[nodiscard]] int slotsPerSide() const { return kAtlasTexels / (kRegionBlocks / blocks_per_texel_); }

    void queueUpload(UploadRequest request);

    void uploadRegion(const UploadRequest& request);

    /// Build the two chessboard tiles the CPU map shows where there is no
    /// terrain: dark for "no chunks here", light for "not loaded yet".
    void buildBlankTiles();

    void collectVisibleRegions(double px_per_block, const QPointF& world_origin);

    void drawStats(QPainter& painter, double frame_ms, double px_per_block);

    AsyncLevelLoader* level_loader_;
    MapView* view_{nullptr};
    /// The overlay layers, shared with the CPU map so both draw them identically.
    /// Null when the widget is used purely as an offscreen capture target.
    MapOverlays* overlays_{nullptr};
    /// Owns the map state and the editing actions the context menu needs. The
    /// menu is the same for both renderers; this widget supplies the click.
    MapHost* host_{nullptr};

    QOpenGLShaderProgram* shader_{nullptr};
    GLuint vao_{0};
    GLuint color_texture_{0};
    GLuint height_texture_{0};

    int atlas_dim_{-1};        // dimension the atlas currently holds, -1 = nothing
    int blocks_per_texel_{1};  // atlas resolution, see blocksPerTexelFor()
    // Atlas colours depend on the selected base layer.  Changing layer must
    // evict the old terrain/biome texels even when the region data is unchanged.
    bool atlas_biome_layer_{false};
    // Only visible slots are resident. A dense side*side table becomes enormous
    // at coarse LODs, while the renderer only touches the current viewport.
    std::unordered_map<int, SlotState> slots_;
    std::deque<UploadRequest> uploads_;
    std::vector<unsigned char> color_buffer_;
    std::vector<float> height_buffer_;
    /// Prebuilt background tiles, uploaded as-is when a slot has no terrain.
    std::vector<unsigned char> blank_dark_;
    std::vector<unsigned char> blank_light_;

    bool shadow_enabled_{true};
    int shadow_steps_{48};
    float penumbra_{0.18f};
    float ao_strength_{0.10f};
    int ao_directions_{16};
    int ao_steps_{16};
    // The march spans this radius in blocks; the near field is sampled finely and the
    // reach is bounded, so nothing beyond it can darken a pixel.
    static constexpr float ao_step0_{0.5f};
    static constexpr float ao_radius_{16.0f};
    float bevel_strength_{1.0f};
    float saturation_{1.0f};
    float brightness_{1.0f};
    float shadow_strength_{1.0f};
    bool flat_shading_{false};

    bool capture_view_{false};
    QPointF capture_center_block_{0.0, 0.0};
    double capture_px_per_block_{4.0};
    ImportOverlay* import_{nullptr};
    MapInteraction* interaction_{nullptr};
    /// Repaints while the debug window is on, so its memory/frame counters keep
    /// up on a still view. The CPU map has the same timer.
    QTimer* debug_refresh_timer_{nullptr};

    // stats for the overlay
    double last_frame_ms_{0};
    bool gpu_timing_{false};
    int uploaded_last_frame_{0};
    int total_uploads_{0};
    int visible_regions_{0};
};

#endif  // BEDROCKMAP_GPUMAPWIDGET_H
