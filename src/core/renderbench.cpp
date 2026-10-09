#include "renderbench.h"

#include <leveldb/decompress_allocator.h>
#include <leveldb/options.h>

#include <QElapsedTimer>
#include <QFile>
#include <QImage>
#include <QMenu>
#include <QObject>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QOpenGLFramebufferObject>
#include <QOpenGLShaderProgram>
#include <QOpenGLVertexArrayObject>
#include <QPainter>
#include <QRectF>
#include <QSlider>
#include <QStackedWidget>
#include <QTreeWidget>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

#include "asynclevelloader.h"
#include "bedrock_key.h"
#include "chunk_task.h"
#include "chunkcoords.h"
#include "chunkregion.h"
#include "config.h"
#include "contextmenubuilder.h"
#include "cpumapwidget.h"
#include "floatingtoolbar.h"
#include "gpumapwidget.h"
#include "leveldbxor.h"
#include "levelpagewidget.h"
#include "leveltabwidget.h"
#include "maphost.h"
#include "maptile.h"
#include "mapview.h"
#include "raw_chunk.h"
#include "renderprofile.h"
#include "settingsdialog.h"

namespace renderbench {

    namespace {

        using Clock = std::chrono::steady_clock;
        using PhaseArray = std::array<int64_t, static_cast<size_t>(RenderPhase::Count)>;

        struct BakedRegion {
            bl::chunk_pos pos{};
            ChunkRegion* region{nullptr};
            int64_t total_us{0};
            PhaseArray phases{};
        };

        void configureGpu(GpuMapWidget& gpu, const Request& request) {
            auto options = gpu.gpuOptions();
            options.shadow_steps = request.shadow_steps;
            options.shadow_enabled = !request.no_shadow;
            options.flat_shading = request.flat_shading;
            options.ao_strength = request.ao_strength;
            options.ao_directions = request.ao_directions;
            options.ao_steps = request.ao_steps;
            gpu.setGpuOptions(options);
        }

        PhaseArray snapshot() {
            PhaseArray out{};
            for (size_t i = 0; i < out.size(); i++) out[i] = RenderProfile::instance().get(static_cast<RenderPhase>(i));
            return out;
        }

        /// Bakes one region through the real LoadRegionTask, synchronously.
        BakedRegion bakeRegion(AsyncLevelLoader& loader, const bl::chunk_pos& region_pos, const MapFilter& filter) {
            BakedRegion out;
            out.pos = region_pos;

            const auto before = snapshot();
            const auto t0 = Clock::now();

            LoadRegionTask task(&loader, region_pos, filter);
            QObject::connect(&task, &LoadRegionTask::finish,
                             [&out](int, int, int, ChunkRegion* region, long long, long long, bl::chunk**) { out.region = region; });
            task.run();

            out.total_us = microsSince(t0);
            const auto after = snapshot();
            for (size_t i = 0; i < after.size(); i++) out.phases[i] = after[i] - before[i];
            return out;
        }

        void printPhaseTable(const std::vector<BakedRegion>& regions, const std::string& title) {
            const double n = static_cast<double>(std::max<size_t>(regions.size(), 1));
            std::printf("\n== %s - %zu baked regions ==\n", title.c_str(), regions.size());
            std::printf("%-14s %12s %14s\n", "phase", "total ms", "us / region");

            int64_t total = 0;
            for (size_t i = 0; i < static_cast<size_t>(RenderPhase::Count); i++) {
                int64_t sum = 0;
                for (const auto& r : regions) sum += r.phases[i];
                total += sum;
                if (sum == 0) continue;
                std::printf("%-14s %12.1f %14.0f\n", RenderProfile::name(static_cast<RenderPhase>(i)), sum / 1000.0, sum / n);
            }
            int64_t wall = 0;
            for (const auto& r : regions) wall += r.total_us;
            std::printf("%-14s %12.1f %14.0f  (sum of the phases above: %.1f ms)\n", "wall", wall / 1000.0, wall / n, total / 1000.0);
        }

        /// Enumerates candidate regions around the requested centre, nearest first.
        std::vector<bl::chunk_pos> candidateRegions(const Request& request) {
            struct Candidate {
                int dx, dz;
            };
            std::vector<Candidate> offsets;
            constexpr int RADIUS = 32;  // regions
            for (int dx = -RADIUS; dx <= RADIUS; dx++) {
                for (int dz = -RADIUS; dz <= RADIUS; dz++) offsets.push_back({dx, dz});
            }
            std::sort(offsets.begin(), offsets.end(),
                      [](const Candidate& a, const Candidate& b) { return a.dx * a.dx + a.dz * a.dz < b.dx * b.dx + b.dz * b.dz; });

            std::vector<bl::chunk_pos> out;
            out.reserve(offsets.size());
            for (const auto& o : offsets) {
                out.emplace_back((request.center_region_x + o.dx) * constant::RW, (request.center_region_z + o.dz) * constant::RW,
                                 request.dim);
            }
            return out;
        }

        void runPaintBench(const std::vector<BakedRegion>& regions, int canvas_w, int canvas_h) {
            std::vector<const QImage*> tiles;
            for (const auto& r : regions) {
                if (r.region && !r.region->terrain_bake_image_.isNull()) tiles.push_back(&r.region->terrain_bake_image_);
            }
            if (tiles.empty()) {
                std::printf("paint bench skipped: no baked tiles\n");
                return;
            }

            QImage canvas(canvas_w, canvas_h, QImage::Format_RGB32);
            canvas.fill(Qt::black);

            std::printf("\n== paint bench - %dx%d canvas, %zu distinct region images ==\n", canvas_w, canvas_h, tiles.size());
            std::printf("%11s %7s %14s %14s %14s\n", "px/chunk", "tiles", "offscreen us", "smooth us", "1px/block us");

            for (double px_per_chunk : {4.0, 8.0, 16.0, 32.0, 64.0, 128.0, 256.0, 512.0, 1024.0}) {
                const double tile_px = constant::RW * px_per_chunk;
                const int cols = std::max(1, static_cast<int>(std::ceil(canvas_w / tile_px)));
                const int rows = std::max(1, static_cast<int>(std::ceil(canvas_h / tile_px)));
                const int tile_count = cols * rows;

                // 1 px/block composite covering the viewport, drawn once and scaled up.
                const int comp_w = cols * (constant::RW << 4);
                const int comp_h = rows * (constant::RW << 4);
                QImage composite(comp_w, comp_h, QImage::Format_RGB32);
                composite.fill(Qt::black);
                {
                    std::vector<QImage> low_res;
                    low_res.reserve(tiles.size());
                    for (const auto* img : tiles)
                        low_res.push_back(img->scaled(constant::RW << 4, constant::RW << 4, Qt::IgnoreAspectRatio, Qt::FastTransformation));
                    QPainter cp(&composite);
                    int k = 0;
                    for (int c = 0; c < cols; c++) {
                        for (int r = 0; r < rows; r++) {
                            cp.drawImage(QPointF(c * (constant::RW << 4), r * (constant::RW << 4)), low_res[k++ % low_res.size()]);
                        }
                    }
                }

                double offscreen_us = 0;
                double smooth_us = 0;
                double composite_us = 0;
                for (int rep = 0; rep < 3; rep++) {
                    // Current paintEvent behaviour: one drawImage per visible region.
                    {
                        QElapsedTimer t;
                        t.start();
                        QPainter p(&canvas);
                        p.setRenderHint(QPainter::SmoothPixmapTransform, false);
                        p.scale(px_per_chunk, px_per_chunk);
                        int k = 0;
                        for (int c = 0; c < cols; c++) {
                            for (int r = 0; r < rows; r++) {
                                const QImage& img = *tiles[k++ % tiles.size()];
                                p.drawImage(QRectF(c * constant::RW, r * constant::RW, constant::RW, constant::RW), img, img.rect());
                            }
                        }
                        const double us = t.nsecsElapsed() / 1000.0;
                        offscreen_us = (rep == 0) ? us : std::min(offscreen_us, us);
                    }
                    {
                        QElapsedTimer t;
                        t.start();
                        QPainter p(&canvas);
                        p.setRenderHint(QPainter::SmoothPixmapTransform, true);
                        p.scale(px_per_chunk, px_per_chunk);
                        int k = 0;
                        for (int c = 0; c < cols; c++) {
                            for (int r = 0; r < rows; r++) {
                                const QImage& img = *tiles[k++ % tiles.size()];
                                p.drawImage(QRectF(c * constant::RW, r * constant::RW, constant::RW, constant::RW), img, img.rect());
                            }
                        }
                        const double us = t.nsecsElapsed() / 1000.0;
                        smooth_us = (rep == 0) ? us : std::min(smooth_us, us);
                    }
                    {
                        QElapsedTimer t;
                        t.start();
                        QPainter p(&canvas);
                        p.setRenderHint(QPainter::SmoothPixmapTransform, true);
                        p.scale(px_per_chunk / 16.0, px_per_chunk / 16.0);
                        p.drawImage(QPointF(0, 0), composite);
                        const double us = t.nsecsElapsed() / 1000.0;
                        composite_us = (rep == 0) ? us : std::min(composite_us, us);
                    }
                }
                std::printf("%11.2f %7d %14.0f %14.0f %14.0f\n", px_per_chunk, tile_count, offscreen_us, smooth_us, composite_us);
            }
            std::printf(
                "(fast transform = no SmoothPixmapTransform, i.e. today's paint path;\n"
                " 1px/block = one drawImage of a 1 px-per-block composite covering the viewport)\n");
        }

        /// Count distinct sampled pixels.
        int countDistinctColors(const QImage& image) {
            QSet<QRgb> seen;
            for (int y = 0; y < image.height(); y += 3) {
                for (int x = 0; x < image.width(); x += 3) seen.insert(image.pixel(x, y));
            }
            return static_cast<int>(seen.size());
        }

        /// Compare GPU and CPU region orientation cell by cell.
        /// `region_block` locates the region inside the capture.
        void checkOrientation(const QImage& gpu_frame, const QImage& cpu_tile, const QPointF& centre_block, double px_per_block,
                              const QPointF& region_block) {
            constexpr int CELLS = 8;
            constexpr double LOGICAL_W = 1024.0;
            constexpr double LOGICAL_H = 640.0;
            const double dpr = gpu_frame.width() / LOGICAL_W;
            const double world_left = centre_block.x() - LOGICAL_W / px_per_block / 2.0;
            const double world_top = centre_block.y() - LOGICAL_H / px_per_block / 2.0;
            const int x0 = static_cast<int>(std::lround((region_block.x() - world_left) * dpr));
            const int y0 = static_cast<int>(std::lround((region_block.y() - world_top) * dpr));
            const int side = static_cast<int>(std::lround(128 * dpr));
            constexpr int STATS_OVERLAY_PX = 40;  // the stats bar covers the top of the widget
            if (x0 < 0 || y0 < STATS_OVERLAY_PX || x0 + side > gpu_frame.width() || y0 + side > gpu_frame.height()) {
                std::printf("renderbench: orientation check skipped (region overlaps the widget edge or stats bar)\n");
                return;
            }

            const auto grid = [CELLS](const QImage& image) {
                std::vector<std::vector<double>> rows;
                for (int cy = 0; cy < CELLS; cy++) {
                    std::vector<double> row;
                    for (int cx = 0; cx < CELLS; cx++) {
                        const int px0 = cx * image.width() / CELLS;
                        const int py0 = cy * image.height() / CELLS;
                        const int px1 = std::max(px0 + 1, (cx + 1) * image.width() / CELLS);
                        const int py1 = std::max(py0 + 1, (cy + 1) * image.height() / CELLS);
                        double sum = 0;
                        int n = 0;
                        for (int y = py0; y < py1; y++) {
                            for (int x = px0; x < px1; x++) {
                                const QRgb p = image.pixel(x, y);
                                sum += (qRed(p) + qGreen(p) + qBlue(p)) / 3.0;
                                n++;
                            }
                        }
                        row.push_back(n > 0 ? sum / n : 0.0);
                    }
                    rows.push_back(std::move(row));
                }
                return rows;
            };
            const auto rms = [](const std::vector<std::vector<double>>& a, const std::vector<std::vector<double>>& b) {
                double acc = 0;
                size_t n = 0;
                for (size_t r = 0; r < a.size(); r++) {
                    for (size_t c = 0; c < a[r].size(); c++) {
                        acc += (a[r][c] - b[r][c]) * (a[r][c] - b[r][c]);
                        n++;
                    }
                }
                return n > 0 ? std::sqrt(acc / static_cast<double>(n)) : 0.0;
            };

            const auto cpu = grid(cpu_tile);
            const auto gpu = grid(gpu_frame.copy(x0, y0, side, side));
            auto mirrored = gpu;
            std::reverse(mirrored.begin(), mirrored.end());  // rows only, i.e. a vertical flip
            const double normal = rms(cpu, gpu);
            const double flipped = rms(cpu, mirrored);
            std::printf("renderbench: orientation vs CPU tile: rms=%.1f mirrored=%.1f -> %s\n", normal, flipped,
                        normal < flipped ? "same way up" : "VERTICALLY MIRRORED");
        }

        /// Measure CPU shading cost per region.
        double measureCpuStyleCostMs(AsyncLevelLoader& loader, const std::vector<BakedRegion>& regions, const MapFilter& filter) {
            if (regions.empty()) return 0.0;
            auto original = setting::current();
            auto style2 = original;
            style2.MAP_RENDER_STYLE = 2;
            setting::apply(style2);

            constexpr int REPEATS = 3;
            RenderProfile::instance().reset();
            for (int i = 0; i < REPEATS; i++) {
                for (const auto& sample : regions) loader.invalidateRegionTiles({sample.pos});
                for (const auto& sample : regions) {
                    auto baked = bakeRegion(loader, sample.pos, filter);
                    delete baked.region;
                }
            }
            setting::apply(original);

            const auto& prof = RenderProfile::instance();
            const int64_t shading = prof.get(RenderPhase::Bevel) + prof.get(RenderPhase::ShadowGather) + prof.get(RenderPhase::ShadowRay) +
                                    prof.get(RenderPhase::ShadowApply);
            const int64_t baked_regions = prof.regions.load(std::memory_order_relaxed);
            return baked_regions > 0 ? static_cast<double>(shading) / baked_regions / 1000.0 : 0.0;
        }

