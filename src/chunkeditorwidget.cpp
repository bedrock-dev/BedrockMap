#include "chunkeditorwidget.h"

#include <qcontainerfwd.h>
#include <qglobal.h>
#include <qnamespace.h>
#include <winscard.h>

#include <QCryptographicHash>
#include <QFileDialog>
#include <QMessageBox>
#include <QMouseEvent>
#include <QToolTip>
#include <memory>
#include <vector>

#include "actor.h"
#include "bedrock_key.h"
#include "chunk.h"
#include "chunksectionwidget.h"
#include "include/msg.h"
#include "loguru/loguru.hpp"
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

ChunkEditorWidget::ChunkEditorWidget(QWidget *parent, AsyncLevelLoader *levelLoader)
    : QWidget(parent), ui(new Ui::ChunkEditorWidget), level_loader_(levelLoader) {
    ui->setupUi(this);
    // 3D render widget — no parent so it opens as a standalone window
    this->terrain_render_widget_ = new class VoxelWidget(nullptr);
    // terrain tab
    ui->base_info_label->clear();
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

    ui->block_actor_tab->layout()->replaceWidget(ui->empty_block_actor_editor_widget, this->block_entity_editor_);
    ui->actor_tab->layout()->replaceWidget(ui->empyt_actor_editor_widget, this->actor_editor_);
    ui->pt_tab->layout()->replaceWidget(ui->empty_pt_editor_widget, this->pending_tick_editor_);

    // dirty indicator on tab names
    auto setupDirtyTab = [this](NbtWidget *editor, QWidget *tab) {
        connect(editor, &NbtWidget::nbtModified, this, [this, editor, tab]() {
            int idx = ui->tabWidget->indexOf(tab);
            if (idx < 0) return;
            auto text = ui->tabWidget->tabText(idx);
            if (editor->dirty()) {
                if (!text.endsWith(" *")) text += " *";
            } else {
                if (text.endsWith(" *")) text.chop(2);
            }
            ui->tabWidget->setTabText(idx, text);
            setDirty(true);
        });
    };
    setupDirtyTab(this->actor_editor_, ui->actor_tab);
    setupDirtyTab(this->block_entity_editor_, ui->block_actor_tab);
    setupDirtyTab(this->pending_tick_editor_, ui->pt_tab);
    //
    QFont f;
    f.setFamily("JetBrains Mono");
    ui->stats_label->setFont(f);
}

ChunkEditorWidget::~ChunkEditorWidget() {
    this->clearData();
    delete terrain_render_widget_;
    // this->mw_->mapWidget()->unselectChunk();
    delete ui;
}

void ChunkEditorWidget::loadChunkData(bl::raw_chunk raw) {
    this->clearData();
    this->raw_chunk_ = std::move(raw);
    this->has_chunk_ = true;
    auto ptr = std::make_unique<bl::chunk>(this->raw_chunk_.pos());
    auto *chunk = ptr.get();
    if (!chunk->load_from_raw_chunk(this->raw_chunk_)) return;

    this->cv = chunk->get_version();
    this->cp_ = chunk->get_pos();
    this->refreshBasicData();

    // load data
    LOG_F(INFO, "Load basic chunk data");
    this->chunk_section_->load_data(chunk);
    int cnt{0};
    int index = 0;

    auto data = VoxelWidget::createVoxelDataFromChunks({{chunk}}, [](int) {});
    this->terrain_render_widget_->updateVoxelData(data);
    this->terrain_render_widget_->setWindowTitle(chunk->get_pos().to_string().c_str());
    {
        LOG_F(INFO, "Load chunk block entity data");
        std::vector<NBTListItem *> block_entity_items;
        auto &bes = chunk->block_entities();
        for (auto &b : bes) {
            auto id_tag = b->get("id");
            QString name = "unknown";
            if (id_tag && id_tag->type() == bl::palette::tag_type::String) {
                name = dynamic_cast<bl::palette::string_tag *>(id_tag)->value.c_str();
            }
            auto *item = NBTListItem::from(dynamic_cast<bl::palette::compound_tag *>(b->copy()), name, QString::number(index));
            item->setIcon(QIcon(QPixmap::fromImage(*BlockActorNBTIcon(name.toLower().replace("minecraft:", "")))));
            block_entity_items.push_back(item);
            index++;
        }
        this->block_entity_editor_->loadNewData(block_entity_items);
    }

    {
        LOG_F(INFO, "Load Chunk pending tick data");
        std::vector<NBTListItem *> pt_items;
        auto &pts = chunk->pending_ticks();
        index = 0;
        for (auto &b : pts) {
            auto *item =
                NBTListItem::from(dynamic_cast<bl::palette::compound_tag *>(b->copy()), QString::number(index), QString::number(index));
            pt_items.push_back(item);
            index++;
        }
        this->pending_tick_editor_->loadNewData(pt_items);
    }

    {
        LOG_F(INFO, "Load chunk actors data");
        auto actors = chunk->entities();
        std::vector<NBTListItem *> actor_items;
        index = 0;
        for (auto &b : actors) {
            auto id = QString(b->identifier().c_str()).replace("minecraft:", "");
            auto *item = NBTListItem::from(dynamic_cast<bl::palette::compound_tag *>(b->root()->copy()), id, QString::number(index));
            item->setIcon(QIcon(QPixmap::fromImage(*EntityNBTIcon(id))));
            actor_items.push_back(item);
            index++;
        }
        this->actor_editor_->loadNewData(actor_items);
    }

    // stats
    {
        auto shortHash = [](const std::string &data) {
            auto hash =
                QCryptographicHash::hash(QByteArray::fromRawData(data.data(), static_cast<int>(data.size())), QCryptographicHash::Sha256);
            return QString(hash.toHex().left(8));
        };

        QStringList lines;
        for (auto &[kt, data] : this->raw_chunk_.get_normal_data()) {
            lines.append(
                QString("%1: %2 bytes [%3]").arg(bl::chunk_key::chunk_key_to_str(kt).c_str()).arg(data.size()).arg(shortHash(data)));
        }
        for (auto &[y, data] : this->raw_chunk_.get_sub_chunks()) {
            if (!data.empty())
                lines.append(
                    QString("SubChunk[%1 ~ %2]: %3 bytes [%4]").arg(y * 16).arg((y + 1) * 16 - 1).arg(data.size()).arg(shortHash(data)));
        }
        if (!this->raw_chunk_.get_actor_digest().empty()) {
            auto &d = this->raw_chunk_.get_actor_digest();
            lines.append(QString("ActorDigest: %1 bytes [%2]").arg(d.size()).arg(shortHash(d)));
        }
        for (auto &[uid, data] : this->raw_chunk_.get_entities()) {
            if (uid.size() != 8) {
                LOG_F(WARNING, "Entity uid size is %zu, expected 8, skipping", uid.size());
                continue;
            }
            qulonglong uidVal = 0;
            memcpy(&uidVal, uid.data(), 8);
            lines.append(QString("Entity[%1]: %2 bytes [%3]").arg(uidVal, 16, 16, QChar('0')).arg(data.size()).arg(shortHash(data)));
        }
        ui->stats_label->setText(lines.join('\n'));
    }
}

