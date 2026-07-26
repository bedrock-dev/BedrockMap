#include "worldlisttab.h"

#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidgetItem>
#include <QMenu>
#include <QPixmap>
#include <QUrl>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrent>

#include "config.h"
#include "loguru/loguru.hpp"

// ===========================================================================
//  helpers
// ===========================================================================
namespace {

    constexpr int THUMB_WIDTH = 64;
    constexpr int THUMB_HEIGHT = 36;
    constexpr int ITEM_HEIGHT = 44;
    constexpr int MARGIN = 24;
    constexpr int SPACING = 6;

    QString bytesToString(int64_t bytes) {
        if (bytes < 1024) return QString("%1 B").arg(bytes);
        if (bytes < 1024 * 1024) return QString("%1 KB").arg(bytes / 1024);
        if (bytes < 1024LL * 1024 * 1024) return QString("%1 MB").arg(bytes / (1024.0 * 1024.0), 0, 'f', 1);
        return QString("%1 GB").arg(bytes / (1024.0 * 1024.0 * 1024.0), 0, 'f', 2);
    }

    QString timestampToString(int64_t secs) {
        QDateTime dt = QDateTime::fromSecsSinceEpoch(secs);
        return dt.toString("yyyy-MM-dd HH:mm");
    }

    QPixmap makePlaceholderThumb() {
        QPixmap pm(THUMB_WIDTH, THUMB_HEIGHT);
        pm.fill(QColor(45, 45, 45));
        return pm;
    }

    QPixmap loadWorldThumbnail(const QString &dirPath) {
        QString thumbPath = dirPath + "/world_icon.jpeg";
        if (QFileInfo::exists(thumbPath)) {
            QPixmap pm(thumbPath);
            if (!pm.isNull()) {
                return pm.scaled(THUMB_WIDTH, THUMB_HEIGHT, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
            }
        }
        return makePlaceholderThumb();
    }

}  // namespace

// ===========================================================================
//  SectionHeader
// ===========================================================================
SectionHeader::SectionHeader(const QString &title, int count, QWidget *parent) : QWidget(parent) {
    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    title_label_ = new QLabel(title, this);
    auto f = title_label_->font();
    f.setBold(true);
    f.setPointSize(f.pointSize() + 1);
    title_label_->setFont(f);
    layout->addWidget(title_label_);

    count_label_ = new QLabel(QString("(%1)").arg(count), this);
    count_label_->setStyleSheet("color: #888;");
    layout->addWidget(count_label_);

    layout->addStretch();
    setLayout(layout);
}

void SectionHeader::setCount(int count) { count_label_->setText(QString("(%1)").arg(count)); }

// ===========================================================================
//  WorldListItem
// ===========================================================================
WorldListItem::WorldListItem(const LevelPathInfo &info, QWidget *parent) : QWidget(parent), info_(info) { setupUI(); }

void WorldListItem::setupUI() {
    setFixedHeight(ITEM_HEIGHT);
    setCursor(Qt::PointingHandCursor);
    setAttribute(Qt::WA_StyledBackground, true);
    setStyleSheet("WorldListItem { background-color: transparent; }");

    auto *root = new QHBoxLayout(this);
    root->setContentsMargins(6, 3, 8, 3);
    root->setSpacing(8);

    // --- thumbnail ---
    thumb_label_ = new QLabel(this);
    thumb_label_->setFixedSize(THUMB_WIDTH, THUMB_HEIGHT);
    QPixmap thumb = loadThumbnail();
    thumb_label_->setPixmap(thumb);
    thumb_label_->setScaledContents(true);
    root->addWidget(thumb_label_);

    // --- middle: name + version / path ---
    auto *middleLayout = new QVBoxLayout();
    middleLayout->setSpacing(1);

    auto *nameRow = new QHBoxLayout();
    nameRow->setSpacing(6);

    name_label_ = new QLabel(QString::fromStdString(info_.levelName), this);
    auto nameFont = name_label_->font();
    nameFont.setBold(true);
    nameFont.setPointSize(nameFont.pointSize());
    name_label_->setFont(nameFont);
    nameRow->addWidget(name_label_);

    version_label_ = new QLabel(QString::fromStdString(info_.version), this);
    version_label_->setStyleSheet("color: #aaa; font-size: 10px;");
    nameRow->addWidget(version_label_);

    nameRow->addStretch();
    middleLayout->addLayout(nameRow);

    path_label_ = new QLabel(QString::fromStdString(info_.path), this);
    path_label_->setStyleSheet("color: #888; font-size: 10px;");
    path_label_->setWordWrap(false);
    middleLayout->addWidget(path_label_);

    root->addLayout(middleLayout, 1);  // stretch = 1

    // --- right: time + size ---
    auto *rightLayout = new QVBoxLayout();
    rightLayout->setSpacing(1);
    rightLayout->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

    time_label_ = new QLabel(formatTime(info_.lastModified), this);
    time_label_->setStyleSheet("font-size: 11px;");
    rightLayout->addWidget(time_label_);

    size_label_ = new QLabel(formatSize(info_.sizeBytes), this);
    size_label_->setStyleSheet("color: #888; font-size: 10px;");
    rightLayout->addWidget(size_label_);

    root->addLayout(rightLayout);

    setLayout(root);
}

void WorldListItem::setInfo(const LevelPathInfo &info) {
    info_ = info;

    // update thumbnail
    QPixmap thumb = loadThumbnail();
    thumb_label_->setPixmap(thumb);

    name_label_->setText(QString::fromStdString(info_.levelName));
    version_label_->setText(QString::fromStdString(info_.version));
    path_label_->setText(QString::fromStdString(info_.path));
    time_label_->setText(formatTime(info_.lastModified));
    size_label_->setText(formatSize(info_.sizeBytes));
}

QPixmap WorldListItem::loadThumbnail() const { return loadWorldThumbnail(QString::fromStdString(info_.path)); }

QString WorldListItem::formatSize(int64_t bytes) const { return bytesToString(bytes); }

QString WorldListItem::formatTime(int64_t timestamp) const { return timestampToString(timestamp); }

void WorldListItem::mousePressEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton) {
        emit clicked(info_.path);
    }
    QWidget::mousePressEvent(event);
}

