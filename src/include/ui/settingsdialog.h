#ifndef BEDROCKMAP_SETTINGSDIALOG_H
#define BEDROCKMAP_SETTINGSDIALOG_H

#include <QColor>
#include <QDialog>
#include <QTreeWidgetItem>

namespace Ui {
    class SettingsDialog;
}

class SettingsDialog : public QDialog {
    Q_OBJECT

   public:
    explicit SettingsDialog(QWidget* parent = nullptr);
    ~SettingsDialog() override;

   private slots:
    void onCategoryChanged(QTreeWidgetItem* current, QTreeWidgetItem* previous);
    void onPickColor(QLineEdit* edit, bool withAlpha = false);
    void onSave();

    // Color picker helpers
    void onGridColorPick();
    void onVoidColorPick();
    void onActorBorderColorPick();
    void onChunkEditorColorPick();
    void onVoxelSelectionColorPick();

    /// Set the shadow strength on both of its controls.
    void setGpuShadowStrength(double value);

    /// Set the AO strength on both of its controls.
    void setGpuAoStrength(double value);

    /// Set the bevel strength on both of its controls.
    void setGpuBevelStrength(double value);

    /// Set the bevel width multiplier on both of its controls.
    void setGpuBevelWidth(double value);

    /// Set the saturation on both of its controls.
    void setGpuSaturation(double value);

    /// Set the brightness on both of its controls.
    void setGpuBrightness(double value);

    /// Set the biome tint interpolation span on both of its controls.
    void setGpuBiomeBlendBlocks(int value);

   private:
    void setupCategories();
    void loadSettings();
    void saveSettings();
    void updateShadowOptions();
    void updateGlobalDataOptions();

    Ui::SettingsDialog* ui;
};

#endif  // BEDROCKMAP_SETTINGSDIALOG_H
