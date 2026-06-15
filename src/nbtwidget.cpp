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
#include <cstdio>

#include "loguru/loguru.hpp"
#include "msg.h"
#include "nbtmodifydialog.h"
#include "palette.h"
#include "resourcemanager.h"
#include "ui_nbtwidget.h"
#include "utils.h"

namespace {

    NBTTreeItem *nbt2QTreeItem(bl::palette::abstract_tag *t, int index, int &ma, bool hex_mode) {
        ma = std::max(ma, index);
        using namespace bl::palette;
        if (!t) return nullptr;
        auto *item = new NBTTreeItem();
        item->root_ = t;
        item->updateLabel(hex_mode);
        item->setIcon(0, QIcon(QPixmap::fromImage(*TagIcon(t->type()))));
        if (t->type() == bl::palette::tag_type::Compound) {
            const auto *ct = dynamic_cast<bl::palette::compound_tag *>(t);
            for (const auto &[fst, snd] : ct->value) {
                item->addChild(nbt2QTreeItem(snd, index + 1, ma, hex_mode));
            }
        } else if (t->type() == bl::palette::tag_type::List) {
            auto *ct = dynamic_cast<bl::palette::list_tag *>(t);
            for (auto k : ct->value) {
                item->addChild(nbt2QTreeItem(k, index + 1, ma, hex_mode));
            }
        }
        return item;
    }

    NBTListItem *TN(QListWidgetItem *i) { return dynamic_cast<NBTListItem *>(i); }

}  // namespace

std::string NBTTreeItem::hexValueString(bl::palette::abstract_tag *tag) {
    if (!tag) return {};
    using namespace bl::palette;
    switch (tag->type()) {
        case Byte: {
            auto v = static_cast<uint8_t>(dynamic_cast<byte_tag *>(tag)->value);
            char buf[16];
            snprintf(buf, sizeof(buf), "0x%02X", v);
            return buf;
        }
        case Short: {
            auto v = static_cast<uint16_t>(dynamic_cast<short_tag *>(tag)->value);
            char buf[16];
            snprintf(buf, sizeof(buf), "0x%04X", v);
            return buf;
        }
        case Int: {
            auto v = static_cast<uint32_t>(dynamic_cast<int_tag *>(tag)->value);
            char buf[16];
            snprintf(buf, sizeof(buf), "0x%08X", v);
            return buf;
        }
        case Long: {
            auto v = static_cast<uint64_t>(dynamic_cast<long_tag *>(tag)->value);
            char buf[32];
            snprintf(buf, sizeof(buf), "0x%016llX", v);
            return buf;
        }
        default:
            return tag->value_string();
    }
}

std::string NBTTreeItem::hexArrayString(bl::palette::abstract_tag *tag) {
    if (!tag) return {};
    using namespace bl::palette;
    std::string res;
    switch (tag->type()) {
        case ByteArray: {
            auto &vec = dynamic_cast<byte_array_tag *>(tag)->value;
            if (vec.empty()) return "[]";
            char buf[16];
            for (size_t i = 0; i < std::min(vec.size(), size_t(16)); ++i) {
                snprintf(buf, sizeof(buf), "0x%02X", static_cast<uint8_t>(vec[i]));
                if (!res.empty()) res += ' ';
                res += buf;
            }
            if (vec.size() > 16) res += " ...";
            break;
        }
        case IntArray: {
            auto &vec = dynamic_cast<int_array_tag *>(tag)->value;
            if (vec.empty()) return "[]";
            char buf[16];
            for (size_t i = 0; i < std::min(vec.size(), size_t(16)); ++i) {
                snprintf(buf, sizeof(buf), "0x%08X", static_cast<uint32_t>(vec[i]));
                if (!res.empty()) res += ' ';
                res += buf;
            }
            if (vec.size() > 16) res += " ...";
            break;
        }
        case LongArray: {
            auto &vec = dynamic_cast<long_array_tag *>(tag)->value;
            if (vec.empty()) return "[]";
            char buf[32];
            for (size_t i = 0; i < std::min(vec.size(), size_t(16)); ++i) {
                snprintf(buf, sizeof(buf), "0x%016llX", static_cast<uint64_t>(vec[i]));
                if (!res.empty()) res += ' ';
                res += buf;
            }
            if (vec.size() > 16) res += " ...";
            break;
        }
        default:
            return tag->value_string();
    }
    return res;
}

