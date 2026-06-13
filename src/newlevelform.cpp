#include "newlevelform.h"

#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QVBoxLayout>

#include "msg.h"

NewLevelForm::NewLevelForm(QWidget *parent) : QDialog(parent) {
    setWindowTitle(tr("newLevelForm.title.newLevel"));
    setMinimumWidth(450);

    auto *mainLayout = new QVBoxLayout(this);

    // Row 1: path + browse
    auto *pathLayout = new QHBoxLayout();
    path_edit_ = new QLineEdit(this);
    path_edit_->setPlaceholderText(tr("newLevelForm.placeholder.selectDir"));
    browse_btn_ = new QPushButton(tr("..."), this);
    browse_btn_->setFixedWidth(36);
    pathLayout->addWidget(path_edit_, 1);
    pathLayout->addWidget(browse_btn_);

    // Row 2: version
    version_edit_ = new QLineEdit(this);
    version_edit_->setPlaceholderText(tr("newLevelForm.placeholder.version"));

    // Row 3: superflat checkbox
    flat_check_ = new QCheckBox(tr("newLevelForm.label.superflat"), this);

    // Row 4: superflat block list (disabled by default)
    flat_blocks_edit_ = new QLineEdit(this);
    flat_blocks_edit_->setPlaceholderText(tr("newLevelForm.placeholder.blocks"));
    flat_blocks_edit_->setEnabled(false);

    // Enable/disable block list based on checkbox
    connect(flat_check_, &QCheckBox::toggled, flat_blocks_edit_, &QLineEdit::setEnabled);

    // Row 5: button box
    auto *buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);

    // Layout assembly
    auto *formLayout = new QFormLayout();
    formLayout->addRow(tr("newLevelForm.label.path"), pathLayout);
    formLayout->addRow(tr("newLevelForm.label.version"), version_edit_);
    formLayout->addRow(tr(""), flat_check_);
    formLayout->addRow(tr("newLevelForm.label.blockList"), flat_blocks_edit_);

    mainLayout->addLayout(formLayout);
    mainLayout->addStretch();
    mainLayout->addWidget(buttonBox);

    // Browse button action
    connect(browse_btn_, &QPushButton::clicked, this, [this]() {
        auto dir = QFileDialog::getExistingDirectory(this, msg::SELECT_LEVEL_DIR());
        if (!dir.isEmpty()) {
            path_edit_->setText(dir);
        }
    });
}
