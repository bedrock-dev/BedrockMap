#include "mainwindow.h"

#include <QDesktopWidget>
#include <QDialog>
#include <QDir>
#include <QFileDialog>
#include <QGridLayout>
#include <QMessageBox>
#include <QSplitter>
#include <QStandardPaths>
#include <QtConcurrent>
#include <QtDebug>

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

}  // namespace

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent), ui(new Ui::MainWindow) {
    ui->setupUi(this);
    //level loader
    this->level_loader_ = new AsyncLevelLoader();
    //map widget
    this->map_widget_ = new MapWidget(this, nullptr);
    this->map_widget_->gotoBlockPos(0, 0);
    this->chunk_editor_widget_ = new ChunkEditorWidget(this);
    ui->map_visual_layout->addWidget(this->map_widget_);
    ui->splitter->addWidget(this->chunk_editor_widget_);
    ui->splitter->setStretchFactor(0, 1);
    ui->splitter->setStretchFactor(1, 3);
    ui->splitter->setStretchFactor(2, 1);

    this->chunk_editor_widget_->setVisible(false);

    // layer btn
    this->layer_btns_ = {{MapWidget::MainRenderType::Biome,   ui->biome_layer_btn},
                         {MapWidget::MainRenderType::Terrain, ui->terrain_layer_btn},
                         {MapWidget::MainRenderType::Height,  ui->height_layer_btn}
    };

    for (auto &kv: this->layer_btns_) {
        QObject::connect(kv.second, &QPushButton::clicked, [this, &kv]() {
            for (auto &x: this->layer_btns_) {
                x.second->setEnabled(x.first != kv.first);
            }
            this->map_widget_->changeLayer(kv.first);
        });
    }

    // Dimension Btn
    this->dim_btns_ = {
            {MapWidget::DimType::OverWorld, ui->overwrold_btn},
            {MapWidget::DimType::Nether,    ui->nether_btn},
            {MapWidget::DimType::TheEnd,    ui->theend_btn},
    };

    for (auto &kv: this->dim_btns_) {
        QObject::connect(kv.second, &QPushButton::clicked, [this, &kv]() {
            for (auto &x: this->dim_btns_) {
                x.second->setEnabled(x.first != kv.first);
            }
            this->map_widget_->changeDimension(kv.first);
            //            switch (kv.first) {
            //                case MapWidget::DimType::OverWorld:
            //                    ui->layer_slider->setRange(-64, 319);
            //                    break;
            //                case MapWidget::Nether:
            //                    ui->layer_slider->setRange(0, 127);
            //                    break;
            //                case MapWidget::TheEnd:
            //                    ui->layer_slider->setRange(0, 255);
            //                    break;
            //            }
        });
    }

    // Label and edit
    connect(this->map_widget_, SIGNAL(mouseMove(int, int)), this, SLOT(updateXZEdit(int, int)));

    // menu actions
    connect(ui->action_open, SIGNAL(triggered()), this, SLOT(openLevel()));
    connect(ui->action_close, SIGNAL(triggered()), this, SLOT(closeLevel()));
    connect(ui->action_exit, SIGNAL(triggered()), this, SLOT(close_and_exit()));
    connect(ui->action_NBT, SIGNAL(triggered()), this, SLOT(openNBTEditor()));
    connect(ui->action_about, &QAction::triggered, this, []() { AboutDialog().exec(); });

    // init stat
    ui->biome_layer_btn->setEnabled(true);
    ui->terrain_layer_btn->setEnabled(false);
    ui->overwrold_btn->setEnabled(false);
    ui->grid_checkbox->setChecked(true);
    ui->text_checkbox->setChecked(false);
    ui->debug_checkbox->setChecked(false);

    // debug
    ui->x_edit->setMaximumWidth(160);
    ui->z_edit->setMaximumWidth(160);

    // watcher

    connect(&this->delete_chunks_watcher_, &QFutureWatcher<bool>::finished, this,
            &MainWindow::handle_chunk_delete_finished);
    connect(&this->open_level_watcher_, &QFutureWatcher<bool>::finished, this,
            &MainWindow::handle_level_open_finished);

    //editor
    this->level_dat_editor_ = new NbtWidget();
    level_dat_editor_->hideLoadDataBtn();
    this->player_editor_ = new NbtWidget();
    player_editor_->hideLoadDataBtn();
    this->village_editor_ = new NbtWidget();
    village_editor_->hideLoadDataBtn();

    ui->level_dat_tab->layout()->replaceWidget(ui->level_dat_empty_widget, level_dat_editor_);
    ui->player_tab->layout()->replaceWidget(ui->player_empty_widget, player_editor_);
    ui->village_tab->layout()->replaceWidget(ui->village_empty_widget, village_editor_);

    ui->splitter_2->setStretchFactor(1, 3);
    ui->splitter_2->setStretchFactor(2, 2);
    ui->splitter_2->setVisible(false);
    QObject::connect(ui->open_level_btn, &QPushButton::clicked, this, &MainWindow::openLevel);


    connect(&this->render_filter_dialog_, &RenderFilterDialog::accepted, this, &MainWindow::applyFilter);

    this->setGeometry(centerMainWindowGeometry(0.5));
}

MainWindow::~MainWindow() { delete ui; }

