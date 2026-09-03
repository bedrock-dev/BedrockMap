#ifndef BEDROCKMAP_GOTOPOSITIONDIALOG_H
#define BEDROCKMAP_GOTOPOSITIONDIALOG_H

#include <QDialog>
#include <QLineEdit>
namespace Ui {
    class GoToPositionDialog;
}

class GoToPositionDialog : public QDialog {
    Q_OBJECT

   public:
    explicit GoToPositionDialog(QWidget *parent = nullptr);
    ~GoToPositionDialog();

    bool positionValid();

    int x();
    int z();

   private:
    Ui::GoToPositionDialog *ui;
};

#endif  // BEDROCKMAP_GOTOPOSITIONDIALOG_H
