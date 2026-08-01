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
    bl::palette::abstract_tag *createTagWithCurrent(QString &err) const;
    bool modifyCurrentTag(bl::palette::abstract_tag *&tag, QString &err) const;
    ~NBTModifyDialog();
    bool setCreateMode(bl::palette::abstract_tag *tag) const;
    bool setModifyMode(const bl::palette::abstract_tag *tag) const;

   protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

   private:
    void resetUI() const;
    Ui::NBTModifyDialog *ui;
    mutable bool lock_type_combobox_{false};
};

#endif  // NBTMODIFYDIALOG_H
