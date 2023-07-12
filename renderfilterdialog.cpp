#include "renderfilterdialog.h"
#include "ui_renderfilterdialog.h"

RenderFilterDialog::RenderFilterDialog(QWidget *parent) :
        QDialog(parent),
        ui(new Ui::RenderFilterDialog) {
    ui->setupUi(this);
    this->setWindowTitle("配置地图过滤器");
    ui->layer_slider->setSingleStep(1);
    ui->layer_slider->setRange(-63, 319);
}

RenderFilterDialog::~RenderFilterDialog() {
    delete ui;
}

void RenderFilterDialog::fillInUI() {
    //layer
    ui->layer_slider->setValue(this->filter_.layer);
    ui->enable_layer_box->setChecked(this->filter_.enable_layer_);
    ui->block_black_box->setChecked(this->filter_.block_black_mode_);
    ui->biome_black_box->setChecked(this->filter_.biome_black_mode_);
    ui->actor_black_box->setChecked(this->filter_.actor_black_mode_);

    QStringList actor_list;
    for (const auto &actor: this->filter_.actors_list_)actor_list << actor.c_str();
    ui->actor_text_edit->setPlainText(actor_list.join(','));

    QStringList block_list;
    for (const auto &block: this->filter_.blocks_list_)block_list << block.c_str();
    ui->block_text_edit->setPlainText(block_list.join(','));

    QStringList biome_list;
    for (const auto &biome: this->filter_.biomes_list_)biome_list << QString::number(biome);
    ui->biome_text_edit->setPlainText(biome_list.join(','));
}

void RenderFilterDialog::collectFilerData() {

    this->filter_.layer = ui->layer_slider->value();
    this->filter_.enable_layer_ = ui->enable_layer_box->isChecked();
    this->filter_.block_black_mode_ = ui->block_black_box->isChecked();
    this->filter_.actor_black_mode_ = ui->actor_black_box->isChecked();
    this->filter_.biome_black_mode_ = ui->biome_black_box->isChecked();

    auto blocks = ui->block_text_edit->toPlainText().trimmed().split(",");
    auto biomes = ui->biome_text_edit->toPlainText().trimmed().split(",");
    qDebug() << "biomes size: " << biomes;
    auto actors = ui->actor_text_edit->toPlainText().trimmed().split(",");

    this->filter_.blocks_list_.clear();
    this->filter_.biomes_list_.clear();
    this->filter_.actors_list_.clear();


    for (const auto &b: blocks) {
        auto s = b.trimmed();
        if (!s.isEmpty())this->filter_.blocks_list_.insert(s.toStdString());
    }
    for (const auto &b: biomes) {
        auto s = b.trimmed();
        if (!s.isEmpty())this->filter_.biomes_list_.insert(s.toInt());
    }
    for (const auto &b: actors) {
        auto s = b.trimmed();
        if (!s.isEmpty())this->filter_.actors_list_.insert(s.toStdString());
    }

}

void RenderFilterDialog::on_current_layer_lineedit_textEdited(const QString &arg1) {
    ui->layer_slider->setValue(ui->current_layer_lineedit->text().toInt());
}

void RenderFilterDialog::on_layer_slider_valueChanged(int value) {
    ui->current_layer_lineedit->setText(QString::number(value));
}