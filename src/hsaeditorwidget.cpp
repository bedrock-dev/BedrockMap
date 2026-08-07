#include "hsaeditorwidget.h"

#include <QComboBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLineEdit>
#include <QPainter>
#include <QPushButton>
#include <QRegularExpression>
#include <QRegularExpressionValidator>
#include <QStyledItemDelegate>
#include <QTableWidget>
#include <QVBoxLayout>

namespace {
    // same color scheme as MapWidget::drawHSAs, indexed by HSAType
    QColor hsaColor(bl::HSAType t) {
        switch (t) {
            case bl::NetherFortress:
                return QColor(0, 223, 162, 255);
            case bl::SwampHut:
                return QColor(255, 0, 96, 255);
            case bl::OceanMonument:
                return QColor(246, 250, 112, 255);
            case bl::PillagerOutpost:
                return QColor(0, 121, 255, 255);
            default:
                return QColor(128, 128, 128, 255);
        }
    }

    QString hsaTypeName(bl::HSAType t) {
        switch (t) {
            case bl::NetherFortress:
                return QObject::tr("hsaEditor.type.netherFortress");
            case bl::SwampHut:
                return QObject::tr("hsaEditor.type.swampHut");
            case bl::OceanMonument:
                return QObject::tr("hsaEditor.type.oceanMonument");
            case bl::PillagerOutpost:
                return QObject::tr("hsaEditor.type.pillagerOutpost");
            default:
                return QObject::tr("hsaEditor.type.unknown");
        }
    }

    // restrict coordinate cell editors to the "x,y,z" integer format
    class CoordEditDelegate : public QStyledItemDelegate {
       public:
        explicit CoordEditDelegate(QObject *parent = nullptr) : QStyledItemDelegate(parent) {}

        QWidget *createEditor(QWidget *parent, const QStyleOptionViewItem &, const QModelIndex &) const override {
            auto *editor = new QLineEdit(parent);
            editor->setValidator(new QRegularExpressionValidator(QRegularExpression(R"(^-?\d+,-?\d+,-?\d+$)"), editor));
            return editor;
        }
    };
}  // namespace

HsaGridWidget::HsaGridWidget(QWidget *parent) : QWidget(parent) { setMinimumSize(320, 320); }

void HsaGridWidget::setData(const std::vector<bl::hardcoded_spawn_area> &areas) {
    this->areas_ = areas;
    this->update();
}

void HsaGridWidget::setChunkOrigin(int cx, int cz) {
    this->cx_ = cx;
    this->cz_ = cz;
    this->update();
}

void HsaGridWidget::setSelectedIndex(int idx) {
    this->selected_ = idx;
    this->update();
}

void HsaGridWidget::paintEvent(QPaintEvent *) {
    QPainter p(this);
    const auto w = width();
    const auto h = height();
    const auto cell = static_cast<double>(qMin(w, h)) / 16.0;
    const auto ox = (w - cell * 16.0) / 2.0;
    const auto oy = (h - cell * 16.0) / 2.0;

    // fill cells covered by any HSA region (top-down x/z view, closed interval)
    for (int i = 0; i < 16; i++) {
        for (int j = 0; j < 16; j++) {
            const int wx = this->cx_ * 16 + i;
            const int wz = this->cz_ * 16 + j;
            QColor fill;
            for (const auto &area : this->areas_) {
                if (wx >= area.min_pos.x && wx <= area.max_pos.x && wz >= area.min_pos.z && wz <= area.max_pos.z) {
                    fill = hsaColor(area.type);
                }
            }
            if (fill.alpha() != 0) {
                auto c = fill;
                c.setAlpha(140);
                p.fillRect(QRectF(ox + i * cell, oy + j * cell, cell, cell), QBrush(c));
            }
            p.setPen(QPen(QColor(0, 0, 0, 18), 1));
            p.drawRect(QRectF(ox + i * cell, oy + j * cell, cell, cell));
        }
    }
    // thicker darker outline for each HSA region so covered areas stand out
    for (const auto &area : this->areas_) {
        const double x0 = ox + (area.min_pos.x - this->cx_ * 16) * cell;
        const double z0 = oy + (area.min_pos.z - this->cz_ * 16) * cell;
        const double x1 = ox + (area.max_pos.x - this->cx_ * 16 + 1) * cell;
        const double z1 = oy + (area.max_pos.z - this->cz_ * 16 + 1) * cell;
        auto outline = hsaColor(area.type).darker(160);
        p.setPen(QPen(outline, 3));
        p.drawRect(QRectF(x0, z0, x1 - x0, z1 - z0));
    }
    // draw the selected highlight last so no other area covers it
    if (this->selected_ >= 0 && this->selected_ < static_cast<int>(this->areas_.size())) {
        const auto &area = this->areas_[static_cast<size_t>(this->selected_)];
        const double x0 = ox + (area.min_pos.x - this->cx_ * 16) * cell;
        const double z0 = oy + (area.min_pos.z - this->cz_ * 16) * cell;
        const double x1 = ox + (area.max_pos.x - this->cx_ * 16 + 1) * cell;
        const double z1 = oy + (area.max_pos.z - this->cz_ * 16 + 1) * cell;
        p.setPen(QPen(Qt::white, 4));
        p.drawRect(QRectF(x0 - 1, z0 - 1, x1 - x0 + 2, z1 - z0 + 2));
    }
    p.setPen(QPen(QColor(0, 0, 0, 120), 2));
    p.drawRect(QRectF(ox, oy, cell * 16, cell * 16));
}