        /// Compare GPU frame shading with CPU per-region shading.
        void runGpuTimingBench(const Request& request, AsyncLevelLoader& loader, const std::vector<BakedRegion>& regions,
                               const MapFilter& filter) {
            const auto& centre_region = regions.front().pos;
            const QPointF centre_block(centre_region.x * 16.0 + 64.0, centre_region.z * 16.0 + 64.0);
            const QSize size(1920, 1080);
            const double per_region_cpu = measureCpuStyleCostMs(loader, regions, filter);
            constexpr int WARM_FRAMES = 20;

            std::printf("\n== gpu frame timing - %dx%d widget ==\n", size.width(), size.height());
            std::printf("%11s %9s %12s %12s %14s %14s\n", "px/block", "regions", "cold ms", "warm ms", "cpu shade ms/reg", "cpu ms/screen");

            for (double px_per_block : {0.5, 1.0, 2.0, 4.0, 8.0, 16.0}) {
                MapView capture_view;
                GpuMapWidget gpu(nullptr, &loader, &capture_view, nullptr, nullptr, nullptr);
                configureGpu(gpu, request);
                gpu.setGpuTimingEnabled(true);

                // Cold: every frame until the visible area is fully baked and
                // uploaded, summed - what a freshly panned-to screen costs.
                QImage image;
                double cold_ms = 0;
                int frames = 0;
                for (; frames < 600; frames++) {
                    image = gpu.captureOffscreen(size, centre_block, px_per_block);
                    cold_ms += gpu.lastFrameMs();
                    QCoreApplication::processEvents();
                    if (frames > 0 && loader.pendingRegionTasks() == 0 && gpu.pendingUploadCount() == 0) break;
                }
                const int visible = gpu.visibleRegionCount();

                // Warm: nothing to upload, so this is pure per-frame shading.
                double warm_total = 0;
                for (int i = 0; i < WARM_FRAMES; i++) {
                    image = gpu.captureOffscreen(size, centre_block, px_per_block);
                    warm_total += gpu.lastFrameMs();
                }
                const double warm_ms = warm_total / WARM_FRAMES;

                std::printf("%11.2f %9d %12.1f %12.3f %14.1f %14.1f\n", px_per_block, visible, cold_ms, warm_ms, per_region_cpu,
                            per_region_cpu * visible);
            }
            std::printf(
                "(warm = mean paintGL per frame with an unchanged atlas, glFinish() included, i.e. what a pan/zoom\n"
                " frame costs. cold = summed frames until the visible area is fully uploaded, i.e. the one-time cost\n"
                " of newly revealed terrain. cpu ms/screen = visible regions x the CPU style-2 shading cost, which is\n"
                " what the CPU map pays to show the same newly revealed area. Neither side includes the chunk read +\n"
                " surface bake, which both paths share.)\n");

            // The shadow ray is the knob that makes the CPU path unusable while the
            // GPU path barely notices it, so measure the GPU side across its range.
            std::printf("\n== gpu frame time vs shadow ray length (4 px/block, %dx%d) ==\n", size.width(), size.height());
            std::printf("%12s %14s\n", "shadow steps", "warm ms");
            MapView ray_view;
            GpuMapWidget gpu(nullptr, &loader, &ray_view, nullptr, nullptr, nullptr);
            gpu.setGpuTimingEnabled(true);
            for (int steps : {0, 8, 16, 32, 48, 64, 96, 128}) {
                auto options = gpu.gpuOptions();
                options.shadow_enabled = steps > 0;
                options.shadow_steps = std::max(1, steps);
                gpu.setGpuOptions(options);
                QImage image;
                for (int i = 0; i < 600; i++) {
                    image = gpu.captureOffscreen(size, centre_block, 4.0);
                    QCoreApplication::processEvents();
                    if (i > 0 && loader.pendingRegionTasks() == 0 && gpu.pendingUploadCount() == 0) break;
                }
                double total = 0;
                for (int i = 0; i < WARM_FRAMES; i++) {
                    image = gpu.captureOffscreen(size, centre_block, 4.0);
                    total += gpu.lastFrameMs();
                }
                std::printf("%12d %14.3f\n", steps, total / WARM_FRAMES);
            }
            std::printf(
                "(the CPU equivalent of this knob is SHADOW_MAP_SCALE, which costs about shadow_scale^3:\n"
                " 18 / 142 / 1112 ms per region at 2 / 4 / 8 from --bench-sweep. The ray length here is not a\n"
                " comparable parameter; what matters is that the GPU cost stays flat as quality goes up.)\n");
        }

        /// Compare first-frame costs for a newly visible screen.
        void runScreenCostBench(const Request& request, AsyncLevelLoader& loader, const std::vector<BakedRegion>& regions,
                                const MapFilter& filter) {
            const auto& centre_region = regions.front().pos;
            const QPointF centre_block(centre_region.x * 16.0 + 64.0, centre_region.z * 16.0 + 64.0);
            const QSize size(1920, 1080);
            constexpr double PX_PER_BLOCK = 4.0;
            constexpr int SCREEN_REGIONS = 15;  // ~1920x1080 at 4 px/block, in 8x8-chunk regions

            std::printf("\n== fresh screen, 1920x1080 at 4 px/block (%d regions) ==\n", SCREEN_REGIONS);

            auto saved = setting::current();
            const auto measure_cpu = [&](int style, int shadow_scale, const char* label) {
                auto s = saved;
                s.MAP_RENDER_STYLE = style;
                s.SHADOW_MAP_SCALE = shadow_scale;
                setting::apply(s);

                const auto start = std::chrono::steady_clock::now();
                int baked = 0;
                for (const auto& sample : regions) {
                    if (baked >= SCREEN_REGIONS) break;
                    loader.invalidateRegionTiles({sample.pos});
                    auto result = bakeRegion(loader, sample.pos, filter);
                    delete result.region;
                    baked++;
                }
                const double wall_ms = microsSince(start) / 1000.0;
                std::printf("%-34s %10.1f ms  (bake of %d regions; add the per-frame composite for the full frame)\n", label, wall_ms,
                            baked);
            };
            measure_cpu(1, 2, "cpu style 1");
            measure_cpu(2, 2, "cpu style 2, shadow scale 2");
            measure_cpu(2, 8, "cpu style 2, shadow scale 8");
            setting::apply(saved);

            // GPU: the same regions, but only the shared bake plus the upload.
            MapView screen_view;
            GpuMapWidget gpu(nullptr, &loader, &screen_view, nullptr, nullptr, nullptr);
            configureGpu(gpu, request);
            gpu.setGpuTimingEnabled(true);
            for (const auto& sample : regions) loader.invalidateRegionTiles({sample.pos});
            const auto gpu_start = std::chrono::steady_clock::now();
            double gpu_paint_total = 0;
            QImage image;
            for (int i = 0; i < 600; i++) {
                image = gpu.captureOffscreen(size, centre_block, PX_PER_BLOCK);
                gpu_paint_total += gpu.lastFrameMs();
                QCoreApplication::processEvents();
                if (i > 0 && loader.pendingRegionTasks() == 0 && gpu.pendingUploadCount() == 0) break;
            }
            const double gpu_wall = microsSince(gpu_start) / 1000.0;
            std::printf("%-34s %10.1f ms  (wall, %.1f ms of it in paintGL, %d regions resident)\n", "gpu, same regions", gpu_wall,
                        gpu_paint_total, gpu.residentRegionCount());
            std::printf(
                "(all rows include the shared chunk read + surface bake for these regions, and two caveats apply: the cpu\n"
                " rows bake one region at a time on this thread while the gpu rows go through the loader's %d-thread pool,\n"
                " so the cpu wall is pessimistic by roughly the thread count; and the gpu wall contains the offscreen\n"
                " framebuffer readback, which an on-screen widget never pays. The quantity that separates the two designs is\n"
                " the shading: every cpu row adds a style pass per region, the gpu row shades per frame.)\n",
                setting::current().THREAD_NUM);
        }

        /// Find a terrain pixel so high-zoom captures avoid empty holes.
        QPointF terrainCentreBlock(const std::vector<BakedRegion>& regions) {
            constexpr int HALF = (constant::RW << 4) / 2;
            for (const auto& sample : regions) {
                if (!sample.region) continue;
                const auto& tips = sample.region->tips_info_;
                // Search outwards from the middle so a crop stays inside the region.
                for (int radius = 0; radius < HALF; radius += 4) {
                    for (int x = std::max(0, HALF - radius); x <= std::min(2 * HALF - 1, HALF + radius); x += 4) {
                        for (int z = std::max(0, HALF - radius); z <= std::min(2 * HALF - 1, HALF + radius); z += 4) {
                            if (tips[x][z].height > -128) return QPointF(sample.pos.x * 16.0 + x, sample.pos.z * 16.0 + z);
                        }
                    }
                }
            }
            return QPointF(64.0, 64.0);  // fall back to the region centre
        }

        /// Capture representative CPU and GPU frames.
        void runShotBench(const Request& request, AsyncLevelLoader& loader, const std::vector<BakedRegion>& regions) {
            const QString& prefix = request.shot_prefix;
            for (size_t i = 0; i < regions.size() && i < 4; i++) {
                const auto& image = regions[i].region->terrain_bake_image_;
                const QString path = QString("%1_cpu_%2.png").arg(prefix).arg(i);
                if (image.save(path)) {
                    std::printf("renderbench: wrote %s (%dx%d, CPU style)\n", path.toStdString().c_str(), image.width(), image.height());
                }
            }

            const auto& centre_region = regions.front().pos;
            const QPointF centre_block = terrainCentreBlock(regions);
            // 1 and 2 px/block compare directly against the CPU tiles; the high
            // end shows individual block shading (bevel/AO), the fractions
            // exercise the reduced-resolution levels reached when zooming out.
            for (double px_per_block : {16.0, 8.0, 4.0, 2.0, 1.0, 0.5, 0.25, 0.125}) {
                MapView capture_view;
                GpuMapWidget gpu(nullptr, &loader, &capture_view, nullptr, nullptr, nullptr);
                configureGpu(gpu, request);
                const QSize size(1024, 640);
                QImage image;
                int frames = 0;
                for (; frames < 600; frames++) {
                    image = gpu.captureOffscreen(size, centre_block, px_per_block);
                    const QString progress = QString("frame %1: visible=%2 uploads=%3 resident=%4 loaderPending=%5")
                                                 .arg(frames)
                                                 .arg(gpu.visibleRegionCount())
                                                 .arg(gpu.pendingUploadCount())
                                                 .arg(gpu.residentRegionCount())
                                                 .arg(loader.pendingRegionTasks());
                    if (frames % 50 == 0) std::printf("renderbench:   %s\n", progress.toStdString().c_str());
                    QCoreApplication::processEvents();  // deliver the finished bakes
                    if (frames > 0 && loader.pendingRegionTasks() == 0 && gpu.pendingUploadCount() == 0) break;
                }
                const QString path = QString("%1_gpu_%2px.png").arg(prefix).arg(px_per_block, 0, 'g', 3);
                if (image.save(path)) {
                    std::printf("renderbench: wrote %s (%dx%d, GPU, %g px/block, %d frames, %d distinct colours, %d resident)\n",
                                path.toStdString().c_str(), image.width(), image.height(), px_per_block, frames + 1,
                                countDistinctColors(image), gpu.residentRegionCount());
                } else {
                    std::printf("renderbench: FAILED to capture GPU frame\n");
                }
                // The capture is 1:1 with the CPU tile's texel grid, so it is the
                // one that can be compared against the tile directly.
                if (px_per_block == 1.0) {
                    const QPointF region_block(regions.front().pos.x * 16.0, regions.front().pos.z * 16.0);
                    checkOrientation(image, regions.front().region->terrain_bake_image_, centre_block, px_per_block, region_block);
                }
            }
        }