bool NBTTreeItem::tryAddChild(bl::palette::abstract_tag *tag, bool hex_mode) {
    if (!tag || !root_) return false;
    const auto type = root_->type();
    int max;
    auto *item = nbt2QTreeItem(tag, 1, max, hex_mode);
    if (type == tag_type::Compound) {
        auto *cur = dynamic_cast<compound_tag *>(root_);
        if (cur->value.count(tag->key()) > 0) {
            delete item;
            return false;
        }
        cur->put(tag);
        this->addChild(item);
    } else if (type == tag_type::List) {
        if (auto *cur = dynamic_cast<list_tag *>(root_); cur->push_back(tag)) {
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
    ui->tree_widget->setColumnCount(2);
    ui->tree_widget->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    ui->tree_widget->setContextMenuPolicy(Qt::CustomContextMenu);
    ui->list_widget->setContextMenuPolicy(Qt::CustomContextMenu);
    // right menu
    connect(ui->tree_widget, &QTreeWidget::customContextMenuRequested, this, &NbtWidget::prepareTreeWidgetMenu);
    connect(ui->tree_widget, &QTreeWidget::itemChanged, this, &NbtWidget::on_tree_widget_itemChanged);
    connect(ui->list_widget, &QListWidget::customContextMenuRequested, this, &NbtWidget::prepareListWidgetMenu);

    connect(ui->hex_checkbox, &QCheckBox::checkStateChanged, this, &NbtWidget::on_hex_checkbox_stateChanged);
    modify_dialog_ = new NBTModifyDialog(this);
    this->refreshLabel();
    this->clearModifyCache();
    ui->print_cache_btn->hide();
}

void NbtWidget::on_load_btn_clicked() {
    auto fileName = QFileDialog::getOpenFileName(this, tr("nbtEditor.fileDialog.open"), QDir::homePath(),
                                                 "NBT Files (*.nbt  *.nbts *.mcstructure *.palette);;All Files (*)");
    if (fileName.isEmpty()) return;
    auto data = bl::utils::read_file(fileName.toStdString());
    if (data.empty()) {
        WARN(msg::CANNOT_OPEN_FILE());
        return;
    }

    auto palette = bl::palette::read_palette_to_end(data.data(), data.size());

    if (palette.empty()) {
        WARN(msg::NBT_PARSE_FAILED());
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

void NbtWidget::openNBTItem(bl::palette::compound_tag *root) const {
    assert(root);
    this->extra_load_event_(root);
    ui->tree_widget->clear();
    int max_col = 0;
    auto *top = nbt2QTreeItem(root, 1, max_col, hex_mode_);
    ui->tree_widget->addTopLevelItem(top);
    // only expand the top level
    ui->tree_widget->expandItem(top);
}

void NbtWidget::on_list_widget_itemDoubleClicked(QListWidgetItem *item) {
    auto *nbtItem = TN(item);
    if (!nbtItem) {
        INFO(msg::EMPTY_NBT_DATA());
        return;
    }
    this->current_opened_ = nbtItem;
    LOG_F(INFO, "Select NBT item : [%s] -> %s", this->current_opened_->raw_key.toStdString().c_str(),
          nbtItem->getLabel().toStdString().c_str());
    this->openNBTItem(nbtItem->root_);
    this->refreshLabel();
}

void NbtWidget::hideLoadDataBtn() const { ui->load_btn->setVisible(false); }

void NbtWidget::on_save_btn_clicked() { this->saveNBTs(false); }

void NbtWidget::tryModifyCurrentNode() {
    if (!modify_allowed_) return;
    auto *current = dynamic_cast<NBTTreeItem *>(ui->tree_widget->currentItem());
    if (!current || !current->root_) return;
    if (!modify_dialog_->setModifyMode(current->root_)) {
        WARN(msg::INIT_FAILED());
        return;
    }
    if (modify_dialog_->exec() == QDialog::Accepted) {
        if (QString err; !modify_dialog_->modifyCurrentTag(current->root_, err)) {
            WARN(msg::MODIFY_NODE_FAILED(err));
        } else {
            current->updateLabel(hex_mode_);
            putModifyToCache(current_opened_->raw_key.toStdString(), current_opened_->root_->to_raw());
            emit nbtModified();
        }
    }
}

void NbtWidget::on_tree_widget_itemDoubleClicked(QTreeWidgetItem *item, int column) {
    if (!modify_allowed_ || column != 1) return;
    auto *nbtItem = dynamic_cast<NBTTreeItem *>(item);
    if (!nbtItem || !nbtItem->root_) return;
    auto type = nbtItem->root_->type();
    if (type == bl::palette::Compound || type == bl::palette::List || type == bl::palette::End) return;

    // Always show decimal value in the editor
    editing_in_progress_ = true;
    nbtItem->setText(1, nbtItem->root_->value_string().c_str());
    editing_in_progress_ = false;

    item->setFlags(item->flags() | Qt::ItemIsEditable);
    ui->tree_widget->editItem(item, column);
}

namespace {
    bool parseAndSetTag(bl::palette::abstract_tag *tag, const QString &text, QString &err) {
        using namespace bl::palette;
        bool ok = false;
        switch (tag->type()) {
            case Byte: {
                auto v = static_cast<int8_t>(text.toInt(&ok));
                if (ok) dynamic_cast<byte_tag *>(tag)->value = v;
                break;
            }
            case Short: {
                auto v = static_cast<int16_t>(text.toInt(&ok));
                if (ok) dynamic_cast<short_tag *>(tag)->value = v;
                break;
            }
            case Int: {
                auto v = text.toInt(&ok);
                if (ok) dynamic_cast<int_tag *>(tag)->value = v;
                break;
            }
            case Long: {
                auto v = text.toLongLong(&ok);
                if (ok) dynamic_cast<long_tag *>(tag)->value = v;
                break;
            }
            case Float: {
                auto v = text.toFloat(&ok);
                if (ok) dynamic_cast<float_tag *>(tag)->value = v;
                break;
            }
            case Double: {
                auto v = text.toDouble(&ok);
                if (ok) dynamic_cast<double_tag *>(tag)->value = v;
                break;
            }
            case String:
                dynamic_cast<string_tag *>(tag)->value = text.toStdString();
                ok = true;
                break;
            default:
                err = "Cannot edit this type inline";
                return false;
        }
        if (!ok) {
            err = "Invalid value for this type";
            return false;
        }
        return true;
    }
}  // namespace

void NbtWidget::on_tree_widget_itemChanged(QTreeWidgetItem *item, int column) {
    if (column != 1 || editing_in_progress_) return;
    auto *nbtItem = dynamic_cast<NBTTreeItem *>(item);
    if (!nbtItem || !nbtItem->root_) return;

    QString text = item->text(1);
    // Ignore if text hasn't actually changed from the raw value string
    if (text == nbtItem->root_->value_string().c_str() && hex_mode_) return;

    QString err;
    if (!parseAndSetTag(nbtItem->root_, text, err)) {
        editing_in_progress_ = true;
        nbtItem->updateLabel(hex_mode_);
        editing_in_progress_ = false;
        return;
    }
    editing_in_progress_ = true;
    nbtItem->updateLabel(hex_mode_);
    editing_in_progress_ = false;
    putModifyToCache(current_opened_->raw_key.toStdString(), current_opened_->root_->to_raw());
    emit nbtModified();
}

void NbtWidget::prepareTreeWidgetMenu(const QPoint &pos) {
    if (!modify_allowed_) return;
    auto *addAction = new QAction(tr("nbtEditor.rightMenu.new"), this);
    auto *removeAction = new QAction(tr("nbtEditor.rightMenu.delete"), this);
    auto *modifyAction = new QAction(tr("nbtEditor.rightMenu.modify"), this);
    auto *clearAction = new QAction(tr("nbtEditor.rightMenu.clear"), this);

    QMenu menu(this);
    auto *current = ui->tree_widget->currentItem();
    if (!current) return;
    auto *nbtItem = dynamic_cast<NBTTreeItem *>(current);
    if (!nbtItem || !nbtItem->root_) {
        WARN(msg::PASTE_DATA_INVALID());
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
        if (!modify_dialog_->setCreateMode(current->root_)) {
            WARN(msg::INIT_FAILED());
            return;
        }
        if (modify_dialog_->exec() == QDialog::Accepted) {
            QString err;
            auto *tag = modify_dialog_->createTagWithCurrent(err);
            if (!tag) {
                WARN(msg::CREATE_NODE_FAILED(err));
            } else if (!current->tryAddChild(tag, hex_mode_)) {
                WARN(msg::CREATE_NODE_FAILED(""));
            } else {
                current->updateLabel(hex_mode_);
                putModifyToCache(current_opened_->raw_key.toStdString(), current_opened_->root_->to_raw());
            }
        }
    });

    QObject::connect(modifyAction, &QAction::triggered, this, [this]() { tryModifyCurrentNode(); });

    QObject::connect(removeAction, &QAction::triggered, [this, pos](bool) {
        auto current = dynamic_cast<NBTTreeItem *>(ui->tree_widget->currentItem());
        auto parent = dynamic_cast<NBTTreeItem *>(ui->tree_widget->currentItem()->parent());
        if (!parent) {
            WARN(msg::CANNOT_DELETE_ROOT());
            return;
        }
        auto parentType = parent->root_->type();
        if (parentType == bl::palette::Compound) {
            auto *tag = dynamic_cast<bl::palette::compound_tag *>(parent->root_);
            if (!tag) {
                WARN(msg::NBT_DATA_CORRUPTED());
                return;
            }
            tag->remove(current->root_->key());
        } else if (parentType == bl::palette::List) {
            auto *tag = dynamic_cast<bl::palette::list_tag *>(parent->root_);
            if (!tag) {
                WARN(msg::NBT_DATA_CORRUPTED());
                return;
            }
            auto idx = parent->indexOfChild(current);
            tag->remove(idx);
            parent->updateLabel(hex_mode_);
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
            for (auto &[fst, snd] : tag->value) {
                delete snd;
            }
            tag->value.clear();
        } else if (current->root_->type() == bl::palette::tag_type::List) {
            auto *tag = dynamic_cast<bl::palette::list_tag *>(current->root_);
            for (const auto *child : tag->value) {
                delete child;
            }
            tag->value.clear();
        }
        auto children = current->takeChildren();
        qDeleteAll(children);
        current->updateLabel(hex_mode_);
        putModifyToCache(current_opened_->raw_key.toStdString(), current_opened_->root_->to_raw());
    });
    menu.exec(ui->tree_widget->mapToGlobal(pos));
}

void NbtWidget::prepareListWidgetMenu(const QPoint &pos) {
    // 单选模式
    if (ui->list_widget->selectionMode() == QAbstractItemView::SingleSelection) {
        auto *removeAction = new QAction(tr("nbtEditor.rightMenu.delete"), this);
        auto *exportAction = new QAction(tr("nbtEditor.rightMenu.exportSelected"), this);
        auto *createAction = new QAction(tr("nbtEditor.rightMenu.new"), this);
        auto *clearAction = new QAction(tr("nbtEditor.rightMenu.clear"), this);
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
            auto reply = QMessageBox::question(this, QObject::tr("msg.clear"), QObject::tr("msg.confirmClearAll"),
                                               QMessageBox::Yes | QMessageBox::No);
            if (reply != QMessageBox::Yes) return;
            ui->tree_widget->clear();
            // clear all items in the list widget, and put them to the modify cache, free the memory
            auto row = ui->list_widget->count();
            for (int i = 0; i < row; ++i) {
                if (const auto *item = dynamic_cast<NBTListItem *>(ui->list_widget->item(i))) {
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
        auto *removeSelect = new QAction(tr("nbtEditor.rightMenu.deleteSelected"), this);
        auto *unselectAll = new QAction(tr("nbtEditor.rightMenu.unselectAll"), this);
        auto *exportAction = new QAction(tr("nbtEditor.rightMenu.exportSelected"), this);

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

void NbtWidget::on_hex_checkbox_stateChanged(int arg1) {
    hex_mode_ = (arg1 > 0);
    if (current_opened_) {
        openNBTItem(current_opened_->root_);
    }
}

void NbtWidget::saveNBTs(bool selectOnly) {
    // save
    const auto fileName =
        QFileDialog::getSaveFileName(this, tr("nbtEditor.fileDialog.save"), "C:/Users/xhy/Desktop/", tr("nbtEditor.fileDialog.nbtFiles"));
    if (fileName.size() == 0) return;
    std::string res;
    if (selectOnly) {
        for (const auto &item : ui->list_widget->selectedItems()) {
            if (!item->isHidden()) res += dynamic_cast<NBTListItem *>(item)->root_->to_raw();
        }
    } else {
        for (int i = 0; i < ui->list_widget->count(); ++i) {
            if (auto *item = ui->list_widget->item(i); !item->isHidden()) res += dynamic_cast<NBTListItem *>(item)->root_->to_raw();
        }
    }

    bl::utils::write_file(fileName.toStdString(), res.data(), res.size());
}

std::string NbtWidget::getCurrentPaletteRaw() const {
    std::string res;
    for (int i = 0; i < ui->list_widget->count(); ++i) {
        res += dynamic_cast<NBTListItem *>(ui->list_widget->item(i))->root_->to_raw();
    }
    return res;
}

std::vector<compound_tag *> NbtWidget::getPaletteCopy() const {
    std::vector<compound_tag *> res;
    for (int i = 0; i < ui->list_widget->count(); ++i) {
        res.push_back(dynamic_cast<compound_tag *>(dynamic_cast<NBTListItem *>(ui->list_widget->item(i))->root_->copy()));
    }
    return res;
}

void NbtWidget::foreachItem(const std::function<void(const std::string &, bl::palette::compound_tag *)> &func) const {
    for (int i = 0; i < ui->list_widget->count(); ++i) {
        if (auto *item = dynamic_cast<NBTListItem *>(ui->list_widget->item(i))) {
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

void NbtWidget::refreshLabel() const {
    int selected = 0;
    int notHidden = 0;
    for (int i = 0; i < ui->list_widget->count(); ++i) {
        const auto *item = ui->list_widget->item(i);
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
    blockSignals(true);
    this->clearModifyCache();
    blockSignals(false);
}

NbtWidget::~NbtWidget() {
    this->clearData();
    delete ui;
}

void NbtWidget::clearModifyCache() {
    this->modified_cache_.clear();
    emit nbtModified();
}

void NbtWidget::putModifyToCache(const std::string &key, const std::string &value) {
    if (enable_modify_cache_) {
        this->modified_cache_[key] = value;
    };
    // 日志不受影响
    if (value.empty()) {
        LOG_F(INFO, "Delete key: %s", key.c_str());
    } else {
        LOG_F(INFO, "Modify key: %s -> Data[%zu]", key.c_str(), value.size());
    }
    emit nbtModified();
}

void NbtWidget::on_print_cache_btn_clicked() {
    LOG_F(INFO, "Total %zu items in the modify cache:", this->modified_cache_.size());
    for (auto &[fst, snd] : this->modified_cache_) {
        if (snd.empty()) {
            LOG_F(INFO, " - Delete key: %s", fst.c_str());
        } else {
            LOG_F(INFO, " - Modify key: %s -> Data[%zu]", fst.c_str(), snd.size());
        }
    }
}
