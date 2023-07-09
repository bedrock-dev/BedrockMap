#include "chunkeditorwidget.h"

#include <QMessageBox>
#include <QtDebug>

#include "chunksectionwidget.h"
#include "nbtwidget.h"
#include "ui_chunkeditorwidget.h"
#include <QToolTip>
#include <QMouseEvent>
#include "mainwindow.h"

namespace {
    void WARN(const QString &msg) {
        QMessageBox::warning(nullptr, "警告", msg, QMessageBox::Yes, QMessageBox::Yes);
    }

    void INFO(const QString &msg) {
        QMessageBox::information(nullptr, "信息", msg, QMessageBox::Yes, QMessageBox::Yes);
    }

}


ChunkEditorWidget::ChunkEditorWidget(MainWindow *mw, QWidget *parent) : QWidget(parent), ui(new Ui::ChunkEditorWidget),
                                                                        mw_(mw) {
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

    //

    ui->block_actor_tab->layout()->replaceWidget(ui->empty_block_actor_editor_widget, this->block_entity_editor_);
    ui->actor_tab->layout()->replaceWidget(ui->empyt_actor_editor_widget, this->actor_editor_);
    ui->pt_tab->layout()->replaceWidget(ui->empty_pt_editor_widget, this->pending_tick_editor_);
}

ChunkEditorWidget::~ChunkEditorWidget() { delete ui; }


void ChunkEditorWidget::load_chunk_data(bl::chunk *chunk) {
    if (chunk) {
        this->chunk_ = chunk;
        this->refreshData();
        this->chunk_section_->set_chunk(chunk);
        this->chunk_section_->setDrawType(ChunkSectionWidget::DrawType::Biome);
        this->chunk_section_->setYLevel(0);
        this->block_entity_editor_->load_new_data(chunk_->block_entities(),
                                                  [](bl::palette::compound_tag *nbt) {
                                                      using namespace bl::palette;
                                                      if (!nbt) return QString();
                                                      auto id_tag = nbt->get("id");
                                                      QString name = "unknown";
                                                      if (id_tag && id_tag->type() == tag_type::String) {
                                                          name = dynamic_cast<string_tag *>(id_tag)->value.c_str();
                                                      }

                                                      QString x = "#";
                                                      QString y = "#";
                                                      QString z = "#";
                                                      auto x_tag = nbt->get("x");
                                                      auto y_tag = nbt->get("y");
                                                      auto z_tag = nbt->get("z");
                                                      if (x_tag && x_tag->type() == tag_type::Int) {
                                                          x = QString::number(dynamic_cast<int_tag *>(x_tag)->value);
                                                      }

                                                      if (y_tag && y_tag->type() == tag_type::Int) {
                                                          y = QString::number(dynamic_cast<int_tag *>(y_tag)->value);
                                                      }

                                                      if (z_tag && z_tag->type() == tag_type::Int) {
                                                          z = QString::number(dynamic_cast<int_tag *>(z_tag)->value);
                                                      }
                                                      return name + "[" + x + "," + y + "," + z + "]";
                                                  }, {});

        this->pending_tick_editor_->load_new_data(chunk_->pending_ticks(),
                                                  [](auto *nbt) { return QString(); }, {});
    } else {
        QMessageBox::information(nullptr, "警告", "这是一个空区块", QMessageBox::Yes, QMessageBox::Yes);
    }
}

void ChunkEditorWidget::on_close_btn_clicked() { this->setVisible(false); }

void ChunkEditorWidget::refreshData() {
    if (!this->chunk_) return;
    ui->base_info_label->setText(this->chunk_->get_pos().to_string().c_str());
    auto [miny, maxy] = this->chunk_->get_pos().get_y_range(this->chunk_->get_version());
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


void ChunkEditorWidget::on_save_actor_btn_clicked() {

    if (!this->mw_->enable_write()) {
        WARN("未开启写模式");
        return;
    }

}

void ChunkEditorWidget::on_save_block_actor_btn_clicked() {
    if (!this->mw_->enable_write()) {
        WARN("未开启写模式");
        return;
    }

    if (this->mw_->get_world()->getLevelLoader().
            modifyChunkBlockEntities(this->chunk_->get_pos(),
                                     this->block_entity_editor_->getCurrentPaletteRaw())) {
        INFO("写入方块实体数据成功");
    } else {
        WARN("写入方块实体数据失败");
    }
}


void ChunkEditorWidget::on_save_pt_btn_clicked() {
    if (!this->mw_->enable_write()) {
        WARN("未开启写模式");
        return;
    }

    if (this->mw_->get_world()->getLevelLoader().
            modifyChunkPendingTicks(this->chunk_->get_pos(),
                                    this->pending_tick_editor_->getCurrentPaletteRaw())) {
        INFO("写入计划刻数据成功");
    } else {
        WARN("写入计划刻数据失败");
    }
}

void ChunkEditorWidget::clearAll() {
}


