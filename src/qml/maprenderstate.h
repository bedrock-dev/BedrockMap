#ifndef BEDROCKMAP_MAPRENDERSTATE_H
#define BEDROCKMAP_MAPRENDERSTATE_H

#include <atomic>

/// Camera transform of the frame the terrain renderer has actually committed.
/// Overlay nodes read this instead of the controller's latest camera so they
/// stay aligned while a terrain LOD refill temporarily holds the previous frame.
struct MapRenderState {
    std::atomic<double> centerX{0.0};
    std::atomic<double> centerZ{0.0};
    std::atomic<double> scale{64.0};
    std::atomic<int> dimension{0};
    std::atomic<quint64> revision{0};

    void publish(double x, double z, double cameraScale, int dim) {
        if (centerX.load(std::memory_order_relaxed) == x && centerZ.load(std::memory_order_relaxed) == z &&
            scale.load(std::memory_order_relaxed) == cameraScale && dimension.load(std::memory_order_relaxed) == dim)
            return;
        centerX.store(x, std::memory_order_release);
        centerZ.store(z, std::memory_order_release);
        scale.store(cameraScale, std::memory_order_release);
        dimension.store(dim, std::memory_order_release);
        revision.fetch_add(1, std::memory_order_acq_rel);
    }

    void read(double& x, double& z, double& cameraScale, int& dim, quint64& stateRevision) const {
        stateRevision = revision.load(std::memory_order_acquire);
        x = centerX.load(std::memory_order_acquire);
        z = centerZ.load(std::memory_order_acquire);
        cameraScale = scale.load(std::memory_order_acquire);
        dim = dimension.load(std::memory_order_acquire);
    }
};

#endif
