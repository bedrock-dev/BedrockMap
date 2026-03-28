#include "chunkeditorwidget.h"

#include <qglobal.h>
#include <qnamespace.h>

#include <QFileDialog>
#include <QMessageBox>
#include <QMouseEvent>
#include <QToolTip>
#include <QtDebug>
#include <vector>

#include "bedrock_key.h"
#include "chunksectionwidget.h"
#include "mainwindow.h"
#include "msg.h"
#include "nbtwidget.h"
#include "palette.h"
#include "resourcemanager.h"
#include "ui_chunkeditorwidget.h"
#include "voxelwidget.h"

namespace {
    bool load_raw(leveldb::DB *&db, const std::string &raw_key, std::string &raw) {
        auto r = db->Get(leveldb::ReadOptions(), raw_key, &raw);
        return r.ok();
    }
}  // namespace

ChunkEditorWidget::ChunkEditorWidget(MainWindow *mw, QWidget *parent) : QWidget(parent), ui(new Ui::ChunkEditorWidget), mw_(mw) {
    ui->setupUi(this);
    // render view
    this->terrain_render_widget_ = new class VoxelWidget(this);
    // terrain tab
    ui->base_info_label->setText("无数据");
    this->chunk_section_ = new ChunkSectionWidget();

    ui->terrain_tab->layout()->addWidget(this->chunk_section_);
    ui->terrain_level_slider->setRange(-64, 319);
    ui->terrain_level_slider->setSingleStep(1);
    ui->terrain_level_edit->setRange(-64, 319);
    // actor tab
    this->actor_editor_ = new NbtWidget();
    this->pending_tick_editor_ = new NbtWidget();
    this->block_entity_editor_ = new NbtWidget();
    this->actor_editor_->hideLoadDataBtn();
    this->pending_tick_editor_->hideLoadDataBtn();
    this->block_entity_editor_->hideLoadDataBtn();

    this->actor_editor_->setEnableModifyCache(false);
    this->pending_tick_editor_->setEnableModifyCache(false);
    this->block_entity_editor_->setEnableModifyCache(false);
    //

    ui->render_view_tab->layout()->replaceWidget(ui->empty_3d_view_widget, this->terrain_render_widget_);
    ui->block_actor_tab->layout()->replaceWidget(ui->empty_block_actor_editor_widget, this->block_entity_editor_);
    ui->actor_tab->layout()->replaceWidget(ui->empyt_actor_editor_widget, this->actor_editor_);
    ui->pt_tab->layout()->replaceWidget(ui->empty_pt_editor_widget, this->pending_tick_editor_);

    // terrain_render_widget_->setAttribute(Qt::WA_NativeWindow);
    // terrain_render_widget_->setAttribute(Qt::WA_OpaquePaintEvent);

    // connect(ui->tabWidget, &QTabWidget::currentChanged, this, [this](int index) {
    //     terrain_render_widget_->makeCurrent();
    //     terrain_render_widget_->doneCurrent();
    //     terrain_render_widget_->update();
    // });
}

ChunkEditorWidget::~ChunkEditorWidget() {
    this->clearData();
    this->mw_->mapWidget()->unselectChunk();
    delete ui;
}

void ChunkEditorWidget::loadChunkData(bl::chunk *chunk) {
    assert(chunk);
    this->clearData();
    this->cv = chunk->get_version();
    this->cp_ = chunk->get_pos();
    this->refreshBasicData();

    // load data
    this->chunk_section_->load_data(chunk);
    int cnt{0};
    auto data = VoxelWidget::createVoxelDataFromChunks({{chunk}}, {});
    this->terrain_render_widget_->updateVoxelData(data);

    std::vector<NBTListItem *> block_entity_items;
    auto &bes = chunk->block_entities();
    int index = 0;
    for (auto &b : bes) {
        auto id_tag = b->get("id");
        QString name = "unknown";
        if (id_tag && id_tag->type() == bl::palette::tag_type::String) {
            name = dynamic_cast<bl::palette::string_tag *>(id_tag)->value.c_str();
        }
        auto *item = NBTListItem::from(b, name, QString::number(index));
        item->setIcon(QIcon(QPixmap::fromImage(*BlockActorNBTIcon(name.toLower().replace("minecraft:", "")))));
        block_entity_items.push_back(item);
        index++;
    }

    this->block_entity_editor_->loadNewData(block_entity_items);
    qDebug() << "Load pending tick data";
    std::vector<NBTListItem *> pt_items;
    auto &pts = chunk->pending_ticks();
    index = 0;
    for (auto &b : pts) {
        auto *item = NBTListItem::from(b, QString::number(index), QString::number(index));
        pt_items.push_back(item);
        index++;
    }

    this->pending_tick_editor_->loadNewData(pt_items);
    qDebug() << "Load actors";

    auto actors = chunk->entities();
    std::vector<NBTListItem *> actor_items;
    index = 0;
    for (auto &b : actors) {
        auto id = QString(b->identifier().c_str()).replace("minecraft:", "");
        auto *item = NBTListItem::from(reinterpret_cast<bl::palette::compound_tag *>(b->root()->copy()), id, QString::number(index));
        item->setIcon(QIcon(QPixmap::fromImage(*EntityNBTIcon(id))));
        actor_items.push_back(item);
        index++;
    }

    this->actor_editor_->loadNewData(actor_items);

    // prevent data destructor
    chunk->pending_ticks().clear();
    chunk->block_entities().clear();
    delete chunk;
}

