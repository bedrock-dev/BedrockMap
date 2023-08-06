#ifndef NBTWIDGET_H
#define NBTWIDGET_H

#include <QListWidgetItem>
#include <QTreeWidgetItem>
#include <QWidget>
#include <QAction>

#include "palette.h"
#include <QMenu>

namespace Ui {
    class NbtWidget;
}


//不持有数据
class NBTTreeItem : public QTreeWidgetItem {
public:
    [[nodiscard]] inline QString getRawText() const {
        if (!root_)return ":";
        return QString() + root_->key().c_str() + ": " + root_->value_string().c_str();
    }

    bl::palette::abstract_tag *root_{nullptr};
};

//持有数据
struct NBTListItem : public QListWidgetItem {

    QString getLabel() {
        auto dyn = this->namer_(root_);
        return dyn.size() == 0 ? default_label : dyn;
    }

    bl::palette::compound_tag *root_{nullptr};
    std::function<QString(bl::palette::compound_tag *)> namer_;
    QString default_label;

    ~NBTListItem() override {
        delete this->root_;
    }
};


//自身会持有数据，所以每次加载数据会析构之前的并复制一份
class NbtWidget : public QWidget {
Q_OBJECT
public:
    explicit NbtWidget(QWidget *parent = nullptr);

    ~NbtWidget() override;

    inline void clearModifyCache() { this->modified_cache_.clear(); };

    void load_new_data(const std::vector<bl::palette::compound_tag *> &data,
                       const std::function<QString(bl::palette::compound_tag *)> &namer,
                       const std::vector<std::string> &default_labels,
                       const std::vector<QImage *> &icons = {});

    void setExtraLoadEvent(
            const std::function<void(bl::palette::compound_tag *)> &event) { this->extra_load_event_ = event; }

    void hideLoadDataBtn();

    std::string getCurrentPaletteRaw();

    std::vector<bl::palette::compound_tag *> getPaletteCopy();


    void foreachItem(const std::function<void(const std::string &label, bl::palette::compound_tag *data)> &func);

    void refreshLabel();

    void clearData();

private slots:

    void on_load_btn_clicked();

    void on_list_widget_itemDoubleClicked(QListWidgetItem *item);

    void on_save_btn_clicked();

    void prepareTreeWidgetMenu(const QPoint &pos);

    void prepareListWidgetMenu(const QPoint &pos);

    void saveNBTs(bool selectOnly);


    void on_tree_widget_itemDoubleClicked(QTreeWidgetItem *item, int column);

    void on_multi_select_checkbox_stateChanged(int arg1);

    void on_modify_checkbox_stateChanged(int arg1);


    void on_search_edit_textEdited(const QString &arg1);

    void on_list_widget_itemSelectionChanged();

private:
    void loadNBTItem(bl::palette::compound_tag *root);

private:
    //不存数据，只引用数据
    Ui::NbtWidget *ui;
    bool modify_allowed_{false};
    std::function<void(bl::palette::compound_tag *)> extra_load_event_{[](const bl::palette::compound_tag *) {}};

    std::unordered_map<std::string, std::string> modified_cache_;
};

#endif  // NBTWIDGET_H
