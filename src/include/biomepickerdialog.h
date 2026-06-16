#ifndef BIOMEPICKERDIALOG_H
#define BIOMEPICKERDIALOG_H

#include <QDialog>

#include "data_3d.h"

namespace Ui {
    class BiomePickerDialog;
}

class BiomePickerDialog : public QDialog {
    Q_OBJECT

   public:
    explicit BiomePickerDialog(QWidget *parent = nullptr);
    ~BiomePickerDialog();

    [[nodiscard]] bl::biome selectedBiome() const;
    [[nodiscard]] bool hasSelection() const;

   private:
    void populateTables();
    static std::vector<bl::biome> allBiomes();

    Ui::BiomePickerDialog *ui;
    bl::biome selected_{bl::biome::none};
};

#endif  // BIOMEPICKERDIALOG_H
