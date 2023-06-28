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

#include "palette.h"
#include "ui_nbtwidget.h"
#include "utils.h"
namespace {

QWidget *newTreeItemEditWidget(const QString &key, const QString &value) {
    QWidget *w = new QWidget();
    QHBoxLayout *layout = new QHBoxLayout();
    QLabel *key_label = new QLabel();
    key_label->setText(key);
    QLineEdit *edit = new QLineEdit();
    edit->setText(value);
    layout->addWidget(key_label);
    layout->addWidget(edit);
    w->setLayout(layout);
    return w;
}

QString nbtTagIconName(bl::palette::tag_type t) {
    using namespace bl::palette;
    std::unordered_map<tag_type, std::string> names{
        {tag_type::Int, "Int"},       {tag_type::Byte, "Byte"},   {tag_type::Compound, "Compound"},
        {tag_type::Double, "Double"}, {tag_type::Float, "Float"}, {tag_type::List, "List"},
        {tag_type::Long, "Long"},     {tag_type::Short, "Short"}, {tag_type::String, "String"}};
    auto it = names.find(t);
    if (it == names.end()) return ":/res/nbt/Tag_End.ico";
    return QString(":/res/nbt/TAG_") + it->second.c_str() + ".ico";
}

QTreeWidgetItem *nbt2QTreeItem(bl::palette::abstract_tag *t, int index, int &ma) {
    ma = std::max(ma, index);
    using namespace bl::palette;
    if (!t) return nullptr;
    auto *item = new QTreeWidgetItem();
    item->setIcon(0, QIcon(nbtTagIconName(t->type())));
    if (t->type() == bl::palette::tag_type::Compound) {
        item->setText(0, t->key().c_str());
        auto *ct = dynamic_cast<bl::palette::compound_tag *>(t);
        for (auto &kv : ct->value) {
            item->addChild(nbt2QTreeItem(kv.second, index + 1, ma));
        }
    } else if (t->type() == bl::palette::tag_type::List) {
        auto *ct = dynamic_cast<bl::palette::list_tag *>(t);
        item->setText(0, t->key().c_str() + QString("[%1]").arg(QString::number(ct->value.size())));
        for (auto k : ct->value) {
            item->addChild(nbt2QTreeItem(k, index + 1, ma));
        }
    } else {
        item->setText(0, QString() + t->key().c_str() + ": " + t->value_string().c_str());

        item->setData(1, 0, QString(t->key().c_str()));
        item->setData(1, 1, QString(t->value_string().c_str()));
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
}

NbtWidget::~NbtWidget() { delete ui; }

void NbtWidget::on_load_btn_clicked() {
    auto fileName = QFileDialog::getOpenFileName(this, tr("Open File"), "C:\\Users\\xhy\\dev\\bedrock-level\\data\\dumps\\",
                                                 tr("NBT Files (*.nbt *.palette)"));
    if (fileName.isEmpty()) return;
    auto data = bl::utils::read_file(fileName.toStdString());
    if (data.empty()) {
        QMessageBox::information(NULL, "警告", "无法打开文件", QMessageBox::Yes, QMessageBox::Yes);
        return;
    }
    auto palette = bl::palette::read_palette_to_end(data.data(), data.size());
    if (palette.empty()) {
        QMessageBox::information(NULL, "警告", "空的nbt数据", QMessageBox::Yes, QMessageBox::Yes);
        return;
    }
    this->load_new_data(palette);
}

void NbtWidget::load_new_data(const std::vector<bl::palette::compound_tag *> &data) {
    //    for (auto &nbt : this->nbts_) delete nbt;

    this->nbts_ = data;
    //自动刷新
    this->refreshDataView();
}

void NbtWidget::loadNBT(bl::palette::compound_tag *root) {
    if (!root) {
        QMessageBox::information(NULL, "警告", "空的nbt数据", QMessageBox::Yes, QMessageBox::Yes);
        return;
    }
    ui->tree_widget->clear();
    int max_col = 0;
    auto *top = nbt2QTreeItem(root, 1, max_col);
    ui->tree_widget->setColumnCount(1);
    ui->tree_widget->addTopLevelItem(top);
    //    ui->tree_widget->header();

    ui->tree_widget->expandAll();
}

void NbtWidget::refreshDataView() {
    size_t n = 0;
    for (auto &nbt : this->nbts_) {
        ui->list_widget->addItem(QString("%1").arg(QString::number(n)) + nbt->key().c_str());
        n++;
    }
}

void NbtWidget::on_list_widget_itemDoubleClicked(QListWidgetItem *item) {
    auto index = item->text().toInt();
    this->loadNBT(this->nbts_[index]);
}

void NbtWidget::on_tree_widget_itemDoubleClicked(QTreeWidgetItem *item, int column) {
    item->setExpanded(true);
    //    ui->tree_widget->setItemWidget(item, column, newTreeItemEditWidget("key", "value"));
}

void NbtWidget::on_tree_widget_itemChanged(QTreeWidgetItem *item, int column) {
    auto key = item->data(1, 0).toString();
    auto value = item->data(1, 1).toString();

    if (!item->text(0).startsWith(key + ": ")) {
        item->setText(0, key + ": " + value);
    }
}
