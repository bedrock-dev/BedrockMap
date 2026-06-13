#ifndef NEWLEVELFORM_H
#define NEWLEVELFORM_H

#include <QCheckBox>
#include <QDialog>
#include <QLineEdit>
#include <QPushButton>

class NewLevelForm : public QDialog {
    Q_OBJECT

   public:
    explicit NewLevelForm(QWidget *parent = nullptr);

    QString path() const { return path_edit_->text(); }
    QString version() const { return version_edit_->text(); }
    bool isFlat() const { return flat_check_->isChecked(); }
    QString flatBlocks() const { return flat_blocks_edit_->text(); }

   private:
    QLineEdit *path_edit_;
    QPushButton *browse_btn_;
    QLineEdit *version_edit_;
    QCheckBox *flat_check_;
    QLineEdit *flat_blocks_edit_;
};

#endif  // NEWLEVELFORM_H
