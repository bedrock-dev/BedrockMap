#include "chunkcoordsprogresswidget.h"

#include <QLabel>
#include <QProgressBar>
#include <QVBoxLayout>

ChunkCoordsProgressWidget::ChunkCoordsProgressWidget(QWidget* parent) : QWidget(parent) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(48, 48, 48, 48);
    layout->setSpacing(12);
    layout->addStretch();

    progress_bar_ = new QProgressBar(this);
    progress_bar_->setRange(0, 0);  // LevelDB does not expose a cheap total-key count.
    progress_bar_->setTextVisible(false);
    progress_bar_->setMinimumHeight(12);
    layout->addWidget(progress_bar_);

    status_label_ = new QLabel(tr("chunkCoordsProgress.scanning"), this);
    status_label_->setAlignment(Qt::AlignCenter);
    layout->addWidget(status_label_);
    layout->addStretch();
}

void ChunkCoordsProgressWidget::setProgress(qulonglong scannedKeys, qulonglong chunks) {
    status_label_->setText(tr("chunkCoordsProgress.stats").arg(scannedKeys).arg(chunks));
}
