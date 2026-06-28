#include "leveltabwidget.h"

#include <qdialog.h>
#include <qlabel.h>
#include <qlogging.h>
#include <qmessagebox.h>
#include <qnamespace.h>
#include <qtmetamacros.h>
#include <qwidget.h>

#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrent>

#include "asynclevelloader.h"
#include "levelpagewidget.h"
#include "mapwidget.h"
#include "msg.h"

LevelTabWidget::LevelTabWidget(QWidget *parent) : QTabWidget(parent) {
    setTabsClosable(true);
    this->welcome_tab_ = new WelcomeTab(this);
    this->addTab(this->welcome_tab_, tr("levelTabWidget.title.welcome"));

    close_level_mss_box_ = new QDialog(this);
    close_level_mss_box_->setWindowTitle(tr("levelTabWidget.title.pleaseWait"));
    close_level_mss_box_->setFixedSize(200, 80);
    auto *layout = new QVBoxLayout(close_level_mss_box_);
    auto *label = new QLabel(tr("levelTabWidget.title.pleaseWait"), close_level_mss_box_);
    label->setAlignment(Qt::AlignCenter);
    layout->addWidget(label);
    close_level_mss_box_->setLayout(layout);
    close_level_mss_box_->setModal(false);
    close_level_mss_box_->setWindowFlags(Qt::Tool | Qt::CustomizeWindowHint | Qt::WindowTitleHint);

    this->levedb_debug_widget_ = new LevelDBDebugDialog(this);
    this->render_filter_dialog_ = new RenderFilterDialog(this);

    // connect
    connect(this, &QTabWidget::tabCloseRequested, this, &LevelTabWidget::onTabClosed);
    connect(&this->close_level_watcher_, &QFutureWatcher<void>::finished, this, &LevelTabWidget::onCloseLevelFinished);
    connect(this, &QTabWidget::currentChanged, this, [&]() {
        auto *page = qobject_cast<LevelPageWidget *>(currentWidget());
        emit currentLevelChanged(page);
    });

    // welcome tab signals — forward to mainwindow-level slots
    // (mainwindow will connect these after construction)
}

void LevelTabWidget::openNewLevel(const QString &path) {
    auto *page = new LevelPageWidget(this, index++);
    if (!page->loadLevel(path)) {
        WARN(msg::OPEN_LEVEL_FAILED());
        delete page;
        return;
    }
    int idx = this->addTab(page, page->getLevelName());
    this->setCurrentIndex(idx);
    this->level_pages_[page->getTabId()] = page;
    emit currentLevelChanged(page);
}

void LevelTabWidget::openLevelDBDebugDialog() {
    auto *page = currentLevelPage();
    if (!page) {
        WARN(msg::LEVEL_NOT_OPEN());
        return;
    }
    this->levedb_debug_widget_->initData(page->levelLoader()->level().db());
    this->levedb_debug_widget_->exec();
}

void LevelTabWidget::setEnableDebugWindow(bool bo) {
    for (const auto &kv : level_pages_) {
        if (kv && kv->getMapWidget()) kv->getMapWidget()->setDrawDebug(bo);
    }
}

void LevelTabWidget::onTabClosed() {
    auto *tab = currentWidget();
    if (!tab) return;
    if (tab == welcome_tab_) {
        removeTab(currentIndex());
        return;
    }

    auto *levelPage = currentLevelPage();
    if (!levelPage) return;

    if (levelPage->isDirty()) {
        auto btn = QMessageBox::question(this, msg::UNSAVED_CHANGES(), msg::UNSAVED_CHANGES_PROMPT(),
                                         QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel);
        if (btn == QMessageBox::Cancel) return;
        if (btn == QMessageBox::Yes) levelPage->commit();
    }

    auto future = QtConcurrent::run([levelPage] { levelPage->closeLevel(); });
    this->close_level_watcher_.setFuture(future);
    close_level_mss_box_->show();
}

void LevelTabWidget::onCloseLevelFinished() {
    auto page = currentWidget();
    removeTab(currentIndex());
    page->deleteLater();
    close_level_mss_box_->hide();
}

void LevelTabWidget::closeCurrentLevel() { onTabClosed(); }

bool LevelTabWidget::confirmCloseAllLevels() {
    for (auto *page : level_pages_) {
        if (!page) continue;
        if (page->isDirty()) {
            int idx = indexOf(page);
            if (idx >= 0) setCurrentIndex(idx);

            auto btn = QMessageBox::question(this, msg::UNSAVED_CHANGES(), msg::UNSAVED_CHANGES_PROMPT(),
                                             QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel);
            if (btn == QMessageBox::Cancel) return false;
            if (btn == QMessageBox::Yes) page->commit();
        }
    }
    return true;
}

void LevelTabWidget::onMapDimensionChanged(int dim) {
    if (auto *page = currentLevelPage(); page && page->getMapWidget()) {
        page->getMapWidget()->changeDimension(static_cast<MapWidget::RenderOption::DimType>(dim));
    }
}

void LevelTabWidget::onMapLayerChanged(int layer) {
    if (auto *page = currentLevelPage(); page && page->getMapWidget()) {
        page->getMapWidget()->changeLayer(static_cast<MapWidget::RenderOption::LayerType>(layer));
    }
}

void LevelTabWidget::onMapToggleOtherLayer(int other) {
    if (auto *page = currentLevelPage(); page && page->getMapWidget()) {
        page->getMapWidget()->toggleOther(static_cast<MapWidget::RenderOption::OtherType>(other));
    }
}

void LevelTabWidget::onMapOpenFilterDialog() {
    if (!currentLevelPage()) return;
    auto *loader = currentLevelPage()->levelLoader();
    if (!loader) return;
    render_filter_dialog_->setFilter(loader->filter());
    render_filter_dialog_->fillInUI();
    if (render_filter_dialog_->exec() == QDialog::Accepted) {
        render_filter_dialog_->collectFilerData();
        loader->setFilter(this->render_filter_dialog_->getFilter());
        loader->clearAllCache();
    }
}

void LevelTabWidget::onMapToggleGlobalDataWidget() {
    if (auto *page = currentLevelPage(); page != nullptr) {
        page->toggleGlobalDataWidget();
    }
}

LevelPageWidget *LevelTabWidget::currentLevelPage() {
    auto *tab = currentWidget();
    if (!tab) return nullptr;
    return dynamic_cast<LevelPageWidget *>(tab);
}