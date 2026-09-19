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

   private:
    void setupCategories();
    void loadSettings();
    void saveSettings();
    void updateShadowOptions();
    void updateGlobalDataOptions();

    Ui::SettingsDialog* ui;
};

#endif  // BEDROCKMAP_SETTINGSDIALOG_H
