#include "settingsdialog.h"

#include <QColorDialog>
#include <QSettings>
#include <QSignalBlocker>
#include <QStringList>
#include <QTreeWidgetItem>
#include <cmath>

#include "config.h"
#include "loguru/loguru.hpp"
#include "ui_settingsdialog.h"

SettingsDialog::SettingsDialog(QWidget* parent) : QDialog(parent), ui(new Ui::SettingsDialog) {
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
    connect(ui->voxelSelectionColorBtn, &QPushButton::clicked, this, &SettingsDialog::onVoxelSelectionColorPick);

    // Connect buttons
    connect(ui->saveBtn, &QPushButton::clicked, this, &SettingsDialog::onSave);

    // Connect interdependent settings
    connect(ui->renderStyleCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &SettingsDialog::updateShadowOptions);
    connect(ui->loadGlobalDataCheck, &QCheckBox::toggled, this, &SettingsDialog::updateGlobalDataOptions);
    connect(ui->fontFamilyCombo, &QFontComboBox::currentFontChanged, this,
            [this](const QFont& font) { ui->fontSizeSpin->setValue(font.pointSize()); });

    // Both controls edit the same value, so each follows the other.
    connect(ui->gpuShadowSlider, &QSlider::valueChanged, this, [this](int value) { setGpuShadowStrength(value / 100.0); });
    connect(ui->gpuShadowSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &SettingsDialog::setGpuShadowStrength);
    connect(ui->gpuAoSlider, &QSlider::valueChanged, this, [this](int value) { setGpuAoStrength(value / 100.0); });
    connect(ui->gpuAoSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &SettingsDialog::setGpuAoStrength);
    connect(ui->gpuBevelSlider, &QSlider::valueChanged, this, [this](int value) { setGpuBevelStrength(value / 100.0); });
    connect(ui->gpuBevelSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &SettingsDialog::setGpuBevelStrength);
    connect(ui->gpuSaturationSlider, &QSlider::valueChanged, this, [this](int value) { setGpuSaturation(value / 100.0); });
    connect(ui->gpuSaturationSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &SettingsDialog::setGpuSaturation);
    connect(ui->gpuBrightnessSlider, &QSlider::valueChanged, this, [this](int value) { setGpuBrightness(value / 100.0); });
    connect(ui->gpuBrightnessSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &SettingsDialog::setGpuBrightness);

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

    auto* guiItem = new QTreeWidgetItem(ui->categoryTree);
    guiItem->setText(0, tr("settingsDialog.category.gui"));
    guiItem->setData(0, Qt::UserRole, 0);

    auto* mapItem = new QTreeWidgetItem(ui->categoryTree);
    mapItem->setText(0, tr("settingsDialog.category.map"));
    mapItem->setData(0, Qt::UserRole, 1);

    auto* voxelItem = new QTreeWidgetItem(ui->categoryTree);
    voxelItem->setText(0, tr("settingsDialog.category.voxel"));
    voxelItem->setData(0, Qt::UserRole, 2);

    auto* cacheItem = new QTreeWidgetItem(ui->categoryTree);
    cacheItem->setText(0, tr("settingsDialog.category.cache"));
    cacheItem->setData(0, Qt::UserRole, 3);

    auto* miscItem = new QTreeWidgetItem(ui->categoryTree);
    miscItem->setText(0, tr("settingsDialog.category.misc"));
    miscItem->setData(0, Qt::UserRole, 4);

    auto* extraItem = new QTreeWidgetItem(ui->categoryTree);
    extraItem->setText(0, tr("settingsDialog.category.extra"));
    extraItem->setData(0, Qt::UserRole, 5);
    auto* renderItem = new QTreeWidgetItem(ui->categoryTree);
    renderItem->setText(0, tr("settingsDialog.category.render"));
    renderItem->setData(0, Qt::UserRole, 6);

    auto* langItem = new QTreeWidgetItem(ui->categoryTree);
    langItem->setText(0, tr("settingsDialog.category.lang"));
    langItem->setData(0, Qt::UserRole, 7);
}

