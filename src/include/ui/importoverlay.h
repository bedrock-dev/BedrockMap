#ifndef BEDROCKMAP_IMPORTOVERLAY_H
#define BEDROCKMAP_IMPORTOVERLAY_H

#include <QObject>
#include <QPointer>
#include <QWidget>

#include "bedrock_key.h"
#include "chunkio.h"

class AsyncLevelLoader;
class FloatingToolBar;
class MapHost;
class QPainter;

/// Interactive placement of an imported/pasted region: a ghost preview that
/// follows the cursor, a click to pin it down, then confirm or cancel.
///
/// It owns no widget. The state and the drawing live here, and whichever
/// renderer is on screen draws it and forwards its input, so the two renderers
/// share one placement - the same way they share the overlay layers. Its chrome
/// belongs to the pane the host points at, which is the renderer on screen.
class ImportOverlay : public QObject {
    Q_OBJECT

   public:
    ImportOverlay(AsyncLevelLoader* loader, MapHost* host);

    /// The widget the confirm bar and the warning dialogs belong to: the pane
    /// on screen.
    [[nodiscard]] QWidget* paneWidget() const;

    bool active() const { return mode_; }
    bool placed() const { return placed_; }

    /// Load a .bchks file and start interactive placement.
    /// The preview's first chunk is anchored at initialCp.
    void startImport(const QString& filePath, uint8_t dim, const bl::chunk_pos& initialCp);

    /// Start interactive placement from raw serialized data (for clipboard paste).
    /// Returns false if deserialization fails.
    bool startPaste(const QByteArray& data, uint8_t dim, const bl::chunk_pos& initialCp);

    /// Follow mouse movement (called from mouseMoveEvent)
    void handleMouseMove(const bl::chunk_pos& mouseCp);

    /// Left-click: place the preview and show confirm bar
    void handleLeftClick();

    /// Right-click: revert placed preview back to movable. Returns true if consumed.
    bool handleRightClick();

    /// Esc key: cancel entirely. Returns true if consumed.
    bool handleKeyPress(int key);

    /// Draw the preview overlay
    void draw(QPainter* p, qreal scaleLevel);

    /// Reposition confirm bar on parent resize
    void resize(int parentW, int parentH);

   signals:
    void confirmed();
    void toolbarsVisibleRequested(bool visible);

   private slots:
    void confirm();
    void cancel();

   private:
    void cleanup();

    AsyncLevelLoader* loader_;
    MapHost* host_;

    bool mode_{false};
    bool placed_{false};
    uint8_t dim_{0};
    ExportedRegion preview_;
    bl::chunk_pos offset_{0, 0, 0};
    /// Owned by the pane, which may be destroyed before this overlay is.
    QPointer<FloatingToolBar> confirm_bar_;
};

#endif  // BEDROCKMAP_IMPORTOVERLAY_H
