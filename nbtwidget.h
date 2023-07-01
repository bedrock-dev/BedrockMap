#ifndef NBTWIDGET_H
#define NBTWIDGET_H

#include <QListWidgetItem>
#include <QTreeWidgetItem>
#include <QWidget>

#include "palette.h"

namespace Ui {
    class NbtWidget;
}

//自身会持有数据，所以每次加载数据会析构之前的并复制一份
class NbtWidget : public QWidget {
Q_OBJECT

public:
    explicit NbtWidget(QWidget *parent = nullptr);

    ~NbtWidget() override;

    void load_new_data(const std::vector<bl::palette::compound_tag *> &data);

    void hideLoadDataBtn();

private slots:

    void on_load_btn_clicked();

    void on_list_widget_itemDoubleClicked(QListWidgetItem *item);

    void on_tree_widget_itemDoubleClicked(QTreeWidgetItem *item, int column);

    void on_tree_widget_itemChanged(QTreeWidgetItem *item, int column);

private:
    void loadNBT(bl::palette::compound_tag *root);

    void refreshDataView();

private:
    //不存数据，只引用数据
    std::vector<bl::palette::compound_tag *> nbts_;
    Ui::NbtWidget *ui;
};

#endif  // NBTWIDGET_H