void MainWindow::updateXZEdit(int x, int z) {
    bl::block_pos bp{x, 0, z};
    auto cp = bp.to_chunk_pos();
    ui->block_pos_label->setText(
            QString::number(x) + "," + QString::number(z) + "  /  [" + QString::number(cp.x) + "," +
            QString::number(cp.z) + "]");
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

void MainWindow::on_grid_checkbox_stateChanged(int arg1) { this->map_widget_->enableGrid(arg1 > 0); }

void MainWindow::on_text_checkbox_stateChanged(int arg1) { this->map_widget_->enableText(arg1 > 0); }

void MainWindow::openLevel() {

    if (this->level_loader_->isOpen())this->level_loader_->close();
    auto path = QStandardPaths::standardLocations(QStandardPaths::GenericDataLocation)[0] +
                "/Packages/Microsoft.MinecraftUWP_8wekyb3d8bbwe/LocalState/games/com.mojang/minecraftWorlds";
    QString root = QFileDialog::getExistingDirectory(this, tr("打开存档根目录"),
                                                     path,
                                                     QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
    if (root.size() == 0) {
        return;
    }

    ui->open_level_btn->setText(QString("正在打开\n") + root);
    ui->open_level_btn->setEnabled(false);
    auto future = QtConcurrent::run([this](const QString &path) {
        try {
            auto res = this->level_loader_->open(path.toStdString());
            if (!res) {
                return false;
            }
            this->level_loader_->level().load_global_data();
            return true;
        } catch (std::exception &e) {
            return false;
        }
    }, root);
    this->open_level_watcher_.setFuture(future);
}

void MainWindow::closeLevel() {
    this->level_loader_->close();
    this->setWindowTitle(QString(cfg::SOFTWARE_NAME) + " " + QString(cfg::SOFTWARE_VERSION));
    ui->splitter_2->setVisible(false);
    ui->open_level_btn->setVisible(true);
    ui->open_level_btn->setText("未打开存档");
    ui->global_nbt_checkbox->setChecked(false);
}

void MainWindow::close_and_exit() {
    this->level_loader_->close();
    this->close();
}

void MainWindow::on_debug_checkbox_stateChanged(int arg1) { this->map_widget_->enableDebug(arg1 > 0); }

//void MainWindow::toggle_chunk_edit_view() {
//    auto x = this->chunk_editor_widget_->isVisible();
//    this->chunk_editor_widget_->setVisible(!x);
//}


void MainWindow::on_enable_chunk_edit_check_box_stateChanged(int arg1) { this->write_mode_ = arg1 > 0; }

void MainWindow::on_screenshot_btn_clicked() {
    this->map_widget_->saveImage(true);
}

void MainWindow::openNBTEditor() {
    auto *w = new NbtWidget();
    auto g = this->geometry();
    const int ext = 100;
    w->setWindowTitle("NBT Editor");
    w->setGeometry(QRect(g.x() + ext, g.y() + ext, g.width() - ext * 2, g.height() - ext * 2));
    w->show();
}

// void MainWindow::on_refresh_cache_btn_clicked() {
//    //collect filter;
//    RenderFilter f;
//    //    f.enable_layer_ = ui->enable_layer_checkbox->isChecked();
//    //    f.layer_ = ui->layer_slider->value();

//    //    world_.setFilter(f);

//    this->level_loader_->clearAllCache();
//}

void MainWindow::on_slime_layer_btn_clicked() { this->map_widget_->toggleSlime(); }

void MainWindow::on_actor_layer_btn_clicked() { this->map_widget_->toggleActor(); }

void MainWindow::on_village_layer_btn_clicked() { this->map_widget_->toggleVillage(); }

void MainWindow::on_hsa_layer_btn_clicked() { this->map_widget_->toggleHSAs(); }

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


void MainWindow::on_global_nbt_checkbox_stateChanged(int arg1) { ui->global_nbt_pannel->setVisible(arg1 > 0); }

void MainWindow::handle_level_open_finished() {

    auto res = this->open_level_watcher_.result();
    if (!res) {
        QMessageBox::warning(nullptr, "警告", "无法打开存档", QMessageBox::Yes, QMessageBox::Yes);
        ui->open_level_btn->setText("未打开存档");
    } else {
        this->refreshTitle();
        auto *ld = dynamic_cast<bl::palette::compound_tag *>(this->level_loader_->level().dat().root());
        this->level_dat_editor_->load_new_data({ld}, [](auto *) { return "level.dat"; }, {});
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

        //load villages
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

        this->village_editor_->load_new_data(vss, [&](auto *) { return QString(); }, village_keys, icons);
        ui->splitter_2->setVisible(true);
        ui->open_level_btn->setVisible(false);
        ui->global_nbt_pannel->setVisible(false);

        this->setWindowState(Qt::WindowMaximized);
        auto sp = this->level_loader_->level().dat().spawn_position();
        qDebug() << sp.x << "  " << sp.z;
        ui->x_edit->setText(QString::number(sp.x));
        ui->z_edit->setText(QString::number(sp.z));
        this->map_widget_->gotoBlockPos(sp.x, sp.z);
    }
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
        this->refreshTitle();
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

void MainWindow::refreshTitle() {
    auto levelName = QString();
    if (this->level_loader_->isOpen())
        levelName = this->level_loader_->level().dat().level_name().c_str();
    this->setWindowTitle(QString(cfg::SOFTWARE_NAME) + " " + QString(cfg::SOFTWARE_VERSION) + " - " + levelName);
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
}