void WorldListItem::contextMenuEvent(QContextMenuEvent *event) {
    QMenu menu(this);
    auto *action = menu.addAction(tr("msg.welcome.openFolder"));
    connect(action, &QAction::triggered, this, [this]() {
        QString winPath = QString::fromStdString(info_.path);
        // Use explorer /select, to open the folder and highlight the world folder
        QDesktopServices::openUrl(QUrl::fromLocalFile(winPath));
    });
    menu.exec(event->globalPos());
}

void WorldListItem::enterEvent(QEnterEvent *event) {
    QWidget::enterEvent(event);
    auto bg = qApp->palette().color(QPalette::Window);
    setStyleSheet(QString("WorldListItem { background-color: %1; }").arg(bg.darker(110).name()));
}

void WorldListItem::leaveEvent(QEvent *event) {
    QWidget::leaveEvent(event);
    setStyleSheet("WorldListItem { background-color: transparent; }");
}

// ===========================================================================
//  WorldListTab
// ===========================================================================
WorldListTab::WorldListTab(QWidget *parent) : QWidget(parent) { setupUI(); }

void WorldListTab::setupUI() {
    auto *root = new QVBoxLayout(this);
    root->setAlignment(Qt::AlignTop);
    root->setContentsMargins(MARGIN, MARGIN, MARGIN, MARGIN);
    root->setSpacing(SPACING);

    // --- recent worlds ---
    recent_list_ = createSection(root, tr("msg.welcome.recentLevels"), &recent_header_);

    // --- release worlds ---
    release_list_ = createSection(root, tr("msg.welcome.release"));

    // --- preview worlds ---
    preview_list_ = createSection(root, tr("msg.welcome.preview"));

    setLayout(root);
}

QListWidget *WorldListTab::createSection(QVBoxLayout *parent, const QString &title, SectionHeader **outHeader) {
    auto *header = new SectionHeader(title, 0, this);
    if (outHeader) *outHeader = header;
    parent->addWidget(header);
    parent->addSpacing(4);

    auto *listWidget = new QListWidget(this);
    listWidget->setSelectionMode(QAbstractItemView::NoSelection);
    listWidget->setFocusPolicy(Qt::NoFocus);
    listWidget->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    listWidget->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    listWidget->setSpacing(0);
    listWidget->setStyleSheet(
        "QListWidget { border: none; background: transparent; }"
        "QListWidget::item { border: none; }");

    // max visible height = ~5 items
    listWidget->setMaximumHeight(ITEM_HEIGHT * 5 + 10);
    listWidget->setMinimumHeight(ITEM_HEIGHT + 10);

    parent->addWidget(listWidget);
    parent->addSpacing(8);

    return listWidget;
}

// ---------------------------------------------------------------------------
//  data setters
// ---------------------------------------------------------------------------
void WorldListTab::setRecentPaths(const QStringList &paths) {
    recent_paths_ = paths;
    rebuildRecentList();
}

void WorldListTab::setDiscoveredLevels(const std::vector<LevelPathInfo> &levels) {
    discovered_levels_ = levels;
    rebuildScannedLists();
}

