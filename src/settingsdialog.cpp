#include "settingsdialog.h"

#include <QColorDialog>
#include <QSettings>
#include <QStringList>
#include <QTreeWidgetItem>

#include "config.h"
#include "loguru/loguru.hpp"
#include "ui_settingsdialog.h"

SettingsDialog::SettingsDialog(QWidget *parent) : QDialog(parent), ui(new Ui::SettingsDialog) {
    ui->setupUi(this);
    this->setWindowTitle(tr("settingsDialog.title"));
    this->setWindowFlags(this->windowFlags() & ~Qt::WindowContextHelpButtonHint);

    setupCategories();
    loadSettings();

    // Connect tree widget selection change
    connect(ui->categoryTree, &QTreeWidget::currentItemChanged, this, &SettingsDialog::onCategoryChanged);

    // Connect color picker buttons
    connect(ui->gridColorBtn, &QPushButton::clicked, this, &SettingsDialog::onGridColorPick);
    connect(ui->voidColorBtn, &QPushButton::clicked, this, &SettingsDialog::onVoidColorPick);
    connect(ui->actorBorderColorBtn, &QPushButton::clicked, this, &SettingsDialog::onActorBorderColorPick);
    connect(ui->chunkEditorColorBtn, &QPushButton::clicked, this, &SettingsDialog::onChunkEditorColorPick);

    // Connect buttons
    connect(ui->saveBtn, &QPushButton::clicked, this, &SettingsDialog::onSave);

    // Connect interdependent settings
    connect(ui->renderStyleCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &SettingsDialog::updateShadowOptions);
    connect(ui->loadGlobalDataCheck, &QCheckBox::toggled, this, &SettingsDialog::updateGlobalDataOptions);
    connect(ui->fontFamilyCombo, &QFontComboBox::currentFontChanged, this,
            [this](const QFont &font) { ui->fontSizeSpin->setValue(font.pointSize()); });

    // Init interdependent state
    updateShadowOptions();
    updateGlobalDataOptions();

    // Select first category by default
    if (ui->categoryTree->topLevelItemCount() > 0) {
        ui->categoryTree->setCurrentItem(ui->categoryTree->topLevelItem(0));
    }
}

SettingsDialog::~SettingsDialog() { delete ui; }

void SettingsDialog::setupCategories() {
    ui->categoryTree->clear();

    auto *guiItem = new QTreeWidgetItem(ui->categoryTree);
    guiItem->setText(0, tr("settingsDialog.category.gui"));
    guiItem->setData(0, Qt::UserRole, 0);

    auto *mapItem = new QTreeWidgetItem(ui->categoryTree);
    mapItem->setText(0, tr("settingsDialog.category.map"));
    mapItem->setData(0, Qt::UserRole, 1);

    auto *cacheItem = new QTreeWidgetItem(ui->categoryTree);
    cacheItem->setText(0, tr("settingsDialog.category.cache"));
    cacheItem->setData(0, Qt::UserRole, 2);

    auto *miscItem = new QTreeWidgetItem(ui->categoryTree);
    miscItem->setText(0, tr("settingsDialog.category.misc"));
    miscItem->setData(0, Qt::UserRole, 3);

    auto *debugItem = new QTreeWidgetItem(ui->categoryTree);
    debugItem->setText(0, tr("settingsDialog.category.debug"));
    debugItem->setData(0, Qt::UserRole, 4);

    auto *langItem = new QTreeWidgetItem(ui->categoryTree);
    langItem->setText(0, tr("settingsDialog.category.lang"));
    langItem->setData(0, Qt::UserRole, 5);
}

