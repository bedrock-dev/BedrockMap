#ifndef BEDROCKMAP_SELECTIONREGION_H
#define BEDROCKMAP_SELECTIONREGION_H

#include <QRect>
#include <QRegion>

/// Manages a chunk-level selection region.
/// Coordinates are in chunk space (each unit = one chunk).
class SelectionRegion {
   public:
    enum class Mode { Replace, Add, Subtract };

    SelectionRegion() = default;

    void setMode(Mode mode) { mode_ = mode; }
    Mode mode() const { return mode_; }

    bool isEmpty() const { return region_.isEmpty(); }
    void clear() { region_ = QRegion(); }

    const QRegion &region() const { return region_; }

    /// Apply a rectangle (in chunk coordinates) using the current mode.
    void applyRect(const QRect &chunkRect) {
        QRegion r(chunkRect);
        switch (mode_) {
            case Mode::Replace:
                region_ = r;
                break;
            case Mode::Add:
                region_ = region_.united(r);
                break;
            case Mode::Subtract:
                region_ = region_.subtracted(r);
                break;
        }
    }

    int chunkCount() const {
        int count = 0;
        for (const auto &r : region_) count += r.width() * r.height();
        return count;
    }

   private:
    QRegion region_;
    Mode mode_{Mode::Replace};
};

#endif  // BEDROCKMAP_SELECTIONREGION_H
