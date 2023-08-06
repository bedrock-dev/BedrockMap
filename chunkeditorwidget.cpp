#include "chunkeditorwidget.h"
#include "msg.h"
#include <QMessageBox>
#include <QMouseEvent>
#include <QToolTip>
#include <QtDebug>

#include "chunksectionwidget.h"
#include "iconmanager.h"
#include "mainwindow.h"
#include "nbtwidget.h"
#include "ui_chunkeditorwidget.h"


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

ChunkEditorWidget::~ChunkEditorWidget() {
    this->clearData();
    this->mw_->mapWidget()->unselectChunk();
    delete ui;
}

void ChunkEditorWidget::load_chunk_data(bl::chunk *chunk) {
    assert(chunk);
    this->clearData();
    this->chunk_ = chunk;
    this->refreshBasicData();
    //setup chunk section
    this->chunk_section_->load_data(this->chunk_);
    qDebug() << "Load block actor data";
    auto block_entity_namer = [](bl::palette::compound_tag *nbt) {
        using namespace bl::palette;
        if (!nbt) return QString();
        auto id_tag = nbt->get("id");
        QString name = "unknown";
        if (id_tag && id_tag->type() == tag_type::String) {
            name = dynamic_cast<string_tag *>(id_tag)->value.c_str();
        }
        return name;
    };

    auto bes = chunk_->block_entities();
    std::vector<QImage *> be_icons;
    for (auto &b: bes) {
        auto id_tag = b->get("id");
        QString name = "unknown";
        if (id_tag && id_tag->type() == bl::palette::tag_type::String) {
            name = dynamic_cast<bl::palette::string_tag *>(id_tag)->value.c_str();
        }
        be_icons.push_back(BlockActorNBTIcon(name.toLower().replace("minecraft:", "")));
    }



    this->block_entity_editor_->load_new_data(chunk_->block_entities(), block_entity_namer, {}, be_icons);

    qDebug() << "Load pending tick data";
    this->pending_tick_editor_->load_new_data(chunk_->pending_ticks(), [](auto *nbt) { return QString(); }, {});

    qDebug() << "Load actors";
    auto actors = chunk_->entities();
    std::vector<QImage *> actor_icons;
    std::vector<std::string> actor_default_labels;
    std::vector<bl::palette::compound_tag *> actor_palettes;
    for (auto &b: actors) {
        auto id = QString(b->identifier().c_str()).replace("minecraft:", "");
        actor_icons.push_back(EntityNBTIcon(id));
        actor_palettes.push_back(b->root());
        actor_default_labels.push_back(id.toStdString());
    }
    this->actor_editor_->load_new_data(actor_palettes, [](auto *) { return QString(); }, actor_default_labels,
                                       actor_icons);

}

void ChunkEditorWidget::on_close_btn_clicked() {
    this->clearData();
    this->mw_->mapWidget()->unselectChunk();
    this->setVisible(false);
}

void ChunkEditorWidget::refreshBasicData() {
    qDebug() << "Refresh basic data";
    if (!this->chunk_) return;
    auto [miny, maxy] = this->chunk_->get_pos().get_y_range(this->chunk_->get_version());
    auto cp = this->chunk_->get_pos();

    auto label = QString("%1, %2 / [%3 ~ %4]").arg(QString::number(cp.x),
                                                   QString::number(cp.z),
                                                   QString::number(miny),
                                                   QString::number(maxy)
    );
    ui->base_info_label->setText(label);
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
    auto [miny, maxy] = this->chunk_->get_pos().get_y_range(this->chunk_->get_version());
    if (y > maxy) y = maxy;
    if (y < miny) y = miny;
    ui->terrain_level_edit->setText(QString::number(y));
    ui->terrain_level_slider->setValue(y);
    this->chunk_section_->setYLevel(y);
}

void ChunkEditorWidget::mousePressEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton) {
    }
}


void ChunkEditorWidget::on_save_actor_btn_clicked() {
    if (!this->mw_->enable_write()) {
        WARN("未开启写模式");
        return;
    }
    auto palettes = this->actor_editor_->getPaletteCopy();
    std::vector<bl::actor *> actors;
    for (auto &pa: palettes) {
        if (!pa)continue;
        auto *a = new bl::actor;
        if (a->load_from_nbt(pa)) {
            actors.push_back(a);
        }
    }

    auto res = this->mw_->levelLoader()->modifyChunkActors(this->chunk_, actors);
    for (auto &p: palettes)delete p;
    for (auto &ac: actors)delete ac;
    if (res) {
        INFO("写入实体数据成功");
    } else {
        WARN("写入实体数据失败");
    }
}

void ChunkEditorWidget::on_save_block_actor_btn_clicked() {
    if (!this->mw_->enable_write()) {
        WARN("未开启写模式");
        return;
    }

    if (this->mw_->levelLoader()->
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

    if (this->mw_->levelLoader()->
            modifyChunkPendingTicks(this->chunk_->get_pos(),
                                    this->pending_tick_editor_->getCurrentPaletteRaw())) {
        INFO("写入计划刻数据成功");
    } else {
        WARN("写入计划刻数据失败");
    }
}


void ChunkEditorWidget::clearData() {
    qDebug() << "Clear chunk data";
    this->actor_editor_->clearData();
    this->block_entity_editor_->clearData();
    this->pending_tick_editor_->clearData();
    delete this->chunk_;
    this->chunk_ = nullptr;
}

void ChunkEditorWidget::on_locate_btn_clicked() {
    if (!this->chunk_) return;
    auto pos = this->chunk_->get_pos();
    this->mw_->mapWidget()->gotoBlockPos(pos.x * 16 + 8, pos.z * 16 + 8);
}

