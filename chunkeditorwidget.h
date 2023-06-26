#ifndef CHUNKEDITORWIDGET_H
#define CHUNKEDITORWIDGET_H

#include <QWidget>

namespace Ui {
class ChunkEditorWidget;
}

class ChunkEditorWidget : public QWidget
{
    Q_OBJECT

   public:
    explicit ChunkEditorWidget(QWidget *parent = nullptr);

    ~ChunkEditorWidget();

   private slots:
    void on_close_btn_clicked();

   private:
    Ui::ChunkEditorWidget *ui;
};

#endif // CHUNKEDITORWIDGET_H
