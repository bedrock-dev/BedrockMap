#include "nbtmodifydialog.h"

#include <qchar.h>
#include <qlist.h>
#include <qvariant.h>
#include <sys/stat.h>

#include <cstdint>
#include <limits>
#include <unordered_map>
#include <utility>
#include <vector>

#include "palette.h"
#include "resourcemanager.h"
#include "ui_nbtmodifydialog.h"

namespace {
    template <typename T>
    using nl = std::numeric_limits<T>;
    using namespace bl::palette;
    bool parseIntegerValue(tag_type type, const QString &str, std::vector<int64_t> &values) {
        static std::unordered_map<tag_type, std::pair<int64_t, int64_t>> range_map{
            {Byte, {nl<int8_t>::min(), nl<int8_t>::max()}},       {ByteArray, {nl<int8_t>::min(), nl<int8_t>::max()}},
            {Short, {nl<int16_t>::min(), nl<int16_t>::max()}},    {Int, {nl<int32_t>::min(), nl<int32_t>::max()}},
            {IntArray, {nl<int32_t>::min(), nl<int32_t>::max()}}, {Long, {nl<int64_t>::min(), nl<int64_t>::max()}},
            {LongArray, {nl<int64_t>::min(), nl<int64_t>::max()}}};
        QStringList strs;
        if (type == ByteArray || type == IntArray || type == LongArray) {
            strs = str.split(' ');
        } else {
            strs << str;
        }

        for (auto &nStr : strs) {
            bool ok;
            auto value = nStr.toLongLong(&ok);
            if (!ok) return false;
            auto it = range_map.find(type);
            if (it == range_map.end()) return false;
            auto &range = it->second;
            if (value < range.first || value > range.second) return false;
            values.push_back(value);
        }

        return true;
    }

}  // namespace

using namespace bl::palette;
NBTModifyDialog::NBTModifyDialog(QWidget *parent) : QDialog(parent), ui(new Ui::NBTModifyDialog) {
    ui->setupUi(this);
    auto begin = static_cast<tag_type>(Byte);
    auto end = static_cast<tag_type>(LongArray);
    for (auto i = (int)tag_type::Byte; i <= (int)tag_type::LongArray; i++) {
        auto type = static_cast<tag_type>(i);
        ui->type_combobox->addItem(tag_type_to_str(type).c_str(), QVariant::fromValue(i));
        ui->type_combobox->setItemIcon(ui->type_combobox->count() - 1, QIcon(QPixmap::fromImage(*TagIcon(type))));
    }
}

void NBTModifyDialog::resetUI() {
    ui->name_lineedit->setEnabled(true);
    ui->value_lineedit->setEnabled(true);
    ui->type_combobox->setEnabled(true);
}

bool NBTModifyDialog::setCreateMode(bl::palette::abstract_tag *tag) {
    if (!tag) return false;
    auto type = tag->type();
    if (type == bl::palette::List) {
        auto *list = static_cast<list_tag *>(tag);
        if (!list) return false;
        if (!list->value.empty()) {
            auto child_type = list->value[0]->type();
            ui->type_combobox->setCurrentText(tag_type_to_str(child_type).c_str());
            ui->type_combobox->setEnabled(false);
            return true;
        }
    }
    ui->type_combobox->setEnabled(true);
    return true;
}

bool NBTModifyDialog::setModifyMode(bl::palette::abstract_tag *tag) {
    if (!tag) return false;
    auto type = tag->type();
    if (type == bl::palette::Compound || type == bl::palette::List) {
        ui->value_lineedit->setEnabled(false);
    }
    ui->type_combobox->setCurrentText(tag_type_to_str(type).c_str());
    ui->name_lineedit->setText(tag->key().c_str());
    ui->value_lineedit->setText(tag->restricted_value_string().c_str());
    ui->type_combobox->setEnabled(false);
    return true;
}