void ChunkEditorWidget::on_close_btn_clicked() {
    this->clearData();
    this->mw_->mapWidget()->unselectChunk();
    this->setVisible(false);
}

void ChunkEditorWidget::refreshBasicData() {
    qDebug() << "Refresh basic data";
    auto [miny, maxy] = this->cp_.get_y_range(this->cv);

    auto label =
        QString("%1, %2 / [%3 ~ %4]").arg(QString::number(cp_.x), QString::number(cp_.z), QString::number(miny), QString::number(maxy));
    ui->base_info_label->setText(label);
    ui->terrain_level_slider->setRange(miny, maxy);
}

void ChunkEditorWidget::on_terrain_level_slider_valueChanged(int value) {
    ui->terrain_level_edit->setValue(ui->terrain_level_slider->value());
    auto y = ui->terrain_level_edit->value();
    this->chunk_section_->setYLevel(y);
    this->chunk_section_->update();
}

// void ChunkEditorWidget::on_terrain_goto_level_btn_clicked() {
//     auto y = ui->terrain_level_edit->text().toInt();
//     auto [miny, maxy] = this->cp_.get_y_range(this->cv);
//     if (y > maxy) y = maxy;
//     if (y < miny) y = miny;
//     ui->terrain_level_edit->setValue(7);
//     ui->terrain_level_slider->setValue(y);
//     this->chunk_section_->setYLevel(y);
// }

void ChunkEditorWidget::mousePressEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton) {
    }
}

void ChunkEditorWidget::on_save_actor_btn_clicked() {
    if (!CHECK_CONDITION(this->mw_->enable_write(), "未开启写模式")) return;

    auto palettes = this->actor_editor_->getPaletteCopy();
    std::vector<bl::actor *> actors;
    for (auto &pa : palettes) {
        if (!pa) continue;
        auto *a = new bl::actor;
        if (a->load_from_nbt(pa)) {
            actors.push_back(a);
        }
    }

    auto res = this->mw_->levelLoader()->modifyChunkActors(this->cp_, this->cv, actors);
    for (auto &p : palettes) delete p;
    for (auto &ac : actors) delete ac;
    CHECK_DATA_SAVE(res);
}

void ChunkEditorWidget::on_save_block_actor_btn_clicked() {
    if (!CHECK_CONDITION(this->mw_->enable_write(), "未开启写模式")) return;
    CHECK_DATA_SAVE(this->mw_->levelLoader()->modifyChunkBlockEntities(this->cp_, this->block_entity_editor_->getCurrentPaletteRaw()));
}

void ChunkEditorWidget::on_save_pt_btn_clicked() {
    if (!CHECK_CONDITION(this->mw_->enable_write(), "未开启写模式")) return;
    CHECK_DATA_SAVE(this->mw_->levelLoader()->modifyChunkPendingTicks(this->cp_, this->pending_tick_editor_->getCurrentPaletteRaw()));
}

void ChunkEditorWidget::clearData() {
    qDebug() << "Clear chunk data";
    this->actor_editor_->clearData();
    this->block_entity_editor_->clearData();
    this->pending_tick_editor_->clearData();
}

void ChunkEditorWidget::on_locate_btn_clicked() { this->mw_->mapWidget()->gotoBlockPos(cp_.x * 16 + 8, cp_.z * 16 + 8); }

void ChunkEditorWidget::on_terrain_show_grid_cb_stateChanged(int arg1) {
    this->chunk_section_->setDrawGrid(ui->terrain_show_grid_cb->isChecked());
    this->chunk_section_->update();
}

void ChunkEditorWidget::on_terrain_level_edit_valueChanged(int arg1) {
    ui->terrain_level_slider->setValue(ui->terrain_level_edit->value());
    auto y = ui->terrain_level_edit->value();
    this->chunk_section_->setYLevel(y);
    this->chunk_section_->update();
}

void ChunkEditorWidget::on_dump_btn_clicked() {
    auto *level_loader = this->mw_->levelLoader();
    if (!CHECK_CONDITION(level_loader && level_loader->isOpen(), "未打开存档")) return;
    auto pos = this->cp_;
    QString dir = QFileDialog::getExistingDirectory(this, "选择导出目录", "", QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
    if (dir.size() == 0) return;

    auto begin = static_cast<int>(bl::chunk_key::Data3D);
    auto end = static_cast<int>(bl::chunk_key::ActorDigestVersion);
    qDebug() << "Dump chunk data to " << dir;
    for (auto i = begin; i <= end; i++) {
        auto type = static_cast<bl::chunk_key::key_type>(i);
        if (type == bl::chunk_key::SubChunkTerrain) continue;
        bl::chunk_key key{type, pos};
        std::string raw;
        auto dir_name = bl::chunk_key::chunk_key_to_str(type);
        if (!load_raw(level_loader->level().db(), key.to_raw(), raw)) continue;
        qDebug() << " - " << key.to_string().c_str();
        QFile f(dir + "/" + dir_name.c_str() + ".bin");
        if (!f.open(QIODevice::WriteOnly)) {
            WARN("无法打开文件 " + f.fileName());
            continue;
        }
        f.write(raw.data(), static_cast<qint64>(raw.size()));
        f.close();
    }
}
