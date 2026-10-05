#ifndef BEDROCKMAP_DATAMANAGERPAGEWIDGET_H
#define BEDROCKMAP_DATAMANAGERPAGEWIDGET_H

#include <QFuture>
#include <QFutureWatcher>
#include <QString>
#include <atomic>
#include <cstdint>
#include <memory>

#include "bedrock_level.h"
#include "datakeyindex.h"
#include "datakeymodel.h"
#include "tabpagewidget.h"

class QLabel;
class QHexView;
class NbtWidget;
class QProgressBar;
class QStackedWidget;
class QTreeView;

/// Page for inspecting all raw LevelDB keys in a Bedrock world.
/// Only key metadata is indexed; values are read on demand for the hex view.
class DataManagerPageWidget : public TabPageWidget {
    Q_OBJECT

   public:
    explicit DataManagerPageWidget(QWidget* parent = nullptr);
    ~DataManagerPageWidget() override;

    bool loadArchive(const QString& path);
    [[nodiscard]] QString getPageName() const;

   private slots:
    void onScanFinished();
    void onTreeItemDoubleClicked(const QModelIndex& index);
    void onTreeContextMenu(const QPoint& position);

   private:
    void updateProgress(qulonglong scannedKeys, qulonglong indexedKeys);
    void showEntry(std::size_t index);
    void exportEntry(std::size_t index);

    QString path_;
    std::unique_ptr<bl::bedrock_level> level_;
    DataKeyIndex index_;
    std::atomic_bool stop_scan_{false};
    QFuture<bool> scan_future_;
    QFutureWatcher<bool>* scan_watcher_{nullptr};

    QStackedWidget* stack_{nullptr};
    QWidget* progress_page_{nullptr};
    QProgressBar* progress_bar_{nullptr};
    QLabel* progress_label_{nullptr};
    QWidget* content_page_{nullptr};
    QTreeView* tree_{nullptr};
    DataKeyModel* model_{nullptr};
    QLabel* key_label_{nullptr};
    QStackedWidget* value_stack_{nullptr};
    QHexView* hex_view_{nullptr};
    NbtWidget* nbt_view_{nullptr};
};

#endif  // BEDROCKMAP_DATAMANAGERPAGEWIDGET_H