void SettingsDialog::loadSettings() {
    // --- Gui ---
    if (setting::current().COLOR_THEME == "light")
        ui->themeCombo->setCurrentIndex(0);
    else if (setting::current().COLOR_THEME == "dark")
        ui->themeCombo->setCurrentIndex(1);
    else if (setting::current().COLOR_THEME == "system")
        ui->themeCombo->setCurrentIndex(2);

    if (!setting::current().FONT_FAMILY.isEmpty()) {
        ui->fontFamilyCombo->setCurrentFont(QFont(setting::current().FONT_FAMILY));
    }
    ui->fontSizeSpin->setValue(setting::current().FONT_SIZE > 0 ? setting::current().FONT_SIZE
                                                                : ui->fontFamilyCombo->currentFont().pointSize());

    // --- Map ---
    // Rendering controls live on the Rendering page; map keeps only camera,
    // overlay, and appearance settings.
    ui->minScaleSpin->setValue(setting::current().MINIMUM_SCALE_LEVEL);
    ui->maxScaleSpin->setValue(setting::current().MAXIMUM_SCALE_LEVEL);
    ui->gridColorEdit->setText(setting::current().GRID_LINE_COLOR);
    ui->voidColorEdit->setText(setting::current().VOID_MAP_COLOR);
    ui->actorStyleCombo->setCurrentIndex(std::clamp(setting::current().ACTOR_RENDER_STYLE, 0, 1));
    ui->actorBorderWidthSpin->setValue(setting::current().ACTOR_BORDER_WIDTH);
    ui->actorBorderColorEdit->setText(setting::current().ACTOR_BORDER_COLOR);
    ui->chunkEditorColorEdit->setText(setting::current().CHUNK_EDITOR_HIGHLIGHT_COLOR);
    ui->chunkEditorWidthSpin->setValue(setting::current().CHUNK_EDITOR_HIGHLIGHT_WIDTH);
    ui->voxelSelectionColorEdit->setText(setting::current().VOXEL_SELECTION_COLOR);

    // --- Cache ---
    ui->regionCacheSpin->setValue(setting::current().REGION_CACHE_SIZE);
    ui->emptyCacheSpin->setValue(setting::current().EMPTY_REGION_CACHE_SIZE);
    ui->threadNumSpin->setValue(setting::current().THREAD_NUM);

    // --- Misc ---
    ui->loadGlobalDataCheck->setChecked(setting::current().LOAD_GLOBAL_DATA);
    ui->maxGlobalDataSpin->setValue(setting::current().MAX_GLOBAL_DATA_LOAD_COUNT);
    ui->iconThemeCombo->setCurrentText(setting::current().ICON_THEME);

    // --- Extra features ---
    ui->preloadCoordsCheck->setChecked(setting::current().PRELOAD_ALL_CHUNK_COORDS);

    // --- Rendering ---
    ui->gpuRenderCheck->setChecked(setting::current().GPU_RENDER_ENABLED);
    ui->renderStyleCombo->setCurrentIndex(std::clamp(setting::current().MAP_RENDER_STYLE, 0, 2));
    {
        const int scaleVals[] = {1, 2, 4, 8, 16, 32};
        int idx = 0;
        for (int i = 0; i < 6; i++) {
            if (scaleVals[i] == setting::current().TILE_RENDER_SCALE) {
                idx = i;
                break;
            }
        }
        ui->shadowScaleCombo->setCurrentIndex(idx);
    }
    {
        const int mapScaleVals[] = {1, 2, 4, 8};
        int idx = 0;
        for (int i = 0; i < 4; i++) {
            if (mapScaleVals[i] == setting::current().SHADOW_MAP_SCALE) {
                idx = i;
                break;
            }
        }
        ui->shadowMapScaleCombo->setCurrentIndex(idx);
    }
    ui->shadowLevelSpin->setValue(setting::current().SHADOW_LEVEL);
    setGpuShadowStrength(setting::current().GPU_SHADOW_STRENGTH);
    setGpuAoStrength(setting::current().GPU_AO_STRENGTH);
    setGpuBevelStrength(setting::current().GPU_BEVEL_STRENGTH);
    setGpuSaturation(setting::current().GPU_SATURATION);
    setGpuBrightness(setting::current().GPU_BRIGHTNESS);

    // --- Lang ---
    ui->langCombo->setCurrentIndex(setting::current().LANGUAGE == "en" ? 1 : 0);
}

