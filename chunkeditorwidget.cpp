#include "chunkeditorwidget.h"

#include <QMessageBox>
#include <QtDebug>

#include "chunksectionwidget.h"
#include "nbtwidget.h"
#include "ui_chunkeditorwidget.h"
#include <QToolTip>
#include <QMouseEvent>


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
    this->pending_tick_editor_ = new NbtWidget();
    this->block_entity_editor_ = new NbtWidget();

    this->actor_editor_->hideLoadDataBtn();
    this->pending_tick_editor_->hideLoadDataBtn();
    this->block_entity_editor_->hideLoadDataBtn();

    ui->actor_tab->layout()->addWidget(this->actor_editor_);
    ui->pt_tab->layout()->addWidget(this->pending_tick_editor_);
    ui->block_actor_tab->layout()->addWidget(this->block_entity_editor_);
    this->setMouseTracking(true);
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
        qDebug() << "read basic data" << "pt size is " << chunk->pending_ticks().size() << "  be size is"
                 << chunk->block_entities().size();
        this->pending_tick_editor_->load_new_data(chunk_->pending_ticks());
        this->block_entity_editor_->load_new_data(chunk_->block_entities());
    } else {
        QMessageBox::information(nullptr, "警告", "这是一个空区块", QMessageBox::Yes, QMessageBox::Yes);
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

void ChunkEditorWidget::mousePressEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton) {
        showInfoPopMenu();
    }
}

void ChunkEditorWidget::showInfoPopMenu() {}

void ChunkEditorWidget::on_locate_btn_clicked() {}
