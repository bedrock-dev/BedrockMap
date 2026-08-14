#ifndef NBTWIDGET_H
#define NBTWIDGET_H

#include <qchar.h>

#include <QAction>
#include <QListWidgetItem>
#include <QMenu>
#include <QTreeWidgetItem>
#include <QWidget>
#include <string>

#include "nbt.h"
#include "nbtmodifydialog.h"


namespace Ui {
    class NbtWidget;
}

using namespace bl::nbt;

// does not own data
class NBTTreeItem : public QTreeWidgetItem {
   public:
    bool tryAddChild(bl::nbt::abstract_tag *tag, bool hex_mode);

    void updateLabel(bool hex_mode) {
        if (!root_) return;
        auto type = root_->type();
        if (type == Compound) {
            setText(0, dynamic_cast<compound_tag *>(root_)->key().c_str());
            setText(1, "");
        } else if (type == List) {
            auto *list = dynamic_cast<bl::nbt::list_tag *>(root_);
            setText(0, list->key().c_str());
            setText(1, QString("[%1]").arg(QString::number(list->value.size())));
        } else if (hex_mode && (type >= Byte && type <= Long)) {
            setText(0, root_->key().c_str());
            setText(1, NBTTreeItem::hexValueString(root_).c_str());
        } else if (hex_mode && (type == ByteArray || type == IntArray || type == LongArray)) {
            setText(0, root_->key().c_str());
            setText(1, NBTTreeItem::hexArrayString(root_).c_str());
        } else {
            setText(0, root_->key().c_str());
            setText(1, root_->value_string().c_str());
        }
    }

    static std::string hexValueString(bl::nbt::abstract_tag *tag);
    static std::string hexArrayString(bl::nbt::abstract_tag *tag);

    bl::nbt::abstract_tag *root_{nullptr};
};

// owns data

struct NBTListItem : public QListWidgetItem {
    QString getLabel() {
        auto dyn = this->namer_(root_);
        return dyn.size() == 0 ? default_label : dyn;
    }

    bl::nbt::compound_tag *root_{nullptr};                                                               // raw data
    std::function<QString(bl::nbt::compound_tag *)> namer_{[](bl::nbt::compound_tag *) { return ""; }};  // dynamic label
    QString default_label;                                                                               // display label
    QString raw_key;  // original key in the leveldb key structure
    ~NBTListItem() override { delete this->root_; }

    /*
     * Construct an NBTListItem without a dynamic label or icon
     */
    static NBTListItem *from(bl::nbt::compound_tag *data, const QString &default_label, const QString &key = "") {
        auto *it = new NBTListItem();
        it->root_ = data;
        it->default_label = default_label;
        it->raw_key = key.isEmpty() ? default_label : key;
        it->setText(it->getLabel());
        return it;
    }
};

struct NBTNodeUIAttr {
    bool canRemove;  // remove current item
    bool canModify;  // modify current value
    bool canAdd;     // add child
    bool canClear;   // clear all children
    static NBTNodeUIAttr get(bl::nbt::tag_type type, bool is_root) {
        NBTNodeUIAttr attr{true, true, false, false};
        if (type == bl::nbt::tag_type::Compound) {
            attr.canRemove = !is_root;
            attr.canAdd = true;
            attr.canClear = true;
        } else if (type == bl::nbt::List) {
            attr.canAdd = true;
            attr.canClear = true;
        } else if (type == bl::nbt::End) {
            attr.canRemove = false;
            attr.canModify = false;
            attr.canAdd = false;
        }
        return attr;
    }
};

// Where the NBT data displayed by the widget comes from.
enum class NbtMode {
    File,    // data is read from disk via the load button (or provided by the host)
    Memory,  // data is injected directly via loadNewData(); the load button is hidden
};

// owns its own data, so every load destroys the previous one and copies a new one
class NbtWidget : public QWidget {
    Q_OBJECT
   public:
    explicit NbtWidget(QWidget *parent = nullptr);

    ~NbtWidget() override;

    void loadNewData(const std::vector<NBTListItem *> &items);

    /// Open the NBT tree for the item at the given list index.
    /// Used when the left item list is hidden (e.g. single-document viewers).
    bool openItem(int index);

    void setExtraLoadEvent(const std::function<void(compound_tag *)> &event) { this->extra_load_event_ = event; }

    void setMode(NbtMode mode);
    NbtMode mode() const { return mode_; }

    /// Source file used by the in-place "Save" button (File mode only).
    void setFilePath(const QString &path);
    [[nodiscard]] QString filePath() const { return file_path_; }

    /// Write all items back to the source file (File mode only).
    /// Clears the modify cache on success so dirty() turns false.
    bool saveToFile();

    /// Disable in-place NBT editing (modify / add / remove / load).
    /// "Save As" (export) stays available.
    void setReadOnly(bool read_only);

    /// Show or hide the left-side item list (and the toolbar controls tied to it).
    void setListVisible(bool visible);

    std::string getCurrentPaletteRaw() const;

    std::vector<compound_tag *> getPaletteCopy() const;

    void foreachItem(const std::function<void(const std::string &label, compound_tag *data)> &func) const;

    void refreshLabel() const;

    void clearData();

    inline void setEnableModifyCache(bool enable) {
        if (!enable) this->clearModifyCache();
        this->enable_modify_cache_ = enable;
    }

    inline bool dirty() const { return !this->modified_cache_.empty(); }

    const std::unordered_map<std::string, std::string> &getModifyCache() { return this->modified_cache_; }

    void putModifyToCache(const std::string &key, const std::string &value);

    void putRemoveToCache(const std::string &key) { putModifyToCache(key, ""); }

    void clearModifyCache();

    inline NBTListItem *openedItem() { return this->current_opened_; }

    inline bool modifyAllowed() const { return this->modify_allowed_; }

   signals:
    void nbtModified();
    /// Emitted after saveToFile() has written the data back to disk.
    void dataSaved();

   private slots:
    void on_print_cache_btn_clicked();

   private slots:
    void on_load_btn_clicked();

    void on_list_widget_itemDoubleClicked(QListWidgetItem *item);

    void on_tree_widget_itemDoubleClicked(QTreeWidgetItem *item, int column);

    void on_tree_widget_itemChanged(QTreeWidgetItem *item, int column);

    void on_save_btn_clicked();

    void on_save_to_file_btn_clicked();

    void prepareTreeWidgetMenu(const QPoint &pos);

    void prepareListWidgetMenu(const QPoint &pos);

    void saveNBTs(bool selectOnly);

    void on_multi_select_checkbox_stateChanged(int arg1);

    void on_hex_checkbox_stateChanged(int arg1);

    void on_search_edit_textEdited(const QString &arg1);

    void on_list_widget_itemSelectionChanged();

   private:
    void openNBTItem(bl::nbt::compound_tag *root) const;
    void tryModifyCurrentNode();
    void applyToolbarVisibility();
    std::string collectRawNBT(bool selectOnly) const;

   private:
    // does not store data, only references it
    Ui::NbtWidget *ui;
    NBTModifyDialog *modify_dialog_{nullptr};
    bool modify_allowed_{true};
    std::function<void(compound_tag *)> extra_load_event_{[](const compound_tag *) {}};
    std::unordered_map<std::string, std::string> modified_cache_;
    NBTListItem *current_opened_{nullptr};
    bool enable_modify_cache_{true};
    bool hex_mode_{false};
    bool editing_in_progress_{false};
    std::string current_palette_path_;
    NbtMode mode_{NbtMode::File};
    bool read_only_{false};
    bool list_visible_{true};
    QString file_path_;
};

#endif  // NBTWIDGET_H
