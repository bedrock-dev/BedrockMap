#include "chunkeditorwidget.h"

#include <QMessageBox>

#include "chunksectionwidget.h"
#include "nbtwidget.h"
#include "ui_chunkeditorwidget.h"
ChunkEditorWidget::ChunkEditorWidget(QWidget *parent) : QWidget(parent), ui(new Ui::ChunkEditorWidget) {
    ui->setupUi(this);
    // terrain tab
    ui->base_info_label->setText("无数据");
    this->chunk_section_ = new ChunkSectionWidget();
    ui->terrain_tab->layout()->addWidget(this->chunk_section_);
    ui->terrain_level_slider->setRange(-64, 319);
    ui->terrain_level_slider->setSingleStep(1);
    ui->terrain_level_edit->setText("0");
    // actor tab
    this->actor_editor_ = new NbtWidget();
    ui->actor_tab->layout()->addWidget(this->actor_editor_);
}

ChunkEditorWidget::~ChunkEditorWidget() { delete ui; }

void ChunkEditorWidget::load_chunk_data(bl::chunk *chunk) {
    if (chunk) {
        delete this->chunk_;
        this->chunk_ = chunk;
        this->refreshData();
        this->chunk_section_->set_chunk(chunk);
        this->chunk_section_->setDrawType(ChunkSectionWidget::DrawType::Biome);
        this->chunk_section_->setYLevel(0);
    } else {
        QMessageBox::information(NULL, "警告", "这是一个空区块", QMessageBox::Yes, QMessageBox::Yes);
    }
}

void ChunkEditorWidget::on_close_btn_clicked() { this->setVisible(false); }

void ChunkEditorWidget::refreshData() {
    if (!this->chunk_) return;
    ui->base_info_label->setText(this->chunk_->get_pos().to_string().c_str());
    auto [miny, maxy] = this->chunk_->get_pos().get_y_range();
    ui->terrain_level_slider->setRange(miny, maxy);
}

void ChunkEditorWidget::on_terrain_level_slider_valueChanged(int value) {
    ui->terrain_level_edit->setText(QString::number(ui->terrain_level_slider->value()));
    auto y = ui->terrain_level_edit->text().toInt();
    this->chunk_section_->setYLevel(y);
}

void ChunkEditorWidget::on_terrain_goto_level_btn_clicked() {
    if (!this->chunk_) return;
    auto y = ui->terrain_level_edit->text().toInt();
    this->chunk_section_->setYLevel(y);
}
