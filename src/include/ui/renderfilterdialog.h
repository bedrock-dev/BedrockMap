#ifndef BEDROCKMAP_RENDERFILTERDIALOG_H
#define BEDROCKMAP_RENDERFILTERDIALOG_H

#include <QDialog>

#include "render_options.h"

namespace Ui {
    class RenderFilterDialog;
}

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

#endif  // BEDROCKMAP_RENDERFILTERDIALOG_H