void SettingsDialog::onCategoryChanged(QTreeWidgetItem* current, QTreeWidgetItem* /*previous*/) {
    if (!current) return;
    int page = current->data(0, Qt::UserRole).toInt();
    ui->settingsStack->setCurrentIndex(page);
}

void SettingsDialog::setGpuShadowStrength(double value) {
    const QSignalBlocker block_slider(ui->gpuShadowSlider);
    const QSignalBlocker block_spin(ui->gpuShadowSpin);
    ui->gpuShadowSpin->setValue(value);
    ui->gpuShadowSlider->setValue(static_cast<int>(std::lround(value * 100.0)));
}

void SettingsDialog::setGpuAoStrength(double value) {
    // The slider is the coarse control (percentage) and the spin box the exact
    // one; setting both from one value keeps them from disagreeing.
    const QSignalBlocker block_slider(ui->gpuAoSlider);
    const QSignalBlocker block_spin(ui->gpuAoSpin);
    ui->gpuAoSpin->setValue(value);
    ui->gpuAoSlider->setValue(static_cast<int>(std::lround(value * 100.0)));
}

void SettingsDialog::setGpuBevelStrength(double value) {
    const QSignalBlocker block_slider(ui->gpuBevelSlider);
    const QSignalBlocker block_spin(ui->gpuBevelSpin);
    ui->gpuBevelSpin->setValue(value);
    ui->gpuBevelSlider->setValue(static_cast<int>(std::lround(value * 100.0)));
}

void SettingsDialog::setGpuSaturation(double value) {
    // The slider spans 0..200% so that boosting is on the same control as cutting.
    const QSignalBlocker block_slider(ui->gpuSaturationSlider);
    const QSignalBlocker block_spin(ui->gpuSaturationSpin);
    ui->gpuSaturationSpin->setValue(value);
    ui->gpuSaturationSlider->setValue(static_cast<int>(std::lround(value * 100.0)));
}

void SettingsDialog::setGpuBrightness(double value) {
    // 1.0 is neutral; the control spans 0..200% to allow both dimming and lifting.
    const QSignalBlocker block_slider(ui->gpuBrightnessSlider);
    const QSignalBlocker block_spin(ui->gpuBrightnessSpin);
    ui->gpuBrightnessSpin->setValue(value);
    ui->gpuBrightnessSlider->setValue(static_cast<int>(std::lround(value * 100.0)));
}

void SettingsDialog::onGridColorPick() { onPickColor(ui->gridColorEdit); }

void SettingsDialog::onVoidColorPick() { onPickColor(ui->voidColorEdit); }

void SettingsDialog::onActorBorderColorPick() { onPickColor(ui->actorBorderColorEdit); }

void SettingsDialog::onChunkEditorColorPick() { onPickColor(ui->chunkEditorColorEdit); }

void SettingsDialog::onVoxelSelectionColorPick() { onPickColor(ui->voxelSelectionColorEdit, true); }

void SettingsDialog::onPickColor(QLineEdit* edit, bool withAlpha) {
    QColor current(edit->text());
    const auto options = withAlpha ? QColorDialog::ShowAlphaChannel : QColorDialog::ColorDialogOptions();
    QColor chosen = QColorDialog::getColor(current, this, tr("settingsDialog.colorPicker.title"), options);
    if (chosen.isValid()) {
        // Colors that support alpha are stored as #AARRGGBB; the others keep the shorter form.
        edit->setText(withAlpha ? chosen.name(QColor::HexArgb) : chosen.name());
    }
}

void SettingsDialog::updateShadowOptions() {
    int idx = ui->renderStyleCombo->currentIndex();
    bool hasShadow = idx > 0;    // style 1 or 2
    bool hasAdvanced = idx > 1;  // style 2 only

    ui->label_shadow_scale->setEnabled(hasAdvanced);
    ui->shadowScaleCombo->setEnabled(hasAdvanced);
    ui->label_shadow_map_scale->setEnabled(hasAdvanced);
    ui->shadowMapScaleCombo->setEnabled(hasAdvanced);
    ui->label_shadow_level->setEnabled(hasShadow);
    ui->shadowLevelSpin->setEnabled(hasShadow);
}