void SettingsDialog::loadSettings() {
    // --- Gui ---
    if (setting::COLOR_THEME == "light")
        ui->themeCombo->setCurrentIndex(0);
    else if (setting::COLOR_THEME == "dark")
        ui->themeCombo->setCurrentIndex(1);
    else if (setting::COLOR_THEME == "system")
        ui->themeCombo->setCurrentIndex(2);

    if (!setting::FONT_FAMILY.isEmpty()) {
        ui->fontFamilyCombo->setCurrentFont(QFont(setting::FONT_FAMILY));
    }
    ui->fontSizeSpin->setValue(setting::FONT_SIZE > 0 ? setting::FONT_SIZE : ui->fontFamilyCombo->currentFont().pointSize());
    ui->nbtEditorModeCheck->setChecked(setting::OPEN_NBT_EDITOR_ONLY);

    // --- Map ---
    ui->renderStyleCombo->setCurrentIndex(std::clamp(setting::MAP_RENDER_STYLE, 0, 2));
    ui->shadowScaleCombo->setCurrentIndex((setting::SHADOW_RENDER_SCALE >> 0) - 1);  // 1→0, 2→1, 4→2, 8→3, 16→4, 32→5
    ui->shadowPCFSpin->setValue(setting::SHADOW_PCF_RADIUS);
    ui->shadowLevelSpin->setValue(setting::SHADOW_LEVEL);
    ui->minScaleSpin->setValue(setting::MINIMUM_SCALE_LEVEL);
    ui->maxScaleSpin->setValue(setting::MAXIMUM_SCALE_LEVEL);
    ui->zoomSpeedSpin->setValue(setting::ZOOM_SPEED);
    ui->gridColorEdit->setText(setting::GRID_LINE_COLOR);
    ui->voidColorEdit->setText(setting::VOID_MAP_COLOR);
    ui->transparentWaterCheck->setChecked(setting::TRANSPARENT_WATER);
    ui->thumbnailModeCheck->setChecked(setting::ENABLE_THUMBNAIL_MODE);
    ui->actorStyleCombo->setCurrentIndex(std::clamp(setting::ACTOR_RENDER_STYLE, 0, 1));
    ui->actorBorderWidthSpin->setValue(setting::ACTOR_BORDER_WIDTH);
    ui->actorBorderColorEdit->setText(setting::ACTOR_BORDER_COLOR);
    ui->chunkEditorColorEdit->setText(setting::CHUNK_EDITOR_HIGHLIGHT_COLOR);
    ui->chunkEditorWidthSpin->setValue(setting::CHUNK_EDITOR_HIGHLIGHT_WIDTH);

    // --- Cache ---
    ui->regionCacheSpin->setValue(setting::REGION_CACHE_SIZE);
    ui->emptyCacheSpin->setValue(setting::EMPTY_REGION_CACHE_SIZE);
    ui->threadNumSpin->setValue(setting::THREAD_NUM);

    // --- Misc ---
    ui->loadGlobalDataCheck->setChecked(setting::LOAD_GLOBAL_DATA);
    ui->maxGlobalDataSpin->setValue(setting::MAX_GLOBAL_DATA_LOAD_COUNT);
    ui->iconThemeCombo->setCurrentText(setting::ICON_THEME);

    // --- Debug ---
    ui->logMissingTextureCheck->setChecked(setting::LOG_OUT_MISSING_TEXTURE);

    // --- Lang ---
    ui->langCombo->setCurrentIndex(setting::LANGUAGE == "en" ? 1 : 0);
}

void SettingsDialog::onCategoryChanged(QTreeWidgetItem *current, QTreeWidgetItem * /*previous*/) {
    if (!current) return;
    int page = current->data(0, Qt::UserRole).toInt();
    ui->settingsStack->setCurrentIndex(page);
}

void SettingsDialog::onGridColorPick() { onPickColor(ui->gridColorEdit); }

void SettingsDialog::onVoidColorPick() { onPickColor(ui->voidColorEdit); }

void SettingsDialog::onActorBorderColorPick() { onPickColor(ui->actorBorderColorEdit); }

void SettingsDialog::onChunkEditorColorPick() { onPickColor(ui->chunkEditorColorEdit); }

void SettingsDialog::onPickColor(QLineEdit *edit) {
    QColor current(edit->text());
    QColor chosen = QColorDialog::getColor(current, this, tr("settingsDialog.colorPicker.title"));
    if (chosen.isValid()) {
        edit->setText(chosen.name());
    }
}

