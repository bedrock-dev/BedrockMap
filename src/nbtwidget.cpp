#include "nbtwidget.h"

#include <qaction.h>
#include <qchar.h>
#include <qdialog.h>
#include <qobject.h>

#include <QFileDialog>
#include <QFont>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QMessageBox>
#include <QPair>
#include <QString>
#include <QTreeWidgetItem>
#include <QtDebug>

#include "msg.h"
#include "nbtmodifydialog.h"
#include "palette.h"
#include "resourcemanager.h"
#include "ui_nbtwidget.h"
#include "utils.h"

namespace {

    NBTTreeItem *nbt2QTreeItem(bl::palette::abstract_tag *t, int index, int &ma) {
        ma = std::max(ma, index);
        using namespace bl::palette;
        if (!t) return nullptr;
        auto *item = new NBTTreeItem();
        item->root_ = t;
        item->updateLabel();
        item->setIcon(0, QIcon(QPixmap::fromImage(*TagIcon(t->type()))));
        if (t->type() == bl::palette::tag_type::Compound) {
            auto *ct = dynamic_cast<bl::palette::compound_tag *>(t);
            for (auto &kv : ct->value) {
                item->addChild(nbt2QTreeItem(kv.second, index + 1, ma));
            }
        } else if (t->type() == bl::palette::tag_type::List) {
            auto *ct = dynamic_cast<bl::palette::list_tag *>(t);
            for (auto k : ct->value) {
                item->addChild(nbt2QTreeItem(k, index + 1, ma));
            }
        }
        return item;
    }

    NBTListItem *TN(QListWidgetItem *i) { return dynamic_cast<NBTListItem *>(i); }

}  // namespace

bool NBTTreeItem::tryAddChild(bl::palette::abstract_tag *tag) {
    if (!tag || !root_) return false;
    auto type = root_->type();
    int max;
    auto *item = nbt2QTreeItem(tag, 1, max);
    if (type == tag_type::Compound) {
        auto *cur = dynamic_cast<compound_tag *>(root_);
        if (cur->value.count(tag->key()) > 0) {
            delete item;
            return false;
        }
        cur->put(tag);
        this->addChild(item);
    } else if (type == tag_type::List) {
        auto *cur = dynamic_cast<list_tag *>(root_);
        if (cur->push_back(tag)) {
            this->addChild(item);
        } else {
            return false;
        }
    }
    return true;
}

NbtWidget::NbtWidget(QWidget *parent) : QWidget(parent), ui(new Ui::NbtWidget) {
    ui->setupUi(this);
    ui->splitter->setStretchFactor(0, 1);
    ui->splitter->setStretchFactor(1, 2);
    QFont f;
    f.setFamily("JetBrains Mono");
    ui->list_widget->setFont(f);
    ui->tree_widget->setFont(f);
    ui->tree_widget->setHeaderHidden(true);
    ui->tree_widget->setContextMenuPolicy(Qt::CustomContextMenu);
    ui->list_widget->setContextMenuPolicy(Qt::CustomContextMenu);
    // right menu
    connect(ui->tree_widget, &QTreeWidget::customContextMenuRequested, this, &NbtWidget::prepareTreeWidgetMenu);
    connect(ui->list_widget, &QListWidget::customContextMenuRequested, this, &NbtWidget::prepareListWidgetMenu);

    this->refreshLabel();
    this->clearModifyCache();
}

void NbtWidget::on_load_btn_clicked() {
    auto fileName = QFileDialog::getOpenFileName(this, tr("Open File"), R"(C:\Users\xhy\dev\bedrock-level\data\dumps\)",
                                                 tr("NBT Files (*.nbt  *.nbts *.mcstructure *.palette)"));
    if (fileName.isEmpty()) return;
    auto data = bl::utils::read_file(fileName.toStdString());
    if (data.empty()) {
        WARN("无法打开文件");
        return;
    }

    auto palette = bl::palette::read_palette_to_end(data.data(), data.size());

    if (palette.empty()) {
        QMessageBox::information(nullptr, "警告", "无法解析NBT数据(请确定是基岩版格式)", QMessageBox::Yes, QMessageBox::Yes);
        return;
    }

    size_t i = 0;
    std::vector<NBTListItem *> items;
    for (auto *nbt : palette) {
        items.push_back(NBTListItem::from(nbt, QString::number(i)));
        i++;
    }
    this->loadNewData(items);
}