        /// Smoke-test both renderers with one shared host and view.
        int runWidgetSmoke(AsyncLevelLoader& loader) {
            MapHost host(nullptr, &loader);
            CpuMapWidget map(nullptr, &loader, host.mapView(), &host.overlays(), host.importOverlay(), &host);
            map.resize(800, 600);

            GpuMapWidget gpu(nullptr, &loader, host.mapView(), &host.overlays(), host.importOverlay(), &host);
            gpu.resize(800, 600);
            gpu.setGpuTimingEnabled(true);
            // Keep smoke-test shading independent of config.ini.
            auto options = gpu.gpuOptions();
            options.bevel_strength = 1.0f;
            options.saturation = 1.0f;
            options.brightness = 1.0f;
            gpu.setGpuOptions(options);

            for (int i = 0; i < 300; i++) {
                QCoreApplication::processEvents();
                if (i > 0 && loader.pendingRegionTasks() == 0) break;
            }

            QImage cpu_frame(map.size(), QImage::Format_RGB32);
            int cpu_colors = 0;
            for (int i = 0; i < 300; i++) {
                cpu_frame.fill(Qt::black);
                map.render(&cpu_frame);
                cpu_colors = countDistinctColors(cpu_frame);
                QCoreApplication::processEvents();
                if (i > 0 && cpu_colors > 4 && loader.pendingRegionTasks() == 0) break;
            }

            QImage gpu_frame;
            for (int i = 0; i < 300; i++) {
                gpu_frame = gpu.captureOffscreen(QSize(800, 600), QPointF(64, 64), 4.0);
                QCoreApplication::processEvents();
                if (i > 0 && loader.pendingRegionTasks() == 0 && gpu.pendingUploadCount() == 0) break;
            }

            MapView* view = host.mapView();
            const QTransform before = view->worldToView();
            view->zoomBy(1.5, QPointF(400, 300));
            const bool zoom_applied = before != view->worldToView();
            const bool same_view = gpu.view() == view;

            const int gpu_colors = countDistinctColors(gpu_frame);
            std::printf("renderbench: widget smoke - cpu frame %dx%d, %d colours\n", cpu_frame.width(), cpu_frame.height(), cpu_colors);
            std::printf("renderbench: widget smoke - gpu frame %dx%d, %d colours\n", gpu_frame.width(), gpu_frame.height(), gpu_colors);
            std::printf("renderbench: widget smoke - zoom moves the shared transform: %s; same view object: %s\n",
                        zoom_applied ? "yes" : "NO", same_view ? "yes" : "NO");

            const QColor grid_color(setting::current().GRID_LINE_COLOR);
            const auto count_grid_pixels = [&](const QImage& image) {
                int found = 0;
                for (int y = 0; y < image.height(); y += 2) {
                    for (int x = 0; x < image.width(); x += 2) {
                        const QRgb p = image.pixel(x, y);
                        if (std::abs(qRed(p) - grid_color.red()) <= 2 && std::abs(qGreen(p) - grid_color.green()) <= 2 &&
                            std::abs(qBlue(p) - grid_color.blue()) <= 2) {
                            ++found;
                        }
                    }
                }
                return found;
            };
            const auto render_cpu = [&]() {
                QImage frame(map.size(), QImage::Format_RGB32);
                frame.fill(Qt::black);
                map.render(&frame);
                QCoreApplication::processEvents();
                return frame;
            };
            const auto render_gpu = [&]() {
                QImage frame = gpu.grabFramebuffer();
                QCoreApplication::processEvents();
                return frame;
            };

            view->setScale(64.0, QPointF(400, 300));
            for (int i = 0; i < 200; i++) {
                QCoreApplication::processEvents();
                if (i > 0 && loader.pendingRegionTasks() == 0) break;
            }

            host.setOther(RenderOption::Grid, false);
            const int cpu_grid_off = count_grid_pixels(render_cpu());
            const int gpu_grid_off = count_grid_pixels(render_gpu());
            host.setOther(RenderOption::Grid, true);
            const int cpu_grid_on = count_grid_pixels(render_cpu());
            const int gpu_grid_on = count_grid_pixels(render_gpu());
            host.setOther(RenderOption::Grid, false);

            std::printf("renderbench: widget smoke - grid pixels (sampled): cpu %d -> %d, gpu %d -> %d\n", cpu_grid_off, cpu_grid_on,
                        gpu_grid_off, gpu_grid_on);
            const bool overlays_ok = cpu_grid_on > cpu_grid_off + 20 && gpu_grid_on > gpu_grid_off + 20;
            std::printf("renderbench: widget smoke - overlays: %s\n", overlays_ok ? "grid drawn by both renderers" : "MISSING");
            int failures = overlays_ok ? 0 : 1;

            struct Chroma {
                double max_chroma{0};
                double mean_luma{0};
            };
            const auto measure_chroma = [](const QImage& image) {
                Chroma result;
                int count = 0;
                for (int y = 0; y < image.height(); y += 2) {
                    for (int x = 0; x < image.width(); x += 2) {
                        const QRgb p = image.pixel(x, y);
                        const int rg = std::abs(static_cast<int>(qRed(p)) - static_cast<int>(qGreen(p)));
                        const int gb = std::abs(static_cast<int>(qGreen(p)) - static_cast<int>(qBlue(p)));
                        const int rb = std::abs(static_cast<int>(qRed(p)) - static_cast<int>(qBlue(p)));
                        result.max_chroma = std::max(result.max_chroma, static_cast<double>(std::max({rg, gb, rb})));
                        result.mean_luma += 0.2126 * qRed(p) + 0.7152 * qGreen(p) + 0.0722 * qBlue(p);
                        ++count;
                    }
                }
                if (count > 0) result.mean_luma /= count;
                return result;
            };
            const Chroma colour_stats = measure_chroma(gpu_frame);
            options = gpu.gpuOptions();
            options.saturation = 0.0f;
            gpu.setGpuOptions(options);
            const Chroma grey_stats = measure_chroma(gpu.captureOffscreen(QSize(800, 600), QPointF(64, 64), 4.0));
            options.saturation = 1.0f;
            gpu.setGpuOptions(options);
            const bool saturation_ok = colour_stats.max_chroma > 4.0 && grey_stats.max_chroma <= 1.0 &&
                                       std::abs(grey_stats.mean_luma - colour_stats.mean_luma) <= 2.0;
            std::printf("renderbench: widget smoke - saturation 1: chroma %.0f, luma %.1f; saturation 0: chroma %.0f, luma %.1f: %s\n",
                        colour_stats.max_chroma, colour_stats.mean_luma, grey_stats.max_chroma, grey_stats.mean_luma,
                        saturation_ok ? "ok" : "WRONG");
            if (!saturation_ok) ++failures;

            {
                const QPointF local(200.0, 150.0);
                const bl::chunk_pos cpu_chunk = host.mapView()->viewPosToChunkPos(local);
                const bl::chunk_pos gpu_chunk = host.mapView()->chunkPosAt(local, gpu.size());
                const QPoint cpu_block = host.mapView()->viewPosToBlockPos(local);
                const QPoint gpu_block = host.mapView()->blockPosAt(local, gpu.size());
                std::printf(
                    "renderbench: widget smoke - click(200,150): cpu chunk (%d,%d) block (%d,%d); gpu chunk (%d,%d) block (%d,%d)\n",
                    cpu_chunk.x, cpu_chunk.z, cpu_block.x(), cpu_block.y(), gpu_chunk.x, gpu_chunk.z, gpu_block.x(), gpu_block.y());
                const bool clicks_agree = cpu_chunk == gpu_chunk && cpu_block == gpu_block;
                std::printf("renderbench: widget smoke - click resolution agrees: %s\n", clicks_agree ? "yes" : "NO");
                if (!clicks_agree) ++failures;

                const auto menu_entries = [&](const bl::chunk_pos& c, const QPoint& b) {
                    MapMenuRequest req;
                    req.global_pos = QPoint(0, 0);
                    req.chunk = c;
                    req.block = bl::block_pos(b.x(), 0, b.y());
                    req.dim = c.dim;
                    QMenu menu;
                    ContextMenuBuilder::build(menu, &host, req);
                    return menu.actions().size();
                };
                const int outside = menu_entries(gpu_chunk, gpu_block);
                bool menu_ok = outside > 0;
                std::printf("renderbench: widget smoke - menu outside selection: %d entries\n", outside);

                // Selecting the clicked chunk must add the selection operations.
                host.mapView()->beginSelectionDrag(gpu_chunk);
                host.mapView()->finishSelectionDrag();
                const int inside = menu_entries(gpu_chunk, gpu_block);
                menu_ok = menu_ok && inside > outside;
                std::printf("renderbench: widget smoke - menu inside selection: %d entries; selection actions offered: %s\n", inside,
                            inside > outside ? "yes" : "NO");
                host.mapView()->clearSelection();
                if (!menu_ok) ++failures;

                // Middle-drag selection on the GPU pane. The two panes differ in
                // width here, so this only lands on the right chunk if the click
                // is resolved through the widget that received it.
                gpu.resize(1000, 800);
                QCoreApplication::processEvents();
                const QPointF a(320.0, 240.0), b(520.0, 380.0);
                QMouseEvent press(QEvent::MouseButtonPress, a, gpu.mapToGlobal(a.toPoint()), Qt::MiddleButton, Qt::MiddleButton,
                                  Qt::NoModifier);
                QMouseEvent move(QEvent::MouseMove, b, gpu.mapToGlobal(b.toPoint()), Qt::NoButton, Qt::MiddleButton, Qt::NoModifier);
                QMouseEvent release(QEvent::MouseButtonRelease, b, gpu.mapToGlobal(b.toPoint()), Qt::MiddleButton, Qt::NoButton,
                                    Qt::NoModifier);
                QCoreApplication::sendEvent(&gpu, &press);
                QCoreApplication::sendEvent(&gpu, &move);
                QCoreApplication::sendEvent(&gpu, &release);

                const bl::chunk_pos want_a = host.mapView()->chunkPosAt(a, gpu.size());
                const bl::chunk_pos want_b = host.mapView()->chunkPosAt(b, gpu.size());
                const QRect rect = host.mapView()->selection().region().boundingRect();
                const QRect want(QPoint(std::min(want_a.x, want_b.x), std::min(want_a.z, want_b.z)),
                                 QPoint(std::max(want_a.x, want_b.x), std::max(want_a.z, want_b.z)));
                const bool drag_ok = host.mapView()->selection().rectCount() == 1 && rect == want;
                std::printf("renderbench: widget smoke - gpu middle-drag selection: %s (got (%d,%d,%dx%d), want (%d,%d,%dx%d))\n",
                            drag_ok ? "ok" : "WRONG PLACE", rect.x(), rect.y(), rect.width(), rect.height(), want.x(), want.y(),
                            want.width(), want.height());
                host.mapView()->clearSelection();
                if (!drag_ok) ++failures;
            }

            // Map chrome: the renderers are alternatives, so there is one set of
            // toolbars, and what it shows comes from the shared view.
            {
                LevelTabWidget tabs(nullptr);
                LevelPageWidget page(&tabs, 0);
                page.resize(1200, 800);

                auto* cpu_pane = page.mapHost();

                std::vector<FloatingToolBar*> selection_bars;
                for (auto* tb : page.findChildren<FloatingToolBar*>())
                    if (tb->orientation() == Qt::Horizontal) selection_bars.push_back(tb);

                bool toolbar_ok = cpu_pane && selection_bars.size() == 1;
                std::printf("renderbench: widget smoke - selection toolbars: %d (want 1)\n", static_cast<int>(selection_bars.size()));
                if (toolbar_ok) {
                    auto* shared = cpu_pane->mapView();
                    auto* bar = selection_bars.front();
                    toolbar_ok = shared->selection().mode() == SelectionRegion::Mode::Replace;
                    // Group 0 is the mode group: replace, add, subtract.
                    bar->buttonAt(0, 1)->click();
                    toolbar_ok = toolbar_ok && shared->selection().mode() == SelectionRegion::Mode::Add;
                    bar->buttonAt(0, 2)->click();
                    toolbar_ok = toolbar_ok && shared->selection().mode() == SelectionRegion::Mode::Subtract;
                    std::printf("renderbench: widget smoke - toolbar drives the shared mode: %s (mode=%d)\n", toolbar_ok ? "yes" : "NO",
                                static_cast<int>(shared->selection().mode()));
                } else {
                    std::printf("renderbench: widget smoke - toolbar drives the shared mode: MISSING\n");
                }
                if (!toolbar_ok) ++failures;
            }

            // Renderer choice: exactly one of the two renderers is built, and it
            // is the one on screen driving the shared camera.
            {
                const auto defaults = setting::current();
                if (!defaults.GPU_RENDER_ENABLED) {
                    // Off by default: the CPU renderer is the pane, and no GL
                    // widget is even constructed.
                    LevelTabWidget tabs(nullptr);
                    LevelPageWidget page(&tabs, 0);
                    page.resize(1200, 800);
                    const bool cpu_only = page.activeMapPane() == page.cpuMapPane();
                    std::printf("renderbench: widget smoke - default renderer is the CPU map: %s\n", cpu_only ? "yes" : "NO");
                    if (!cpu_only) ++failures;
                } else {
                    std::printf("renderbench: widget smoke - default renderer is the CPU map: OVERRIDDEN by config\n");
                }
            }
            {
                const auto saved = setting::current();
                auto gpu_settings = saved;
                gpu_settings.GPU_RENDER_ENABLED = true;
                setting::apply(gpu_settings);
                {
                    LevelTabWidget tabs(nullptr);
                    LevelPageWidget page(&tabs, 0);
                    page.resize(1200, 800);

                    auto* host = page.mapHost();
                    auto* active = page.activeMapPane();
                    auto* gpu_pane = qobject_cast<GpuMapWidget*>(active);
                    // Only the configured renderer exists: asking for the GPU one
                    // must not leave a hidden CPU widget behind.
                    bool mode_ok = gpu_pane != nullptr && page.cpuMapPane() == nullptr;
                    if (mode_ok) mode_ok = gpu_pane->isVisibleTo(&page);
                    std::printf("renderbench: widget smoke - gpu mode builds only the GPU pane: %s\n", mode_ok ? "yes" : "NO");
                    if (!mode_ok) ++failures;

                    // The chrome follows the visible pane.
                    bool chrome_ok = false;
                    if (gpu_pane) {
                        for (auto* tb : page.findChildren<FloatingToolBar*>())
                            if (tb->orientation() == Qt::Horizontal) chrome_ok = tb->parentWidget() == gpu_pane;
                        std::printf("renderbench: widget smoke - gpu mode parents the toolbars to the GPU pane: %s\n",
                                    chrome_ok ? "yes" : "NO");
                    }
                    if (!chrome_ok) ++failures;

                    // The pane is the only renderer, so it defines the camera: the
                    // shared view (and everything derived from it) is sized like it.
                    bool camera_ok = false;
                    if (gpu_pane && host) {
                        auto* shared = host->mapView();
                        gpu_pane->resize(1000, 800);
                        gpu_pane->grabFramebuffer();  // paintGL is where the pane reports its size
                        camera_ok = shared->viewportSize() == QSize(1000, 800);
                        std::printf("renderbench: widget smoke - gpu pane defines the camera: %s (view %dx%d)\n", camera_ok ? "yes" : "NO",
                                    shared->viewportSize().width(), shared->viewportSize().height());
                    }
                    if (!camera_ok) ++failures;

                    // The import overlay's chrome belongs to the pane on screen.
                    bool import_pane_ok = false;
                    if (host) {
                        import_pane_ok = host->importOverlay()->paneWidget() == active;
                        std::printf("renderbench: widget smoke - gpu mode points the import overlay at the GPU pane: %s\n",
                                    import_pane_ok ? "yes" : "NO");
                    }
                    if (!import_pane_ok) ++failures;

                    // Toolbar toggles are shared state, so they reach the pane
                    // through the host rather than through the widget.
                    bool repaint_ok = false;
                    if (host) {
                        auto* shared = host->mapView();
                        host->setOther(RenderOption::Grid, false);
                        const bool grid_off = !shared->options().getOther(RenderOption::Grid);
                        host->setOther(RenderOption::Grid, true);
                        repaint_ok = grid_off && shared->options().getOther(RenderOption::Grid);
                        std::printf("renderbench: widget smoke - overlays toggle through the shared view: %s\n", repaint_ok ? "yes" : "NO");
                    }
                    if (!repaint_ok) ++failures;
                }
                setting::apply(saved);
            }

            // Placed import: one ImportOverlay instance, drawn and driven by the
            // GPU pane. The ghost lives only in the frame, and the confirm bar is
            // a child of the pane, so both are observable here.
            {
                // A pane reports its size to the shared view when it paints, so
                // settle one frame first: everything below derives pixels from the
                // shared transform, and a stale viewport would put the ghost
                // somewhere else than the paint does.
                gpu.grabFramebuffer();
                MapView* shared = host.mapView();
                const int dim = shared->dim();
                const QTransform to_view = shared->transformForViewport(gpu.size());

                // LevelPageWidget points the host at the pane on screen; here the
                // GPU widget is that pane and the CPU renderer is not on screen.
                host.setPaneWidget(&gpu);

                // A real payload: the first chunks that exist in view, serialized
                // the way the clipboard/export path does it. The render range
                // deliberately over-covers the widget (camera margin plus a chunk
                // of padding), so a chunk in it can still be off screen - and a
                // ghost placed there is drawn outside the frame, which reads as
                // "no ghost". Only chunks that map inside the widget will do.
                bl::chunk_pos anchor{0, 0, dim};
                ExportedRegion sample;
                const auto [min_chunk, max_chunk, range_rect] = shared->renderRange();
                (void)range_rect;
                const auto in_view = [&](int cx, int cz) {
                    return gpu.rect().contains(to_view.map(QPointF(cx + 0.5, cz + 0.5)).toPoint());
                };
                for (int cz = min_chunk.z; cz <= max_chunk.z && sample.isEmpty(); ++cz) {
                    for (int cx = min_chunk.x; cx <= max_chunk.x && sample.isEmpty(); ++cx) {
                        if (!in_view(cx, cz)) continue;
                        auto raw = loader.getRawChunk(bl::chunk_pos(cx, cz, dim));
                        if (!raw) continue;
                        anchor = bl::chunk_pos(cx, cz, dim);
                        sample.addChunk(*raw);
                        for (int dx = 0; dx <= 1; ++dx) {
                            for (int dz = 0; dz <= 1; ++dz) {
                                if (auto neighbour = loader.getRawChunk(bl::chunk_pos(cx + dx, cz + dz, dim))) sample.addChunk(*neighbour);
                            }
                        }
                    }
                }
                std::printf("renderbench: widget smoke - import payload: %d chunks around (%d,%d)\n", static_cast<int>(sample.chunkCount()),
                            anchor.x, anchor.z);

                auto* overlay = host.importOverlay();
                bool import_ok = overlay && overlay->paneWidget() == &gpu && !sample.isEmpty();
                if (import_ok) {
                    auto bytes = sample.serialize();
                    const QByteArray payload(bytes.data(), static_cast<int>(bytes.size()));
                    import_ok = overlay->startPaste(payload, static_cast<uint8_t>(dim), anchor);
                    std::printf("renderbench: widget smoke - placement started on the shared overlay: %s\n", import_ok ? "yes" : "NO");
                }

                // The cursor goes inside the anchor chunk. It is moved with no
                // button held, which the pane only sees with mouse tracking on.
                const QPointF cursor = to_view.map(QPointF(anchor.x + 0.5, anchor.z + 0.5));
                if (import_ok) {
                    QMouseEvent move(QEvent::MouseMove, cursor, gpu.mapToGlobal(cursor.toPoint()), Qt::NoButton, Qt::NoButton,
                                     Qt::NoModifier);
                    QCoreApplication::sendEvent(&gpu, &move);
                    import_ok = !overlay->placed();
                }

                // Settle first: regions finishing between the two grabs change
                // terrain pixels, which would otherwise count as ghost pixels.
                for (int i = 0; i < 300; i++) {
                    QCoreApplication::processEvents();
                    if (i > 0 && loader.pendingRegionTasks() == 0 && gpu.pendingUploadCount() == 0) break;
                }
                const QImage with_ghost = gpu.grabFramebuffer();
                // The stats strip along the top prints the frame time, which differs
                // between any two captures; it is not part of the ghost.
                const auto differing_pixels = [](const QImage& a, const QImage& b) {
                    constexpr int SKIP_TOP = 24;
                    if (a.size() != b.size() || a.isNull()) return -1;
                    int diff = 0;
                    for (int y = SKIP_TOP; y < a.height(); y += 2) {
                        for (int x = 0; x < a.width(); x += 2) {
                            if (a.pixel(x, y) != b.pixel(x, y)) ++diff;
                        }
                    }
                    return diff;
                };

                // Left-click pins it down and shows the confirm bar on this pane.
                bool bar_ok = false;
                if (import_ok) {
                    QMouseEvent press(QEvent::MouseButtonPress, cursor, gpu.mapToGlobal(cursor.toPoint()), Qt::LeftButton, Qt::LeftButton,
                                      Qt::NoModifier);
                    QMouseEvent release(QEvent::MouseButtonRelease, cursor, gpu.mapToGlobal(cursor.toPoint()), Qt::LeftButton, Qt::NoButton,
                                        Qt::NoModifier);
                    QCoreApplication::sendEvent(&gpu, &press);
                    QCoreApplication::sendEvent(&gpu, &release);
                    import_ok = overlay->placed();
                    for (auto* bar : gpu.findChildren<FloatingToolBar*>()) bar_ok = bar->isVisibleTo(&gpu);
                }

                // Esc cancels, which removes the ghost: the frame difference is
                // the ghost, without needing to know its blended colour.
                QKeyEvent escape(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
                if (import_ok) QCoreApplication::sendEvent(&gpu, &escape);
                const bool cancelled = overlay && !overlay->active();
                for (int i = 0; i < 300; i++) {
                    QCoreApplication::processEvents();
                    if (i > 0 && loader.pendingRegionTasks() == 0 && gpu.pendingUploadCount() == 0) break;
                }
                const QImage without_ghost = gpu.grabFramebuffer();
                const int ghost_pixels = differing_pixels(with_ghost, without_ghost);

                // Well above the frame-to-frame jitter (measured ~135 pixels from
                // bakes landing between the two grabs, i.e. off-threshold noise).
                constexpr int MIN_GHOST_PIXELS = 1000;
                import_ok = import_ok && bar_ok && cancelled && ghost_pixels > MIN_GHOST_PIXELS;
                std::printf(
                    "renderbench: widget smoke - ghost drawn by the GPU pane: %s (%d pixels; confirm bar on the pane: %s; Esc "
                    "cancels: %s)\n",
                    ghost_pixels > MIN_GHOST_PIXELS ? "yes" : "NO", ghost_pixels, bar_ok ? "yes" : "NO", cancelled ? "yes" : "NO");
                if (!import_ok) ++failures;
            }

            {
                SettingsDialog dialog;
                auto* tree = dialog.findChild<QTreeWidget*>("categoryTree");
                auto* stack = dialog.findChild<QStackedWidget*>("settingsStack");
                bool pages_ok = tree && stack && tree->topLevelItemCount() > 0;
                if (pages_ok) {
                    bool seen[16] = {};
                    for (int i = 0; i < tree->topLevelItemCount(); ++i) {
                        tree->setCurrentItem(tree->topLevelItem(i));
                        const int page = stack->currentIndex();
                        const QString name = stack->currentWidget() ? stack->currentWidget()->objectName() : QString();
                        const bool in_range = page >= 0 && page < 16;
                        if (!in_range || seen[page]) {
                            pages_ok = false;
                            std::printf("renderbench: widget smoke - settings page %d from '%s' is a duplicate or out of range\n", page,
                                        qPrintable(tree->topLevelItem(i)->text(0)));
                            continue;
                        }
                        seen[page] = true;
                        std::printf("renderbench: widget smoke - settings page %d -> %s (%s)\n", page, qPrintable(name),
                                    qPrintable(tree->topLevelItem(i)->text(0)));
                    }
                    for (int page = 0; page < stack->count(); ++page) {
                        if (!seen[page]) {
                            pages_ok = false;
                            std::printf("renderbench: widget smoke - settings page %d (%s) has no category\n", page,
                                        qPrintable(stack->widget(page)->objectName()));
                        }
                    }
                }
                std::printf("renderbench: widget smoke - settings categories match the pages: %s\n", pages_ok ? "yes" : "NO");
                if (!pages_ok) ++failures;

                auto* ao_spin = dialog.findChild<QDoubleSpinBox*>("gpuAoSpin");
                auto* ao_slider = dialog.findChild<QSlider*>("gpuAoSlider");
                auto* bevel_spin = dialog.findChild<QDoubleSpinBox*>("gpuBevelSpin");
                auto* bevel_slider = dialog.findChild<QSlider*>("gpuBevelSlider");
                auto* bevel_width_spin = dialog.findChild<QDoubleSpinBox*>("gpuBevelWidthSpin");
                auto* bevel_width_slider = dialog.findChild<QSlider*>("gpuBevelWidthSlider");
                auto* sat_spin = dialog.findChild<QDoubleSpinBox*>("gpuSaturationSpin");
                auto* sat_slider = dialog.findChild<QSlider*>("gpuSaturationSlider");
                auto* brightness_spin = dialog.findChild<QDoubleSpinBox*>("gpuBrightnessSpin");
                auto* brightness_slider = dialog.findChild<QSlider*>("gpuBrightnessSlider");
                auto* shadow_spin = dialog.findChild<QDoubleSpinBox*>("gpuShadowSpin");
                auto* shadow_slider = dialog.findChild<QSlider*>("gpuShadowSlider");
                bool ao_ok = ao_spin && ao_slider && bevel_spin && bevel_slider && bevel_width_spin && bevel_width_slider && sat_spin &&
                             sat_slider && brightness_spin && brightness_slider && shadow_spin && shadow_slider;
                if (ao_ok) {
                    ao_slider->setValue(60);
                    ao_ok = std::abs(ao_spin->value() - 0.6) < 1e-6;
                    ao_spin->setValue(0.35);
                    ao_ok = ao_ok && ao_slider->value() == 35;
                    bevel_slider->setValue(25);
                    ao_ok = ao_ok && std::abs(bevel_spin->value() - 0.25) < 1e-6;
                    bevel_width_slider->setValue(150);
                    ao_ok = ao_ok && std::abs(bevel_width_spin->value() - 1.5) < 1e-6;
                    sat_slider->setValue(150);
                    ao_ok = ao_ok && std::abs(sat_spin->value() - 1.5) < 1e-6;
                    brightness_slider->setValue(125);
                    ao_ok = ao_ok && std::abs(brightness_spin->value() - 1.25) < 1e-6;
                    shadow_slider->setValue(40);
                    ao_ok = ao_ok && std::abs(shadow_spin->value() - 0.4) < 1e-6;
                }
                auto* gpu_check = dialog.findChild<QCheckBox*>("gpuRenderCheck");
                bool saved_ok = false;
                if (ao_ok && gpu_check) {
                    const auto saved = setting::current();
                    const QString config_path = QString::fromStdString(constant::CONFIG_FILE_PATH);
                    QFile config_file(config_path);
                    QByteArray original_config;
                    const bool config_readable = config_file.exists() && config_file.open(QIODevice::ReadOnly);
                    if (config_readable) original_config = config_file.readAll();
                    if (config_file.isOpen()) config_file.close();

                    gpu_check->setChecked(true);
                    if (ao_spin) ao_spin->setValue(0.5);
                    if (bevel_spin) bevel_spin->setValue(0.3);
                    if (bevel_width_spin) bevel_width_spin->setValue(1.25);
                    if (sat_spin) sat_spin->setValue(1.2);
                    if (brightness_spin) brightness_spin->setValue(1.1);
                    if (shadow_spin) shadow_spin->setValue(0.8);
                    QMetaObject::invokeMethod(&dialog, "onSave");
                    saved_ok = setting::current().GPU_RENDER_ENABLED && std::abs(setting::current().GPU_AO_STRENGTH - 0.5f) < 1e-6 &&
                               std::abs(setting::current().GPU_BEVEL_STRENGTH - 0.3f) < 1e-6 &&
                               std::abs(setting::current().GPU_BEVEL_WIDTH - 1.25f) < 1e-6 &&
                               std::abs(setting::current().GPU_SATURATION - 1.2f) < 1e-6 &&
                               std::abs(setting::current().GPU_BRIGHTNESS - 1.1f) < 1e-6 &&
                               std::abs(setting::current().GPU_SHADOW_STRENGTH - 0.8f) < 1e-6;
                    std::printf(
                        "renderbench: widget smoke - saving the GPU page writes the settings: %s (enabled=%d ao=%.2f bevel=%.2f width=%.2f "
                        "saturation=%.2f brightness=%.2f shadow=%.2f)\n",
                        saved_ok ? "yes" : "NO", setting::current().GPU_RENDER_ENABLED ? 1 : 0,
                        static_cast<double>(setting::current().GPU_AO_STRENGTH), static_cast<double>(setting::current().GPU_BEVEL_STRENGTH),
                        static_cast<double>(setting::current().GPU_BEVEL_WIDTH), static_cast<double>(setting::current().GPU_SATURATION),
                        static_cast<double>(setting::current().GPU_BRIGHTNESS),
                        static_cast<double>(setting::current().GPU_SHADOW_STRENGTH));
                    setting::apply(saved);

                    if (config_readable && config_file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                        config_file.write(original_config);
                        config_file.close();
                    }
                }
                if (!ao_ok) ++failures;
                if (!saved_ok) ++failures;

                // And the renderer picks that value up when it is created.
                const auto probe_saved = setting::current();
                auto probe_settings = probe_saved;
                probe_settings.GPU_AO_STRENGTH = 0.7f;
                probe_settings.GPU_BEVEL_STRENGTH = 0.4f;
                probe_settings.GPU_BEVEL_WIDTH = 1.5f;
                probe_settings.GPU_SATURATION = 1.4f;
                probe_settings.GPU_BRIGHTNESS = 1.1f;
                probe_settings.GPU_SHADOW_STRENGTH = 0.6f;
                setting::apply(probe_settings);
                {
                    MapView probe_view;
                    GpuMapWidget probe(nullptr, nullptr, &probe_view, nullptr, nullptr, nullptr);
                    const auto& options = probe.gpuOptions();
                    const bool wired = std::abs(options.ao_strength - 0.7f) < 1e-6 && std::abs(options.bevel_strength - 0.4f) < 1e-6 &&
                                       std::abs(options.bevel_width - 1.5f) < 1e-6 && std::abs(options.saturation - 1.4f) < 1e-6 &&
                                       std::abs(options.brightness - 1.1f) < 1e-6 && std::abs(options.shadow_strength - 0.6f) < 1e-6;
                    std::printf(
                        "renderbench: widget smoke - the renderer starts from the configured shading (ao=%.2f bevel=%.2f width=%.2f "
                        "saturation=%.2f brightness=%.2f shadow=%.2f): %s\n",
                        static_cast<double>(options.ao_strength), static_cast<double>(options.bevel_strength),
                        static_cast<double>(options.bevel_width), static_cast<double>(options.saturation),
                        static_cast<double>(options.brightness), static_cast<double>(options.shadow_strength), wired ? "yes" : "NO");
                    if (!wired) ++failures;
                }
                setting::apply(probe_saved);
            }

            const bool ok = cpu_colors > 4 && gpu_colors > 4 && zoom_applied && same_view && overlays_ok && failures == 0;
            std::printf("renderbench: widget smoke - %s\n", ok ? "PASS" : "FAIL");
            return ok ? 0 : 3;
        }

        /// Fraction of the frame still showing the unloaded checkerboard.
        double unloadedFraction(const QImage& frame) {
            if (frame.isNull() || frame.width() == 0) return 0.0;
            int64_t unloaded = 0;
            int64_t total = 0;
            for (int y = 0; y < frame.height(); y += 2) {
                for (int x = 0; x < frame.width(); x += 2) {
                    const QRgb p = frame.pixel(x, y);
                    const int r = qRed(p);
                    const bool light_chess = (r == 128 || r == 148) && r == qGreen(p) && r == qBlue(p);
                    if (light_chess) ++unloaded;
                    ++total;
                }
            }
            return total > 0 ? static_cast<double>(unloaded) / static_cast<double>(total) : 0.0;
        }

        /// Measure background coverage after each large zoom step.
        void runZoomSweepBench(AsyncLevelLoader& loader) {
            const QSize size(1024, 640);
            MapView view;
            GpuMapWidget gpu(nullptr, &loader, &view, nullptr, nullptr, nullptr);
            gpu.resize(size);
            view.setViewportSize(size);
            gpu.setGpuTimingEnabled(true);

            const auto pump = [&]() {
                const auto [min_chunk, max_chunk, rect] = view.renderRange();
                (void)rect;
                loader.setRenderViewport(constant::c2r(min_chunk), constant::c2r(max_chunk));
                gpu.grabFramebuffer();
                QCoreApplication::processEvents();
                loader.setRenderViewport(constant::c2r(min_chunk), constant::c2r(max_chunk));
            };

            // Start at the closest zoom, fully settled.
            view.setScale(1024.0, QPointF(size.width() / 2.0, size.height() / 2.0));
            for (int i = 0; i < 200; i++) pump();

            std::printf("\n== zoom sweep - %dx%d, background %% per frame after each zoom step ==\n", size.width(), size.height());
            constexpr int FRAMES = 12;
            std::printf("%11s %6s %7s", "scale", "texel", "frame:");
            for (int i = 0; i < FRAMES; i++) std::printf("%6d", i);
            std::printf("\n");

            for (double scale : {256.0, 64.0, 32.0, 24.0, 16.0, 12.0, 9.0, 8.0, 6.0, 4.0}) {
                view.setScale(scale, QPointF(size.width() / 2.0, size.height() / 2.0));
                std::printf("%11.2f %6d %7s", scale, gpu.blocksPerTexel(), "");
                for (int i = 0; i < FRAMES; i++) {
                    const QImage frame = gpu.grabFramebuffer();
                    std::printf("%6.0f", unloadedFraction(frame) * 100.0);
                    QCoreApplication::processEvents();
                }
                std::printf("   (pending %d, queued %d, resident %d, regions %d slots %d %s)\n", loader.pendingRegionTasks(),
                            gpu.pendingUploadCount(), gpu.residentRegionCount(), gpu.regionsSpannedBy(size, scale / 16.0),
                            gpu.atlasSlotsPerSide(),
                            gpu.atlasSlotsPerSide() >= gpu.regionsSpannedBy(size, scale / 16.0) ? "covered" : "ALIASING");
            }
            std::printf(
                "(each column is one frame after the zoom step. Decaying within a frame or two is loading; staying high\n"
                " means terrain that is already in memory is not being drawn.)\n");

            std::printf("\n== steady state, no input - should be 0 uploads ==\n");
            std::printf("%11s %8s %10s %12s %12s\n", "scale", "texel", "resident", "uploads/60f", "queued");
            for (double scale : {64.0, 16.0, 8.0, 4.0}) {
                view.setScale(scale, QPointF(size.width() / 2.0, size.height() / 2.0));
                for (int i = 0; i < 4000; i++) pump();  // settle, however long it takes
                const int before = gpu.totalUploadCount();
                for (int i = 0; i < 60; i++) pump();
                std::printf("%11.2f %8d %10d %12d %12d\n", scale, gpu.blocksPerTexel(), gpu.residentRegionCount(),
                            gpu.totalUploadCount() - before, gpu.pendingUploadCount());
            }
            std::printf("(a settled view must show 0; anything else means slots are re-filled every frame.)\n");
        }

        /// Verify atlas slot coverage across the zoom range.
        int runCoverageBench(AsyncLevelLoader& loader, const QSize& viewport, int shadow_steps) {
            MapView view;
            GpuMapWidget gpu(nullptr, &loader, &view, nullptr, nullptr, nullptr);
            gpu.resize(viewport);
            view.setViewportSize(viewport);
            auto options = gpu.gpuOptions();
            options.shadow_steps = shadow_steps;
            gpu.setGpuOptions(options);

            std::printf("\n== atlas coverage - %dx%d, shadow steps %d ==\n", viewport.width(), viewport.height(), shadow_steps);
            std::printf("%11s %12s %8s %10s %10s %s\n", "scale", "px/block", "texel", "regions", "slots", "ok");

            int failures = 0;
            double scale = 4096.0;
            while (scale >= 4.0) {
                view.setScale(scale, QPointF(viewport.width() / 2.0, viewport.height() / 2.0));
                // Resolve the level the way paintGL would, then re-check it.
                const double px_per_block = scale / 16.0;
                const int level = gpu.blocksPerTexelFor(viewport, px_per_block);
                // "slots" is a macro in this toolchain, hence slot_count.
                const int slot_count = GpuMapWidget::ATLAS_TEXELS / (GpuMapWidget::REGION_BLOCKS / level);
                const int region_count = gpu.regionsSpannedBy(viewport, px_per_block);
                const bool ok = slot_count >= region_count;
                if (!ok) ++failures;
                // Print the tight cases, not every step.
                if (!ok || region_count > slot_count / 2) {
                    std::printf("%11.2f %12.4f %8d %10d %10d %s\n", scale, px_per_block, level, region_count, slot_count,
                                ok ? "ok" : "TOO FEW SLOTS");
                }
                scale /= 1.05;  // fine steps, to catch off-by-one boundaries
            }
            std::printf("%s (%d failures)\n", failures == 0 ? "coverage holds across the zoom range" : "COVERAGE BROKEN", failures);
            return failures == 0 ? 0 : 4;
        }

        /// Verify checkerboard run lengths across atlas resolution changes.
        // Measures coordinate-index scan cost and its main contributors.
        int runCoordsBench(AsyncLevelLoader& loader) {
            auto* db = loader.level().db();
            if (!db) {
                std::printf("renderbench: coords - no open database\n");
                return 1;
            }
            const std::atomic_bool no_stop{false};
            const auto is_marker = [](int type) {
                for (auto marker : bl::raw_chunk::MARKER_KEYS) {
                    if (type == static_cast<int>(marker)) return true;
                }
                return false;
            };
            const auto now = [] { return std::chrono::steady_clock::now(); };
            const auto elapsed_ms = [](const auto& start) {
                return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
            };
            // Match the production scan's key classification and read options.
            const auto key_view = [](const leveldb::Slice& slice) { return std::string_view(slice.data(), slice.size()); };
            const auto scan_options = loader.level().bulk_read_options();

            // --- what the key space holds ---
            std::int64_t total_keys = 0;
            std::int64_t chunk_keys = 0;
            std::int64_t marker_keys = 0;
            std::int64_t foreign_keys = 0;
            std::int64_t short_keys = 0;
            std::int64_t long_keys = 0;
            std::unordered_set<std::int64_t> columns;
            {
                auto* it = db->NewIterator(scan_options);
                for (it->SeekToFirst(); it->Valid(); it->Next()) {
                    const auto key = key_view(it->key());
                    ++total_keys;
                    if (key.size() <= 16) {
                        ++short_keys;
                    } else {
                        ++long_keys;
                    }
                    const auto ck = bl::chunk_key::parse(key);
                    if (!ck.valid()) {
                        ++foreign_keys;
                        continue;
                    }
                    ++chunk_keys;
                    if (is_marker(static_cast<int>(ck.type))) ++marker_keys;
                    // The column is what a skip-ahead would land on: every key of
                    // one (x, z) shares this 8-byte prefix, so they are adjacent.
                    columns.insert((static_cast<std::int64_t>(ck.cp.x) << 32) | static_cast<std::uint32_t>(ck.cp.z));
                }
                delete it;
            }

            std::printf("\n== chunk coordinate scan ==\n");
            std::printf("renderbench: coords - %lld keys: %lld chunk keys (%lld marker, %lld other), %lld foreign\n",
                        static_cast<long long>(total_keys), static_cast<long long>(chunk_keys), static_cast<long long>(marker_keys),
                        static_cast<long long>(chunk_keys - marker_keys), static_cast<long long>(foreign_keys));
            std::printf("renderbench: coords - %lld columns, %.1f keys per column, %.2f marker keys per column\n",
                        static_cast<long long>(columns.size()), static_cast<double>(total_keys) / std::max<std::size_t>(1, columns.size()),
                        static_cast<double>(marker_keys) / std::max<std::size_t>(1, columns.size()));
            std::printf("renderbench: coords - key sizes: %lld fit an inline string (<=16), %lld do not\n",
                        static_cast<long long>(short_keys), static_cast<long long>(long_keys));

            // --- how the time splits, same iteration each time ---
            const auto iterate = [&](const auto& body) {
                auto* it = db->NewIterator(scan_options);
                for (it->SeekToFirst(); it->Valid(); it->Next()) body(it->key());
                delete it;
            };

            std::int64_t sink = 0;
            auto start = now();
            iterate([&](const leveldb::Slice& key) { sink += static_cast<std::int64_t>(key.size()); });
            const double step_ms = elapsed_ms(start);

            start = now();
            iterate([&](const leveldb::Slice& key) { sink += static_cast<std::int64_t>(key.ToString().size()); });
            const double string_ms = elapsed_ms(start);

            start = now();
            iterate([&](const leveldb::Slice& key) {
                const auto ck = bl::chunk_key::parse(key_view(key));
                sink += ck.valid() ? 1 : 0;
            });
            const double parse_ms = elapsed_ms(start);

            ChunkCoordsIndex index;
            start = now();
            const bool loaded = index.load(loader.level(), no_stop);
            const double load_ms = elapsed_ms(start);

            // load() generates the region images inline, so re-running it measures
            // that part on its own (same amount of work, already-built index).
            start = now();
            index.generateImages();
            const double images_ms = elapsed_ms(start);

            std::printf("%13s %10s\n", "pass", "ms");
            std::printf("%13s %10.1f  (LevelDB stepping only)\n", "step", step_ms);
            std::printf("%13s %10.1f  (+ key.ToString(), the copy the scan no longer makes)\n", "materialise", string_ms);
            std::printf("%13s %10.1f  (+ chunk_key::parse on the key view, i.e. a count-only pre-pass)\n", "parse", parse_ms);
            std::printf("%13s %10.1f  (the real load: scan + index + images)\n", "load", load_ms);
            std::printf("%13s %10.1f  (region images alone)\n", "images", images_ms);
            std::printf("renderbench: coords - load is %.1fx the bare stepping, image generation is %.0f%% of it%s\n",
                        step_ms > 0 ? load_ms / step_ms : 0.0, load_ms > 0 ? 100.0 * images_ms / load_ms : 0.0,
                        loaded ? "" : " (LOAD FAILED)");
            std::printf("renderbench: coords - ceiling for a skip-ahead: %lld steps instead of %lld (%.1fx fewer)\n",
                        static_cast<long long>(columns.size()), static_cast<long long>(total_keys),
                        static_cast<double>(total_keys) / std::max<std::size_t>(1, columns.size()));

            // --- is skipping keys cheaper than stepping over them? ---
            // A skip-ahead scan would replace the ~10 keys of a column it does not
            // want with a single Seek. That only pays if a Seek costs less than the
            // Next() steps it replaces, so both are timed visiting the same keys:
            // Seek(k + '\0') lands on the key after k, i.e. one seek per key.
            {
                auto* seek_it = db->NewIterator(scan_options);
                std::int64_t visited = 0;
                std::string next_target;
                const auto seek_start = now();
                for (seek_it->SeekToFirst(); seek_it->Valid();) {
                    next_target = seek_it->key().ToString();
                    next_target.push_back('\0');
                    ++visited;
                    seek_it->Seek(next_target);
                }
                const double seek_ms = elapsed_ms(seek_start);
                delete seek_it;
                std::printf(
                    "renderbench: coords - %lld keys visited by Seek: %.1f ms (%.2f us/key); by Next: %.1f ms (%.2f us/key), "
                    "ratio %.2f\n",
                    static_cast<long long>(visited), seek_ms, visited > 0 ? seek_ms * 1000.0 / visited : 0.0, step_ms,
                    total_keys > 0 ? step_ms * 1000.0 / total_keys : 0.0, step_ms > 0 ? seek_ms / step_ms : 0.0);
            }

            // --- on-disk size, so the scan has a throughput figure ---
            std::int64_t db_bytes = 0;
            {
                namespace fs = std::filesystem;
                std::error_code ec;
                const fs::path db_dir = fs::path(loader.level().root_path()) / bl::bedrock_level::LEVEL_DB;
                for (const auto& entry : fs::directory_iterator(db_dir, ec)) {
                    if (entry.is_regular_file(ec)) db_bytes += static_cast<std::int64_t>(entry.file_size(ec));
                }
            }
            std::printf("renderbench: coords - database is %.1f MB on disk, so the scan reads it at %.0f MB/s\n",
                        static_cast<double>(db_bytes) / (1024.0 * 1024.0), step_ms > 0 ? db_bytes / 1024.0 / step_ms : 0.0);

            // The same scan during startup took 5x longer than these passes, which
            // says the first run is bound by something other than the pipeline. Two
            // candidates: the device itself, or the merging iterator walking many
            // level-0 files (scattered reads). The stats and the raw read separate
            // them.
            {
                std::string stats;
                if (db->GetProperty("leveldb.stats", &stats)) std::printf("%s\n", stats.c_str());
            }
            {
                namespace fs = std::filesystem;
                std::error_code ec;
                std::vector<fs::path> files;
                std::int64_t raw_bytes = 0;
                const fs::path db_dir = fs::path(loader.level().root_path()) / bl::bedrock_level::LEVEL_DB;
                for (const auto& entry : fs::directory_iterator(db_dir, ec)) {
                    if (entry.is_regular_file(ec) && entry.path().extension() == ".ldb") files.push_back(entry.path());
                }
                std::vector<char> buffer(1 << 20);
                const auto raw_start = now();
                for (const auto& file : files) {
                    std::FILE* handle = std::fopen(file.string().c_str(), "rb");
                    if (!handle) continue;
                    std::size_t read = 0;
                    while ((read = std::fread(buffer.data(), 1, buffer.size(), handle)) > 0) raw_bytes += static_cast<std::int64_t>(read);
                    std::fclose(handle);
                }
                const double raw_ms = elapsed_ms(raw_start);
                std::printf("renderbench: coords - raw sequential read of %lld .ldb files, %.1f MB in %.1f ms = %.0f MB/s\n",
                            static_cast<long long>(files.size()), static_cast<double>(raw_bytes) / (1024.0 * 1024.0), raw_ms,
                            raw_ms > 0 ? raw_bytes / 1024.0 / raw_ms : 0.0);
            }

            // --- the read options the scan uses vs. the alternatives ---
            // CpuMapWidget-style scan cost is per-key work plus block reads; the
            // former measured out as ~1%, so what is left is how the blocks are
            // read. Both knobs below are per-scan, not per-database.
            struct Variant {
                const char* label;
                bool allocator;
                bool fill_cache;
            };
            const Variant variants[] = {
                {"default", false, true},
                {"+ decompress allocator", true, true},
                {"+ fill_cache=false", false, false},
                {"+ both (what the scan uses)", true, false},
            };
            std::printf("%30s %10s %10s\n", "read options", "ms", "MB/s");
            for (const auto& variant : variants) {
                leveldb::ReadOptions options;
                options.fill_cache = variant.fill_cache;
                leveldb::DecompressAllocator* allocator = nullptr;
                if (variant.allocator) {
                    allocator = new leveldb::DecompressAllocator();
                    options.decompress_allocator = allocator;
                }
                auto* it = db->NewIterator(options);
                std::int64_t hits = 0;
                const auto variant_start = now();
                for (it->SeekToFirst(); it->Valid(); it->Next()) {
                    const auto ck = bl::chunk_key::parse(key_view(it->key()));
                    hits += ck.valid() ? 1 : 0;
                }
                const double variant_ms = elapsed_ms(variant_start);
                delete it;
                delete allocator;
                std::printf("%30s %10.1f %10.0f   (%lld chunk keys)\n", variant.label, variant_ms,
                            variant_ms > 0 ? db_bytes / 1024.0 / variant_ms : 0.0, static_cast<long long>(hits));
            }

            return 0;
        }

        /// given zoom every run should be the same width - unequal runs mean the
        /// tile is being uploaded at the wrong resolution and its rows are being
        /// read out of stride.
        int runBlankCheck(AsyncLevelLoader& loader) {
            const QSize size(1024, 640);
            MapView view;
            GpuMapWidget gpu(nullptr, &loader, &view, nullptr, nullptr, nullptr);
            gpu.resize(size);
            view.setViewportSize(size);
            gpu.setGpuTimingEnabled(true);

            const auto pump = [&](int frames) {
                for (int i = 0; i < frames; i++) {
                    const auto [min_chunk, max_chunk, rect] = view.renderRange();
                    (void)rect;
                    loader.setRenderViewport(constant::c2r(min_chunk), constant::c2r(max_chunk));
                    gpu.grabFramebuffer();
                    QCoreApplication::processEvents();
                    loader.setRenderViewport(constant::c2r(min_chunk), constant::c2r(max_chunk));
                }
            };

            std::printf("\n== blank tile checkerboard ==\n");
            std::printf("%11s %6s %10s %14s %14s %s\n", "scale", "texel", "px/cell", "distinct runs", "modal run", "ok");

            int failures = 0;
            for (double scale : {8.0, 4.0, 2.0, 1.0}) {
                view.setScale(std::max(scale, 0.5), QPointF(size.width() / 2.0, size.height() / 2.0));
                pump(4000);  // let it settle, however long that takes
                const QImage frame = gpu.grabFramebuffer();
                const double dpr = frame.width() / static_cast<double>(size.width());
                const double px_per_block = (view.scale() / 16.0) * dpr;
                const double expected = 64.0 * px_per_block;  // one chessboard cell

                // Histogram of run lengths of a single shade. The run key is the
                // exact value: treating the two dark shades as one class would
                // merge neighbouring cells into a single run.
                std::map<int, int> runs;
                for (int y = frame.height() / 8; y < frame.height(); y += frame.height() / 8) {
                    int run = 0;
                    QRgb previous = 0;
                    for (int x = 0; x < frame.width(); x++) {
                        const QRgb p = frame.pixel(x, y);
                        const bool blank_dark = (qRed(p) == 20 || qRed(p) == 40) && qRed(p) == qGreen(p) && qRed(p) == qBlue(p);
                        if (blank_dark && p == previous) {
                            ++run;
                        } else {
                            if (run >= 4) runs[run]++;
                            run = blank_dark ? 1 : 0;
                        }
                        previous = p;
                    }
                    if (run >= 4) runs[run]++;
                }
                // The modal run is the true cell width; differing ones are the fault.
                int modal = 0;
                int modal_count = 0;
                for (const auto& [len, count] : runs) {
                    if (count > modal_count) {
                        modal = len;
                        modal_count = count;
                    }
                }
                const int expected_px = static_cast<int>(std::lround(expected));
                const bool ok = !runs.empty() && std::abs(modal - expected_px) <= 2;
                if (!ok) ++failures;
                std::printf("%11.2f %6d %10.1f %14zu %14d %s\n", view.scale() / 16.0, gpu.blocksPerTexel(), expected, runs.size(), modal,
                            ok ? "ok" : "WRONG");
                if (!ok && !runs.empty()) {
                    std::printf("            run lengths (px -> count, expected %d):", expected_px);
                    int shown = 0;
                    for (const auto& [len, count] : runs) {
                        if (shown++ >= 10) break;
                        std::printf(" %d->%d", len, count);
                    }
                    std::printf("\n");
                }
            }
            std::printf("%s\n",
                        failures == 0 ? "background checkerboard is uniform at every resolution" : "BACKGROUND CHECKERBOARD BROKEN");
            return failures == 0 ? 0 : 5;
        }

        /// Check that AO darkens only concave corners in a real capture.
        int runAoCheck(const Request& request, AsyncLevelLoader& loader, const std::vector<BakedRegion>& regions) {
            const QPointF centre_block = terrainCentreBlock(regions);
            const QSize size(1024, 640);
            constexpr double PX_PER_BLOCK = 16.0;  // blocks are big enough to see the gradient

            const auto render = [&](float ao, bool flat_shading = false) {
                MapView capture_view;
                GpuMapWidget gpu(nullptr, &loader, &capture_view, nullptr, nullptr, nullptr);
                configureGpu(gpu, request);
                auto options = gpu.gpuOptions();
                options.ao_strength = ao;
                options.flat_shading = flat_shading;
                gpu.setGpuOptions(options);
                QImage image;
                for (int i = 0; i < 600; i++) {
                    image = gpu.captureOffscreen(size, centre_block, PX_PER_BLOCK);
                    QCoreApplication::processEvents();
                    if (i > 0 && loader.pendingRegionTasks() == 0 && gpu.pendingUploadCount() == 0) break;
                }
                return image;
            };

            const QImage off = render(0.0f);
            if (off.isNull()) {
                std::printf("renderbench: ao check FAILED to render\n");
                return 6;
            }

            // The widget draws its own stats overlay across the top of the frame,
            // and its text (frame time, counters) differs between captures. It has
            // to be excluded or it dominates the difference.
            const double dpr = off.width() / static_cast<double>(size.width());
            const int overlay_rows = static_cast<int>(44 * dpr);
            // Both checkerboard shades count as background, in either image: if a
            // region was still uploading during one of the captures it would
            // otherwise be compared against terrain.
            const auto is_background = [](QRgb p) {
                const int r = qRed(p);
                const bool grey = r == qGreen(p) && r == qBlue(p);
                return grey && (r == 20 || r == 40 || r == 128 || r == 148);
            };

            struct Metrics {
                double affected{0};  // % of painted pixels darkened at all
                int median{0};
                int deepest{0};
                int brightened{0};
                double mean_darkening{0};  // average over painted pixels, the "grime" level
            };
            const auto measure = [&](const QImage& on) {
                Metrics m;
                std::vector<int> darker;
                int64_t darkening_sum = 0;
                int painted = 0;
                for (int y = overlay_rows; y < off.height(); y++) {
                    for (int x = 0; x < off.width(); x++) {
                        const QRgb a = off.pixel(x, y);
                        const QRgb b = on.pixel(x, y);
                        if (is_background(a) || is_background(b)) continue;
                        ++painted;
                        const int delta = (qRed(a) + qGreen(a) + qBlue(a)) - (qRed(b) + qGreen(b) + qBlue(b));
                        if (delta < -3) ++m.brightened;
                        if (delta > 0) {
                            darker.push_back(delta / 3);
                            darkening_sum += delta / 3;
                        }
                    }
                }
                if (painted == 0) return m;
                m.affected = 100.0 * static_cast<double>(darker.size()) / painted;
                m.mean_darkening = static_cast<double>(darkening_sum) / painted;
                if (!darker.empty()) {
                    std::sort(darker.begin(), darker.end());
                    m.median = darker[darker.size() / 2];
                    m.deepest = darker.back();
                }
                return m;
            };

            std::printf("\n== ambient occlusion strength sweep at %g px/block ==\n", PX_PER_BLOCK);
            std::printf("%10s %12s %12s %12s %12s %12s\n", "strength", "affected %", "median", "deepest", "mean/255", "brightened");

            std::vector<float> strengths;
            for (float s : {0.0f, 0.10f, 0.15f, 0.20f, 0.25f, 0.30f, 0.45f}) strengths.push_back(s);
            if (std::find(strengths.begin(), strengths.end(), request.ao_strength) == strengths.end()) {
                strengths.push_back(request.ao_strength);
            }

            Metrics chosen;
            for (float strength : strengths) {
                const QImage on = (strength <= 0.0f) ? off : render(strength);
                const Metrics m = measure(on);
                std::printf("%10.2f %12.2f %12d %12d %12.2f %12d%s\n", strength, m.affected, m.median, m.deepest, m.mean_darkening,
                            m.brightened, std::abs(strength - request.ao_strength) < 1e-6f ? "   <- default" : "");
                if (std::abs(strength - request.ao_strength) < 1e-6f) chosen = m;
            }

            const bool ok = chosen.brightened == 0 && chosen.affected > 1.0 && chosen.deepest > chosen.median * 2;
            std::printf("  %s\n", ok ? "PASS" : "FAIL");
            if (chosen.brightened > 0) std::printf("  (AO brightened pixels - the occlusion term is not purely subtractive)\n");
            if (chosen.affected <= 1.0) std::printf("  (AO barely affects anything - the proximity weighting is collapsing to zero)\n");
            if (chosen.deepest <= chosen.median * 2) {
                std::printf("  (AO is uniform rather than concentrated - corners get no extra shading)\n");
            }
            // What is left when AO is off. The bevel (EDGE_DARK on the lower side
            // of a step) and the diagonal corner patch are separate terms with
            // their own constants, so "ao 0" does not mean "no shading at all".
            // Measured against a flat-shaded capture, i.e. against the raw colours.
            if (request.no_shadow) {
                const QImage flat = render(0.0f, /*flat_shading=*/true);
                const Metrics bevel = measure(flat);
                std::printf("  with AO off, the bevel + corner patch still darken %.2f%% of painted pixels (median %d, deepest %d)\n",
                            bevel.affected, bevel.median, bevel.deepest);
            }
            std::printf(
                "(mean/255 is the average darkening across the whole painted area, i.e. how much of a film it puts\n"
                " over the scene. deep >= 2x median means corners are darker than flat edges.)\n");
            return ok ? 0 : 6;
        }

        /// Check map2d shading against a manufactured height field.
        int runShadeProbe(const QString& shot_prefix, float ao_strength, int ao_directions, int ao_steps, bool quality_sweep) {
            constexpr int BLOCKS = 64;
            constexpr int PIX_PER_BLOCK = 12;
            constexpr int PLATEAU0 = 8, PLATEAU1 = 56;  // plateau extent
            constexpr int PIT0 = 30, PIT1 = 34;         // pit carved out of it
            constexpr unsigned char BASE = 200;         // flat colour, so shading is the only signal

            QOpenGLContext context;
            QSurfaceFormat format;
            format.setVersion(3, 3);
            format.setProfile(QSurfaceFormat::CoreProfile);
            context.setFormat(format);
            if (!context.create()) {
                std::printf("renderbench: shade probe could not create a GL context\n");
                return 7;
            }
            QOffscreenSurface surface;
            surface.setFormat(format);
            surface.create();
            if (!surface.isValid()) {
                std::printf("renderbench: shade probe could not create an offscreen surface\n");
                return 7;
            }
            context.makeCurrent(&surface);
            QOpenGLFunctions_3_3_Core core;
            if (!core.initializeOpenGLFunctions()) {
                std::printf("renderbench: shade probe has no 3.3 core functions\n");
                return 7;
            }
            auto* gl = &core;

            const auto height_of = [&](int x, int z) {
                if (x < PLATEAU0 || x >= PLATEAU1 || z < PLATEAU0 || z >= PLATEAU1) return 0;
                if (x >= PIT0 && x < PIT1 && z >= PIT0 && z < PIT1) return 0;
                return 1;
            };

            std::vector<unsigned char> colours(static_cast<size_t>(BLOCKS) * BLOCKS * 4);
            std::vector<float> heights(static_cast<size_t>(BLOCKS) * BLOCKS * 2);
            std::vector<unsigned char> materials(static_cast<size_t>(BLOCKS) * BLOCKS * 2, 0);
            for (int z = 0; z < BLOCKS; z++) {
                for (int x = 0; x < BLOCKS; x++) {
                    const float h = static_cast<float>(height_of(x, z));
                    const size_t c = (static_cast<size_t>(z) * BLOCKS + x) * 4;
                    colours[c + 0] = colours[c + 1] = colours[c + 2] = BASE;
                    // Match GpuMapWidget's atlas convention: alpha is a water
                    // flag, so zero denotes a dry column.
                    colours[c + 3] = 0;
                    const size_t hh = (static_cast<size_t>(z) * BLOCKS + x) * 2;
                    heights[hh + 0] = h;  // solid surface
                    heights[hh + 1] = h;  // top surface (no water)
                }
            }

            GLuint colour_tex = 0, height_tex = 0, material_tex = 0, palette_tex = 0;
            gl->glGenTextures(1, &colour_tex);
            gl->glBindTexture(GL_TEXTURE_2D, colour_tex);
            gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            gl->glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, BLOCKS, BLOCKS, 0, GL_RGBA, GL_UNSIGNED_BYTE, colours.data());

            gl->glGenTextures(1, &height_tex);
            gl->glBindTexture(GL_TEXTURE_2D, height_tex);
            gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            gl->glTexImage2D(GL_TEXTURE_2D, 0, GL_RG32F, BLOCKS, BLOCKS, 0, GL_RG, GL_FLOAT, heights.data());

            gl->glGenTextures(1, &material_tex);
            gl->glBindTexture(GL_TEXTURE_2D, material_tex);
            gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            gl->glTexImage2D(GL_TEXTURE_2D, 0, GL_RG8, BLOCKS, BLOCKS, 0, GL_RG, GL_UNSIGNED_BYTE, materials.data());

            std::vector<unsigned char> palette(256u * 3u * 3u, 255u);
            gl->glGenTextures(1, &palette_tex);
            gl->glBindTexture(GL_TEXTURE_2D, palette_tex);
            gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            gl->glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, 256, 3, 0, GL_RGB, GL_UNSIGNED_BYTE, palette.data());

