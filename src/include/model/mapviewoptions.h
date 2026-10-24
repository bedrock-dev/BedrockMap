#ifndef BEDROCKMAP_MAPVIEWOPTIONS_H
#define BEDROCKMAP_MAPVIEWOPTIONS_H

#include <algorithm>
#include <array>

/// View state (dimension/layer/overlays) for the 2D map. A plain value type
/// shared by controllers and render code without pulling in UI headers.
struct RenderOption {
    enum LayerType { Terrain = 0, Biome = 1 };
    enum OtherType { Grid = 0, Coords = 1, SlimeChunk = 2, Actors = 3, Village = 4, HSA = 5, Portal = 6, OtherLen = 7 };

    static constexpr int OverWorld = 0;
    static constexpr int Nether = 1;
    static constexpr int TheEnd = 2;

    RenderOption() { reset(); }

    int dim{OverWorld};
    LayerType layer{Terrain};
    std::array<bool, OtherType::OtherLen> others{};

    void reset() {
        dim = OverWorld;
        layer = Terrain;
        std::fill(others.begin(), others.end(), false);
        setOther(Grid, true);
    }

    void setDim(int nDim) { dim = nDim; }
    void setLayer(LayerType nLayer) { layer = nLayer; }

    bool toggleOther(OtherType type) {
        if (type < 0 || type >= OtherType::OtherLen) return true;
        others[type] = !others[type];
        return others[type];
    }

    void setOther(OtherType type, bool value) {
        if (type < 0 || type >= OtherType::OtherLen) return;
        others[type] = value;
    }

    bool getOther(OtherType type) const {
        if (type < 0 || type >= OtherType::OtherLen) return false;
        return others[type];
    }
};

#endif  // BEDROCKMAP_MAPVIEWOPTIONS_H