void SettingsDialog::updateGlobalDataOptions() {
    bool enable = ui->loadGlobalDataCheck->isChecked();
    ui->label_max_global->setEnabled(enable);
    ui->maxGlobalDataSpin->setEnabled(enable);
}

void SettingsDialog::onSave() {
    // Build a pending configuration without changing the active runtime settings.
    auto values = setting::current();
    switch (ui->themeCombo->currentIndex()) {
        case 0:
            values.COLOR_THEME = "light";
            break;
        case 1:
            values.COLOR_THEME = "dark";
            break;
        case 2:
            values.COLOR_THEME = "system";
            break;
    }
    values.FONT_FAMILY = ui->fontFamilyCombo->currentFont().family();
    values.FONT_SIZE = ui->fontSizeSpin->value();

    values.MAP_RENDER_STYLE = ui->renderStyleCombo->currentIndex();
    int scaleValues[] = {1, 2, 4, 8, 16, 32};
    int scaleIdx = std::clamp(ui->shadowScaleCombo->currentIndex(), 0, 5);
    values.TILE_RENDER_SCALE = scaleValues[scaleIdx];
    {
        const int mapScaleVals[] = {1, 2, 4, 8};
        int idx = std::clamp(ui->shadowMapScaleCombo->currentIndex(), 0, 3);
        values.SHADOW_MAP_SCALE = mapScaleVals[idx];
    }
    values.SHADOW_LEVEL = ui->shadowLevelSpin->value();
    values.MINIMUM_SCALE_LEVEL = ui->minScaleSpin->value();
    values.MAXIMUM_SCALE_LEVEL = ui->maxScaleSpin->value();
    values.GRID_LINE_COLOR = ui->gridColorEdit->text();
    values.VOID_MAP_COLOR = ui->voidColorEdit->text();
    values.ACTOR_RENDER_STYLE = ui->actorStyleCombo->currentIndex();
    values.ACTOR_BORDER_WIDTH = ui->actorBorderWidthSpin->value();
    values.ACTOR_BORDER_COLOR = ui->actorBorderColorEdit->text();
    values.CHUNK_EDITOR_HIGHLIGHT_COLOR = ui->chunkEditorColorEdit->text();
    values.CHUNK_EDITOR_HIGHLIGHT_WIDTH = ui->chunkEditorWidthSpin->value();
    values.VOXEL_SELECTION_COLOR = ui->voxelSelectionColorEdit->text();

    values.REGION_CACHE_SIZE = ui->regionCacheSpin->value();
    values.EMPTY_REGION_CACHE_SIZE = ui->emptyCacheSpin->value();
    values.THREAD_NUM = ui->threadNumSpin->value();

    values.LOAD_GLOBAL_DATA = ui->loadGlobalDataCheck->isChecked();
    values.MAX_GLOBAL_DATA_LOAD_COUNT = ui->maxGlobalDataSpin->value();
    values.ICON_THEME = ui->iconThemeCombo->currentText();

    values.PRELOAD_ALL_CHUNK_COORDS = ui->preloadCoordsCheck->isChecked();
    values.GPU_RENDER_ENABLED = ui->gpuRenderCheck->isChecked();
    values.GPU_AO_STRENGTH = static_cast<float>(ui->gpuAoSpin->value());
    values.GPU_BEVEL_STRENGTH = static_cast<float>(ui->gpuBevelSpin->value());
    values.GPU_SATURATION = static_cast<float>(ui->gpuSaturationSpin->value());
    values.GPU_BRIGHTNESS = static_cast<float>(ui->gpuBrightnessSpin->value());
    values.GPU_SHADOW_STRENGTH = static_cast<float>(ui->gpuShadowSpin->value());

    values.LANGUAGE = ui->langCombo->currentIndex() == 1 ? "en" : "zh_CN";

    setting::apply(values);
    setting::save(values);
}