            QOpenGLShaderProgram shader;
            // Both stages have to be checked: a fragment shader that fails to
            // compile still leaves a linkable program (with no fragment stage),
            // which renders black without any error - and a black frame silently
            // "fails" every shading assertion with meaningless numbers.
            const bool vs_ok = shader.addShaderFromSourceFile(QOpenGLShader::Vertex, ":/res/shaders/map2d.vert");
            const bool fs_ok = shader.addShaderFromSourceFile(QOpenGLShader::Fragment, ":/res/shaders/map2d.frag");
            if (!vs_ok || !fs_ok) {
                std::printf("renderbench: shade probe could not compile the shader: %s\n", shader.log().toStdString().c_str());
                return 7;
            }
            if (!shader.link()) {
                std::printf("renderbench: shade probe failed to link the shader: %s\n", shader.log().toStdString().c_str());
                return 7;
            }

            const int side = BLOCKS * PIX_PER_BLOCK;
            QOpenGLFramebufferObject fbo(side, side, QOpenGLFramebufferObject::NoAttachment);
            if (!fbo.bind()) {
                std::printf("renderbench: shade probe could not bind an FBO\n");
                return 7;
            }
            gl->glViewport(0, 0, side, side);
            gl->glDisable(GL_DEPTH_TEST);
            gl->glDisable(GL_BLEND);
            gl->glDisable(GL_CULL_FACE);
            gl->glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
            gl->glClear(GL_COLOR_BUFFER_BIT);