HsaEditorWidget::HsaEditorWidget(QWidget *parent) : QWidget(parent) {
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(2, 2, 2, 2);
    layout->setSpacing(2);

    this->grid_ = new HsaGridWidget();
    layout->addWidget(this->grid_, 1);

    this->table_ = new QTableWidget(0, 4, this);
    this->table_->setHorizontalHeaderLabels(
        {tr("hsaEditor.colIndex"), tr("hsaEditor.colMin"), tr("hsaEditor.colMax"), tr("hsaEditor.colType")});
    this->table_->horizontalHeader()->setStretchLastSection(true);
    this->table_->verticalHeader()->setVisible(false);
    this->table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    this->table_->setSelectionMode(QAbstractItemView::SingleSelection);
    this->table_->setItemDelegate(new CoordEditDelegate(this));
    layout->addWidget(this->table_, 2);

    auto *btnRow = new QHBoxLayout();
    auto *addBtn = new QPushButton(tr("hsaEditor.add"), this);
    auto *removeBtn = new QPushButton(tr("hsaEditor.remove"), this);
    btnRow->addWidget(addBtn);
    btnRow->addWidget(removeBtn);
    btnRow->addStretch();
    layout->addLayout(btnRow);

    connect(addBtn, &QPushButton::clicked, this, &HsaEditorWidget::on_add_btn_clicked);
    connect(removeBtn, &QPushButton::clicked, this, &HsaEditorWidget::on_remove_btn_clicked);
    connect(this->table_, &QTableWidget::cellChanged, this, &HsaEditorWidget::on_table_cellChanged);
    connect(this->table_, &QTableWidget::itemSelectionChanged, this,
            [this]() { this->grid_->setSelectedIndex(this->table_->currentRow()); });
}

void HsaEditorWidget::setChunk(const bl::chunk_pos &cp) {
    this->cp_ = cp;
    this->grid_->setChunkOrigin(cp.x, cp.z);
}

void HsaEditorWidget::setData(const std::vector<bl::hardcoded_spawn_area> &areas) {
    this->filling_ = true;
    this->list_.clear();
    for (const auto &a : areas) this->list_.add(a);
    this->grid_->setData(areas);
    this->rebuildTable();
    this->filling_ = false;
    this->dirty_ = false;
}

std::string HsaEditorWidget::serialize() const { return this->list_.to_raw(); }

void HsaEditorWidget::markClean() {
    this->dirty_ = false;
    emit hsaModified();
}

void HsaEditorWidget::clearData() {
    this->filling_ = true;
    this->list_.clear();
    this->table_->clearContents();
    this->table_->setRowCount(0);
    this->grid_->setData({});
    this->filling_ = false;
    this->dirty_ = false;
}

