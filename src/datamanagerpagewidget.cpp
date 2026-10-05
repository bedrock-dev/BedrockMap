#include "datamanagerpagewidget.h"

#include <QHexView/model/buffer/qmemorybuffer.h>
#include <QHexView/model/qhexdocument.h>
#include <QHexView/qhexview.h>

#include <QAbstractItemView>
#include <QApplication>
#include <QClipboard>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFont>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QPoint>
#include <QProgressBar>
#include <QSplitter>
#include <QStackedWidget>
#include <QTreeView>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrent>
#include <string>

#include "nbtwidget.h"

namespace {

    [[nodiscard]] QString binaryKeyString(const std::string& key) {
        static constexpr char HEX[] = "0123456789ABCDEF";
        QString result;
        result.reserve(static_cast<int>(key.size() * 3));
        for (std::size_t i = 0; i < key.size(); ++i) {
            if (i != 0) result += QLatin1Char(' ');
            const auto byte = static_cast<unsigned char>(key[i]);
            result += QChar(HEX[byte >> 4]);
            result += QChar(HEX[byte & 0x0f]);
        }
        return result;
    }

}  // namespace

DataManagerPageWidget::DataManagerPageWidget(QWidget* parent) : TabPageWidget(parent) {
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);

    stack_ = new QStackedWidget(this);
    root->addWidget(stack_);

    progress_page_ = new QWidget(stack_);
    auto* progress_layout = new QVBoxLayout(progress_page_);
    progress_layout->setContentsMargins(48, 48, 48, 48);
    progress_layout->addStretch();
    progress_bar_ = new QProgressBar(progress_page_);
    progress_bar_->setRange(0, 0);
    progress_bar_->setTextVisible(false);
    progress_bar_->setMinimumHeight(12);
    progress_layout->addWidget(progress_bar_);
    progress_label_ = new QLabel(tr("dataManager.progress.scanning"), progress_page_);
    progress_label_->setAlignment(Qt::AlignCenter);
    progress_layout->addWidget(progress_label_);
    progress_layout->addStretch();
    stack_->addWidget(progress_page_);

    content_page_ = new QWidget(stack_);
    auto* content_layout = new QHBoxLayout(content_page_);
    content_layout->setContentsMargins(0, 0, 0, 0);

    auto* splitter = new QSplitter(Qt::Horizontal, content_page_);
    auto* left = new QWidget(splitter);
    left->setMinimumWidth(320);
    auto* left_layout = new QVBoxLayout(left);
    left_layout->setContentsMargins(0, 0, 6, 0);

    auto* navigation = new QHBoxLayout();
    auto* path_label = new QLabel(QStringLiteral("keys"), left);
    path_label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    navigation->addWidget(path_label, 1);
    left_layout->addLayout(navigation);

    tree_ = new QTreeView(left);
    tree_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    tree_->setSelectionBehavior(QAbstractItemView::SelectRows);
    tree_->setSelectionMode(QAbstractItemView::SingleSelection);
    tree_->setUniformRowHeights(true);
    tree_->setAlternatingRowColors(true);
    tree_->setContextMenuPolicy(Qt::CustomContextMenu);
    model_ = new DataKeyModel(&index_, this);
    tree_->setModel(model_);
    tree_->header()->setStretchLastSection(false);
    tree_->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    tree_->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    connect(tree_, &QTreeView::doubleClicked, this, &DataManagerPageWidget::onTreeItemDoubleClicked);
    connect(tree_, &QTreeView::customContextMenuRequested, this, &DataManagerPageWidget::onTreeContextMenu);
    left_layout->addWidget(tree_);

    auto* right = new QWidget(splitter);
    auto* right_layout = new QVBoxLayout(right);
    right_layout->setContentsMargins(6, 0, 0, 0);
    key_label_ = new QLabel(tr("dataManager.noSelection"), right);
    key_label_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    right_layout->addWidget(key_label_);

    value_stack_ = new QStackedWidget(right);

    hex_view_ = new QHexView(value_stack_);
    QFont font;
    font.setFamilies({"JetBrains Mono", "Microsoft YaHei", "Microsoft YaHei UI"});
    hex_view_->setFont(font);
    hex_view_->setReadOnly(true);
    hex_view_->setDocument(QHexDocument::fromMemory<QMemoryBuffer>(QByteArray(), hex_view_));

    nbt_view_ = new NbtWidget(value_stack_);
    nbt_view_->setMode(NbtMode::Memory);
    nbt_view_->setReadOnly(true);

    value_stack_->addWidget(hex_view_);
    value_stack_->addWidget(nbt_view_);
    value_stack_->setCurrentWidget(hex_view_);
    right_layout->addWidget(value_stack_);

    splitter->addWidget(left);
    splitter->addWidget(right);
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    content_layout->addWidget(splitter);
    stack_->addWidget(content_page_);
    stack_->setCurrentWidget(progress_page_);

    scan_watcher_ = new QFutureWatcher<bool>(this);
    connect(scan_watcher_, &QFutureWatcher<bool>::finished, this, &DataManagerPageWidget::onScanFinished);
}

