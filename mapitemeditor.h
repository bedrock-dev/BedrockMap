#ifndef MAPITEMEDITOR_H
#define MAPITEMEDITOR_H

#include <QWidget>

namespace Ui {
class MapItemEditor;
}

class MapItemEditor : public QWidget
{
    Q_OBJECT

        public:
                  explicit MapItemEditor(QWidget *parent = nullptr);
    ~MapItemEditor();

   private:
    Ui::MapItemEditor *ui;
};

#endif // MAPITEMEDITOR_H