void NbtWidget::openNBTItem(bl::palette::compound_tag *root) {
    assert(root);
    this->extra_load_event_(root);
    ui->tree_widget->clear();
    int max_col = 0;
    ui->tree_widget->setColumnCount(1);
    auto *top = nbt2QTreeItem(root, 1, max_col);
    ui->tree_widget->addTopLevelItem(top);
    // only expand the top level
    ui->tree_widget->expandItem(top);
}

void NbtWidget::on_list_widget_itemDoubleClicked(QListWidgetItem *item) {
    auto *nbtItem = TN(item);
    if (!nbtItem) {
        QMessageBox::information(nullptr, "警告", "空的nbt数据", QMessageBox::Yes, QMessageBox::Yes);
        return;
    }
    this->current_opened_ = nbtItem;
    qDebug() << "Select NBT item : [" << this->current_opened_->raw_key << "] -> " << nbtItem->getLabel();
    this->openNBTItem(nbtItem->root_);
    this->refreshLabel();
}

void NbtWidget::hideLoadDataBtn() { ui->load_btn->setVisible(false); }

void NbtWidget::on_save_btn_clicked() { this->saveNBTs(false); }

void NbtWidget::prepareTreeWidgetMenu(const QPoint &pos) {
    if (!modify_allowed_) return;
    auto *addAction = new QAction("新建", this);
    auto *removeAction = new QAction("删除", this);
    auto *modifyAction = new QAction("修改", this);
    auto *clearAction = new QAction("清空", this);

    QMenu menu(this);
    auto *current = ui->tree_widget->currentItem();
    if (!current) return;
    auto *nbtItem = dynamic_cast<NBTTreeItem *>(current);
    if (!nbtItem || !nbtItem->root_) {
        WARN("当前NBT数据已损坏");
        return;
    }
    auto type = nbtItem->root_->type();
    auto attr = NBTNodeUIAttr::get(type);
    if (attr.canAdd) menu.addAction(addAction);
    if (attr.canRemove) menu.addAction(removeAction);
    if (attr.canModify) menu.addAction(modifyAction);
    if (attr.canClear) menu.addAction(clearAction);

    QObject::connect(addAction, &QAction::triggered, [this, pos](bool) {
        auto current = dynamic_cast<NBTTreeItem *>(ui->tree_widget->currentItem());
        if (!current) return;
        NBTModifyDialog dialog(this);
        if (!dialog.setCreateMode(current->root_)) {
            WARN("初始化失败");
            return;
        }
        if (dialog.exec() == QDialog::Accepted) {
            QString err;
            auto *tag = dialog.createTagWithCurrent(err);
            if (!tag) {
                WARN("创建节点失败: " + err);
            } else if (!current->tryAddChild(tag)) {
                WARN("创建节点失败: 已存在相同key的TAG");
            } else {
                current->updateLabel();
                putModifyToCache(current_opened_->raw_key.toStdString(), current_opened_->root_->to_raw());
            }
        }
    });

    QObject::connect(modifyAction, &QAction::triggered, [this, pos](bool) {
        auto current = dynamic_cast<NBTTreeItem *>(ui->tree_widget->currentItem());
        if (!current) return;
        NBTModifyDialog dialog(this);
        if (!dialog.setModifyMode(current->root_)) {
            WARN("初始化失败");
            return;
        }
        if (dialog.exec() == QDialog::Accepted) {
            QString err;
            if (!dialog.modityCurrentTag(current->root_, err)) {
                WARN("修改节点失败： " + err);
            } else {
                current->updateLabel();
                putModifyToCache(current_opened_->raw_key.toStdString(), current_opened_->root_->to_raw());
            }
        }
    });

    QObject::connect(removeAction, &QAction::triggered, [this, pos](bool) {
        auto current = dynamic_cast<NBTTreeItem *>(ui->tree_widget->currentItem());
        auto parent = dynamic_cast<NBTTreeItem *>(ui->tree_widget->currentItem()->parent());
        if (!parent) {
            WARN("不能删除根节点");
            return;
        }
        auto parentType = parent->root_->type();
        if (parentType == bl::palette::Compound) {
            auto *tag = dynamic_cast<bl::palette::compound_tag *>(parent->root_);
            if (!tag) {
                WARN("当前NBT数据已损坏");
                return;
            }
            tag->remove(current->root_->key());
        } else if (parentType == bl::palette::List) {
            auto *tag = dynamic_cast<bl::palette::list_tag *>(parent->root_);
            if (!tag) {
                WARN("当前NBT数据已损坏");
                return;
            }
            auto idx = parent->indexOfChild(current);
            tag->remove(idx);
            parent->updateLabel();
        }
        putModifyToCache(current_opened_->raw_key.toStdString(), current_opened_->root_->to_raw());
        parent->removeChild(current);
        delete current;
    });

    QObject::connect(clearAction, &QAction::triggered, [this, pos](bool) {
        auto current = dynamic_cast<NBTTreeItem *>(ui->tree_widget->currentItem());
        if (!current) return;
        if (current->root_->type() == bl::palette::tag_type::Compound) {
            auto *tag = dynamic_cast<bl::palette::compound_tag *>(current->root_);
            for (auto &kv : tag->value) {
                delete kv.second;
            }
            tag->value.clear();
        } else if (current->root_->type() == bl::palette::tag_type::List) {
            auto *tag = dynamic_cast<bl::palette::list_tag *>(current->root_);
            for (auto *child : tag->value) {
                delete child;
            }
            tag->value.clear();
        }
        auto children = current->takeChildren();
        qDeleteAll(children);
        current->updateLabel();
        putModifyToCache(current_opened_->raw_key.toStdString(), current_opened_->root_->to_raw());
    });
    menu.exec(ui->tree_widget->mapToGlobal(pos));
}

