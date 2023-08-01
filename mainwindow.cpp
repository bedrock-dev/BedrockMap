#include "mainwindow.h"

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
#include "mapwidget.h"
#include "nbtwidget.h"
#include "palette.h"
#include "renderfilterdialog.h"

namespace {
    void WARN(const QString &msg) { QMessageBox::warning(nullptr, "警告", msg, QMessageBox::Yes, QMessageBox::Yes); }

    void INFO(const QString &msg) {
        QMessageBox::information(nullptr, "信息", msg, QMessageBox::Yes, QMessageBox::Yes);
    }

    QRect centerMainWindowGeometry(double rate) {
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
    //level loader
    this->level_loader_ = new AsyncLevelLoader();
    //init and insert map widget
    this->map_widget_ = new MapWidget(this, nullptr);
    this->map_widget_->gotoBlockPos(0, 0);
    ui->map_visual_layout->addWidget(this->map_widget_);
    connect(this->map_widget_, SIGNAL(mouseMove(int, int)), this, SLOT(updateXZEdit(int, int)));


    //init chunk editor layout
    this->chunk_editor_widget_ = new ChunkEditorWidget(this);
    ui->map_splitter->addWidget(this->chunk_editor_widget_);
    ui->map_splitter->setStretchFactor(0, 1);
    ui->map_splitter->setStretchFactor(1, 3);
    ui->map_splitter->setStretchFactor(2, 1);


    //basic layer btns group
    this->layer_btns_ = {{MapWidget::MainRenderType::Biome,   ui->biome_layer_btn},
                         {MapWidget::MainRenderType::Terrain, ui->terrain_layer_btn},
                         {MapWidget::MainRenderType::Height,  ui->height_layer_btn}
    };

    for (auto &kv: this->layer_btns_) {
        QObject::connect(kv.second, &QPushButton::clicked, [this, &kv]() {
            for (auto &x: this->layer_btns_) {
                updateButtonBackground(x.second, x.first == kv.first);
            }
            this->map_widget_->changeLayer(kv.first);
        });
    }

    // dimension btns group
    this->dim_btns_ = {
            {MapWidget::DimType::OverWorld, ui->overwrold_btn},
            {MapWidget::DimType::Nether,    ui->nether_btn},
            {MapWidget::DimType::TheEnd,    ui->theend_btn},
    };

    for (auto &kv: this->dim_btns_) {
        QObject::connect(kv.second, &QPushButton::clicked, [this, &kv]() {
            for (auto &x: this->dim_btns_) {
                updateButtonBackground(x.second, x.first == kv.first);
            }
            this->map_widget_->changeDimension(kv.first);
        });
    }

    // Label and edit


    //global editor
    this->level_dat_editor_ = new NbtWidget();
    level_dat_editor_->hideLoadDataBtn();
    this->player_editor_ = new NbtWidget();
    player_editor_->hideLoadDataBtn();
    this->village_editor_ = new NbtWidget();
    village_editor_->hideLoadDataBtn();
    ui->level_dat_tab->layout()->replaceWidget(ui->level_dat_empty_widget, level_dat_editor_);
    ui->player_tab->layout()->replaceWidget(ui->player_empty_widget, player_editor_);
    ui->village_tab->layout()->replaceWidget(ui->village_empty_widget, village_editor_);
    ui->main_splitter->setStretchFactor(0, 3);
    ui->main_splitter->setStretchFactor(1, 2);

    connect(ui->open_level_btn, &QPushButton::clicked, this, &MainWindow::openLevel);
    connect(&this->render_filter_dialog_, &RenderFilterDialog::accepted, this, &MainWindow::applyFilter);
    // menu actions
    connect(ui->action_open, SIGNAL(triggered()), this, SLOT(openLevel()));
    connect(ui->action_close, SIGNAL(triggered()), this, SLOT(closeLevel()));
    connect(ui->action_exit, SIGNAL(triggered()), this, SLOT(close_and_exit()));
    connect(ui->action_NBT, SIGNAL(triggered()), this, SLOT(openNBTEditor()));
    connect(ui->action_about, &QAction::triggered, this, []() { AboutDialog().exec(); });


    //modify
    ui->action_modify->setCheckable(true);
    connect(ui->action_modify, &QAction::triggered, this, [this]() {
        auto checked = this->ui->action_modify->isChecked();
        this->ui->action_modify->setChecked(checked);
        this->write_mode_ = checked;
    });

    //debug
    ui->action_debug->setCheckable(true);
    connect(ui->action_debug, &QAction::triggered, this, [this]() {
        auto checked = this->ui->action_debug->isChecked();
        this->ui->action_debug->setChecked(checked);
        this->map_widget_->setDrawDebug(checked);
    });


    // watcher
    connect(&this->delete_chunks_watcher_, &QFutureWatcher<bool>::finished, this,
            &MainWindow::handle_chunk_delete_finished);
    connect(&this->load_global_data_watcher_, &QFutureWatcher<bool>::finished, this,
            &MainWindow::handle_level_open_finished);

    //reset UI
    this->resetToInitUI();
}

void MainWindow::resetToInitUI() {
    //main_splitter: 分割按钮 +  主要UI
    //map_splitter: 分割按钮面板 + 地图 + 区块编辑器
    ui->map_buttom_toolbar_widget->setVisible(false);
    ui->main_splitter->setVisible(false);

    //全局nbt panel的状态
    ui->global_nbt_pannel->setVisible(false);
    this->village_editor_->setVisible(false);
    this->player_editor_->setVisible(false);
    ui->save_players_btn->setVisible(false);
    ui->save_village_btn->setVisible(false);
    ui->player_nbt_loading_label->setVisible(true);
    ui->village_nbt_loading_label->setVisible(true);
    //checkbox and btns
    updateButtonBackground(ui->overwrold_btn, true);
    updateButtonBackground(ui->terrain_layer_btn, true);
    updateButtonBackground(ui->grid_btn, true);

    this->chunk_editor_widget_->setVisible(false);
    ui->open_level_btn->setText("未打开存档");
    ui->open_level_btn->setVisible(true);
    ui->open_level_btn->setEnabled(true);
    this->setGeometry(centerMainWindowGeometry(0.7));
    this->setWindowTitle(this->getStaticTitle());
}

MainWindow::~MainWindow() {
    this->closeLevel();
    delete ui;
}

void MainWindow::updateXZEdit(int x, int z) {
    this->setWindowTitle(this->getStaticTitle() + " @  [" + QString::number(x) + ", " + QString::number(z) + "]");
}

void MainWindow::openChunkEditor(const bl::chunk_pos &p) {
    if (!this->level_loader_->isOpen()) {
        QMessageBox::information(nullptr, "警告", "未打开存档", QMessageBox::Yes, QMessageBox::Yes);
        return;
    }
    // add a watcher
    qDebug() << "Load: " << p.to_string().c_str();
    auto chunk = this->level_loader_->getChunkDirect(p);
    if (!chunk) {
        QMessageBox::information(nullptr, "警告", "无法打开区块数据", QMessageBox::Yes, QMessageBox::Yes);
    } else {
        this->chunk_editor_widget_->setVisible(true);
        this->chunk_editor_widget_->load_chunk_data(chunk);
    }
}


void MainWindow::on_goto_btn_clicked() {
    int x = ui->x_edit->text().toInt();
    int z = ui->z_edit->text().toInt();
    this->map_widget_->gotoBlockPos(x, z);
}


void MainWindow::openLevel() {
    this->closeLevel();
    auto path = QStandardPaths::standardLocations(QStandardPaths::GenericDataLocation)[0] +
                "/Packages/Microsoft.MinecraftUWP_8wekyb3d8bbwe/LocalState/games/com.mojang/minecraftWorlds";
    QString root = QFileDialog::getExistingDirectory(this, tr("打开存档根目录"),
                                                     path,
                                                     QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
    if (root.size() == 0) {
        return;
    }
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

    //打开了存档
    this->setWindowTitle(getStaticTitle());                      //刷新标题
    ui->main_splitter->setVisible(true);        //显示GUI
    ui->open_level_btn->setVisible(false);      //隐藏窗口
    //设置出生点坐标
    auto sp = this->level_loader_->level().dat().spawn_position();
    this->map_widget_->gotoBlockPos(sp.x, sp.z);
    //写入level.dat数据
    auto *ld = dynamic_cast<bl::palette::compound_tag *>(this->level_loader_->level().dat().root());
    this->level_dat_editor_->load_new_data({ld}, [](auto *) { return "level.dat"; }, {});
    qDebug() << "Loading global data in background thread";
    //后台加载全局数据
    this->loading_global_data_ = true;
    auto future = QtConcurrent::run(
            [this](const QString &path) {
                try {
                    this->level_loader_->level().foreach_global_keys(
                            [this](const std::string &key, const std::string &value) {
                                if (!this->loading_global_data_) {
                                    throw std::logic_error("EXIT"); //手动中止
                                }
                                if (key.find("player") != std::string::npos) {
                                    this->level_loader_->level().player_list().append_player(key, value);
                                } else {
                                    bl::village_key vk = bl::village_key::parse(key);
                                    if (vk.valid()) {
                                        this->level_loader_->level().village_list().append_village(vk, value);
                                    }
                                }
                            });
                    X:
                    return true;
                } catch (std::exception &e) {
                    return false;
                }
            },
            root);
    this->load_global_data_watcher_.setFuture(future);
}

bool MainWindow::closeLevel() {
    //cancel background task
    if (!this->level_loader_->isOpen())return true;
    this->loading_global_data_ = false;
    this->load_global_data_watcher_.waitForFinished();
    this->level_loader_->close();
    //free spaces
    this->chunk_editor_widget_->clearData();
    this->village_editor_->clearData();
    this->player_editor_->clearData();
    this->level_dat_editor_->clearData();
    this->villages_.clear();
    this->resetToInitUI();
    return true;
}

void MainWindow::close_and_exit() {
    this->closeLevel();
    this->close();
}


void MainWindow::on_screenshot_btn_clicked() {
    this->map_widget_->saveImageAction(true);
}

void MainWindow::openNBTEditor() {
    auto *w = new NbtWidget();
    auto g = this->geometry();
    const int ext = 100;
    w->setWindowTitle("NBT Editor");
    w->setGeometry(QRect(g.x() + ext, g.y() + ext, g.width() - ext * 2, g.height() - ext * 2));
    w->show();
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


void MainWindow::handle_level_open_finished() {

    auto res = this->load_global_data_watcher_.result();
    if (!res) {
        if (!this->loading_global_data_) { //说明是主动停止的
            qDebug() << "Stop loading global data (by user)";
            return;
        }
        qDebug() << "Load global data failed";
        WARN("无法加载全局NBT数据，但是你仍然可以查看地图和区块数据");
    } else {
        // load players
        auto &player_list = this->level_loader_->level().player_list().data();
        std::vector<bl::palette::compound_tag *> players;
        std::vector<std::string> keys;
        std::vector<QImage *> icon_players;
        for (auto &kv: player_list) {
            icon_players.push_back(VillagerIcon(bl::village_key::PLAYERS));
            keys.emplace_back(kv.first);
            players.push_back(kv.second); //内部会复制数据，这里就不用复制了
        }
        this->player_editor_->load_new_data(players, [&](auto *) { return QString(); }, keys, icon_players);
        qDebug() << "Load player data finished";
        // load villages
        std::vector<bl::palette::compound_tag *> vss;
        std::vector<std::string> village_keys;
        auto &village_list = this->level_loader_->level().village_list().data();
        this->collect_villages(village_list);
        std::vector<QImage *> icons;
        for (auto &kv: village_list) {
            int index = 0;
            for (auto &p: kv.second) {
                if (p) {
                    village_keys.push_back(kv.first + "_" + bl::village_key::village_key_type_to_str(
                            static_cast<bl::village_key::key_type>(index)));
                    vss.push_back(dynamic_cast<bl::palette::compound_tag *>(p));
                    icons.push_back(VillagerIcon(static_cast<bl::village_key::key_type>(index)));
                }
                index++;
            }
        }

        this->village_editor_->load_new_data(
                vss, [&](auto *) { return QString(); }, village_keys, icons);
        qDebug() << "Load village data finished";
    }
    this->village_editor_->setVisible(true);
    this->player_editor_->setVisible(true);
    ui->save_players_btn->setVisible(true);
    ui->save_village_btn->setVisible(true);
    ui->player_nbt_loading_label->setVisible(false);
    ui->village_nbt_loading_label->setVisible(false);
    //打开完成了设置为可用（虽然）
    ui->open_level_btn->setEnabled(true);
}

void MainWindow::on_save_leveldat_btn_clicked() {
    if (!this->write_mode_) {
        WARN("未开启写模式");
        return;
    }

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
    if (!this->write_mode_) {
        WARN("未开启写模式");
        return;
    }

    std::unordered_map<std::string, std::array<bl::palette::compound_tag *, 4>> vs;
    this->village_editor_->foreachItem([&](const std::string &key, bl::palette::compound_tag *root) {
        auto village_key = bl::village_key::parse("VILLAGE_" + key);
        if (!village_key.valid()) {
            qDebug() << "Invalid village  key " << ("VILLAGE_" + key).c_str();
            return;
        }
        vs[village_key.uuid][static_cast<int>(village_key.type)] = dynamic_cast<bl::palette::compound_tag *>(root->copy());
    });
    if (this->level_loader_->modifyVillageList(vs)) {
        INFO("成功保存村庄数据");
    } else {
        INFO("无法保存村庄数据");
    }
}

void MainWindow::on_save_players_btn_clicked() {
    if (!this->write_mode_) {
        WARN("未开启写模式");
        return;
    }
    std::unordered_map<std::string, bl::palette::compound_tag *> newList;
    this->player_editor_->foreachItem([&](const std::string &key, bl::palette::compound_tag *root) {
        newList[key] = dynamic_cast<bl::palette::compound_tag *>(root->copy());
    });

    if (this->level_loader_->modifyPlayerList(newList)) {
        INFO("已成功保存玩家数据");
    } else {
        WARN("无法保存玩家数据");
    }
}


void
MainWindow::collect_villages(const std::unordered_map<std::string, std::array<bl::palette::compound_tag *, 4>> &vs) {
    for (auto kv: vs) {
        auto *nbt = kv.second[static_cast<int>(bl::village_key::key_type::INFO)];
        if (!nbt)continue;
        auto x0 = dynamic_cast<bl::palette::int_tag *>(nbt->get("X0"));
        auto z0 = dynamic_cast<bl::palette::int_tag *>(nbt->get("Z0"));
        auto x1 = dynamic_cast<bl::palette::int_tag *>(nbt->get("X1"));
        auto z1 = dynamic_cast<bl::palette::int_tag *>(nbt->get("Z1"));
        if (x0 && z0 && x1 && z1) {
            this->villages_.insert(kv.first.c_str(),
                                   QRect(std::min(x0->value, x1->value), std::min(z0->value, z1->value),
                                         std::abs(x0->value - x1->value),
                                         std::abs(z0->value - z1->value)
                                   ));
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
    if (this->levelLoader()->isOpen()) return;
    auto sz = this->size();
    const int w = static_cast<int>(std::min(sz.width(), sz.height()) * 1.2);
    QPainter p(this);
    int x = static_cast<int>(logoPos.x * sz.width());
    int z = static_cast<int>( logoPos.y * sz.height());
    p.translate(x, z);
    p.rotate(logoPos.angle);
    p.drawImage(QRect(-w / 2, -w / 2, w, w), *cfg::BG(), QRect(0, 0, 8, 8));
    QMainWindow::paintEvent(event);
}

void MainWindow::on_grid_btn_clicked() {

    auto r = this->map_widget_->toggleGrid();
    updateButtonBackground(ui->grid_btn, r);
}

void MainWindow::on_coord_btn_clicked() {
    auto r = this->map_widget_->toggleCoords();
    updateButtonBackground(ui->coord_btn, r);
}

void MainWindow::on_global_data_btn_clicked() {
    ui->global_nbt_pannel->setVisible(!ui->global_nbt_pannel->isVisible());
}

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
    auto software_str = cfg::SOFTWARE_NAME + " " + cfg::SOFTWARE_VERSION;
    std::string level_name;
    if (this->level_loader_->isOpen()) {
        level_name = this->level_loader_->level().dat().level_name();
    }
    return {(software_str + " " + level_name).c_str()};
}

