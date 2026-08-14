#include "nbtfilepagewidget.h"

#include <QFileInfo>
#include <QVBoxLayout>

#include "loguru/loguru.hpp"
#include "nbt.h"

NbtFilePageWidget::NbtFilePageWidget(QWidget *parent) : TabPageWidget(parent) {
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    nbt_editor_ = new NbtWidget(this);
    nbt_editor_->setMode(NbtMode::File);
    root->addWidget(nbt_editor_);
    connect(nbt_editor_, &NbtWidget::nbtModified, this, [this]() {
        dirty_ = true;
        emit dirtyChanged(true);
    });
    connect(nbt_editor_, &NbtWidget::dataSaved, this, [this]() {
        dirty_ = false;
        // a new (untitled) document now has a real path: use it as the tab name
        const auto savedPath = nbt_editor_->filePath();
        if (!savedPath.isEmpty()) {
            file_name_ = QFileInfo(savedPath).baseName();
        }
        emit dirtyChanged(false);
        emit saved(savedPath);
    });
}

NbtFilePageWidget::~NbtFilePageWidget() = default;

bool NbtFilePageWidget::isDirty() const { return dirty_; }

bool NbtFilePageWidget::commit() { return nbt_editor_->saveToFile(); }

void NbtFilePageWidget::createNew() {
    file_name_ = "untitled";
    // start with one empty root tag so the editor is immediately usable
    auto *root = new bl::nbt::compound_tag("root");
    nbt_editor_->loadNewData({NBTListItem::from(root, "0")});
    nbt_editor_->openItem(0);
    dirty_ = true;
    emit dirtyChanged(true);
}

bool NbtFilePageWidget::loadFile(const QString &path) {
    auto data = bl::utils::read_file(path.toStdString());
    if (data.empty()) {
        LOG_F(WARNING, "Can not read NBT file: %s", path.toStdString().c_str());
        return false;
    }

    auto palette = bl::nbt::read_palette_to_end(data.data(), data.size());
    if (palette.empty()) {
        LOG_F(WARNING, "No NBT tags found in file: %s", path.toStdString().c_str());
        return false;
    }

    std::vector<NBTListItem *> items;
    for (size_t i = 0; i < palette.size(); ++i) {
        items.push_back(NBTListItem::from(palette[i], QString::number(i)));
    }
    nbt_editor_->setFilePath(path);
    nbt_editor_->loadNewData(items);
    nbt_editor_->openItem(0);
    file_name_ = QFileInfo(path).baseName();
    dirty_ = false;
    emit dirtyChanged(false);
    return true;
}