            QOpenGLVertexArrayObject vao;
            vao.create();
            QOpenGLVertexArrayObject::Binder binder(&vao);

            shader.bind();
            shader.setUniformValue("uColor", 0);
            shader.setUniformValue("uHeight", 1);
            shader.setUniformValue("uMaterial", 2);
            shader.setUniformValue("uBiomePalette", 3);
            shader.setUniformValue("uWaterBaseColor", 1.0f, 1.0f, 1.0f);
            // World (0, BLOCKS) at the bottom-left pixel, so image column == world x
            // and image row (from the bottom) == BLOCKS - world z.
            shader.setUniformValue("uViewOrigin", 0.0f, static_cast<float>(BLOCKS));
            shader.setUniformValue("uPxPerBlock", static_cast<float>(PIX_PER_BLOCK));
            shader.setUniformValue("uAtlasTexels", static_cast<float>(BLOCKS));
            shader.setUniformValue("uBlocksPerTexel", 1.0f);
            shader.setUniformValue("uSunStep", -1.0f, -1.0f);
            // The terrain-only frames draw with no ray march, so the darkness only has
            // to be a value the shadow frames can be measured against.
            shader.setUniformValue("uShadowReach", 0.0f);
            shader.setUniformValue("uShadowStep", 0.25f);
            shader.setUniformValue("uShadowStrength", 1.0f);
            shader.setUniformValue("uShadowDarkness", 0.7f);
            shader.setUniformValue("uEdgeWidth", 0.3f);
            shader.setUniformValue("uSaturation", 1.0f);
            shader.setUniformValue("uBrightness", 1.0f);
            shader.setUniformValue("uFlatShading", 0.0f);