DataManagerPageWidget::~DataManagerPageWidget() {
    stop_scan_.store(true, std::memory_order_release);
    if (scan_future_.isRunning()) scan_future_.waitForFinished();
    if (level_) level_->close();
}

bool DataManagerPageWidget::loadArchive(const QString& path) {
    path_ = path;
    level_ = std::make_unique<bl::bedrock_level>();
    if (!level_->open(path.toStdString())) return false;

    stop_scan_.store(false, std::memory_order_release);
    scan_future_ = QtConcurrent::run([this]() {
        return index_.load(*level_, stop_scan_, [this](std::uint64_t scannedKeys, std::uint64_t indexedKeys) {
            QMetaObject::invokeMethod(
                this,
                [this, scannedKeys, indexedKeys]() {
                    updateProgress(static_cast<qulonglong>(scannedKeys), static_cast<qulonglong>(indexedKeys));
                },
                Qt::QueuedConnection);
        });
    });
    scan_watcher_->setFuture(scan_future_);
    return true;
}

QString DataManagerPageWidget::getPageName() const {
    const QString name = QFileInfo(path_).fileName();
    return name.isEmpty() ? tr("dataManager.title") : tr("dataManager.titleWithName").arg(name);
}

void DataManagerPageWidget::updateProgress(qulonglong scannedKeys, qulonglong indexedKeys) {
    if (!progress_label_) return;
    progress_label_->setText(tr("dataManager.progress.stats").arg(scannedKeys).arg(indexedKeys));
}

void DataManagerPageWidget::onScanFinished() {
    if (!scan_future_.result()) {
        progress_label_->setText(tr("dataManager.progress.failed"));
        return;
    }
    model_->resetIndex(&index_);
    stack_->setCurrentWidget(content_page_);
}

void DataManagerPageWidget::onTreeItemDoubleClicked(const QModelIndex& index) {
    if (const auto entry_index = model_->activate(index)) showEntry(*entry_index);
}

void DataManagerPageWidget::onTreeContextMenu(const QPoint& position) {
    const QModelIndex index = tree_->indexAt(position);
    const auto entry_index = model_->entryIndex(index);
    if (!entry_index) return;

    tree_->setCurrentIndex(index);
    QMenu menu(tree_);
    QAction* copy_readable_action = menu.addAction(tr("dataManager.copyReadableKey"));
    QAction* copy_binary_action = menu.addAction(tr("dataManager.copyBinaryKey"));
    menu.addSeparator();
    QAction* export_action = menu.addAction(tr("dataManager.exportNbt"));
    const QAction* selected_action = menu.exec(tree_->viewport()->mapToGlobal(position));
    if (selected_action == copy_readable_action) {
        const auto& entry = index_.entries()[*entry_index];
        QApplication::clipboard()->setText(QString::fromStdString(index_.labelForEntry(*entry_index)));
    } else if (selected_action == copy_binary_action) {
        const auto& entry = index_.entries()[*entry_index];
        QApplication::clipboard()->setText(binaryKeyString(std::string(index_.keyForEntry(*entry_index))));
    } else if (selected_action == export_action) {
        exportEntry(*entry_index);
    }
}

