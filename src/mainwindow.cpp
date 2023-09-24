#include "mainwindow.h"

#include <QDesktopServices>
#include <QDesktopWidget>
#include <QDialog>
#include <QDir>
#include <QFileDialog>
#include <QGridLayout>
#include <QMessageBox>
#include <QPalette>
#include <QPixmap>
#include <QSplitter>
#include <QStandardPaths>
#include <QtConcurrent>
#include <QtDebug>
#include <exception>

#include "./ui_mainwindow.h"
#include "aboutdialog.h"
#include "iconmanager.h"
#include "mapitemeditor.h"
#include "mapwidget.h"
#include "msg.h"
#include "nbtwidget.h"
#include "palette.h"
#include "renderfilterdialog.h"

namespace {

    [[nodiscard]] QRect centerMainWindowGeometry(double rate) {
        // finished adjust size
        auto const rec = QApplication::desktop()->screenGeometry();
        auto const height = static_cast<int>(rec.height() * rate);
        auto const width = static_cast<int>(rec.width() * rate);
        return {(rec.width() - width) / 2, (rec.height() - height) / 2, width, height};
    }

    void updateButtonBackground(QPushButton *btn, bool enable) {
        auto color = enable ? QColor(180, 180, 180, 200) : QColor(255, 255, 255, 0);
        QPalette pal = btn->palette();
        pal.setColor(QPalette::Button, color);
        btn->setAutoFillBackground(true);
        btn->setPalette(pal);
        btn->update();
    }
}  // namespace

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent), ui(new Ui::MainWindow) {
    ui->setupUi(this);
    // level loader
    this->level_loader_ = new AsyncLevelLoader();
    // init and insert map widget
    this->map_widget_ = new MapWidget(this, nullptr);
    this->map_widget_->gotoBlockPos(0, 0);
    ui->map_visual_layout->addWidget(this->map_widget_);
    connect(this->map_widget_, SIGNAL(mouseMove(int, int)), this, SLOT(updateXZEdit(int, int)));  // NOLINT
    // init chunk editor layout
    this->chunk_editor_widget_ = new ChunkEditorWidget(this);
    ui->map_splitter->addWidget(this->chunk_editor_widget_);
    ui->map_splitter->setStretchFactor(1, 1);
    ui->map_splitter->setStretchFactor(1, 10);
    ui->map_splitter->setStretchFactor(2, 1);

    // basic layer btns group

    this->layer_btns_ = {{MapWidget::MainRenderType::Biome, ui->biome_layer_btn},
                         {MapWidget::MainRenderType::Terrain, ui->terrain_layer_btn},
                         {MapWidget::MainRenderType::Height, ui->height_layer_btn}};

    for (auto &kv : this->layer_btns_) {
        QObject::connect(kv.second, &QPushButton::clicked, [this, &kv]() {
            for (auto &x : this->layer_btns_) {
                updateButtonBackground(x.second, x.first == kv.first);
            }
            this->map_widget_->changeLayer(kv.first);
        });
    }

    // dimension btns group
    this->dim_btns_ = {
        {MapWidget::DimType::OverWorld, ui->overwrold_btn},
        {MapWidget::DimType::Nether, ui->nether_btn},
        {MapWidget::DimType::TheEnd, ui->theend_btn},
    };

    for (auto &kv : this->dim_btns_) {
        QObject::connect(kv.second, &QPushButton::clicked, [this, &kv]() {
            for (auto &x : this->dim_btns_) {
                updateButtonBackground(x.second, x.first == kv.first);
            }
            this->map_widget_->changeDimension(kv.first);
        });
    }

    // Label and edit

    // global editor
    this->level_dat_editor_ = new NbtWidget();
    level_dat_editor_->hideLoadDataBtn();
    this->player_editor_ = new NbtWidget();
    player_editor_->hideLoadDataBtn();
    this->village_editor_ = new NbtWidget();
    village_editor_->hideLoadDataBtn();
    this->other_nbt_editor_ = new NbtWidget();
    other_nbt_editor_->hideLoadDataBtn();

    ui->level_dat_tab->layout()->replaceWidget(ui->level_dat_empty_widget, level_dat_editor_);
    ui->player_tab->layout()->replaceWidget(ui->player_empty_widget, player_editor_);
    ui->village_tab->layout()->replaceWidget(ui->village_empty_widget, village_editor_);
    ui->other_tab->layout()->replaceWidget(ui->other_empty_widget, other_nbt_editor_);

    ui->main_splitter->setStretchFactor(0, 3);
    ui->main_splitter->setStretchFactor(1, 2);

    this->map_item_editor_ = new MapItemEditor(this);
    connect(ui->open_level_btn, &QPushButton::clicked, this, &MainWindow::openLevel);
    connect(&this->render_filter_dialog_, &RenderFilterDialog::accepted, this, &MainWindow::applyFilter);
    // menu actions
    connect(ui->action_open, SIGNAL(triggered()), this, SLOT(openLevel()));
    connect(ui->action_close, SIGNAL(triggered()), this, SLOT(closeLevel()));
    connect(ui->action_exit, SIGNAL(triggered()), this, SLOT(close_and_exit()));
    connect(ui->action_NBT, SIGNAL(triggered()), this, SLOT(openNBTEditor()));
    connect(ui->action_about, &QAction::triggered, this, []() { AboutDialog().exec(); });

    connect(ui->action_settings, &QAction::triggered, this,
            []() { QDesktopServices::openUrl(QUrl::fromLocalFile(cfg::CONFIG_FILE_PATH.c_str())); });

    connect(ui->action_map_item, &QAction::triggered, this, [this]() { openMapItemEditor(); });

    // modify
    ui->action_modify->setCheckable(true);
    connect(ui->action_modify, &QAction::triggered, this, [this]() {
        auto checked = this->ui->action_modify->isChecked();
        this->ui->action_modify->setChecked(checked);
        this->write_mode_ = checked;
    });

    // debug
    ui->action_debug->setCheckable(true);
    connect(ui->action_debug, &QAction::triggered, this, [this]() {
        auto checked = this->ui->action_debug->isChecked();
        this->ui->action_debug->setChecked(checked);
        this->map_widget_->setDrawDebug(checked);
    });

    // watcher
    connect(&this->delete_chunks_watcher_, &QFutureWatcher<bool>::finished, this, &MainWindow::handle_chunk_delete_finished);
    connect(&this->load_global_data_watcher_, &QFutureWatcher<bool>::finished, this, &MainWindow::handle_level_open_finished);

    // reset UI

    this->setupShortcuts();

    this->resetToInitUI();
}