            gl->glActiveTexture(GL_TEXTURE0);
            gl->glBindTexture(GL_TEXTURE_2D, colour_tex);
            gl->glActiveTexture(GL_TEXTURE1);
            gl->glBindTexture(GL_TEXTURE_2D, height_tex);
            gl->glActiveTexture(GL_TEXTURE2);
            gl->glBindTexture(GL_TEXTURE_2D, material_tex);
            gl->glActiveTexture(GL_TEXTURE3);
            gl->glBindTexture(GL_TEXTURE_2D, palette_tex);

            // Drawn twice: once as configured, once with AO off. The difference is
            // ambient occlusion alone, which is the only way to tell "AO is missing
            // here" from "this corner is shaded by the bevel instead".
            const auto draw = [&](float strength, int directions, int steps, float bevel = 1.0f, float shadow_reach = 0.0f,
                                  float shadow_strength = 1.0f) {
                shader.setUniformValue("uAoStrength", strength);
                shader.setUniformValue("uAoDirections", directions);
                shader.setUniformValue("uAoSteps", steps);
                // Without this the march samples distance 0, i.e. this pixel's own cell,
                // and every occlusion comes out 0.
                shader.setUniformValue("uAoStep0", 0.5f);
                shader.setUniformValue("uAoRadius", 16.0f);
                shader.setUniformValue("uBevelStrength", bevel);
                shader.setUniformValue("uShadowReach", shadow_reach);
                shader.setUniformValue("uShadowStrength", shadow_strength);
                gl->glDrawArrays(GL_TRIANGLES, 0, 3);
                return fbo.toImage().convertToFormat(QImage::Format_RGB32);
            };
            const QImage frame = draw(ao_strength, ao_directions, ao_steps);
            const QImage no_ao = draw(0.0f, ao_directions, ao_steps);
            // The shader and the FBO stay bound: the drop-scale sweep at the end of
            // this function draws more frames, and a released FBO silently returns the
            // last image instead of the new one.
            if (!shot_prefix.isEmpty()) {
                const QString path = QString("%1_shade_probe.png").arg(shot_prefix);
                if (frame.save(path)) std::printf("renderbench: wrote %s\n", path.toStdString().c_str());
            }

            // Image row 0 is the top of the framebuffer, and the shader puts
            // world z = uViewOrigin.y at the bottom, so the smallest z is at the
            // top. Sampling a world position therefore means row = z * px_per_block.
            const auto sample = [&](const QImage& image, double world_x, double world_z) {
                const int px = static_cast<int>(std::lround(world_x * PIX_PER_BLOCK - 0.5));
                const int py = static_cast<int>(std::lround(world_z * PIX_PER_BLOCK - 0.5));
                return static_cast<int>(qRed(image.pixel(std::clamp(px, 0, side - 1), std::clamp(py, 0, side - 1))));
            };
            // Bevel checks read the AO-off frame and AO checks the difference
            // between the two: they are separate terms (AO has its own strength
            // setting), so neither result may depend on the other.
            const auto bevel_at = [&](double world_x, double world_z) { return sample(no_ao, world_x, world_z); };
            const auto ao_at = [&](double world_x, double world_z) {
                return sample(no_ao, world_x, world_z) - sample(frame, world_x, world_z);
            };
            const auto block_profile = [&](int bx, int bz, const char* label) {
                // Corners and centre of the block, offset inwards so the sample is
                // inside the corner patch rather than exactly on its tip.
                const double inset = 1.5 / PIX_PER_BLOCK;
                std::printf("  %-16s block(%2d,%2d): centre %3d  nw %3d  ne %3d  sw %3d  se %3d\n", label, bx, bz,
                            bevel_at(bx + 0.5, bz + 0.5), bevel_at(bx + inset, bz + inset), bevel_at(bx + 1 - inset, bz + inset),
                            bevel_at(bx + inset, bz + 1 - inset), bevel_at(bx + 1 - inset, bz + 1 - inset));
                return std::array<int, 5>{bevel_at(bx + 0.5, bz + 0.5), bevel_at(bx + inset, bz + inset),
                                          bevel_at(bx + 1 - inset, bz + inset), bevel_at(bx + inset, bz + 1 - inset),
                                          bevel_at(bx + 1 - inset, bz + 1 - inset)};
            };

