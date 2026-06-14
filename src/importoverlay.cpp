#include "importoverlay.h"

#include <QFile>
#include <QPainter>
#include <QPainterPath>

#include "asynclevelloader.h"
#include "chunkoperator.h"
#include "floatingtoolbar.h"
#include "levelpagewidget.h"
#include "loguru/loguru.hpp"
#include "resourcemanager.h"

ImportOverlay::ImportOverlay(QWidget *parent, AsyncLevelLoader *loader, LevelPageWidget *levelPage)
    : QObject(parent), parent_(parent), loader_(loader), level_page_(levelPage) {}

void ImportOverlay::startImport(const QString &filePath, uint8_t dim, const bl::chunk_pos &initialCp) {
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        LOG_F(WARNING, "ImportOverlay: cannot open file %s", filePath.toStdString().c_str());
        return;
    }
    QByteArray rawData = file.readAll();
    file.close();

    startPaste(rawData, dim, initialCp);
}

bool ImportOverlay::startPaste(const QByteArray &data, uint8_t dim, const bl::chunk_pos &initialCp) {
    preview_ = ExportedRegion::deserialize(data.constData(), data.size());
    if (preview_.isEmpty()) return false;

    dim_ = dim;
    if (!preview_.chunks().empty()) {
        auto &first = preview_.chunks().front();
        offset_.x = initialCp.x - first.pos().x;
        offset_.z = initialCp.z - first.pos().z;
    } else {
        offset_ = bl::chunk_pos{0, 0, 0};
    }
    placed_ = false;
    mode_ = true;

    level_page_->setToolBarsVisible(false);

    // Lazy-create confirm bar using FloatingToolBar
    if (!confirm_bar_) {
        using GC = FloatingToolBar::GroupConfig;

        confirm_bar_ = new FloatingToolBar(parent_);
        confirm_bar_->setOrientation(Qt::Horizontal);
        confirm_bar_->setAnchor(Qt::AlignHCenter | Qt::AlignBottom);
        confirm_bar_->setAnchorMargins(8);

        GC group;
        group.mode = GC::Toggle;
        group.buttons = {
            {ToolBarIcon("accept"), QObject::tr("importOverlay.button.confirm"), false},
            {ToolBarIcon("cancel"), QObject::tr("importOverlay.button.cancel"), false},
        };
        confirm_bar_->addGroup(group);

        connect(confirm_bar_, &FloatingToolBar::buttonToggled, this, [this](int, int btnIdx, bool) {
            if (btnIdx == 0)
                confirm();
            else
                cancel();
        });
    }
    confirm_bar_->hide();

    return true;
}

void ImportOverlay::handleMouseMove(const bl::chunk_pos &mouseCp) {
    if (!mode_ || placed_) return;
    if (preview_.isEmpty()) return;
    auto &first = preview_.chunks().front();
    offset_.x = mouseCp.x - first.pos().x;
    offset_.z = mouseCp.z - first.pos().z;
}

void ImportOverlay::handleLeftClick() {
    if (!mode_ || placed_) return;
    placed_ = true;
    confirm_bar_->show();
    confirm_bar_->raise();
}

bool ImportOverlay::handleRightClick() {
    if (!mode_ || !placed_) return false;
    placed_ = false;
    confirm_bar_->hide();
    return true;
}

bool ImportOverlay::handleKeyPress(int key) {
    if (key != Qt::Key_Escape || !mode_) return false;
    cancel();
    return true;
}

void ImportOverlay::draw(QPainter *p, qreal scaleLevel) {
    if (!mode_ || preview_.isEmpty()) return;

    QPainterPath path;
    for (const auto &chunk : preview_.chunks()) {
        auto cp = chunk.pos();
        path.addRect(QRectF(static_cast<qreal>(cp.x + offset_.x), static_cast<qreal>(cp.z + offset_.z), 1.0, 1.0));
    }
    path = path.simplified();

    QColor fillColor = placed_ ? QColor(0, 200, 100, 100) : QColor(255, 255, 0, 80);
    QColor borderColor = placed_ ? QColor(0, 200, 100, 220) : QColor(255, 255, 0, 200);

    p->fillPath(path, fillColor);
    p->strokePath(path, QPen(borderColor, 2.0 / scaleLevel));
}

void ImportOverlay::resize(int, int) {
    // FloatingToolBar reposition is handled via showEvent + eventFilter
}

void ImportOverlay::confirm() {
    if (preview_.isEmpty()) return;

    for (auto &chunk : preview_.chunks()) {
        auto cp = chunk.pos();
        cp.x += offset_.x;
        cp.z += offset_.z;
        cp.dim = static_cast<int8_t>(dim_);
        // BUG here, the imported enitites will be mixed with original, so we disabled it now
        // chunk.clear_entities();
        chunk.set_pos(cp, &this->level_page_->levelLoader()->level());
    }

    ChunkOperator::importRegion(preview_, *loader_);

    emit confirmed();
    cleanup();
}

void ImportOverlay::cancel() { cleanup(); }

void ImportOverlay::cleanup() {
    mode_ = false;
    placed_ = false;
    preview_.clear();
    if (confirm_bar_) confirm_bar_->hide();
    level_page_->setToolBarsVisible(true);
}