void ChunkEditorWidget::on_close_btn_clicked() {
    terrain_render_widget_->hide();
    hide();
}

void ChunkEditorWidget::refreshBasicData() {
    LOG_F(INFO, "Refresh basic data");
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

void ChunkEditorWidget::mousePressEvent(QMouseEvent *event) {}

void ChunkEditorWidget::clearData() {
    this->actor_editor_->clearData();
    this->block_entity_editor_->clearData();
    this->pending_tick_editor_->clearData();
    this->has_chunk_ = false;
}

void ChunkEditorWidget::on_locate_btn_clicked() { emit locateChunk(cp_.x, cp_.z, cp_.dim); }

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

void ChunkEditorWidget::on_export_btn_clicked() {
    if (!this->has_chunk_) return;
    auto data = this->raw_chunk_.to_raw();
    if (data.empty()) return;

    auto fileName = QFileDialog::getSaveFileName(this, "", QString(), "Chunk Files (*.bchk)");
    if (fileName.isEmpty()) return;

    QFile file(fileName);
    if (file.open(QIODevice::WriteOnly)) {
        file.write(data.data(), static_cast<qint64>(data.size()));
        file.close();
    }
}

void ChunkEditorWidget::on_import_btn_clicked() {
    auto fileName = QFileDialog::getOpenFileName(this, "", QString(), "Chunk Files (*.bchk)");
    if (fileName.isEmpty()) return;

    QFile file(fileName);
    if (!file.open(QIODevice::ReadOnly)) return;

    auto data = file.readAll();
    file.close();

    std::vector<byte_t> raw(data.begin(), data.end());
    bl::raw_chunk chunk;
    if (!chunk.from_raw(raw)) {
        WARN(msg::INVALID_CHUNK_FORMAT());
        return;
    }
    // instead with update_position
    chunk.set_pos(this->raw_chunk_.pos(), &this->level_loader_->level());
    this->loadChunkData(std::move(chunk));
    setDirty(true);
}

void ChunkEditorWidget::on_view_3d_btn_clicked() {
    if (!has_chunk_ || !terrain_render_widget_) return;
    terrain_render_widget_->resize(300, 400);
    terrain_render_widget_->show();
    terrain_render_widget_->raise();
}

void ChunkEditorWidget::on_save_btn_clicked() {
    if (!dirty_) {
        INFO(msg::NOTHING_TO_SAVE());
        return;
    }

    // collect entities and pts
    auto pt = this->pending_tick_editor_->getCurrentPaletteRaw();
    raw_chunk_.set_normal(bl::chunk_key::PendingTicks, pt);

    auto be = this->block_entity_editor_->getCurrentPaletteRaw();
    raw_chunk_.set_normal(bl::chunk_key::BlockEntity, be);

    auto actors_palette = this->actor_editor_->getPaletteCopy();
    std::vector<bl::actor *> actors;
    for (const auto &nbt : actors_palette) {
        auto *actor = new bl::actor();
        if (actor->load_from_nbt(nbt)) {
            actors.push_back(actor);
        }
    }
    raw_chunk_.set_entities(actors);
    // write
    this->level_loader_->putRawChunk(this->raw_chunk_);
    actor_editor_->clearModifyCache();
    pending_tick_editor_->clearModifyCache();
    block_entity_editor_->clearModifyCache();
    for (auto *actor : actors) delete actor;
    setDirty(false);
}
