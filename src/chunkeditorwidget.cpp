#include "chunkeditorwidget.h"

#include <qcontainerfwd.h>
#include <qglobal.h>
#include <qnamespace.h>

#include <QCryptographicHash>
#include <QFileDialog>
#include <QHideEvent>
#include <QLabel>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPushButton>
#include <QStackedWidget>
#include <QToolTip>
#include <QVBoxLayout>
#include <memory>
#include <vector>

#include "actor.h"
#include "bedrock_key.h"
#include "chunk.h"
#include "chunkio.h"
#include "chunksectionwidget.h"
#include "hsaeditorwidget.h"
#include "loguru/loguru.hpp"
#include "msg.h"
#include "nbt.h"
#include "nbtwidget.h"
#include "resourcemanager.h"
#include "ui_chunkeditorwidget.h"
#include "voxelwidget.h"

namespace {
    // data above this size is not parsed into the NBT editor
    constexpr size_t kOversizeBytes = 16u * 1024u * 1024u;  // 16 MB

    QString actorLabel(bl::nbt::compound_tag *root) {
        auto *id = root->get("identifier");
        if (id && id->type() == bl::nbt::tag_type::String) {
            return QString(dynamic_cast<bl::nbt::string_tag *>(id)->value.c_str()).replace("minecraft:", "");
        }
        return "unknown";
    }
}  // namespace

ChunkEditorWidget::ChunkEditorWidget(QWidget *parent, AsyncLevelLoader *levelLoader)
    : QWidget(parent), ui(new Ui::ChunkEditorWidget), level_loader_(levelLoader) {
    ui->setupUi(this);

    // 3D render widget — no parent so it opens as a standalone wiAndow
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
    this->actor_editor_->setMode(NbtMode::Memory);
    this->pending_tick_editor_->setMode(NbtMode::Memory);
    this->block_entity_editor_->setMode(NbtMode::Memory);

    // each editor is stacked with an "oversized data" placeholder (label + delete button)
    this->block_entity_stack_ = new QStackedWidget();
    this->block_entity_stack_->addWidget(this->block_entity_editor_);
    this->block_entity_stack_->addWidget(
        makeOversizePlaceholder(tr("chunkEditor.blockEntity.tooLarge"), [this] { deleteBlockEntityData(); }));
    ui->block_actor_tab->layout()->replaceWidget(ui->empty_block_actor_editor_widget, this->block_entity_stack_);

    this->actor_stack_ = new QStackedWidget();
    this->actor_stack_->addWidget(this->actor_editor_);
    this->actor_stack_->addWidget(makeOversizePlaceholder(tr("chunkEditor.actor.tooLarge"), [this] { deleteActorData(); }));
    ui->actor_tab->layout()->replaceWidget(ui->empyt_actor_editor_widget, this->actor_stack_);

    this->pending_tick_stack_ = new QStackedWidget();
    this->pending_tick_stack_->addWidget(this->pending_tick_editor_);
    this->pending_tick_stack_->addWidget(
        makeOversizePlaceholder(tr("chunkEditor.pendingTick.tooLarge"), [this] { deletePendingTickData(); }));
    ui->pt_tab->layout()->replaceWidget(ui->empty_pt_editor_widget, this->pending_tick_stack_);

    // hsa tab
    this->hsa_editor_ = new HsaEditorWidget();
    ui->hsa_tab->layout()->replaceWidget(ui->empty_hsa_editor_widget, this->hsa_editor_);
    connect(this->hsa_editor_, &HsaEditorWidget::hsaModified, this, [this]() {
        int idx = ui->tabWidget->indexOf(ui->hsa_tab);
        if (idx < 0) return;
        auto text = ui->tabWidget->tabText(idx);
        if (hsa_editor_->dirty()) {
            if (!text.endsWith(" *")) text += " *";
        } else {
            if (text.endsWith(" *")) text.chop(2);
        }
        ui->tabWidget->setTabText(idx, text);
        setDirty(true);
    });

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
    delete ui;
}

