#include "pleasewaitdialog.h"

#include <QCoreApplication>
#include <QLabel>
#include <QVBoxLayout>

PleaseWaitDialog& PleaseWaitDialog::instance() {
    static PleaseWaitDialog dialog;
    return dialog;
}

PleaseWaitDialog::PleaseWaitDialog() {
    const QString text = QCoreApplication::translate("PleaseWaitDialog", "Please wait...");
    setWindowTitle(text);
    setFixedSize(200, 80);
    label_ = new QLabel(text, this);
    label_->setAlignment(Qt::AlignCenter);
    auto* layout = new QVBoxLayout(this);
    layout->addWidget(label_);
    setLayout(layout);
    setModal(false);
    setWindowFlags(Qt::Tool | Qt::CustomizeWindowHint | Qt::WindowTitleHint);
}

void PleaseWaitDialog::showBusy() { show(); }

void PleaseWaitDialog::hideBusy() { hide(); }