void HsaEditorWidget::rebuildTable() {
    this->table_->clearContents();
    this->table_->setRowCount(static_cast<int>(this->list_.size()));
    const auto &areas = this->list_.areas();
    for (int row = 0; row < static_cast<int>(areas.size()); row++) {
        const auto &a = areas[static_cast<size_t>(row)];
        auto *indexItem = new QTableWidgetItem(QString::number(row));
        indexItem->setFlags(indexItem->flags() & ~Qt::ItemIsEditable);
        this->table_->setItem(row, 0, indexItem);
        auto *minItem = new QTableWidgetItem(QString("%1,%2,%3").arg(a.min_pos.x).arg(a.min_pos.y).arg(a.min_pos.z));
        auto *maxItem = new QTableWidgetItem(QString("%1,%2,%3").arg(a.max_pos.x).arg(a.max_pos.y).arg(a.max_pos.z));
        this->table_->setItem(row, 1, minItem);
        this->table_->setItem(row, 2, maxItem);
        auto *typeCombo = new QComboBox();
        typeCombo->addItem(hsaTypeName(bl::NetherFortress), static_cast<int>(bl::NetherFortress));
        typeCombo->addItem(hsaTypeName(bl::SwampHut), static_cast<int>(bl::SwampHut));
        typeCombo->addItem(hsaTypeName(bl::OceanMonument), static_cast<int>(bl::OceanMonument));
        typeCombo->addItem(hsaTypeName(bl::PillagerOutpost), static_cast<int>(bl::PillagerOutpost));
        // data with an unknown type falls back to the first entry in the UI (data stays untouched)
        int comboIdx = typeCombo->findData(static_cast<int>(a.type));
        typeCombo->setCurrentIndex(comboIdx < 0 ? 0 : comboIdx);
        this->table_->setCellWidget(row, 3, typeCombo);
        connect(typeCombo, qOverload<int>(&QComboBox::currentIndexChanged), this, &HsaEditorWidget::on_type_changed);
    }
}

void HsaEditorWidget::on_add_btn_clicked() {
    bl::hardcoded_spawn_area area;
    area.type = bl::NetherFortress;
    area.min_pos = bl::block_pos{this->cp_.x * 16, 0, this->cp_.z * 16};
    area.max_pos = bl::block_pos{this->cp_.x * 16 + 15, 127, this->cp_.z * 16 + 15};
    this->list_.add(area);
    this->grid_->setData(this->list_.areas());
    this->rebuildTable();
    this->markDirty();
}

void HsaEditorWidget::on_remove_btn_clicked() {
    const int row = this->table_->currentRow();
    if (row < 0) return;
    this->list_.remove(static_cast<size_t>(row));
    this->grid_->setData(this->list_.areas());
    this->rebuildTable();
    this->markDirty();
}

void HsaEditorWidget::on_table_cellChanged(int row, int column) {
    if (this->filling_ || (column != 1 && column != 2)) return;
    auto *item = this->table_->item(row, column);
    if (!item) return;
    auto &areas = this->list_.areas();
    if (row < 0 || row >= static_cast<int>(areas.size())) return;
    auto &a = areas[static_cast<size_t>(row)];

    const auto parts = item->text().split(',');
    int vals[3];
    bool ok = parts.size() == 3;
    for (int i = 0; i < 3 && ok; i++) {
        bool b = false;
        vals[i] = parts[i].toInt(&b);
        ok = b;
    }
    if (ok) {
        // the coordinate pair must stay consistent (min <= max on every axis)
        if (column == 1) {
            ok = vals[0] <= a.max_pos.x && vals[1] <= a.max_pos.y && vals[2] <= a.max_pos.z;
        } else {
            ok = vals[0] >= a.min_pos.x && vals[1] >= a.min_pos.y && vals[2] >= a.min_pos.z;
        }
    }
    if (!ok) {
        // restore the previous valid value
        const auto &saved = column == 1 ? a.min_pos : a.max_pos;
        this->filling_ = true;
        item->setText(QString("%1,%2,%3").arg(saved.x).arg(saved.y).arg(saved.z));
        this->filling_ = false;
        return;
    }
    const bl::block_pos pos{vals[0], vals[1], vals[2]};
    if (column == 1) {
        a.min_pos = pos;
    } else {
        a.max_pos = pos;
    }
    this->grid_->setData(this->list_.areas());
    this->markDirty();
}

void HsaEditorWidget::on_type_changed(int index) {
    if (this->filling_) return;
    auto *combo = qobject_cast<QComboBox *>(sender());
    if (!combo) return;
    // find the row that owns this combo
    for (int row = 0; row < this->table_->rowCount(); row++) {
        if (this->table_->cellWidget(row, 3) == combo) {
            auto &areas = this->list_.areas();
            if (row >= 0 && row < static_cast<int>(areas.size())) {
                areas[static_cast<size_t>(row)].type = static_cast<bl::HSAType>(combo->itemData(index).toInt());
                this->grid_->setData(this->list_.areas());
                this->markDirty();
            }
            break;
        }
    }
}

void HsaEditorWidget::syncFromTable() { this->markDirty(); }

void HsaEditorWidget::markDirty() {
    if (this->filling_) return;
    this->dirty_ = true;
    emit hsaModified();
}