void ChunkEditorWidget::loadChunkData(bl::raw_chunk raw) {
    this->clearData();
    this->raw_chunk_ = std::move(raw);
    this->has_chunk_ = true;
    auto ptr = std::make_unique<bl::chunk>(this->raw_chunk_.pos());
    auto *chunk = ptr.get();
    if (!chunk->load_from_raw_chunk(this->raw_chunk_, bl::chunk_load_policy::Terrain | bl::chunk_load_policy::Others)) return;

    this->cv = chunk->get_version();
    this->cp_ = chunk->get_pos();
    this->refreshBasicData();
    this->hsa_editor_->setChunk(this->cp_);
    this->hsa_editor_->setData(chunk->HSAs().areas());

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
        auto raw = this->raw_chunk_.get_normal_key(bl::chunk_key::BlockEntity);
        if (raw.size() > kOversizeBytes) {
            this->block_entity_stack_->setCurrentIndex(1);
        } else {
            this->block_entity_stack_->setCurrentIndex(0);
            std::vector<NBTListItem *> block_entity_items;
            if (!raw.empty()) {
                auto palettes = bl::nbt::read_palette_to_end(raw.data(), raw.size());
                for (auto *b : palettes) {
                    auto id_tag = b->get("id");
                    QString name = "unknown";
                    if (id_tag && id_tag->type() == bl::nbt::tag_type::String) {
                        name = dynamic_cast<bl::nbt::string_tag *>(id_tag)->value.c_str();
                    }
                    auto *item = NBTListItem::from(b, name, QString::number(index));
                    item->setIcon(QIcon(QPixmap::fromImage(*BlockActorNBTIcon(name.toLower().replace("minecraft:", "")))));
                    block_entity_items.push_back(item);
                    index++;
                }
            }
            this->block_entity_editor_->loadNewData(block_entity_items);
        }
    }

    {
        LOG_F(INFO, "Load Chunk pending tick data");
        auto raw = this->raw_chunk_.get_normal_key(bl::chunk_key::PendingTicks);
        if (raw.size() > kOversizeBytes) {
            this->pending_tick_stack_->setCurrentIndex(1);
        } else {
            this->pending_tick_stack_->setCurrentIndex(0);
            std::vector<NBTListItem *> pt_items;
            if (!raw.empty()) {
                auto palettes = bl::nbt::read_palette_to_end(raw.data(), raw.size());
                index = 0;
                for (auto *b : palettes) {
                    auto *item = NBTListItem::from(b, QString::number(index), QString::number(index));
                    pt_items.push_back(item);
                    index++;
                }
            }
            this->pending_tick_editor_->loadNewData(pt_items);
        }
    }

    {
        LOG_F(INFO, "Load chunk actors data");
        size_t actorBytes = this->raw_chunk_.get_normal_key(bl::chunk_key::Entity).size();
        for (auto &[uid, data] : this->raw_chunk_.get_entities()) actorBytes += data.size();
        if (actorBytes > kOversizeBytes) {
            this->actor_stack_->setCurrentIndex(1);
        } else {
            this->actor_stack_->setCurrentIndex(0);
            std::vector<NBTListItem *> actor_items;
            auto addActorRaw = [&actor_items, &index](const std::string &raw) {
                if (raw.empty()) return;
                auto palettes = bl::nbt::read_palette_to_end(raw.data(), raw.size());
                for (auto *b : palettes) {
                    auto id = actorLabel(b);
                    auto *item = NBTListItem::from(b, id, QString::number(index));
                    item->setIcon(QIcon(QPixmap::fromImage(*EntityNBTIcon(id))));
                    actor_items.push_back(item);
                    index++;
                }
            };
            index = 0;
            addActorRaw(this->raw_chunk_.get_normal_key(bl::chunk_key::Entity));
            for (auto &[uid, data] : this->raw_chunk_.get_entities()) addActorRaw(data);
            this->actor_editor_->loadNewData(actor_items);
        }
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
        for (auto &[key, data] : this->raw_chunk_.get_entities()) {
            if (key.size() != 8) {
                LOG_F(WARNING, "Entity uid size is %zu, expected 8, skipping", key.size());
                continue;
            }
            QString hexKey = QString::fromLatin1(QByteArray::fromRawData(key.data(), 8).toHex());
            lines.append(QString("Entity[%1]: %2 bytes [%3]").arg(hexKey).arg(data.size()).arg(shortHash(data)));
        }

        ui->stats_label->setText(lines.join('\n'));
    }
}

bool ChunkEditorWidget::saveChunk() {
    if (!dirty_) return true;
    if (!level_loader_ || level_loader_->chunkCoordsLoading()) {
        QMessageBox::warning(this, msg::READ_ONLY(), msg::EDITING_DISABLED_DURING_COORDS_LOADING());
        return false;
    }
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
    raw_chunk_.set_normal(bl::chunk_key::HardCodedSpawnAreas, this->hsa_editor_->serialize());
    const bool saved = this->level_loader_->putRawChunk(this->raw_chunk_);
    if (!saved) {
        for (auto *actor : actors) delete actor;
        return false;
    }
    actor_editor_->clearModifyCache();
    pending_tick_editor_->clearModifyCache();
    block_entity_editor_->clearModifyCache();
    hsa_editor_->markClean();
    for (auto *actor : actors) delete actor;
    setDirty(false);
    return true;
}

