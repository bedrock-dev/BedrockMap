#ifndef IMPORTOVERLAY_H
#define IMPORTOVERLAY_H

#include <QObject>
#include <QWidget>

#include "bedrock_key.h"
#include "chunkio.h"

class QPainter;
class AsyncLevelLoader;
class FloatingToolBar;
class LevelPageWidget;

class ImportOverlay : public QObject {
    Q_OBJECT

   public:
    ImportOverlay(QWidget *parent, AsyncLevelLoader *loader, LevelPageWidget *levelPage);

    bool active() const { return mode_; }
    bool placed() const { return placed_; }

    /// Load a .bchks file and start interactive placement.
    /// The preview's first chunk is anchored at initialCp.
    void startImport(const QString &filePath, uint8_t dim, const bl::chunk_pos &initialCp);

    /// Start interactive placement from raw serialized data (for clipboard paste).
    /// Returns false if deserialization fails.
    bool startPaste(const QByteArray &data, uint8_t dim, const bl::chunk_pos &initialCp);

    /// Follow mouse movement (called from mouseMoveEvent)
    void handleMouseMove(const bl::chunk_pos &mouseCp);

    /// Left-click: place the preview and show confirm bar
    void handleLeftClick();

    /// Right-click: revert placed preview back to movable. Returns true if consumed.
    bool handleRightClick();

    /// Esc key: cancel entirely. Returns true if consumed.
    bool handleKeyPress(int key);

    /// Draw the preview overlay
    void draw(QPainter *p, qreal scaleLevel);

    /// Reposition confirm bar on parent resize
    void resize(int parentW, int parentH);

   signals:
    void confirmed();

   private slots:
    void confirm();
    void cancel();

   private:
    void cleanup();

    QWidget *parent_;
    AsyncLevelLoader *loader_;
    LevelPageWidget *level_page_;

    bool mode_{false};
    bool placed_{false};
    uint8_t dim_{0};
    ExportedRegion preview_;
    bl::chunk_pos offset_{0, 0, 0};
    FloatingToolBar *confirm_bar_{nullptr};
};

#endif  // IMPORTOVERLAY_H
