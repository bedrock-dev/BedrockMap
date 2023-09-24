#ifndef RENDERFILTERDIALOG_H
#define RENDERFILTERDIALOG_H

#include <QDialog>
#include <QtDebug>
#include <unordered_set>

#include "bedrock_key.h"
#include "data_3d.h"

namespace Ui {
    class RenderFilterDialog;
}
namespace bl {
    class chunk;
}

struct ChunkRegion;

struct MapFilter {
    std::unordered_set<int> biomes_list_{};
    std::unordered_set<std::string> blocks_list_{"minecraft:air", "minecraft:unknown"};
    std::unordered_set<std::string> actors_list_{"item"};
    int layer{64};
    bool enable_layer_{false};
    bool biome_black_mode_{true};
    bool block_black_mode_{true};
    bool actor_black_mode_{true};

    void renderImages(bl::chunk *ch, int rw, int rh, ChunkRegion *region) const;

    //    void bakeChunkTerrain(bl::chunk *ch, int rw, int rh, ChunkRegion *region) const;

    //    void bakeChunkBiome(bl::chunk *ch, int rw, int rh, ChunkRegion *region) const;

    void bakeChunkActors(bl::chunk *ch, ChunkRegion *region) const;

    // void bakeChunkHeight(bl::chunk *ch, int rw, int rh, ChunkRegion *region) const;
};

class RenderFilterDialog : public QDialog {
Q_OBJECT

public:
    explicit RenderFilterDialog(QWidget *parent = nullptr);

public:
    ~RenderFilterDialog() override;

    void setFilter(const MapFilter &f) { this->filter_ = f; }

    MapFilter getFilter() const { return this->filter_; }

    void collectFilerData();

    void fillInUI();

private slots:

    void on_current_layer_lineedit_textEdited(const QString &arg1);

    void on_layer_slider_valueChanged(int value);

private:
    Ui::RenderFilterDialog *ui;
    MapFilter filter_;
};

#endif  // RENDERFILTERDIALOG_H
