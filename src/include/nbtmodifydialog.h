#ifndef NBTMODIFYDIALOG_H
#define NBTMODIFYDIALOG_H

#include <qchar.h>

#include <QDialog>

#include "palette.h"

namespace Ui {
    class NBTModifyDialog;
}

class NBTModifyDialog : public QDialog {
    Q_OBJECT

   public:
    explicit NBTModifyDialog(QWidget *parent = nullptr);
    bl::palette::abstract_tag *createTagWithCurrent(QString &err);
    bool modityCurrentTag(bl::palette::abstract_tag *&tag, QString &err);
    ~NBTModifyDialog();
    bool setCreateMode(bl::palette::abstract_tag *tag);
    bool setModifyMode(bl::palette::abstract_tag *tag);

   private:
    void resetUI();
    Ui::NBTModifyDialog *ui;
};

#endif  // NBTMODIFYDIALOG_H