void SettingsDialog::updateShadowOptions() {
    int idx = ui->renderStyleCombo->currentIndex();
    bool hasShadow = idx > 0;    // style 1 or 2
    bool hasAdvanced = idx > 1;  // style 2 only

    ui->label_shadow_scale->setEnabled(hasAdvanced);
    ui->shadowScaleCombo->setEnabled(hasAdvanced);
    ui->label_shadow_pcf->setEnabled(hasAdvanced);
    ui->shadowPCFSpin->setEnabled(hasAdvanced);
    ui->label_shadow_level->setEnabled(hasShadow);
    ui->shadowLevelSpin->setEnabled(hasShadow);
}

void SettingsDialog::updateGlobalDataOptions() {
    bool enable = ui->loadGlobalDataCheck->isChecked();
    ui->label_max_global->setEnabled(enable);
    ui->maxGlobalDataSpin->setEnabled(enable);
}

void SettingsDialog::onSave() {
    // Write UI values to cfg statics
    switch (ui->themeCombo->currentIndex()) {
        case 0:
            setting::COLOR_THEME = "light";
            break;
        case 1:
            setting::COLOR_THEME = "dark";
            break;
        case 2:
            setting::COLOR_THEME = "system";
            break;
    }
    setting::FONT_FAMILY = ui->fontFamilyCombo->currentFont().family();
    setting::FONT_SIZE = ui->fontSizeSpin->value();
    setting::OPEN_NBT_EDITOR_ONLY = ui->nbtEditorModeCheck->isChecked();

    setting::MAP_RENDER_STYLE = ui->renderStyleCombo->currentIndex();
    int scaleValues[] = {1, 2, 4, 8, 16, 32};
    int scaleIdx = std::clamp(ui->shadowScaleCombo->currentIndex(), 0, 5);
    setting::SHADOW_RENDER_SCALE = scaleValues[scaleIdx];
    setting::SHADOW_PCF_RADIUS = ui->shadowPCFSpin->value();
    setting::SHADOW_LEVEL = ui->shadowLevelSpin->value();
    setting::MINIMUM_SCALE_LEVEL = ui->minScaleSpin->value();
    setting::MAXIMUM_SCALE_LEVEL = ui->maxScaleSpin->value();
    setting::ZOOM_SPEED = static_cast<float>(ui->zoomSpeedSpin->value());
    setting::GRID_LINE_COLOR = ui->gridColorEdit->text();
    setting::VOID_MAP_COLOR = ui->voidColorEdit->text();
    setting::TRANSPARENT_WATER = ui->transparentWaterCheck->isChecked();
    setting::ENABLE_THUMBNAIL_MODE = ui->thumbnailModeCheck->isChecked();
    setting::ACTOR_RENDER_STYLE = ui->actorStyleCombo->currentIndex();
    setting::ACTOR_BORDER_WIDTH = ui->actorBorderWidthSpin->value();
    setting::ACTOR_BORDER_COLOR = ui->actorBorderColorEdit->text();
    setting::CHUNK_EDITOR_HIGHLIGHT_COLOR = ui->chunkEditorColorEdit->text();
    setting::CHUNK_EDITOR_HIGHLIGHT_WIDTH = ui->chunkEditorWidthSpin->value();

    setting::REGION_CACHE_SIZE = ui->regionCacheSpin->value();
    setting::EMPTY_REGION_CACHE_SIZE = ui->emptyCacheSpin->value();
    setting::THREAD_NUM = ui->threadNumSpin->value();

    setting::LOAD_GLOBAL_DATA = ui->loadGlobalDataCheck->isChecked();
    setting::MAX_GLOBAL_DATA_LOAD_COUNT = ui->maxGlobalDataSpin->value();
    setting::ICON_THEME = ui->iconThemeCombo->currentText();

    setting::LOG_OUT_MISSING_TEXTURE = ui->logMissingTextureCheck->isChecked();

    setting::LANGUAGE = ui->langCombo->currentIndex() == 1 ? "en" : "zh_CN";

    setting::save();
}