            // Shading must not jump at a block boundary *that lies on a level
            // surface*. At a height step a sharp edge is intended - that is the
            // bevel - so those boundaries are excluded rather than measured.
            // Inside a block the gradient is bounded by the ramp widths, so a
            // level boundary should look the same; a bigger jump means two
            // neighbouring blocks disagree about the shading, i.e. a seam.
            struct Continuity {
                double interior_max{0};
                double flat_max{0};
                double step_max{0};
                double flat_p999{0};
                QPointF flat_at{};
                int flat_left{0};
                int flat_right{0};
            };
            const auto continuity = [&](bool horizontal) {
                Continuity result;
                std::vector<double> flat, interior;
                const int limit = horizontal ? frame.width() : frame.height();
                const int lines = horizontal ? frame.height() : frame.width();
                for (int line = 0; line < lines; line++) {
                    const int bz = static_cast<int>(std::floor((line + 0.5) / PIX_PER_BLOCK));
                    for (int i = 1; i < limit; i++) {
                        const QRgb a = horizontal ? frame.pixel(i - 1, line) : frame.pixel(line, i - 1);
                        const QRgb b = horizontal ? frame.pixel(i, line) : frame.pixel(line, i);
                        const int jump = std::abs(static_cast<int>(qRed(a)) - static_cast<int>(qRed(b)));
                        if (i % PIX_PER_BLOCK != 0) {
                            result.interior_max = std::max(result.interior_max, static_cast<double>(jump));
                            interior.push_back(jump);
                            continue;
                        }
                        const int near = i / PIX_PER_BLOCK - 1;  // block index before the boundary
                        const int far = i / PIX_PER_BLOCK;       // block index after it
                        const bool level =
                            horizontal ? (height_of(near, bz) == height_of(far, bz)) : (height_of(bz, near) == height_of(bz, far));
                        if (!level) {
                            result.step_max = std::max(result.step_max, static_cast<double>(jump));
                            continue;
                        }
                        if (jump > result.flat_max) {
                            result.flat_max = jump;
                            result.flat_at = horizontal ? QPointF(near, bz) : QPointF(bz, near);
                            result.flat_left = qRed(a);
                            result.flat_right = qRed(b);
                        }
                        flat.push_back(jump);
                    }
                }
                if (!flat.empty()) {
                    std::sort(flat.begin(), flat.end());
                    result.flat_p999 = flat[static_cast<size_t>(0.999 * (flat.size() - 1))];
                }
                return result;
            };

            std::printf("\n== shade probe - %d blocks, %d px/block, colour %d, shadow disabled ==\n", BLOCKS, PIX_PER_BLOCK, BASE);
            const auto flat = block_profile(20, 20, "flat interior");
            const auto edge = block_profile(PLATEAU0, 20, "raised, west edge");
            const auto outer = block_profile(PLATEAU0, PLATEAU0, "raised, NW corner");
            const auto east_edge = block_profile(PLATEAU1 - 1, 20, "raised, east edge");
            const auto outside_east = block_profile(PLATEAU1, 20, "ground east of it");
            const auto outside_south = block_profile(20, PLATEAU1, "ground south of it");
            const auto pit_nw = block_profile(PIT0, PIT0, "pit floor, NW corner");
            const auto pit_se = block_profile(PIT1 - 1, PIT1 - 1, "pit floor, SE corner");
            // The raised plane's concave corner: with the light-side comparison it
            // is plain interior, so this is the regression guard against the corner
            // patch that used to be applied here.
            const auto concave = block_profile(PIT0 - 1, PIT0 - 1, "raised concave corner");

            int failures = 0;
            const auto check = [&](bool ok, const char* what) {
                std::printf("  %-58s %s\n", what, ok ? "ok" : "FAIL");
                if (!ok) ++failures;
            };
            // Profile order is {centre, nw, ne, sw, se}. The light is at the
            // top-left of the map, so a west or north step lands on this block's
            // nw/sw or nw/ne samples respectively.
            std::printf("\n");
            check(flat[0] == BASE, "flat interior is unmodified");
            check(edge[1] > edge[0] + 5 && edge[3] > edge[0] + 5, "a raised area's west edge is lit");
            check(edge[2] == edge[0] && edge[4] == edge[0], "the lit band does not reach the east side of the block");
            check(outer[1] > outer[0] + 5, "a raised area's north-west corner is lit");
            check(std::abs(outer[1] - edge[1]) <= 2, "two lit edges at a corner do not stack");
            check(east_edge[0] == BASE && east_edge[2] == BASE && east_edge[4] == BASE,
                  "a raised area's own east edge is not lit (only its far side)");
            check(outside_east[1] < outside_east[0] - 5 && outside_east[3] < outside_east[0] - 5,
                  "the ground east of a raised area is shaded (shadow outside it)");
            // Compared against the far side of the same block rather than against
            // its centre: ambient occlusion darkens the centre as well, so only the
            // west/east difference isolates the bevel. Measured on the AO-off frame
            // the band is 16 levels deep at that sample (EDGE_DARK over its ramp).
            check(outside_east[2] == outside_east[4] && outside_east[2] > outside_east[1] + 10,
                  "that shadow is on the west side of the ground block only");
            check(outside_south[1] < outside_south[0] - 5 && outside_south[2] < outside_south[0] - 5,
                  "the ground south of a raised area is shaded (shadow outside it)");
            check(concave[0] == BASE && concave[1] == BASE && concave[2] == BASE && concave[3] == BASE && concave[4] == BASE,
                  "a raised area's concave corner is plain (no dangling patch)");
            // Where two bands meet at a corner, the block between them is diagonal
            // to the wall and so gets neither band. The continuation patch has to
            // fill that in *at the corner only*: along the shared border with each
            // neighbouring block it must match that block's band, and away from the
            // corner it must be gone, or the line runs on over ground with no step.
            // Measured on the AO-off frame, so this is the bevel and nothing else.
            const double in = 1.5 / PIX_PER_BLOCK;                                      // clear of the corner tip
            const double tip = 0.4 / PIX_PER_BLOCK;                                     // just inside the corner
            const int bevel_band = sample(no_ao, PLATEAU1 + in, PLATEAU1 - 0.5);        // the band along the wall
            const int bevel_tip = sample(no_ao, PLATEAU1 + tip, PLATEAU1 + tip);        // the corner itself
            const int bevel_far_west = sample(no_ao, PLATEAU1 + tip, PLATEAU1 + 0.95);  // same edge, far end
            const int bevel_far_north = sample(no_ao, PLATEAU1 + 0.95, PLATEAU1 + tip);
            std::printf("  corner patch: band %d, corner tip %d, same edge far end %d / %d\n", bevel_band, bevel_tip, bevel_far_west,
                        bevel_far_north);
            check(bevel_tip <= bevel_band + 8, "the shadow bands meet at the corner (no hole)");
            check(bevel_far_west == BASE && bevel_far_north == BASE,
                  "the corner patch stops at the corner (no line on ground with no step)");
            check(sample(no_ao, PIT1 - in, PIT1 - in) <= sample(no_ao, PIT1 - in, PIT0 + 0.5) + 8,
                  "the lit bands meet at a pit's inside corner too");
            check(pit_nw[1] < pit_nw[0] - 10, "a pit is shaded at its north-west inside corner");
            check(std::abs(pit_nw[1] - pit_nw[2]) <= 2 && std::abs(pit_nw[1] - pit_nw[3]) <= 2, "two dark edges at a corner do not stack");
            check(pit_nw[4] > pit_nw[1] + 10 && pit_nw[4] >= pit_nw[0], "the pit's shading fades towards its south-east side");
            check(pit_se[1] >= pit_se[0] - 5 && pit_se[4] >= pit_se[0] - 5, "a pit's south-east inside gets no bevel shading");

            // The bevel strength scales how far the shading moves away from the flat
            // colour, so 0 has to be exactly the flat colour and half has to land
            // halfway between that and the full bevel - a curve of any other shape
            // would make the slider's ends and middle disagree with their labels.
            const QImage bevel_off = draw(0.0f, ao_directions, ao_steps, 0.0f);
            const QImage bevel_half = draw(0.0f, ao_directions, ao_steps, 0.5f);
            const int full_band = bevel_at(PLATEAU1 + in, PLATEAU1 - 0.5);
            const int off_band = sample(bevel_off, PLATEAU1 + in, PLATEAU1 - 0.5);
            const int half_band = sample(bevel_half, PLATEAU1 + in, PLATEAU1 - 0.5);
            std::printf("  bevel strength: off %d, half %d, full %d (flat colour is %d)\n", off_band, half_band, full_band, BASE);
            check(off_band == BASE, "a bevel strength of 0 leaves the flat colour (the switch)");
            check(std::abs(half_band - (off_band + full_band) / 2) <= 1, "half strength is halfway between off and full");

            // Ambient occlusion, sampled on its own (with AO and without, the
            // difference being the occlusion term and nothing else). Read as a
            // fraction of the sky the terrain hides, so the checks do not depend on
            // the strength setting.
            const auto occl = [&](const char* what, double x, double z) {
                const double f = ao_at(x, z) / (255.0 * ao_strength);
                std::printf("    %-46s %5.3f\n", what, f);
                return f;
            };
            std::printf("  occlusion (fraction of the sky hidden by the terrain):\n");
            const double e = 1.5 / PIX_PER_BLOCK;  // inside the corner, off its tip
            // The pit is a closed ring, so all four of its inside corners have walls
            // on two sides and directly across the diagonal.
            const double o_pit_nw = occl("pit inside corner (NW)", PIT0 + e, PIT0 + e);
            const double o_pit_ne = occl("pit inside corner (NE)", PIT1 - e, PIT0 + e);
            const double o_pit_sw = occl("pit inside corner (SW)", PIT0 + e, PIT1 - e);
            const double o_pit_se = occl("pit inside corner (SE)", PIT1 - e, PIT1 - e);
            const double o_pit_wall = occl("pit floor beside its west wall, mid-block", PIT0 + e, 32.5);
            const double o_pit_centre = occl("pit floor centre, far from any wall", 32.5, 32.5);
            // The raised plane's own concave corner: everything around it is at the
            // same height or lower, so nothing hides sky from it.
            const double o_raised_concave = occl("raised plane, concave corner (step drops away)", PIT0 - e, PIT0 - e);
            // Ground outside the raised plane: one wall along each side, and at the
            // corner only the diagonal, which still hides sky.
            const double o_ground_corner = occl("ground diagonal to the plane's corner", PLATEAU1 + e, PLATEAU1 + e);
            const double o_ground_side = occl("ground beside the plane, against its wall", PLATEAU1 + e, 20.5);
            const double o_flat = occl("flat plane interior (no wall anywhere)", 20.5, 20.5);
            // The diagonal occluder must not spread along the block's edges: sampled a
            // whole 0.9 of a block from that corner, where only the diagonal is raised.
            const double o_corner_far_west = occl("ground corner block, west edge away from the corner", PLATEAU1 + e, PLATEAU1 + 0.9);
            const double o_corner_far_north = occl("ground corner block, north edge away from the corner", PLATEAU1 + 0.9, PLATEAU1 + e);

            const double o_corner_min = std::min(std::min(o_pit_nw, o_pit_ne), std::min(o_pit_sw, o_pit_se));
            // With AO off every one of these is 0 by definition, so they only say
            // something when it is on. The bevel checks above cover the AO-off case.
            if (ao_strength <= 0.0f) {
                std::printf("  (AO strength is 0, so the occlusion checks below do not apply)\n");
            } else {
                check(o_corner_min > o_pit_wall, "a pit's inside corners occlude more than its walls do");
                check(o_pit_wall > o_flat + 0.02, "a wall occludes the floor beside it");
                // More, but not much more: in a one-block-deep pit the far wall is only
                // a couple of blocks away, so a mid-block sample already has most of the
                // corner's occlusion (measured 1.09x).
                check(o_corner_min > o_pit_wall * 1.05, "a corner occludes more than a single wall");
                check(o_raised_concave == 0.0, "a raised plane's concave corner is not occluded (nothing rises above it)");
                check(o_ground_side > o_flat + 0.02, "the ground beside a raised area is occluded by its wall");
                check(o_ground_corner > o_flat + 0.01, "the diagonal wall at a corner still occludes it");
                check(o_flat == 0.0, "flat ground is not occluded at all");
                check(o_corner_far_west < o_ground_corner && o_corner_far_north < o_ground_corner,
                      "the diagonal occluder is strongest at the corner it stands at");
                // The one that matters most: two neighbours at the same height must
                // shade alike, whatever the geometry around them. A jump here is a line
                // across ground where nothing changes, which is what the eight-neighbour
                // form kept producing and had to be smoothed by hand. The march depends
                // only on the terrain around the pixel, so this should be sampling noise.
                double worst_pair = 0;
                QPointF worst_at;
                for (int bz = 0; bz < BLOCKS; ++bz) {
                    for (int bx = 1; bx < BLOCKS; ++bx) {
                        if (height_of(bx - 1, bz) != height_of(bx, bz)) continue;
                        const double mid = bz + 0.5;
                        const double jump = std::abs(ao_at(bx - 0.05, mid) - ao_at(bx + 0.05, mid)) / (255.0 * ao_strength);
                        if (jump > worst_pair) {
                            worst_pair = jump;
                            worst_at = QPointF(bx, mid);
                        }
                    }
                }
                for (int bx = 0; bx < BLOCKS; ++bx) {
                    for (int bz = 1; bz < BLOCKS; ++bz) {
                        if (height_of(bx, bz - 1) != height_of(bx, bz)) continue;
                        const double mid = bx + 0.5;
                        const double jump = std::abs(ao_at(mid, bz - 0.05) - ao_at(mid, bz + 0.05)) / (255.0 * ao_strength);
                        if (jump > worst_pair) {
                            worst_pair = jump;
                            worst_at = QPointF(mid, bz);
                        }
                    }
                }
                std::printf("    %-46s %.3f at (%.1f,%.1f)\n", "worst occlusion jump between equal-height neighbours", worst_pair,
                            worst_at.x(), worst_at.y());
                check(worst_pair <= 0.06, "equal-height neighbours shade alike (no dividing line)");
            }

            // The lit band is the bevel itself, so it is measured against the same
            // band elsewhere: it has to be a clean ramp with nothing added at a
            // block boundary, or a straight wall shows a scalloped edge.
            const double inset = 1.0 / PIX_PER_BLOCK;
            const int band_mid = bevel_at(PLATEAU0 + inset, 20.5);
            const int band_end = bevel_at(PLATEAU0 + inset, 20 + 1 - inset);
            std::printf("  lit band: mid-block %d, near block corner %d\n", band_mid, band_end);
            check(band_mid > BASE + 5, "the west band is bright");
            check(std::abs(band_mid - band_end) <= 2, "the band is uniform along the wall");
            // Scalloping guard: a straight edge must not get a bright dot every block.
            const int e1 = bevel_at(PLATEAU0 + 1.5 / PIX_PER_BLOCK, 20.5);
            const int e2 = bevel_at(PLATEAU0 + 1.5 / PIX_PER_BLOCK, 21.5);
            check(std::abs(e1 - e2) <= 2, "straight edge brightness does not vary block to block");

            const auto h_cont = continuity(true);
            const auto v_cont = continuity(false);
            std::printf("  continuity (max jump / 255; level = boundary on level ground, step = intended bevel edge)\n");
            std::printf("    horizontal: interior %.0f, level %.0f, step %.0f\n", h_cont.interior_max, h_cont.flat_max, h_cont.step_max);
            std::printf("    vertical:   interior %.0f, level %.0f, step %.0f\n", v_cont.interior_max, v_cont.flat_max, v_cont.step_max);
            const Continuity& worst = (h_cont.flat_max >= v_cont.flat_max) ? h_cont : v_cont;
            std::printf("    worst level boundary: %.0f at block (%.0f,%.0f), %d -> %d\n", worst.flat_max, worst.flat_at.x(),
                        worst.flat_at.y(), worst.flat_left, worst.flat_right);
            // A band runs along a wall, so it also has to *start* somewhere: at the
            // corner where that wall ends, the level boundary alongside it does jump.
            // The invariant that still holds is that no level boundary may be a
            // harder edge than the bevel itself, which is what a seam would be.
            const double level_max = std::max(h_cont.flat_max, v_cont.flat_max);
            const double step_max = std::max(h_cont.step_max, v_cont.step_max);
            check(level_max <= step_max, "no level block boundary is a harder edge than an intended step");