void NbtWidget::prepareListWidgetMenu(const QPoint &pos) {
    // 单选模式
    if (ui->list_widget->selectionMode() == QAbstractItemView::SingleSelection) {
        auto *removeAction = new QAction("删除", this);
        auto *exportAction = new QAction("导出选中", this);
        auto *createAction = new QAction("新建", this);
        auto *clearAction = new QAction("清空", this);
        QMenu menu(this);
        menu.addAction(exportAction);
        if (modify_allowed_) {
            menu.addAction(removeAction);
            menu.addAction(createAction);
            menu.addAction(clearAction);
        }

        QObject::connect(removeAction, &QAction::triggered, [this, pos](bool) {
            auto *currnet = this->ui->list_widget->currentItem();
            if (!currnet) return;
            auto *nbtItem = dynamic_cast<NBTListItem *>(currnet);
            if (nbtItem == this->current_opened_) ui->tree_widget->clear();
            putRemoveToCache(nbtItem->raw_key.toStdString());
            ui->list_widget->removeItemWidget(nbtItem);
            this->refreshLabel();
            delete nbtItem;
        });
        QObject::connect(exportAction, &QAction::triggered, [this, pos](bool) { this->saveNBTs(true); });
        QObject::connect(createAction, &QAction::triggered, [this, pos](bool) {
            if (!this->modify_allowed_) return;
            // create a new NBT item and push back to the end
            auto *nbtItem = NBTListItem::from(new bl::palette::compound_tag("New"), QString::number(ui->list_widget->count()));
            ui->list_widget->addItem(nbtItem);
            putModifyToCache(nbtItem->raw_key.toStdString(), nbtItem->root_->to_raw());
            this->refreshLabel();
        });
        QObject::connect(clearAction, &QAction::triggered, [this, pos](bool) {
            if (!this->modify_allowed_) return;
            auto reply = QMessageBox::question(this, "清空", "确定要清空所有数据吗？", QMessageBox::Yes | QMessageBox::No);
            if (reply != QMessageBox::Yes) return;
            ui->tree_widget->clear();
            // clear all items in the list widget, and put them to the modify cache, free the memory
            auto row = ui->list_widget->count();
            for (int i = 0; i < row; ++i) {
                auto *item = dynamic_cast<NBTListItem *>(ui->list_widget->item(i));
                if (item) {
                    putRemoveToCache(item->raw_key.toStdString());
                } else {
                    BL_ERROR("当前NBT数据已损坏");
                }
            }
            ui->list_widget->clear();
            this->refreshLabel();
        });
        menu.exec(ui->list_widget->mapToGlobal(pos));
    } else {
        // 多选模式
        auto *removeSelect = new QAction("删除选中", this);
        auto *unselectAll = new QAction("全不选", this);
        auto *exportAction = new QAction("导出选中", this);

        QObject::connect(removeSelect, &QAction::triggered, [this, pos](bool) {
            if (!this->modify_allowed_) return;
            ui->list_widget->blockSignals(true);
            for (auto &item : ui->list_widget->selectedItems()) {
                auto *nbtItem = dynamic_cast<NBTListItem *>(item);
                // 防止闪退
                if (nbtItem == this->current_opened_) ui->tree_widget->clear();
                putRemoveToCache(nbtItem->raw_key.toStdString());
                ui->list_widget->removeItemWidget(item);
                delete item;
                this->refreshLabel();
            }
        });

        QObject::connect(unselectAll, &QAction::triggered, [this, pos](bool) {
            ui->list_widget->clearSelection();
            this->refreshLabel();
        });
        QObject::connect(exportAction, &QAction::triggered, [this, pos](bool) { this->saveNBTs(true); });

        QMenu menu(this);
        if (modify_allowed_) {
            menu.addAction(removeSelect);
        }

        menu.addAction(unselectAll);
        menu.addAction(exportAction);
        menu.exec(ui->list_widget->mapToGlobal(pos));
    }
}