void ChunkEditorWidget::on_close_btn_clicked() {
    if (dirty_) {
        auto btn = QMessageBox::question(this, msg::UNSAVED_CHANGES(), msg::UNSAVED_CHANGES_PROMPT(),
                                         QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel);
        if (btn == QMessageBox::Cancel) return;
        if (btn == QMessageBox::Yes && !saveChunk()) return;
    }
    terrain_render_widget_->hide();
    hide();
}

void ChunkEditorWidget::hideEvent(QHideEvent *event) {
    emit editorClosed();
    QWidget::hideEvent(event);
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
    this->hsa_editor_->clearData();
    if (this->actor_stack_) this->actor_stack_->setCurrentIndex(0);
    if (this->block_entity_stack_) this->block_entity_stack_->setCurrentIndex(0);
    if (this->pending_tick_stack_) this->pending_tick_stack_->setCurrentIndex(0);
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
    ExportedRegion region;
    region.addChunk(this->raw_chunk_);
    auto fileName = QFileDialog::getSaveFileName(this, "", QString(), "BedrockMap Chunk Files (*.bchks)");
    if (fileName.isEmpty()) return;
    auto data = region.serialize();

    QFile file(fileName);
    if (file.open(QIODevice::WriteOnly)) {
        file.write(data.data(), static_cast<qint64>(data.size()));
        file.close();
    }
}

void ChunkEditorWidget::on_import_btn_clicked() {
    auto fileName = QFileDialog::getOpenFileName(this, "", QString(), "BedrockMap Chunk Files (*.bchks)");
    if (fileName.isEmpty()) return;

    QFile file(fileName);
    if (!file.open(QIODevice::ReadOnly)) return;

    auto data = file.readAll();
    file.close();

    auto region = ExportedRegion::deserialize(data.data(), data.size());

    if (region.isEmpty()) {
        WARN(msg::INVALID_CHUNK_FORMAT());
        return;
    }

    auto chunk = region.chunks().front();
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
    saveChunk();
}

QWidget *ChunkEditorWidget::makeOversizePlaceholder(const QString &msg, const std::function<void()> &onDelete) {
    auto *w = new QWidget(this);
    auto *layout = new QVBoxLayout(w);
    layout->setContentsMargins(12, 12, 12, 12);
    auto *label = new QLabel(msg);
    label->setWordWrap(true);
    label->setAlignment(Qt::AlignCenter);
    auto *delBtn = new QPushButton(tr("chunkEditor.tooLarge.delete"), w);
    connect(delBtn, &QPushButton::clicked, w, [onDelete]() { onDelete(); });
    layout->addStretch();
    layout->addWidget(label);
    layout->addWidget(delBtn, 0, Qt::AlignHCenter);
    layout->addStretch();
    return w;
}

void ChunkEditorWidget::setTabDirtyText(QWidget *tab) {
    int idx = ui->tabWidget->indexOf(tab);
    if (idx < 0) return;
    auto text = ui->tabWidget->tabText(idx);
    if (!text.endsWith(" *")) text += " *";
    ui->tabWidget->setTabText(idx, text);
}

void ChunkEditorWidget::deleteBlockEntityData() {
    if (!has_chunk_) return;
    this->raw_chunk_.set_normal(bl::chunk_key::BlockEntity, "");
    this->block_entity_editor_->clearData();
    this->block_entity_stack_->setCurrentIndex(0);
    this->setDirty(true);
    this->setTabDirtyText(ui->block_actor_tab);
}

void ChunkEditorWidget::deletePendingTickData() {
    if (!has_chunk_) return;
    this->raw_chunk_.set_normal(bl::chunk_key::PendingTicks, "");
    this->pending_tick_editor_->clearData();
    this->pending_tick_stack_->setCurrentIndex(0);
    this->setDirty(true);
    this->setTabDirtyText(ui->pt_tab);
}

void ChunkEditorWidget::deleteActorData() {
    if (!has_chunk_) return;
    this->raw_chunk_.clear_entities();
    this->raw_chunk_.set_normal(bl::chunk_key::Entity, "");
    this->actor_editor_->clearData();
    this->actor_stack_->setCurrentIndex(0);
    this->setDirty(true);
    this->setTabDirtyText(ui->actor_tab);
}
