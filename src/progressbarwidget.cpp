#include "progressbarwidget.h"

#include <qboxlayout.h>
#include <qchar.h>
#include <qlabel.h>
#include <qprogressbar.h>
#include <qwidget.h>

ProgressBarWidget::ProgressBarWidget(QWidget* parent) : QWidget(parent) {
    progress_ = new QLabel(this);
    title_ = new QLabel(this);
    bar_ = new QProgressBar(this);

    auto* hb = new QHBoxLayout();
    hb->addWidget(bar_);
    hb->addWidget(progress_);

    auto* vb = new QVBoxLayout();
    vb->addWidget(title_);
    vb->addLayout(hb);
    setLayout(vb);
}

ProgressBarWidget::ProgressBarWidget() : ProgressBarWidget(nullptr) {}

void ProgressBarWidget::setup(const QString& title, int max) {
    title_->setText(title);
    progress_->setText(QString("0/%1").arg(max));
    bar_->setMinimum(0);
    bar_->setMaximum(max);
    bar_->setValue(0);
}

void ProgressBarWidget::setValue(int value) { bar_->setValue(value); }