void NbtWidget::loadNewData(const std::vector<NBTListItem *> &items) {
    this->clearData();
    for (auto *item : items) {
        ui->list_widget->addItem(item);
    }
    this->refreshLabel();
}

void NbtWidget::on_multi_select_checkbox_stateChanged(int arg1) {
    if (arg1 > 0) {
        ui->list_widget->setSelectionMode(QAbstractItemView::MultiSelection);
    } else {
        ui->list_widget->setSelectionMode(QAbstractItemView::SingleSelection);
        ui->list_widget->clearSelection();
    }
    this->refreshLabel();
}

void NbtWidget::on_modify_checkbox_stateChanged(int arg1) {
    modify_allowed_ = arg1 > 0;
    this->refreshLabel();
}

void NbtWidget::saveNBTs(bool selectOnly) {
    // save
    auto fileName = QFileDialog::getSaveFileName(this, tr("Save File"), "C:/Users/xhy/Desktop/", tr("NBT files (*.*)"));
    if (fileName.size() == 0) return;
    std::string res;
    if (selectOnly) {
        for (auto &item : ui->list_widget->selectedItems()) {
            if (!item->isHidden()) res += dynamic_cast<NBTListItem *>(item)->root_->to_raw();
        }
    } else {
        for (int i = 0; i < ui->list_widget->count(); ++i) {
            auto *item = ui->list_widget->item(i);
            if (!item->isHidden()) res += dynamic_cast<NBTListItem *>(item)->root_->to_raw();
        }
    }

    bl::utils::write_file(fileName.toStdString(), res.data(), res.size());
}

std::string NbtWidget::getCurrentPaletteRaw() {
    std::string res;
    for (int i = 0; i < ui->list_widget->count(); ++i) {
        res += dynamic_cast<NBTListItem *>(ui->list_widget->item(i))->root_->to_raw();
    }
    return res;
}

std::vector<bl::palette::compound_tag *> NbtWidget::getPaletteCopy() {
    std::vector<bl::palette::compound_tag *> res;
    for (int i = 0; i < ui->list_widget->count(); ++i) {
        res.push_back(dynamic_cast<bl::palette::compound_tag *>(dynamic_cast<NBTListItem *>(ui->list_widget->item(i))->root_->copy()));
    }
    return res;
}

void NbtWidget::foreachItem(const std::function<void(const std::string &, bl::palette::compound_tag *)> &func) {
    for (int i = 0; i < ui->list_widget->count(); ++i) {
        auto *item = dynamic_cast<NBTListItem *>(ui->list_widget->item(i));
        if (item) {
            func(item->getLabel().toStdString(), item->root_);
        }
    }
}

void NbtWidget::on_search_edit_textEdited(const QString &arg1) {
    ui->list_widget->clearSelection();
    for (int i = 0; i < ui->list_widget->count(); ++i) {
        auto *item = ui->list_widget->item(i);
        item->setHidden(!item->text().contains(arg1));
    }
    this->refreshLabel();
}

void NbtWidget::refreshLabel() {
    int selected = 0;
    int notHidden = 0;
    for (int i = 0; i < ui->list_widget->count(); ++i) {
        auto *item = ui->list_widget->item(i);
        if (!item) continue;
        if (!item->isHidden()) {
            notHidden++;
        }
        if (item->isSelected()) selected++;
    }
    ui->item_num_label->setText(QString("%1 / %2").arg(QString::number(notHidden), QString::number(selected)));
}

void NbtWidget::on_list_widget_itemSelectionChanged() { this->refreshLabel(); }

void NbtWidget::clearData() {
    ui->list_widget->clear();
    ui->tree_widget->clear();
    this->current_opened_ = nullptr;
    this->clearModifyCache();
}

NbtWidget::~NbtWidget() {
    delete ui;
    this->clearData();
}

void NbtWidget::putModifyToCache(const std::string &key, const std::string &value) {
    if (enable_modify_cache_) {
        this->modified_cache_[key] = value;
    };
    // 日志不受影响
    if (value.empty()) {
        qDebug() << "Delete key: " << key.c_str();
    } else {
        qDebug() << "Modify key: " << key.c_str() << " -> Data[" << value.size() << "]";
    }
}
