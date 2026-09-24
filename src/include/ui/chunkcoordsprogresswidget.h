#ifndef BEDROCKMAP_CHUNKCOORDSPROGRESSWIDGET_H
#define BEDROCKMAP_CHUNKCOORDSPROGRESSWIDGET_H

#include <QWidget>

class QLabel;
class QProgressBar;

/// Full-page progress surface shown while the initial chunk-coordinate index
/// is built. Once the scan is complete the level page replaces it with the map.
class ChunkCoordsProgressWidget : public QWidget {
    Q_OBJECT

   public:
    explicit ChunkCoordsProgressWidget(QWidget* parent = nullptr);

   public slots:
    void setProgress(qulonglong scannedKeys, qulonglong chunks);

   private:
    QProgressBar* progress_bar_{nullptr};
    QLabel* status_label_{nullptr};
};

#endif  // BEDROCKMAP_CHUNKCOORDSPROGRESSWIDGET_H
