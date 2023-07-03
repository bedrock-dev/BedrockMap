#include "nbtwidget.h"

#include <QFileDialog>
#include <QFont>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QMessageBox>
#include <QPair>
#include <QString>
#include <QTreeWidgetItem>
#include <QVariant>
#include <QtDebug>
#include <QItemSelectionModel>
#include "palette.h"
#include "ui_nbtwidget.h"
#include "utils.h"

namespace {

    QString nbtTagIconName(bl::palette::tag_type t) {
        using namespace bl::palette;
        std::unordered_map<tag_type, std::string> names{
                {tag_type::Int,      "Int"},
                {tag_type::Byte,     "Byte"},
                {tag_type::Compound, "Compound"},
                {tag_type::Double,   "Double"},
                {tag_type::Float,    "Float"},
                {tag_type::List,     "List"},
                {tag_type::Long,     "Long"},
                {tag_type::Short,    "Short"},
                {tag_type::String,   "String"}};
        auto it = names.find(t);
        if (it == names.end()) return ":/res/nbt/Tag_End.ico";
        return QString(":/res/nbt/TAG_") + it->second.c_str() + ".ico";
    }

    NBTTreeItem *nbt2QTreeItem(bl::palette::abstract_tag *t, int index, int &ma) {
        ma = std::max(ma, index);
        using namespace bl::palette;
        if (!t) return nullptr;
        auto *item = new NBTTreeItem();
        item->root_ = t;
        item->setIcon(0, QIcon(nbtTagIconName(t->type())));
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
            item->setFlags(item->flags() | Qt::ItemIsEditable);
        }

        return item;
    }
}  // namespace

NbtWidget::NbtWidget(QWidget *parent) : QWidget(parent), ui(new Ui::NbtWidget) {
    ui->setupUi(this);
    ui->splitter->setStretchFactor(0, 1);
    ui->splitter->setStretchFactor(1, 5);
    QFont f;
    f.setFamily("JetBrainsMono NFM");
    f.setPointSize(10);
    ui->list_widget->setFont(f);
    ui->tree_widget->setFont(f);
    ui->tree_widget->setHeaderHidden(true);
    ui->tree_widget->setContextMenuPolicy(Qt::CustomContextMenu);
    ui->list_widget->setContextMenuPolicy(Qt::CustomContextMenu);

    //right menu
    connect(ui->tree_widget, &QTreeWidget::customContextMenuRequested, this, &NbtWidget::prepareTreeWidgetMenu);
    connect(ui->list_widget, &QListWidget::customContextMenuRequested, this, &NbtWidget::prepareListWidgetMenu);

}

NbtWidget::~NbtWidget() { delete ui; }

void NbtWidget::on_load_btn_clicked() {
    auto fileName = QFileDialog::getOpenFileName(this, tr("Open File"),
                                                 R"(C:\Users\xhy\dev\bedrock-level\data\dumps\)",
                                                 tr("NBT Files (*.nbt  *.nbts *.mcstructure *.palette)"));
    if (fileName.isEmpty()) return;
    auto data = bl::utils::read_file(fileName.toStdString());
    if (data.empty()) {
        QMessageBox::information(nullptr, "警告", "无法打开文件", QMessageBox::Yes, QMessageBox::Yes);
        return;
    }
    auto palette = bl::palette::read_palette_to_end(data.data(), data.size());
    if (palette.empty()) {
        QMessageBox::information(nullptr, "警告", "空的nbt数据", QMessageBox::Yes, QMessageBox::Yes);
        return;
    }
    this->load_new_data(palette, [](const bl::palette::compound_tag *) { return QString(); }, {});
}


void NbtWidget::loadNBTItem(bl::palette::compound_tag *root) {
    if (!root) {
        QMessageBox::information(nullptr, "警告", "空的nbt数据", QMessageBox::Yes, QMessageBox::Yes);
        return;
    }
    ui->tree_widget->clear();
    int max_col = 0;
    auto *top = nbt2QTreeItem(root, 1, max_col);
    ui->tree_widget->setColumnCount(1);
    ui->tree_widget->addTopLevelItem(top);
    ui->tree_widget->expandAll();
}

void NbtWidget::on_list_widget_itemDoubleClicked(QListWidgetItem *item) {
    this->loadNBTItem(dynamic_cast<NBTListItem *>(item)->root_);
}

void NbtWidget::on_tree_widget_itemChanged(QTreeWidgetItem *item, int column) { //NOLINT

    using namespace bl::palette;
    auto *it = dynamic_cast<NBTTreeItem *>(item);
    if (!it || !it->root_)return;
    if (it->text(0) == it->getRawText())return;
    if (it->prevent_item_change_event) {
        it->prevent_item_change_event = false;
        return;
    }
    switch (it->root_->type()) {
        case Byte:
            dynamic_cast<byte_tag *>( it->root_)->value = static_cast<uint8_t>(item->text(0).toInt());
            break;
        case Short:
            dynamic_cast<short_tag *>( it->root_)->value = item->text(0).toShort();
            break;
        case Int:
            dynamic_cast<int_tag *>( it->root_)->value = item->text(0).toInt();
            break;
        case Long:
            dynamic_cast<long_tag *>( it->root_)->value = item->text(0).toLong();
            break;
        case Float:
            dynamic_cast<float_tag *>( it->root_)->value = item->text(0).toFloat();
            break;
        case Double:
            dynamic_cast<double_tag *>( it->root_)->value = item->text(0).toDouble();
            break;
        case String:
            dynamic_cast<string_tag *>( it->root_)->value = item->text(0).toStdString();
            break;
        case List:
        case Compound:
        case LEN:
        case End:
            break;
    }

    it->prevent_item_change_event = true;
    it->setText(0, it->getRawText());
}