// ---------------------------------------------------------------------------
//  rebuild recent list — load info asynchronously
// ---------------------------------------------------------------------------
void WorldListTab::rebuildRecentList() {
    recent_list_->clear();
    next_recent_idx_ = 0;

    if (recent_paths_.isEmpty()) {
        if (recent_header_) recent_header_->hide();
        recent_list_->hide();
        return;
    }

    if (recent_header_) recent_header_->show();
    recent_list_->show();

    // Show placeholder items first, then load asynchronously
    int validCount = 0;
    for (int i = 0; i < recent_paths_.size(); ++i) {
        // skip paths that no longer exist on disk
        if (!QDir(recent_paths_[i]).exists()) continue;

        LevelPathInfo placeholder;
        placeholder.path = recent_paths_[i].toStdString();
        placeholder.levelName = QDir(QString::fromStdString(placeholder.path)).dirName().toStdString();
        placeholder.lastModified = 0;
        placeholder.sizeBytes = 0;

        auto *item = new QListWidgetItem();
        auto *w = new WorldListItem(placeholder, this);
        item->setSizeHint(QSize(0, ITEM_HEIGHT));
        recent_list_->addItem(item);
        recent_list_->setItemWidget(item, w);

        connect(w, &WorldListItem::clicked, this, [this](const std::string &p) { emit openLevelRequested(QString::fromStdString(p)); });
        ++validCount;
    }
    // rebuild the internal list to skip dead entries
    recent_paths_.erase(std::remove_if(recent_paths_.begin(), recent_paths_.end(), [](const QString &p) { return !QDir(p).exists(); }),
                        recent_paths_.end());

    if (validCount == 0) {
        if (recent_header_) recent_header_->hide();
        recent_list_->hide();
        return;
    }

    // start loading first item
    loadNextRecentInfo();
}

void WorldListTab::loadNextRecentInfo() {
    if (next_recent_idx_ >= recent_paths_.size()) return;

    int idx = next_recent_idx_++;
    auto watcher = new QFutureWatcher<LevelPathInfo>(this);
    connect(watcher, &QFutureWatcher<LevelPathInfo>::finished, this, [this, watcher, idx]() {
        LevelPathInfo info = watcher->result();
        auto *item = recent_list_->item(idx);
        if (!info.isValid) {
            // hide invalid entries
            if (item) {
                auto *w = recent_list_->itemWidget(item);
                recent_list_->removeItemWidget(item);
                delete w;
                delete item;
            }
        } else if (item) {
            auto *w = qobject_cast<WorldListItem *>(recent_list_->itemWidget(item));
            if (w) w->setInfo(info);
        }
        watcher->deleteLater();
        loadNextRecentInfo();
    });
    watcher->setFuture(QtConcurrent::run([idx, this]() { return LevelPathManager::makeLevelInfo(recent_paths_[idx]); }));
}

// ---------------------------------------------------------------------------
//  rebuild scanned lists (release & preview)
// ---------------------------------------------------------------------------
void WorldListTab::rebuildScannedLists() {
    release_list_->clear();
    preview_list_->clear();

    int releaseCount = 0, previewCount = 0;

    auto addLevel = [this](QListWidget *list, const LevelPathInfo &info) {
        auto *item = new QListWidgetItem();
        auto *w = new WorldListItem(info, this);
        item->setSizeHint(QSize(0, ITEM_HEIGHT));
        list->addItem(item);
        list->setItemWidget(item, w);

        connect(w, &WorldListItem::clicked, this, [this](const std::string &p) { emit openLevelRequested(QString::fromStdString(p)); });
    };

    for (const auto &level : discovered_levels_) {
        if (!level.isValid) continue;
        if (level.preview) {
            addLevel(preview_list_, level);
            ++previewCount;
        } else {
            addLevel(release_list_, level);
            ++releaseCount;
        }
    }

    // empty placeholders
    auto addEmpty = [this](QListWidget *list) {
        auto *item = new QListWidgetItem();
        auto *label = new QLabel(tr("msg.welcome.noRecent"), this);
        label->setStyleSheet("color: gray; font-size: 12px; padding: 8px;");
        item->setSizeHint(label->sizeHint());
        list->addItem(item);
        list->setItemWidget(item, label);
    };
    if (releaseCount == 0) addEmpty(release_list_);
    if (previewCount == 0) addEmpty(preview_list_);

    // Update section header counts (order: Recent, Release, Preview)
    int headerIdx = 0;
    auto *layout = qobject_cast<QVBoxLayout *>(this->layout());
    if (layout) {
        for (int i = 0; i < layout->count(); ++i) {
            auto *w = layout->itemAt(i)->widget();
            auto *header = qobject_cast<SectionHeader *>(w);
            if (header) {
                if (headerIdx == 0)
                    header->setCount(recent_list_->count());
                else if (headerIdx == 1)
                    header->setCount(releaseCount);
                else if (headerIdx == 2)
                    header->setCount(previewCount);
                ++headerIdx;
            }
        }
    }
}
