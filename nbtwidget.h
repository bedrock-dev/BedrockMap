#ifndef NBTWIDGET_H
#define NBTWIDGET_H

#include <QWidget>

namespace Ui {
class NbtWidget;
}

class NbtWidget : public QWidget
{
    Q_OBJECT

        public:
         explicit NbtWidget(QWidget *parent = nullptr);
         ~NbtWidget();

        private:
         Ui::NbtWidget *ui;
};

#endif // NBTWIDGET_H