void DataManagerPageWidget::exportEntry(std::size_t index) {
    if (!level_ || index >= index_.entries().size()) return;
    const auto& entry = index_.entries()[index];
    const std::string key(index_.keyForEntry(index));
    std::string value;
    if (!level_->load_raw(key, value)) {
        QMessageBox::warning(this, tr("dataManager.exportNbt"),
                             tr("dataManager.readFailed").arg(QString::fromStdString(index_.labelForEntry(index))));
        return;
    }

    QString base_name = QString::fromStdString(index_.labelForEntry(index));
    for (QChar& character : base_name) {
        if (QStringLiteral("/:*?\\\"<>|").contains(character)) character = QLatin1Char('_');
    }
    if (base_name.isEmpty()) base_name = QStringLiteral("key");
    const QString suggested_name = base_name + QStringLiteral(".nbt");
    const QString file_name = QFileDialog::getSaveFileName(this, tr("dataManager.exportNbt"), suggested_name, tr("dataManager.nbtFilter"));
    if (file_name.isEmpty()) return;

    QFile file(file_name);
    if (!file.open(QIODevice::WriteOnly) ||
        file.write(value.data(), static_cast<qint64>(value.size())) != static_cast<qint64>(value.size())) {
        QMessageBox::warning(this, tr("dataManager.exportNbt"), tr("dataManager.writeFailed").arg(file.errorString()));
    }
}

void DataManagerPageWidget::showEntry(std::size_t index) {
    if (!level_ || index >= index_.entries().size()) return;
    const auto& entry = index_.entries()[index];
    const std::string key(index_.keyForEntry(index));
    std::string value;
    if (!level_->load_raw(key, value)) {
        const QString label = QString::fromStdString(index_.labelForEntry(index));
        key_label_->setText(tr("dataManager.readFailed").arg(label));
        nbt_view_->clearData();
        value_stack_->setCurrentWidget(hex_view_);
        hex_view_->setDocument(QHexDocument::fromMemory<QMemoryBuffer>(QByteArray(), hex_view_));
        return;
    }

    const QString label = QString::fromStdString(index_.labelForEntry(index));
    key_label_->setText(tr("dataManager.keyInfo").arg(label).arg(static_cast<qulonglong>(value.size())));

    // JigsawStructureBlueprint is intentionally kept opaque until its value
    // format is implemented; do not guess that it is an NBT payload.
    if (entry.raw_value) {
        nbt_view_->clearData();
        value_stack_->setCurrentWidget(hex_view_);
        const QByteArray bytes(value.data(), static_cast<int>(value.size()));
        hex_view_->setDocument(QHexDocument::fromMemory<QMemoryBuffer>(bytes, hex_view_));
        return;
    }

    constexpr std::size_t MAX_NBT_DISPLAY_SIZE = 16u * 1024u * 1024u;
    if (entry.nbt_value && value.size() <= MAX_NBT_DISPLAY_SIZE) {
        auto palette = bl::nbt::read_palette_to_end(value.data(), value.size());
        if (!palette.empty()) {
            std::vector<NBTListItem*> items;
            items.reserve(palette.size());
            for (std::size_t i = 0; i < palette.size(); ++i) {
                items.push_back(NBTListItem::from(palette[i], QString::number(static_cast<qulonglong>(i)), label));
            }
            nbt_view_->loadNewData(items);
            nbt_view_->openItem(0);
            value_stack_->setCurrentWidget(nbt_view_);
            return;
        }
    }

    nbt_view_->clearData();
    value_stack_->setCurrentWidget(hex_view_);
    const QByteArray bytes(value.data(), static_cast<int>(value.size()));
    hex_view_->setDocument(QHexDocument::fromMemory<QMemoryBuffer>(bytes, hex_view_));
}
