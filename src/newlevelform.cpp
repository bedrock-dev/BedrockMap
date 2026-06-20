#include "newlevelform.h"

#include <QFileDialog>
#include <QHeaderView>
#include <QMessageBox>
#include <QRegularExpressionValidator>
#include <QTableWidget>

#include "biomepickerdialog.h"
#include "color.h"
#include "msg.h"
#include "ui_newlevelform.h"

NewLevelForm::NewLevelForm(QWidget *parent) : QDialog(parent), ui(new Ui::NewLevelForm) {
    ui->setupUi(this);

    // version input validation: 1.x.y or 1.x.y.z, each segment not starting with 0
    static const QRegularExpression versionRe(QStringLiteral(R"(^1\.(0|[1-9]\d*)\.(0|[1-9]\d*)(?:\.(0|[1-9]\d*))?$)"));
    ui->version_edit->setValidator(new QRegularExpressionValidator(versionRe, this));

    // table setup
    ui->block_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    ui->block_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Fixed);
    ui->block_table->horizontalHeader()->resizeSection(1, 80);
    ui->block_table->setSelectionBehavior(QAbstractItemView::SelectRows);

    // button box
    connect(ui->buttonBox, &QDialogButtonBox::accepted, this, [this]() {
        auto p = params();
        QString msg;
        if (!p.check(msg)) {
            QMessageBox::warning(this, msg::WARNING_TITLE(), msg);
            return;
        }
        accept();
    });
    connect(ui->buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);

    // browse button
    connect(ui->browse_btn, &QPushButton::clicked, this, [this]() {
        auto dir = QFileDialog::getExistingDirectory(this, msg::SELECT_LEVEL_DIR());
        if (!dir.isEmpty()) {
            ui->path_edit->setText(dir);
        }
    });

    // add row
    connect(ui->add_btn, &QPushButton::clicked, this, [this]() {
        int row = ui->block_table->rowCount();
        ui->block_table->insertRow(row);
        ui->block_table->setItem(row, 0, new QTableWidgetItem());
        auto *countItem = new QTableWidgetItem();
        countItem->setData(Qt::DisplayRole, 1);
        ui->block_table->setItem(row, 1, countItem);
    });

    // block name changed → update row color
    auto updateBlockRowColor = [this](int row) {
        return;
        auto *item = ui->block_table->item(row, 0);
        if (!item || item->text().trimmed().isEmpty()) return;
        auto name = item->text().trimmed().toStdString();
        if (name.find(':') == std::string::npos) {
            name = "minecraft:" + name;
        }
        auto c = bl::get_block_by_name_tag(name);
        c = bl::blend_color_with_biome(name, c, selectedBiome_);
        auto bg = QBrush(QColor(c.r, c.g, c.b, c.a));
        item->setBackground(bg);
    };

    connect(ui->block_table, &QTableWidget::cellChanged, this, [updateBlockRowColor](int row, int col) {
        if (col != 0) return;
        updateBlockRowColor(row);
    });

    // delete selected row
    connect(ui->del_btn, &QPushButton::clicked, this, [this]() {
        auto rows = ui->block_table->selectionModel()->selectedRows();
        // remove from bottom to keep indices valid
        std::sort(rows.begin(), rows.end(), [](const QModelIndex &a, const QModelIndex &b) { return a.row() > b.row(); });
        for (const auto &idx : rows) {
            ui->block_table->removeRow(idx.row());
        }
    });

    // biome picker
    auto updateBiomeBtn = [this]() {
        ui->biome_btn->setText(QString::fromStdString(bl::get_biome_name(selectedBiome_)));
        return;
        auto c = bl::get_biome_color(selectedBiome_);
        ui->biome_btn->setStyleSheet(QStringLiteral("QPushButton { background-color: %1; }").arg(QColor(c.r, c.g, c.b, c.a).name()));
    };
    updateBiomeBtn();
    connect(ui->biome_btn, &QPushButton::clicked, this, [this, updateBiomeBtn, updateBlockRowColor]() {
        BiomePickerDialog dlg(this);
        if (dlg.exec() == QDialog::Accepted && dlg.hasSelection()) {
            selectedBiome_ = dlg.selectedBiome();
            updateBiomeBtn();
            for (int i = 0; i < ui->block_table->rowCount(); i++) {
                updateBlockRowColor(i);
            }
        }
    });
}

NewLevelForm::~NewLevelForm() { delete ui; }

NewLevelParams NewLevelForm::params() const {
    NewLevelParams p;
    p.levelName = ui->levelname_edit->text();
    p.path = ui->path_edit->text();
    p.version = ui->version_edit->text();
    p.gameMode = ui->gamemode_combo->currentIndex();
    p.difficulty = ui->difficulty_combo->currentIndex();
    p.dayNightCycle = ui->daynight_check->isChecked();
    p.weatherCycle = ui->weather_check->isChecked();
    p.mobSpawning = ui->mobspawn_check->isChecked();
    p.flat = ui->flat_group->isChecked();
    p.biome = selectedBiome_;

    QStringList parts;
    for (int i = 0; i < ui->block_table->rowCount(); i++) {
        auto *nameItem = ui->block_table->item(i, 0);
        auto *countItem = ui->block_table->item(i, 1);
        if (nameItem && !nameItem->text().isEmpty()) {
            int count = countItem ? countItem->data(Qt::DisplayRole).toInt() : 1;
            p.flatBlocks.append(qMakePair(nameItem->text().trimmed(), count));
        }
    }
    return p;
}
