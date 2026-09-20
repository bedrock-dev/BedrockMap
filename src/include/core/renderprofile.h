#ifndef BEDROCKMAP_RENDERPROFILE_H
#define BEDROCKMAP_RENDERPROFILE_H

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>

// Coarse per-phase timings (microseconds, summed over every render thread) for
// the CPU region bake. The map path is CPU-bound and the phases have very
// different scaling behaviour, so keeping them separate shows which one
// dominates without attaching a profiler. Each phase is touched once per
// chunk/region, i.e. the measurement cost is noise next to the work itself.
enum class RenderPhase {
    ChunkLoad,     // LevelDB read + chunk parse for the 8x8 region chunks
    HeightMap,     // height-map preload for the shadow pass
    Terrain,       // MapTile::bakeChunkTerrain (surface scan + block colours)
    Actors,        // MapTile::bakeChunkActors
    Bevel,         // upscale to TILE_RENDER_SCALE + edge/corner shading
    ShadowGather,  // expanded height array for the shadow pass
    ShadowRay,     // per-shadow-texel ray march
    ShadowApply,   // per-pixel shadow lookup + water overlay
    Count
};

struct RenderProfile {
    std::atomic<int64_t> us[static_cast<size_t>(RenderPhase::Count)];
    std::atomic<int64_t> regions{0};  // regions baked

    static RenderProfile& instance() {
        static RenderProfile profile;
        return profile;
    }

    static const char* name(RenderPhase phase) {
        switch (phase) {
            case RenderPhase::ChunkLoad:
                return "chunk_load";
            case RenderPhase::HeightMap:
                return "heightmap";
            case RenderPhase::Terrain:
                return "terrain";
            case RenderPhase::Actors:
                return "actors";
            case RenderPhase::Bevel:
                return "bevel";
            case RenderPhase::ShadowGather:
                return "shadow_gather";
            case RenderPhase::ShadowRay:
                return "shadow_ray";
            case RenderPhase::ShadowApply:
                return "shadow_apply";
            default:
                return "?";
        }
    }

    void add(RenderPhase phase, int64_t micros) { us[static_cast<size_t>(phase)].fetch_add(micros, std::memory_order_relaxed); }

    [[nodiscard]] int64_t get(RenderPhase phase) const { return us[static_cast<size_t>(phase)].load(std::memory_order_relaxed); }

    void reset() {
        for (auto& v : us) v.store(0, std::memory_order_relaxed);
        regions.store(0, std::memory_order_relaxed);
    }
};

inline int64_t microsSince(const std::chrono::steady_clock::time_point& t0) {
    return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - t0).count();
}

// Adds the enclosing scope's duration to a phase counter.
class ScopedPhase {
   public:
    explicit ScopedPhase(RenderPhase phase) : phase_(phase), t0_(std::chrono::steady_clock::now()) {}
    ~ScopedPhase() { RenderProfile::instance().add(phase_, microsSince(t0_)); }

    ScopedPhase(const ScopedPhase&) = delete;
    ScopedPhase& operator=(const ScopedPhase&) = delete;

   private:
    RenderPhase phase_;
    std::chrono::steady_clock::time_point t0_;
};

#endif  // BEDROCKMAP_RENDERPROFILE_H