void MainWindow::resetToInitUI() {
    // main_splitter: 分割按钮 +  主要UI
    // map_splitter: 分割按钮面板 + 地图 + 区块编辑器
    ui->main_splitter->setVisible(false);

    // 全局nbt panel的状态
    ui->global_nbt_pannel->setVisible(false);
    this->village_editor_->setVisible(false);
    this->player_editor_->setVisible(false);
    this->other_nbt_editor_->setVisible(false);
    ui->save_players_btn->setVisible(false);
    ui->save_village_btn->setVisible(false);
    ui->save_other_btn->setVisible(false);
    ui->player_nbt_loading_label->setVisible(true);
    ui->village_nbt_loading_label->setVisible(true);
    // checkbox and btns
    updateButtonBackground(ui->overwrold_btn, true);
    updateButtonBackground(ui->terrain_layer_btn, true);
    updateButtonBackground(ui->grid_btn, true);

    this->chunk_editor_widget_->setVisible(false);
    ui->open_level_btn->setText("未打开存档");
    ui->open_level_btn->setVisible(true);
    ui->open_level_btn->setEnabled(true);
    this->setGeometry(centerMainWindowGeometry(0.6));
    this->chunk_editor_widget_->setMaximumWidth(this->width() / 2);
    this->setWindowTitle(this->getStaticTitle());
    this->global_data_loaded_ = false;
}

