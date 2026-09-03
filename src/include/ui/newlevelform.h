#ifndef BEDROCKMAP_NEWLEVELFORM_H
#define BEDROCKMAP_NEWLEVELFORM_H

#include <QDialog>

#include "data_3d.h"
#include "leveloperator.h"

namespace Ui {
    class NewLevelForm;
}

class NewLevelForm : public QDialog {
    Q_OBJECT

   public:
    explicit NewLevelForm(QWidget *parent = nullptr);
    ~NewLevelForm();

    NewLevelParams params() const;

   private:
    Ui::NewLevelForm *ui;
    bl::biome selectedBiome_ = bl::biome::plains;
};

#endif  // BEDROCKMAP_NEWLEVELFORM_H
