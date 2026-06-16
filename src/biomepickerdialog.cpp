#include "biomepickerdialog.h"

#include <QHeaderView>
#include <QTableWidgetItem>

#include "color.h"
#include "magic-enum/magic_enum.hpp"
#include "ui_biomepickerdialog.h"

namespace {

    QColor toQColor(bl::color c) { return QColor(c.r, c.g, c.b, c.a); }

}  // namespace

std::vector<bl::biome> BiomePickerDialog::allBiomes() {
    std::vector<bl::biome> result;
    magic_enum::enum_for_each<bl::biome>([&](auto val) {
        constexpr auto b = val;
        if constexpr (b != bl::biome::none) {
            result.push_back(b);
        }
    });
    return result;
}

BiomePickerDialog::BiomePickerDialog(QWidget *parent) : QDialog(parent), ui(new Ui::BiomePickerDialog) {
    ui->setupUi(this);

    auto setupTable = [](QTableWidget *t) {
        t->horizontalHeader()->setVisible(true);
        t->verticalHeader()->setVisible(false);
        t->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Fixed);
        t->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
        t->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Fixed);
        t->setColumnWidth(0, 24);
        t->setColumnWidth(2, 48);
        t->verticalHeader()->setDefaultSectionSize(22);
    };
    setupTable(ui->leftTable);
    setupTable(ui->rightTable);

    // Ensure only one table has a selection at a time
    connect(ui->leftTable, &QTableWidget::itemSelectionChanged, [this]() {
        if (!ui->leftTable->selectedItems().isEmpty()) ui->rightTable->clearSelection();
    });
    connect(ui->rightTable, &QTableWidget::itemSelectionChanged, [this]() {
        if (!ui->rightTable->selectedItems().isEmpty()) ui->leftTable->clearSelection();
    });

    populateTables();

    connect(ui->buttonBox, &QDialogButtonBox::accepted, this, [this]() {
        auto getSelected = [](QTableWidget *t) -> bl::biome {
            auto sel = t->selectedItems();
            if (sel.isEmpty()) return bl::biome::none;
            int row = sel.first()->row();
            auto *item = t->item(row, 2);
            if (!item) return bl::biome::none;
            return static_cast<bl::biome>(item->text().toInt());
        };

        bl::biome left = getSelected(ui->leftTable);
        bl::biome right = getSelected(ui->rightTable);
        selected_ = (left != bl::biome::none) ? left : right;
        if (selected_ != bl::biome::none) accept();
    });

    connect(ui->buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

void BiomePickerDialog::populateTables() {
    auto biomes = allBiomes();
    int mid = static_cast<int>((biomes.size() + 1) / 2);

    auto fillTable = [](QTableWidget *t, const std::vector<bl::biome> &list, int start, int end) {
        t->setRowCount(end - start);
        for (int i = start; i < end; i++) {
            int row = i - start;
            auto b = list[i];

            // Color swatch
            auto *swatch = new QTableWidgetItem();
            swatch->setBackground(QBrush(toQColor(bl::get_biome_color(b))));
            swatch->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
            t->setItem(row, 0, swatch);

            // Name
            auto *name = new QTableWidgetItem(QString::fromStdString(bl::get_biome_name(b)));
            name->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
            t->setItem(row, 1, name);

            // ID
            auto *idItem = new QTableWidgetItem(QString::number(static_cast<int>(b)));
            idItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
            t->setItem(row, 2, idItem);
        }
    };

    fillTable(ui->leftTable, biomes, 0, mid);
    fillTable(ui->rightTable, biomes, mid, static_cast<int>(biomes.size()));
}

bl::biome BiomePickerDialog::selectedBiome() const { return selected_; }

bool BiomePickerDialog::hasSelection() const { return selected_ != bl::biome::none; }

BiomePickerDialog::~BiomePickerDialog() { delete ui; }
