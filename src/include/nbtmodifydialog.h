#ifndef NBTMODIFYDIALOG_H
#define NBTMODIFYDIALOG_H

#include <qchar.h>

#include <QDialog>

#include "nbt.h"

namespace Ui {
    class NBTModifyDialog;
}

class NBTModifyDialog : public QDialog {
    Q_OBJECT

   public:
    explicit NBTModifyDialog(QWidget *parent = nullptr);
    bl::nbt::abstract_tag *createTagWithCurrent(QString &err) const;
    bool modifyCurrentTag(bl::nbt::abstract_tag *&tag, QString &err) const;
    ~NBTModifyDialog();
    bool setCreateMode(bl::nbt::abstract_tag *tag);
    bool setModifyMode(const bl::nbt::abstract_tag *tag);

   protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

   private:
    void resetUI() const;
    void updateValueState() const;
    Ui::NBTModifyDialog *ui;
    mutable bool lock_type_combobox_{false};
};

#endif  // NBTMODIFYDIALOG_H