MainWindow::~MainWindow() {
    this->closeLevel();
    delete ui;
}

void MainWindow::updateXZEdit(int x, int z) {
    this->setWindowTitle(this->getStaticTitle() + " @  [" + QString::number(x) + ", " + QString::number(z) + "]");
}

bool MainWindow::openChunkEditor(const bl::chunk_pos &p) {
    if (!CHECK_CONDITION(this->level_loader_->isOpen(), "未打开存档")) {
        return false;
    }
    // add a watcher
    qDebug() << "Try load chunk: " << p.to_string().c_str();
    auto chunk = this->level_loader_->getChunkDirect(p);
    if (!CHECK_CONDITION((chunk), "无法打开区块数据")) {
        return false;
    }

    this->chunk_editor_widget_->setVisible(true);
    this->chunk_editor_widget_->loadChunkData(chunk);
    return true;
}

void MainWindow::openLevel() {
    // #ifdef  QT_DEBUG
    //     auto path = QString("D:\\MC\\saves");
    // #elif
    auto path = QStandardPaths::standardLocations(QStandardPaths::GenericDataLocation)[0] +
                "/Packages/Microsoft.MinecraftUWP_8wekyb3d8bbwe/LocalState/games/com.mojang/minecraftWorlds";
    // #endif
    QString root =
        QFileDialog::getExistingDirectory(this, tr("打开存档根目录"), path, QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
    if (root.size() == 0) {
        return;
    }
    this->closeLevel();
    qDebug() << "Level root path is " << root;
    ui->open_level_btn->setText("正在打开...");
    ui->open_level_btn->setEnabled(false);
    auto res = this->level_loader_->open(root.toStdString());
    if (!res) {
        this->level_loader_->close();
        WARN("无法打开存档,请确认这是一个合法的存档根目录");
        this->resetToInitUI();
        return;
    }

    // 打开了存档
    this->setWindowTitle(getStaticTitle());  // 刷新标题
    ui->main_splitter->setVisible(true);     // 显示GUI
    ui->open_level_btn->setVisible(false);   // 隐藏窗口
    // 设置出生点坐标
    auto sp = this->level_loader_->level().dat().spawn_position();
    this->map_widget_->gotoBlockPos(sp.x, sp.z);
    // 写入level.dat数据
    auto *ld = dynamic_cast<bl::palette::compound_tag *>(this->level_loader_->level().dat().root());
    this->level_dat_editor_->loadNewData({NBTListItem::from(dynamic_cast<bl::palette::compound_tag *>(ld->copy()), "level.dat")});
    qDebug() << "Loading global data in background thread";
    // 后台加载全局数据
    this->loading_global_data_ = true;

    auto future = QtConcurrent::run(
        [this](const QString &path) -> bool {
            auto result = GlobalNBTLoadResult();
            try {
                if (cfg::LOAD_GLOBAL_DATA) {
                    this->level_loader_->level().foreach_global_keys([this, &result](const std::string &key, const std::string &value) {
                        if (!this->loading_global_data_) {
                            throw std::logic_error("EXIT");  // 手动中止
                        }
                        if (key.find("player") != std::string::npos) {
                            result.playerData.append_nbt(key, value);
                        } else if (key == "portals" || key == "scoreboard" || key == "AutonomousEntities" || key == "BiomeData" ||
                                   key == "Nether" || key == "Overworld" || key == "TheEnd" || key == "schedulerWT" || key == "mobevents") {
                            result.otherData.append_nbt(key, value);
                        } else if (key.find("map_") == 0) {
                            result.mapData.append_nbt(key, value);
                        } else {
                            bl::village_key vk = bl::village_key::parse(key);
                            if (vk.valid()) result.villageData.append_village(vk, value);
                        }
                    });
                }
                this->prepareGlobalData(result);
                return true;
            } catch (std::exception &e) {
                return false;
            }
        },
        root);
    this->load_global_data_watcher_.setFuture(future);
}

void MainWindow::closeLevel() {  // NOLINT
    // cancel background task
    if (!this->level_loader_->isOpen()) return;
    this->loading_global_data_ = false;
    this->load_global_data_watcher_.waitForFinished();
    this->level_loader_->close();
    // free spaces
    this->chunk_editor_widget_->clearData();
    this->village_editor_->clearData();
    this->player_editor_->clearData();
    this->level_dat_editor_->clearData();
    this->other_nbt_editor_->clearData();
    this->map_item_editor_->clearData();
    this->villages_.clear();
    this->resetToInitUI();
}

void MainWindow::close_and_exit() {
    this->closeLevel();
    this->close();
}

void MainWindow::openNBTEditor() {
    auto *w = new NbtWidget();
    auto g = this->geometry();
    const int ext = 100;
    w->setWindowTitle("NBT Editor");
    w->setGeometry(QRect(g.x() + ext, g.y() + ext, g.width() - ext * 2, g.height() - ext * 2));
    w->show();
}

void MainWindow::openMapItemEditor() {
    if (!this->level_loader_->isOpen() || !this->global_data_loaded_) {
        WARN("请打开存档且等待全局数据加载完成后再打开");
        return;
    }

    const int ext = 100;
    auto g = this->geometry();
    this->map_item_editor_->setGeometry(QRect(g.x() + ext * 2, g.y() + ext, g.width() - ext * 4, g.height() - ext * 2));
    this->map_item_editor_->show();
}

void MainWindow::deleteChunks(const bl::chunk_pos &min, const bl::chunk_pos &max) {
    if (!this->level_loader_->isOpen()) {
        QMessageBox::information(nullptr, "警告", "还没有打开世界", QMessageBox::Yes, QMessageBox::Yes);
        return;
    }
    if (!this->write_mode_) {
        QMessageBox::information(nullptr, "警告", "当前为只读模式，无法删除区块", QMessageBox::Yes, QMessageBox::Yes);
        return;
    }
    auto future = this->level_loader_->dropChunk(min, max);
    this->delete_chunks_watcher_.setFuture(future);
}

void MainWindow::handle_chunk_delete_finished() {
    INFO("区块删除成功");
    this->level_loader_->clearAllCache();
}

void MainWindow::prepareGlobalData(GlobalNBTLoadResult &res) {
    // load players
    auto &playerData = res.playerData.data();
    std::vector<NBTListItem *> playerNBTList;
    for (auto &kv : playerData) {
        auto *item = NBTListItem::from(dynamic_cast<compound_tag *>(kv.second->copy()), kv.first.c_str(), kv.first.c_str());
        item->setIcon(QIcon(QPixmap::fromImage(*PlayerNBTIcon())));
        playerNBTList.push_back(item);
    }

    this->player_editor_->loadNewData(playerNBTList);
    qDebug() << "Load player data finished";
    // load other items
    auto &otherData = res.otherData.data();
    std::vector<NBTListItem *> otherNBTList;
    for (auto &kv : otherData) {
        auto *item = NBTListItem::from(dynamic_cast<compound_tag *>(kv.second->copy()), kv.first.c_str(), kv.first.c_str());
        item->setIcon(QIcon(QPixmap::fromImage(*OtherNBTIcon())));
        otherNBTList.push_back(item);
    }
    this->other_nbt_editor_->loadNewData(otherNBTList);
    qDebug() << "Load other data finished";

    // load villages
    auto &villData = res.villageData.data();
    std::vector<NBTListItem *> villNBTList;
    for (auto &kv : villData) {
        int index = 0;
        for (auto &p : kv.second) {
            if (p) {
                auto key = kv.first + "_" + bl::village_key::village_key_type_to_str(static_cast<bl::village_key::key_type>(index));
                auto *item = NBTListItem::from(dynamic_cast<compound_tag *>(p->copy()), key.c_str(), ("VILLAGE_" + key).c_str());
                item->setIcon(QIcon(QPixmap::fromImage(*VillageNBTIcon(static_cast<bl::village_key::key_type>(index)))));
                villNBTList.push_back(item);
            }
            index++;
        }
    }
    this->village_editor_->loadNewData(villNBTList);
    qDebug() << "Load village data finished";
    // load map data
    this->map_item_editor_->load_map_data(res.mapData);
}

void MainWindow::handle_level_open_finished() {
    auto res = this->load_global_data_watcher_.result();
    if (!res) {
        if (!this->loading_global_data_) {  // 说明是主动停止的
            qDebug() << "Stop loading global data (by user)";
            return;
        }
        qDebug() << "Load global data failed";
        WARN("无法加载全局NBT数据，但是你仍然可以查看地图和区块数据");
    }
    qDebug() << "Load global data finished";

    this->village_editor_->setVisible(true);
    this->other_nbt_editor_->setVisible(true);
    this->player_editor_->setVisible(true);
    ui->save_players_btn->setVisible(true);
    ui->save_village_btn->setVisible(true);
    ui->save_other_btn->setVisible(true);
    ui->player_nbt_loading_label->setVisible(false);
    ui->village_nbt_loading_label->setVisible(false);
    ui->other_nbt_loading_label->setVisible(false);
    // 打开完成了设置为可用（虽然）
    ui->open_level_btn->setEnabled(true);
    this->global_data_loaded_ = true;
}

void MainWindow::on_save_leveldat_btn_clicked() {
    if (!CHECK_CONDITION(this->write_mode_, "未开启写模式")) return;
    auto nbts = this->level_dat_editor_->getPaletteCopy();
    if (nbts.size() == 1 && nbts[0]) {
        this->level_loader_->modifyLeveldat(nbts[0]);
        this->setWindowTitle(getStaticTitle());
        INFO("成功保存level.dat文件");
    } else {
        INFO("无法保存level.dat文件");
    }
}

void MainWindow::on_save_village_btn_clicked() {
    if (!CHECK_CONDITION(this->write_mode_, "未开启写模式")) return;
    CHECK_DATA_SAVE(this->level_loader_->modifyDBGlobal(this->village_editor_->getModifyCache()));
    this->village_editor_->clearModifyCache();
}

void MainWindow::on_save_players_btn_clicked() {
    if (!CHECK_CONDITION(this->write_mode_, "未开启写模式")) return;
    CHECK_DATA_SAVE(this->level_loader_->modifyDBGlobal(this->player_editor_->getModifyCache()));
    this->player_editor_->clearModifyCache();
}

void MainWindow::on_save_other_btn_clicked() {
    if (!CHECK_CONDITION(this->write_mode_, "未开启写模式")) return;
    CHECK_DATA_SAVE(this->level_loader_->modifyDBGlobal(this->other_nbt_editor_->getModifyCache()));
    this->other_nbt_editor_->clearModifyCache();
}

void MainWindow::collect_villages(const std::unordered_map<std::string, std::array<bl::palette::compound_tag *, 4>> &vs) {
    for (auto kv : vs) {
        auto *nbt = kv.second[static_cast<int>(bl::village_key::key_type::INFO)];
        if (!nbt) continue;
        auto x0 = dynamic_cast<bl::palette::int_tag *>(nbt->get("X0"));
        auto z0 = dynamic_cast<bl::palette::int_tag *>(nbt->get("Z0"));
        auto x1 = dynamic_cast<bl::palette::int_tag *>(nbt->get("X1"));
        auto z1 = dynamic_cast<bl::palette::int_tag *>(nbt->get("Z1"));
        if (x0 && z0 && x1 && z1) {
            this->villages_.insert(kv.first.c_str(), QRect(std::min(x0->value, x1->value), std::min(z0->value, z1->value),
                                                           std::abs(x0->value - x1->value), std::abs(z0->value - z1->value)));
        }
    }
}

void MainWindow::on_edit_filter_btn_clicked() {
    this->render_filter_dialog_.fillInUI();
    this->render_filter_dialog_.exec();
}

void MainWindow::applyFilter() {
    this->render_filter_dialog_.collectFilerData();
    this->level_loader_->setFilter(this->render_filter_dialog_.getFilter());
    this->level_loader_->clearAllCache();
}

#include <QPainter>

void MainWindow::paintEvent(QPaintEvent *event) {
    //    if (this->levelLoader()->isOpen()) return;
    //    auto sz = this->size();
    //    const int w = static_cast<int>(std::min(sz.width(), sz.height()) * 1.2);
    //    QPainter p(this);
    //    int x = static_cast<int>(logoPos.x * sz.width());
    //    int z = static_cast<int>( logoPos.y * sz.height());
    //    p.translate(x, z);
    //    p.rotate(logoPos.angle);
    //    p.drawImage(QRect(-w / 2, -w / 2, w, w), *cfg::BG(), QRect(0, 0, 8, 8));
    //    QMainWindow::paintEvent(event);
}

void MainWindow::on_grid_btn_clicked() {
    auto r = this->map_widget_->toggleGrid();
    updateButtonBackground(ui->grid_btn, r);
}

void MainWindow::on_coord_btn_clicked() {
    auto r = this->map_widget_->toggleCoords();
    updateButtonBackground(ui->coord_btn, r);
}

void MainWindow::on_global_data_btn_clicked() { ui->global_nbt_pannel->setVisible(!ui->global_nbt_pannel->isVisible()); }

void MainWindow::on_slime_layer_btn_clicked() {
    auto r = this->map_widget_->toggleSlime();
    updateButtonBackground(ui->slime_layer_btn, r);
}

void MainWindow::on_actor_layer_btn_clicked() {
    auto r = this->map_widget_->toggleActor();
    updateButtonBackground(ui->actor_layer_btn, r);
}

void MainWindow::on_village_layer_btn_clicked() {
    auto r = this->map_widget_->toggleVillage();
    updateButtonBackground(ui->village_layer_btn, r);
}

void MainWindow::on_hsa_layer_btn_clicked() {
    auto r = this->map_widget_->toggleHSAs();
    updateButtonBackground(ui->hsa_layer_btn, r);
}

QString MainWindow::getStaticTitle() {
    std::string level_name;
    if (this->level_loader_->isOpen()) {
        level_name = this->level_loader_->level().dat().level_name();
    }
    return cfg::VERSION_STRING() + " " + level_name.c_str();
}

void MainWindow::resizeEvent(QResizeEvent *event) { this->chunk_editor_widget_->setMaximumWidth(this->height() / 2); }

void MainWindow::setupShortcuts() {
    // map move
    auto *up = new QShortcut(QKeySequence(Qt::Key_Up), this);
    auto *down = new QShortcut(QKeySequence(Qt::Key_Down), this);
    auto *left = new QShortcut(QKeySequence(Qt::Key_Left), this);
    auto *right = new QShortcut(QKeySequence(Qt::Key_Right), this);
    const int speed = 16;
    connect(up, &QShortcut::activated, this, [&]() { map_widget_->advancePos(0, speed); });
    connect(down, &QShortcut::activated, this, [&]() { map_widget_->advancePos(0, -speed); });
    connect(left, &QShortcut::activated, this, [&]() { map_widget_->advancePos(speed, 0); });
    connect(right, &QShortcut::activated, this, [&]() { map_widget_->advancePos(-speed, 0); });

    // jump to
    auto *go = new QShortcut(QKeySequence(Qt::Key_J), this);
    go->setAutoRepeat(false);
    connect(go, &QShortcut::activated, this, [&]() { this->map_widget_->gotoPositionAction(); });

    // change dimensions
    const QKeySequence change_dimensions{Qt::Key_1, Qt::Key_2, Qt::Key_3};
    for (int i = 0; i < 3; i++) {
        auto *sh = new QShortcut(change_dimensions[i], this);
        connect(sh, &QShortcut::activated, this->dim_btns_[static_cast<MapWidget::DimType>(i)], &QPushButton::click);
    }
}