bl::palette::abstract_tag *NBTModifyDialog::createTagWithCurrent(QString &err) {
    auto name = ui->name_lineedit->text().toStdString();
    if (name.empty()) {
        err = "TAG名字不能为空";
        return nullptr;
    }
    auto data = ui->type_combobox->currentData();
    if (!data.canConvert<int>()) {
        err = "TAG类型不合法";
        return nullptr;
    }

    bool ok{false};
    auto type = static_cast<tag_type>(data.value<int>());
    auto vs = ui->value_lineedit->text().trimmed();
    if (type == Compound) return new compound_tag(name);
    if (type == String) return new string_tag(name, ui->value_lineedit->text().toStdString());
    if (type == List) return new list_tag(name);
    if (type == Byte || type == Int || type == Short || type == Long || type == ByteArray || type == IntArray || type == LongArray) {
        std::vector<int64_t> values;
        if (!parseIntegerValue(type, vs, values)) {
            err = "TAG的值不合法";
            return nullptr;
        }
        if (type == Byte) return new byte_tag(name, static_cast<int8_t>(values.front()));
        if (type == Short) return new short_tag(name, static_cast<int16_t>(values.front()));
        if (type == Int) return new int_tag(name, static_cast<int32_t>(values.front()));
        if (type == Long) auto *tag = new long_tag(name, static_cast<int64_t>(values.front()));
        if (type == ByteArray) {
            auto *tag = new byte_array_tag(name);
            for (auto &value : values) {
                tag->value.push_back(static_cast<int8_t>(value));
            }
            return tag;
        }
        if (type == IntArray) {
            auto *tag = new int_array_tag(name);
            for (auto &value : values) {
                tag->value.push_back(static_cast<int32_t>(value));
            }
            return tag;
        }
        if (type == LongArray) {
            auto *tag = new long_array_tag(name);
            for (auto &value : values) {
                tag->value.push_back(static_cast<int64_t>(value));
            }
            return tag;
        }
    }

    if (type == Float) {
        auto v = vs.toFloat(&ok);
        if (ok) {
            return new float_tag(name, v);
        } else {
            err = "TAG的值不合法";
            return nullptr;
        }
    }
    if (type == Double) {
        auto v = vs.toDouble(&ok);
        if (ok) {
            return new double_tag(name, v);
        } else {
            err = "TAG的值不合法";
            return nullptr;
        }
    }

    err = "未知的TAG类型";
    return nullptr;
}

bool NBTModifyDialog::modityCurrentTag(bl::palette::abstract_tag *&tag, QString &err) {
    tag->set_key(ui->name_lineedit->text().toStdString());
    auto vs = ui->value_lineedit->text();
    auto type = tag->type();
    if (type == Compound || type == List) return true;
    if (type == String) dynamic_cast<string_tag *>(tag)->value = vs.toStdString();
    vs = vs.trimmed();
    if (vs.isEmpty()) {
        err = "TAG的值为空";
        return false;
    }

    if (type == Byte || type == Int || type == Short || type == Long || type == ByteArray || type == IntArray || type == LongArray) {
        std::vector<int64_t> values;
        if (!parseIntegerValue(type, vs, values)) {
            err = "TAG的值不合法";
            return false;
        }
        if (type == Byte) dynamic_cast<byte_tag *>(tag)->value = static_cast<int8_t>(values.front());
        if (type == Short) dynamic_cast<short_tag *>(tag)->value = static_cast<int16_t>(values.front());
        if (type == Int) dynamic_cast<int_tag *>(tag)->value = static_cast<int32_t>(values.front());
        if (type == Long) dynamic_cast<long_tag *>(tag)->value = static_cast<int64_t>(values.front());
        if (type == ByteArray) {
            auto *ba_tag = dynamic_cast<byte_array_tag *>(tag);
            ba_tag->value.clear();
            for (auto &value : values) {
                ba_tag->value.push_back(static_cast<int8_t>(value));
            }
        }
        if (type == IntArray) {
            auto *ia_tag = dynamic_cast<int_array_tag *>(tag);
            ia_tag->value.clear();
            for (auto &value : values) {
                ia_tag->value.push_back(static_cast<int32_t>(value));
            }
        }
        if (type == LongArray) {
            auto *la_tag = dynamic_cast<long_array_tag *>(tag);
            la_tag->value.clear();
            for (auto &value : values) {
                la_tag->value.push_back(static_cast<int64_t>(value));
            }
        }
        return true;
    }
    if (type == Float) {
        bool ok;
        auto v = vs.toFloat(&ok);
        if (ok) {
            dynamic_cast<float_tag *>(tag)->value = v;
            return true;
        } else {
            err = "TAG的值不合法";
            return false;
        }
    }
    if (type == Double) {
        bool ok;
        auto v = vs.toDouble(&ok);
        if (ok) {
            dynamic_cast<double_tag *>(tag)->value = v;
            return true;
        } else {
            err = "TAG的值不合法";
            return false;
        }
    }
    err = "未知的TAG类型";
    return false;
}

NBTModifyDialog::~NBTModifyDialog() { delete ui; }