            // How the march resolution affects what comes out: too few samples and the
            // occlusion turns patchy (a wall is missed between samples) or the field
            // steps between blocks, both of which show as a jump between equal-height
            // neighbours.
            if (quality_sweep) {
                const auto equal_height_jump = [&](const QImage& img) {
                    int worst = 0;
                    for (int bz = 0; bz < BLOCKS; ++bz) {
                        for (int bx = 1; bx < BLOCKS; ++bx) {
                            if (height_of(bx - 1, bz) != height_of(bx, bz)) continue;
                            const int jump = std::abs(sample(img, bx - 0.05, bz + 0.5) - sample(img, bx + 0.05, bz + 0.5));
                            worst = std::max(worst, jump);
                        }
                    }
                    for (int bx = 0; bx < BLOCKS; ++bx) {
                        for (int bz = 1; bz < BLOCKS; ++bz) {
                            if (height_of(bx, bz - 1) != height_of(bx, bz)) continue;
                            const int jump = std::abs(sample(img, bx + 0.5, bz - 0.05) - sample(img, bx + 0.5, bz + 0.05));
                            worst = std::max(worst, jump);
                        }
                    }
                    return worst;
                };
                std::printf("\n  AO march resolution sweep at strength %.2f (12 px/block):\n", ao_strength);
                std::printf("    %6s %6s %10s %10s %14s %18s\n", "dirs", "steps", "samples", "wall", "corner", "equal-height jump");
                for (const auto [dirs, steps] : std::array<std::pair<int, int>, 6>{{{4, 6}, {4, 12}, {8, 6}, {8, 12}, {8, 24}, {16, 24}}}) {
                    const QImage on = draw(ao_strength, dirs, steps);
                    const QImage off = draw(0.0f, dirs, steps);
                    const auto ao_of = [&](double x, double z) { return sample(off, x, z) - sample(on, x, z); };
                    std::printf("    %6d %6d %10d %10d %10d %18d\n", dirs, steps, dirs * steps, ao_of(PIT0 + 0.05, 32.5),
                                ao_of(PIT0 + 0.05, PIT0 + 0.05), equal_height_jump(on));
                }
                std::printf(
                    "    (wall = the pit floor beside its wall; corner = the pit's inside corner, which must\n"
                    "     come out darker than the wall; equal-height jump must stay small at any setting.)\n");
            }

            // The shadow's ray march, measured on terrain of its own: a wall four blocks
            // tall (the ray starts one block above the surface, so a one-block step casts
            // nothing) and 35 blocks long, so its eastern end is a clean cut. The height
            // texture is re-uploaded for it, after every frame above has been captured, so
            // no earlier check sees this terrain.
            {
                constexpr float WALL_TOP = 4.0f;
                constexpr int WALL_X0 = 6, WALL_X1 = 41;   // the wall's extent in x
                constexpr int WALL_Z0 = 24, WALL_Z1 = 26;  // ...and in z, its southern face at z=26
                std::vector<float> wall_heights(static_cast<size_t>(BLOCKS) * BLOCKS * 2, 0.0f);
                for (int z = 0; z < BLOCKS; ++z) {
                    for (int x = 0; x < BLOCKS; ++x) {
                        const bool wall = x >= WALL_X0 && x < WALL_X1 && z >= WALL_Z0 && z < WALL_Z1;
                        const size_t hh = (static_cast<size_t>(z) * BLOCKS + x) * 2;
                        wall_heights[hh + 0] = wall ? WALL_TOP : 0.0f;
                        wall_heights[hh + 1] = wall ? WALL_TOP : 0.0f;
                    }
                }
                gl->glActiveTexture(GL_TEXTURE1);
                gl->glBindTexture(GL_TEXTURE_2D, height_tex);
                gl->glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, BLOCKS, BLOCKS, GL_RG, GL_FLOAT, wall_heights.data());

                const QImage shadow_full = draw(0.0f, ao_directions, ao_steps, 1.0f, 24.0f);
                const QImage shadow_half = draw(0.0f, ao_directions, ao_steps, 1.0f, 24.0f, 0.5f);
                const QImage shadow_off = draw(0.0f, ao_directions, ao_steps, 1.0f, 24.0f, 0.0f);

                const int at_face = sample(shadow_full, 20.5, 26.5);   // against the wall's south face
                const int one_back = sample(shadow_full, 20.5, 27.5);  // a block further out
                const int out_of_reach = sample(shadow_full, 20.5, 30.5);
                const int far_away = sample(shadow_full, 10.5, 40.5);  // the light never crosses the wall from here
                // A hard shadow is intentionally constant within one height texel.
                const int in_block_near = sample(shadow_full, 20.5, 27.1);
                const int in_block_far = sample(shadow_full, 20.5, 27.9);
                const int end_inside = sample(shadow_full, 41.5, 27.5);  // west of the light line through the wall's corner
                const int end_outside = sample(shadow_full, 43.5, 27.5);
                const int strength_off = sample(shadow_off, 20.5, 26.5);
                const int strength_half = sample(shadow_half, 20.5, 26.5);
                std::printf("\n  shadow (4-block wall, reach 24, darkness %.2f):\n", 1.0 - 0.7);
                std::printf("    beside the wall %d, a block out %d, out of reach %d, far from it %d\n", at_face, one_back, out_of_reach,
                            far_away);
                std::printf("    inside one block at z=27: z=27.1 %d, z=27.9 %d\n", in_block_near, in_block_far);
                std::printf("    past the wall's end: x=41.5 %d, x=43.5 %d\n", end_inside, end_outside);
                std::printf("    strength: off %d, half %d, full %d\n", strength_off, strength_half, at_face);
                check(at_face < BASE - 20, "the ground beside a wall is in its shadow");
                check(at_face < BASE - 20 && one_back < BASE - 20 && out_of_reach == BASE,
                      "the hard shadow persists to its geometric end without a grey tail");
                check(far_away == BASE, "ground the light reaches over no wall is unshaded");
                check(in_block_near == in_block_far, "a height texel has one stable shadow value (no split branch)");
                check(strength_off == BASE, "a shadow strength of 0 removes the shadow");
                check(std::abs(strength_half - (BASE + at_face) / 2) <= 2, "half strength is halfway to no shadow");
            }

            std::printf("  %s (%d failures)\n", failures == 0 ? "PASS" : "FAIL", failures);
            shader.release();
            fbo.release();
            return failures == 0 ? 0 : 8;
        }

    }  // namespace

    std::optional<Request> parseRequest(int argc, char* argv[]) {
        Request request;
        bool found = false;
        for (int i = 1; i < argc; i++) {
            const std::string arg = argv[i];
            const auto next = [&](const char* what) -> std::optional<std::string> {
                if (i + 1 >= argc) {
                    std::printf("renderbench: %s expects a value\n", what);
                    return std::nullopt;
                }
                return std::string(argv[++i]);
            };
            if (arg == "--bench-render") {
                auto value = next("--bench-render");
                if (!value) return std::nullopt;
                request.level_path = QString::fromStdString(*value);
                found = true;
            } else if (arg == "--bench-count") {
                auto value = next("--bench-count");
                if (!value) return std::nullopt;
                request.region_count = std::max(1, std::stoi(*value));
            } else if (arg == "--bench-center") {
                auto x = next("--bench-center");
                auto z = x ? next("--bench-center") : std::nullopt;
                if (!z) return std::nullopt;
                request.center_region_x = std::stoi(*x);
                request.center_region_z = std::stoi(*z);
            } else if (arg.rfind("--bench-dim", 0) == 0) {
                auto value = next("--bench-dim");
                if (!value) return std::nullopt;
                request.dim = std::stoi(*value);
            } else if (arg == "--bench-sweep") {
                request.sweep = true;
            } else if (arg == "--bench-shot") {
                auto value = next("--bench-shot");
                if (!value) return std::nullopt;
                request.shot_prefix = QString::fromStdString(*value);
            } else if (arg == "--bench-shadow-steps") {
                auto value = next("--bench-shadow-steps");
                if (!value) return std::nullopt;
                request.shadow_steps = std::max(1, std::stoi(*value));
            } else if (arg == "--bench-no-shadow") {
                request.no_shadow = true;
            } else if (arg == "--bench-flat") {
                request.flat_shading = true;
            } else if (arg == "--bench-no-ao") {
                request.ao_strength = 0.0f;
            } else if (arg == "--bench-ao-strength") {
                auto value = next("--bench-ao-strength");
                if (!value) return std::nullopt;
                request.ao_strength = std::stof(*value);
            } else if (arg == "--bench-ao-march") {
                auto dirs = next("--bench-ao-march");
                auto steps = dirs ? next("--bench-ao-march") : std::nullopt;
                if (!steps) return std::nullopt;
                request.ao_directions = std::max(1, std::stoi(*dirs));
                request.ao_steps = std::max(1, std::stoi(*steps));
            } else if (arg == "--bench-ao-quality-sweep") {
                request.ao_quality_sweep = true;
                request.shade_probe = true;
                found = true;  // needs no level
            } else if (arg == "--bench-gpu-timing") {
                request.gpu_timing = true;
            } else if (arg == "--bench-widget-smoke") {
                request.widget_smoke = true;
            } else if (arg == "--bench-zoom-sweep") {
                request.zoom_sweep = true;
            } else if (arg == "--bench-coverage") {
                request.coverage_check = true;
            } else if (arg == "--bench-blank") {
                request.blank_check = true;
            } else if (arg == "--bench-coords") {
                request.coords_bench = true;
            } else if (arg == "--bench-ao-check") {
                request.ao_check = true;
            } else if (arg == "--bench-shade-probe") {
                request.shade_probe = true;
                found = true;  // needs no level
            } else if (arg == "--bench-no-paint") {
                request.paint = false;
            }
        }
        if (!found) return std::nullopt;
        return request;
    }

    int run(const Request& request) {
        // The synthetic shading probe needs no level at all.
        if (request.shade_probe) {
            const int rc =
                runShadeProbe(request.shot_prefix, request.ao_strength, request.ao_directions, request.ao_steps, request.ao_quality_sweep);
            if (rc != 0 || request.level_path.isEmpty()) return rc;
        }
        if (request.level_path.isEmpty()) {
            std::printf("renderbench: --bench-render <level> is required\n");
            return 1;
        }

        AsyncLevelLoader loader;
        const auto level_path = request.level_path.toStdString();
        if (!loader.open(level_path, leveldb_xor::keyForLevel(level_path))) {
            std::printf("renderbench: cannot open level '%s'\n", request.level_path.toStdString().c_str());
            return 1;
        }

        const MapFilter filter;
        loader.setFilter(filter);

        std::printf("renderbench: level=%s\n", request.level_path.toStdString().c_str());
        std::printf("renderbench: render_style=%d tile_scale=%d shadow_scale=%d threads=%d\n", setting::current().MAP_RENDER_STYLE,
                    setting::current().TILE_RENDER_SCALE, setting::current().SHADOW_MAP_SCALE, setting::current().THREAD_NUM);

        // The coordinate scan is independent of the region bake, so it runs on its
        // own and finishes in seconds; it is a measurement, not a bake report.
        if (request.coords_bench) return runCoordsBench(loader);

        const auto candidates = candidateRegions(request);

        // Pick the requested number of existing regions, nearest to the centre first.
        RenderProfile::instance().reset();
        std::vector<BakedRegion> samples;
        const int max_attempts = std::min<int>(static_cast<int>(candidates.size()), request.region_count * 40);
        int attempts = 0;
        int empty_attempts = 0;
        int64_t empty_us = 0;
        for (const auto& pos : candidates) {
            if (static_cast<int>(samples.size()) >= request.region_count || attempts >= max_attempts) break;
            attempts++;
            auto baked = bakeRegion(loader, pos, filter);
            if (baked.region && baked.region->valid) {
                samples.push_back(baked);
            } else {
                // Chunks were queried but none is present. This is the common case when zoomed out,
                // so it is counted separately instead of being folded into the phase averages.
                empty_attempts++;
                empty_us += baked.total_us;
                delete baked.region;
            }
        }
        std::printf("renderbench: %d attempts, %zu non-empty regions (centre region %d,%d dim %d)\n", attempts, samples.size(),
                    request.center_region_x, request.center_region_z, request.dim);
        if (empty_attempts > 0) {
            std::printf("renderbench: empty %d/%d attempts, %.1f ms total, %.0f us each\n", empty_attempts, attempts, empty_us / 1000.0,
                        static_cast<double>(empty_us) / empty_attempts);
        }
        std::printf("renderbench: regions =");
        for (const auto& s : samples) std::printf(" (%d,%d)", s.pos.x / constant::RW, s.pos.z / constant::RW);
        std::printf("\n");
        if (samples.empty()) {
            std::printf("renderbench: no non-empty region found near the centre; use --bench-center\n");
            return 2;
        }
        printPhaseTable(samples, "region bake");

        if (request.paint) runPaintBench(samples, 3840, 2160);

        if (request.gpu_timing) {
            runGpuTimingBench(request, loader, samples, filter);
            runScreenCostBench(request, loader, samples, filter);
        }

        if (request.widget_smoke) return runWidgetSmoke(loader);

        if (request.zoom_sweep) runZoomSweepBench(loader);

        if (request.coverage_check) {
            // Several viewport shapes: the failure mode depends on the span, so a
            // single size would not catch it.
            for (const QSize& viewport : {QSize(1920, 1080), QSize(1024, 640), QSize(1600, 1200), QSize(3840, 2160)}) {
                const int rc = runCoverageBench(loader, viewport, request.shadow_steps);
                if (rc != 0) return rc;
            }
        }

        if (request.blank_check) {
            const int rc = runBlankCheck(loader);
            if (rc != 0) return rc;
        }

        if (request.ao_check) {
            const int rc = runAoCheck(request, loader, samples);
            if (rc != 0) return rc;
        }

        if (!request.shot_prefix.isEmpty()) runShotBench(request, loader, samples);

        if (request.sweep) {
            const int sweep_count = std::min<int>(static_cast<int>(samples.size()), 3);
            std::vector<bl::chunk_pos> positions;
            for (int i = 0; i < sweep_count; i++) positions.push_back(samples[i].pos);

            std::printf("\n== resolution sweep - %d regions per case ==\n", sweep_count);
            std::printf("%7s %11s %13s %10s %10s %10s %10s %13s %10s\n", "style", "tile_scale", "shadow_scale", "bevel", "gather", "ray",
                        "apply", "chunk_load", "bake");
            for (int style : {1, 2}) {
                for (int tile_scale : (style == 1) ? std::vector<int>{4} : std::vector<int>{4, 8, 16}) {
                    for (int shadow_scale : (style == 1) ? std::vector<int>{2} : std::vector<int>{2, 4, 8}) {
                        auto s = setting::current();
                        s.MAP_RENDER_STYLE = style;
                        s.TILE_RENDER_SCALE = tile_scale;
                        s.SHADOW_MAP_SCALE = shadow_scale;
                        setting::apply(s);
                        RenderProfile::instance().reset();

                        int64_t total = 0;
                        for (const auto& pos : positions) {
                            auto baked = bakeRegion(loader, pos, filter);
                            total += baked.total_us;
                            delete baked.region;
                        }
                        const auto& prof = RenderProfile::instance();
                        // The knobs only affect the bake, so the same chunk reads are reported
                        // separately instead of being mixed into the case's total.
                        const double load_ms = prof.get(RenderPhase::ChunkLoad) / 1000.0;
                        std::printf("%7d %11d %13d %9.1f %10.1f %10.1f %10.1f %13.1f %10.1f\n", style, tile_scale, shadow_scale,
                                    prof.get(RenderPhase::Bevel) / 1000.0, prof.get(RenderPhase::ShadowGather) / 1000.0,
                                    prof.get(RenderPhase::ShadowRay) / 1000.0, prof.get(RenderPhase::ShadowApply) / 1000.0, load_ms,
                                    total / 1000.0 - load_ms);
                    }
                }
            }
            std::printf(
                "(values in ms for all regions of that case; 'bake' = wall - chunk_load, i.e. what the\n"
                " resolution knobs actually control. LevelDB's own block cache stays warm across cases.)\n");
        }

        for (auto& s : samples) delete s.region;
        return 0;
    }

}  // namespace renderbench
