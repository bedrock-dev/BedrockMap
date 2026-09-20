#ifndef BEDROCKMAP_RENDERBENCH_H
#define BEDROCKMAP_RENDERBENCH_H

#include <QString>
#include <optional>

// Headless profiling entry point for the 2D map path, enabled with
// `BedrockMap.exe --bench-render <level path>`. It bakes a set of regions the
// way LoadRegionTask does, prints the per-phase cost, and then measures how long
// QPainter needs to composite those tiles at several zoom levels (the work
// CpuMapWidget::paintEvent does every frame). The GUI is never created, so the
// numbers are not affected by widgets or a swap chain.
namespace renderbench {

    struct Request {
        QString level_path;
        int region_count{16};
        int center_region_x{0};
        int center_region_z{0};
        int dim{0};
        bool sweep{false};  // re-bake at several tile/shadow resolutions
        bool paint{true};
        int shadow_steps{48};
        bool no_shadow{false};
        bool flat_shading{false};
        /// Ambient occlusion strength for the GPU captures; 0 disables it.
        float ao_strength{0.22f};
        /// Resolution of the AO march (azimuths, samples per azimuth, first step).
        int ao_directions{8};
        int ao_steps{12};
        /// Sweep the march resolution and report quality against cost.
        bool ao_quality_sweep{false};
        /// Time the GPU frame and compare it against the CPU bake cost.
        bool gpu_timing{false};
        /// Construct the real widgets and paint them offscreen, as a check that
        /// the shared view and the event wiring hold together.
        bool widget_smoke{false};
        /// Walk the zoom range and report how much of each frame is still
        /// background right after a resolution change.
        bool zoom_sweep{false};
        /// Check the atlas always has a slot for every region the view spans
        /// across the whole zoom range.
        bool coverage_check{false};
        /// Measure the background checkerboard and fail if its cells are uneven.
        bool blank_check{false};
        /// Break down the chunk-coordinate preload scan: the shape of the key
        /// space, and how much of the time is LevelDB iteration versus per-key work.
        bool coords_bench{false};
        /// Compare ambient occlusion on and off, and check it is subtractive and
        /// concentrated in corners rather than applied uniformly.
        bool ao_check{false};
        /// Run the shader over a manufactured height field and check the shading
        /// of each corner type. Needs no level, so level_path may be empty.
        bool shade_probe{false};
        /// When set, write CPU-style tile images and GPU renderer captures with
        /// this filename prefix.
        QString shot_prefix;
    };

    /// Returns std::nullopt unless --bench-render was given on the command line.
    std::optional<Request> parseRequest(int argc, char* argv[]);

    /// Bakes, prints the report and returns a process exit code.
    int run(const Request& request);

}  // namespace renderbench

#endif  // BEDROCKMAP_RENDERBENCH_H
