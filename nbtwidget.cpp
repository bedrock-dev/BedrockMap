#include "nbtwidget.h"

#include <QFileDialog>
#include <QFont>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QMessageBox>
#include <QPair>
#include <QString>
#include <QTreeWidgetItem>
#include <QtDebug>

#include "config.h"
#include "iconmanager.h"
#include "msg.h"
#include "palette.h"
#include "ui_nbtwidget.h"
#include "utils.h"

namespace {

    NBTTreeItem *nbt2QTreeItem(bl::palette::abstract_tag *t, int index, int &ma) {
        ma = std::max(ma, index);
        using namespace bl::palette;
        if (!t) return nullptr;
        auto *item = new NBTTreeItem();
        item->root_ = t;
        item->setIcon(0, QIcon(QPixmap::fromImage(*TagIcon(t->type()))));
        if (t->type() == bl::palette::tag_type::Compound) {
            item->setText(0, t->key().c_str());
            auto *ct = dynamic_cast<bl::palette::compound_tag *>(t);
            for (auto &kv: ct->value) {
                item->addChild(nbt2QTreeItem(kv.second, index + 1, ma));
            }
        } else if (t->type() == bl::palette::tag_type::List) {
            auto *ct = dynamic_cast<bl::palette::list_tag *>(t);
            item->setText(0, t->key().c_str() + QString("[%1]").arg(QString::number(ct->value.size())));
            for (auto k: ct->value) {
                item->addChild(nbt2QTreeItem(k, index + 1, ma));
            }
        } else {
            item->setText(0, item->getRawText());
        }

        return item;
    }

    NBTListItem *TN(QListWidgetItem *i) { return dynamic_cast<NBTListItem *>(i); }

}  // namespace

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
    //right menu
    connect(ui->tree_widget, &QTreeWidget::customContextMenuRequested, this, &NbtWidget::prepareTreeWidgetMenu);
    connect(ui->list_widget, &QListWidget::customContextMenuRequested, this, &NbtWidget::prepareListWidgetMenu);

    this->refreshLabel();
    this->clearModifyCache();
}