void NbtWidget::hideLoadDataBtn() {
    ui->load_btn->setVisible(false);
}

void NbtWidget::on_save_btn_clicked() {
    this->exportNBTs(false);
}

void NbtWidget::prepareTreeWidgetMenu(const QPoint &pos) {
//    QTreeWidgetItem *nd = this->ui->tree_widget->itemAt(pos);
//    auto *modify = new QAction("修改值", this);
//    auto *remove = new QAction("删除表项", this);
//    QMenu menu(this);
//    menu.addAction(remove);
//    menu.addAction(modify);
//
//    menu.exec(ui->tree_widget->mapToGlobal(pos));

}

void NbtWidget::prepareListWidgetMenu(const QPoint &pos) {


    if (ui->list_widget->selectionMode() == QAbstractItemView::SingleSelection) {

        auto *removeAction = new QAction("删除", this);
        auto *exportAction = new QAction("导出选中", this);
        QMenu menu(this);
        menu.addAction(exportAction);
        if (modify_allowed_) {
            menu.addAction(removeAction);
        }

        QObject::connect(removeAction, &QAction::triggered, [this, pos](bool) {
            auto *item = this->ui->list_widget->currentItem();
            ui->list_widget->removeItemWidget(item);
            delete item;
        });
        QObject::connect(exportAction, &QAction::triggered, [this, pos](bool) {
            this->exportNBTs(true);
        });
        menu.exec(ui->list_widget->mapToGlobal(pos));
    } else {
        auto *removeAll = new QAction("删除所有", this);
        auto *removeSelect = new QAction("删除选中", this);
        auto *unselectAll = new QAction("全不选", this);
        auto *exportAction = new QAction("导出选中", this);
        QObject::connect(removeAll, &QAction::triggered, [this, pos](bool) {
            this->ui->list_widget->clear();
        });
        QObject::connect(removeSelect, &QAction::triggered, [this, pos](bool) {
            for (auto &item: ui->list_widget->selectedItems()) {
                ui->list_widget->removeItemWidget(item);
                delete item;
            }
        });

        QObject::connect(unselectAll, &QAction::triggered, [this, pos](bool) {
            ui->list_widget->clearSelection();
        });
        QObject::connect(exportAction, &QAction::triggered, [this, pos](bool) {
            this->exportNBTs(true);
        });


        QMenu menu(this);
        if (modify_allowed_) {
            menu.addAction(removeSelect);
            menu.addAction(removeAll);
        }
        menu.addAction(unselectAll);
        menu.addAction(exportAction);
        menu.exec(ui->list_widget->mapToGlobal(pos));
    }

}


void NbtWidget::on_tree_widget_itemDoubleClicked(QTreeWidgetItem *item, int column) {
//    auto *it = dynamic_cast<NBTTreeItem *>(item);
//    if (!it || !it->root_) { //正常来说不会出现
//        QMessageBox::information(nullptr, "信息", "当前nbt已损坏", QMessageBox::Yes, QMessageBox::Yes);
//        return;
//    }
//
//    if (it->root_->type() == bl::palette::tag_type::Compound ||
//        it->root_->type() == bl::palette::tag_type::List
//            ) {
//        if (item->isExpanded()) {
//            ui->tree_widget->collapseItem(item);
//        } else {
//            ui->tree_widget->expandItem(item);
//        }
//    } else {
//        it->setText(0, it->root_->value_string().c_str());
//    }
}


void NbtWidget::load_new_data(const std::vector<bl::palette::compound_tag *> &data,
                              const std::function<QString(bl::palette::compound_tag *)> &namer,
                              const std::vector<std::string> &default_labels) {
    ui->list_widget->clear();
    ui->tree_widget->clear();
    for (int i = 0; i < data.size(); i++) {
        auto *it = new NBTListItem();
        it->root_ = dynamic_cast<bl::palette::compound_tag *>(data[i]->copy());
        it->default_label = i < default_labels.size() ? default_labels[i].c_str() : QString(i);
        it->namer_ = namer;
        it->refreshText();
        ui->list_widget->addItem(it);
    }
}

void NbtWidget::on_multi_select_checkbox_stateChanged(int arg1) {
    if (arg1 > 0) {
        ui->list_widget->setSelectionMode(QAbstractItemView::MultiSelection);
    } else {
        ui->list_widget->setSelectionMode(QAbstractItemView::SingleSelection);
        ui->list_widget->clearSelection();
    }
}

void NbtWidget::on_modify_checkbox_stateChanged(int arg1) {
    modify_allowed_ = arg1 > 0;
}

void NbtWidget::exportNBTs(bool selectOnly) {
    // save
    auto fileName = QFileDialog::getSaveFileName(this, tr("Save File"),
                                                 "C:/Users/xhy/Desktop/",
                                                 tr("NBT files (*.*)"));

    if (fileName.size() == 0)return;
    std::string res;
    if (selectOnly) {
        for (auto &item: ui->list_widget->selectedItems()) {
            res += dynamic_cast<NBTListItem *>(item)->root_->to_raw();
        }
    } else {
        for (int i = 0; i < ui->list_widget->count(); ++i) {
            res += dynamic_cast<NBTListItem *>(ui->list_widget->item(i))->root_->to_raw();
        }
    }

    bl::utils::write_file(fileName.toStdString(), res.data(), res.size());

}
