#ifndef RENDERFILTERDIALOG_H
#define RENDERFILTERDIALOG_H

#include <QDialog>
#include <unordered_set>

#include "loguru/loguru.hpp"

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

    // block filter is the default: only excludes air + unknown in blacklist mode
    [[nodiscard]] bool isBlockFilterDefault() const {
        return block_black_mode_ && !enable_layer_ && blocks_list_.size() == 2 && blocks_list_.count("minecraft:air") &&
               blocks_list_.count("minecraft:unknown");
    }

    void renderImages(bl::chunk *ch, int rw, int rh, ChunkRegion *region) const;

    void print() {
        LOG_F(INFO, "BlockList (%d)", block_black_mode_);
        for (const auto &block : blocks_list_) {
            LOG_F(INFO, " - %s", block.c_str());
        }
        LOG_F(INFO, "ActorList (%d)", actor_black_mode_);
        for (const auto &actor : actors_list_) {
            LOG_F(INFO, " - %s", actor.c_str());
        }
    }
};

class RenderFilterDialog : public QDialog {
    Q_OBJECT

   public:
    explicit RenderFilterDialog(QWidget *parent = nullptr);

   public:
    ~RenderFilterDialog() override;

    void setFilter(const MapFilter &f) {
        this->filter_ = f;
        fillInUI();
    }

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