void NbtWidget::on_load_btn_clicked() {
    auto fileName = QFileDialog::getOpenFileName(this, tr("Open File"),
                                                 R"(C:\Users\xhy\dev\bedrock-level\data\dumps\)",
                                                 tr("NBT Files (*.nbt  *.nbts *.mcstructure *.palette)"));
    if (fileName.isEmpty()) return;
    auto data = bl::utils::read_file(fileName.toStdString());
    if (data.empty()) {
        WARN("无法打开文件");
        return;
    }

    auto palette = bl::palette::read_palette_to_end(data.data(), data.size());

    if (palette.empty()) {
        QMessageBox::information(nullptr, "警告", "无法解析NBT数据(请确定是基岩版格式)", QMessageBox::Yes,
                                 QMessageBox::Yes);
        return;
    }

    size_t i = 0;
    std::vector<NBTListItem *> items;
    for (auto *nbt: palette) {
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
    //only expand the top level
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

void NbtWidget::hideLoadDataBtn() {
    ui->load_btn->setVisible(false);
}

void NbtWidget::on_save_btn_clicked() {
    this->saveNBTs(false);
}


void NbtWidget::prepareTreeWidgetMenu(const QPoint &pos) {}

void NbtWidget::prepareListWidgetMenu(const QPoint &pos) {
    //单选模式
    if (ui->list_widget->selectionMode() == QAbstractItemView::SingleSelection) {
        auto *removeAction = new QAction("删除", this);
        auto *exportAction = new QAction("导出选中", this);
        QMenu menu(this);
        menu.addAction(exportAction);
        if (modify_allowed_) {
            menu.addAction(removeAction);
        }

        QObject::connect(removeAction, &QAction::triggered, [this, pos](bool) {
            auto *nbtItem = dynamic_cast <NBTListItem *>(this->ui->list_widget->currentItem());
            if (nbtItem == this->current_opened_) ui->tree_widget->clear();
            this->putModifyCache(nbtItem->raw_key.toStdString(), "");
            ui->list_widget->removeItemWidget(nbtItem);
            this->refreshLabel();
            delete nbtItem;
        });
        QObject::connect(exportAction, &QAction::triggered, [this, pos](bool) {
            this->saveNBTs(true);
        });
        menu.exec(ui->list_widget->mapToGlobal(pos));
    } else {
        //多选模式
        auto *removeSelect = new QAction("删除选中", this);
        auto *unselectAll = new QAction("全不选", this);
        auto *exportAction = new QAction("导出选中", this);

        QObject::connect(removeSelect, &QAction::triggered, [this, pos](bool) {
            if (!this->modify_allowed_)return;
            for (auto &item: ui->list_widget->selectedItems()) {
                auto *nbtItem = dynamic_cast<NBTListItem *>(item);
                //防止闪退
                if (nbtItem == this->current_opened_) ui->tree_widget->clear();
                this->putModifyCache(nbtItem->raw_key.toStdString(), "");
                ui->list_widget->removeItemWidget(item);
                delete item;
                this->refreshLabel();
            }
        });

        QObject::connect(unselectAll, &QAction::triggered, [this, pos](bool) {
            ui->list_widget->clearSelection();
            this->refreshLabel();
        });
        QObject::connect(exportAction, &QAction::triggered, [this, pos](bool) {
            this->saveNBTs(true);
        });

        QMenu menu(this);
        if (modify_allowed_) {
            menu.addAction(removeSelect);
        }

        menu.addAction(unselectAll);
        menu.addAction(exportAction);
        menu.exec(ui->list_widget->mapToGlobal(pos));
    }

}


void NbtWidget::on_tree_widget_itemDoubleClicked(QTreeWidgetItem *item, int column) {
    using namespace bl::palette;
    auto *it = dynamic_cast<NBTTreeItem *>(item);
    if (!it || !it->root_) { //正常来说不会出现
        QMessageBox::information(nullptr, "信息", "当前nbt已损坏", QMessageBox::Yes, QMessageBox::Yes);
        return;
    }
    auto t = it->root_->type();
    if (t == bl::palette::Compound || t == bl::palette::List) {
        if (it->isExpanded()) {
            ui->tree_widget->expandItem(it);
        } else {
            ui->tree_widget->collapseItem(it);
        }
        return;
    }
    if (t == ByteArray || t == LongArray || t == IntArray) {
        return;
    }

    if (!this->modify_allowed_)return;
    if (!this->current_opened_)return;

    QInputDialog d;
    d.setLabelText(it->root_->key().c_str());
    d.setOkButtonText("确定");
    d.setCancelButtonText("取消");
    d.resize(600, 400);
    d.setWindowIcon(it->icon(0));
    auto *r = it->root_;
    if (t == bl::palette::String) {
        d.setWindowTitle("编辑字符串");
        d.setTextValue(it->root_->value_string().c_str());
        d.setInputMode(QInputDialog::InputMode::TextInput);
    } else if (t == Float || t == Double) {
        if (t == Float) {
            d.setDoubleRange(std::numeric_limits<float>::min(), std::numeric_limits<float>::max());
            d.setDoubleValue(dynamic_cast<float_tag *>(r)->value);
        } else {
            d.setDoubleRange(std::numeric_limits<double>::min(), std::numeric_limits<double>::max());
            d.setDoubleValue(dynamic_cast<double_tag *>(r)->value);
        }
        d.setWindowTitle("编辑浮点数");
        d.setInputMode(QInputDialog::InputMode::DoubleInput);

        d.setDoubleDecimals(10);
    } else {
        d.setWindowTitle("编辑整数");
        d.setInputMode(QInputDialog::InputMode::IntInput);
        int min{INT32_MIN};
        int max{INT32_MAX};
        if (t == bl::palette::Byte) {
            min = -128;
            max = 127;
        } else if (t == bl::palette::Short) {
            min = INT16_MIN;
            max = INT16_MAX;
        }
        d.setIntRange(min, max);
        d.setIntValue(QString(r->value_string().c_str()).toInt());
    }
    auto ok = d.exec();
    if (!ok)return;

    switch (it->root_->type()) {
        case Byte:
            dynamic_cast<byte_tag *>(r)->value = static_cast<int8_t>(d.intValue());
            break;
        case Short:
            dynamic_cast<short_tag *>(r)->value = static_cast<int16_t>(d.intValue());
            break;
        case Int:
            dynamic_cast<int_tag *>(r)->value = static_cast<int32_t>(d.intValue());
            break;
        case Long:
            dynamic_cast<long_tag *>(r)->value = static_cast<int64_t>(d.intValue());
            break;
        case Float:
            dynamic_cast<float_tag *>(r)->value = static_cast<float >(d.doubleValue());
            break;
        case Double:
            dynamic_cast<double_tag *>(r)->value = static_cast<double >(d.doubleValue());
            break;
        case String:
            dynamic_cast<string_tag *>(r)->value = d.textValue().toStdString();
            break;
        case List:
        case Compound:
        case End:
        case ByteArray:
        case IntArray:
        case LongArray:
            break;
    }

    this->putModifyCache(this->current_opened_->raw_key.toStdString(), this->current_opened_->root_->to_raw());
    it->setText(0, it->getRawText());
}

void NbtWidget::load_new_data(const std::vector<bl::palette::compound_tag *> &data,
                              const std::function<QString(bl::palette::compound_tag *)> &namer,
                              const std::vector<std::string> &default_labels,
                              const std::vector<QImage *> &icons
) {
    this->clearData();
    bool needIcon{icons.size() == data.size()};
    for (int i = 0; i < data.size(); i++) {
        auto *it = new NBTListItem();
        it->root_ = dynamic_cast<bl::palette::compound_tag *>(data[i]->copy());
        it->default_label = i < default_labels.size() ? default_labels[i].c_str() : QString(i);
        it->namer_ = namer;
        if (needIcon) {
            it->setIcon(QPixmap::fromImage(*icons[i]));
        }
        it->setText(it->getLabel());
        ui->list_widget->addItem(it);
    }
    this->refreshLabel();
}


void NbtWidget::loadNewData(const std::vector<NBTListItem *> &items) {
    this->clearData();
    for (auto *item: items) {
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
    auto fileName = QFileDialog::getSaveFileName(this, tr("Save File"),
                                                 "C:/Users/xhy/Desktop/",
                                                 tr("NBT files (*.*)"));
    if (fileName.size() == 0)return;
    std::string res;
    if (selectOnly) {
        for (auto &item: ui->list_widget->selectedItems()) {
            if (!item->isHidden())
                res += dynamic_cast<NBTListItem *>(item)->root_->to_raw();
        }
    } else {
        for (int i = 0; i < ui->list_widget->count(); ++i) {
            auto *item = ui->list_widget->item(i);
            if (!item->isHidden())
                res += dynamic_cast<NBTListItem *>(item)->root_->to_raw();
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
        res.push_back(dynamic_cast<bl::palette::compound_tag *> (   dynamic_cast<NBTListItem *>(ui->list_widget->item(
                i))->root_->copy()));
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
        if (!item)continue;
        if (!item->isHidden()) {
            notHidden++;
        }
        if (item->isSelected())selected++;
    }
    ui->item_num_label->setText(QString("%1 / %2").arg(QString::number(notHidden), QString::number(selected)));
}

void NbtWidget::on_list_widget_itemSelectionChanged() { this->refreshLabel(); }

void NbtWidget::clearData() {
    ui->list_widget->clear();
    ui->tree_widget->clear();
}

NbtWidget::~NbtWidget() {
    delete ui;
    this->clearData();
}

void NbtWidget::putModifyCache(const std::string &key, const std::string &value) {
    this->modified_cache_[key] = value;
    if (value.empty()) {
        qDebug() << "Delete key: " << key.c_str();
    } else {
        qDebug() << "Modify key: " << key.c_str() << " -> Data[" << value.size() << "]";
    }
}

