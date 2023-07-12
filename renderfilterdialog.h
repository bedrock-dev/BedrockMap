#ifndef RENDERFILTERDIALOG_H
#define RENDERFILTERDIALOG_H

#include <QDialog>
#include <unordered_set>
#include "data_3d.h"
#include <QtDebug>

namespace Ui {
    class RenderFilterDialog;
}

struct MapFilter {
    std::unordered_set<int> biomes_list_;
    std::unordered_set<std::string> blocks_list_;
    std::unordered_set<std::string> actors_list_{"item"};
    int layer{-1};
    bool enable_layer_{false};
    bool biome_black_mode_{true};
    bool block_black_mode_{true};
    bool actor_black_mode_{true};
};


class RenderFilterDialog : public QDialog {
Q_OBJECT

public:
    explicit RenderFilterDialog(QWidget *parent = nullptr);


public:
    ~RenderFilterDialog();

    void setFilter(const MapFilter &f) {
        this->filter_ = f;
    }

    MapFilter getFilter() const {
        return this->filter_;
    }

    void collectFilerData();


    void fillInUI();

private slots:

 void on_current_layer_lineedit_textEdited(const QString &arg1);

 void on_layer_slider_valueChanged(int value);

private:
 Ui::RenderFilterDialog *ui;
 MapFilter filter_;
};


#endif // RENDERFILTERDIALOG_H
