#ifndef NBTPALETTEWIDGET_H
#define NBTPALETTEWIDGET_H

#include <QWidget>

#include "nbtwidget.h"

namespace Ui {
class NbtPaletteWidget;
}

class NbtPaletteWidget : public QWidget
{
    Q_OBJECT

        public:
                  explicit NbtPaletteWidget(QWidget *parent = nullptr);
    ~NbtPaletteWidget();

   private:
    Ui::NbtPaletteWidget *ui;

    NbtWidget *nbt_widget_;
};

#endif // NBTPALETTEWIDGET_H
