#include "controller/nbtdocumentcontroller.h"

#include <QFile>
#include <QFileInfo>
#include <QSet>
#include <QUrl>
#include <utility>

#include "model/nbttreemodel.h"
#include "nbt.h"

namespace {
    QString localFilePath(const QString& path) {
        const QUrl url(path);
        return url.isLocalFile() ? url.toLocalFile() : path;
    }
}  // namespace

NbtDocumentController::NbtDocumentController(QObject* parent) : QObject(parent), tree_model_(new NbtTreeModel(this)) {
    connect(tree_model_, &NbtTreeModel::documentChanged, this, [this] {
        if (!loading_ && !read_only_) markDirty();
    });
}

NbtDocumentController::~NbtDocumentController() {
    // The model borrows tags from items_. Destroy it before those roots rather
    // than leaving it alive until QObject deletes its children.
    delete tree_model_;
}

QStringList NbtDocumentController::itemLabels() const {
    QStringList labels;
    labels.reserve(static_cast<qsizetype>(items_.size()));
    for (size_t i = 0; i < items_.size(); ++i) {
        labels << (items_[i].label.isEmpty() ? QString::number(i) : items_[i].label);
    }
    return labels;
}

QStringList NbtDocumentController::itemIcons() const {
    QStringList icons;
    icons.reserve(static_cast<qsizetype>(items_.size()));
    for (const auto& item : items_) icons << item.icon;
    return icons;
}

QString NbtDocumentController::documentTitle() const {
    if (!document_title_.isEmpty()) return document_title_;
    return file_path_.isEmpty() ? QStringLiteral("Untitled") : QFileInfo(file_path_).fileName();
}

void NbtDocumentController::setError(const QString& message) {
    if (last_error_ == message) return;
    last_error_ = message;
    emit lastErrorChanged();
}

void NbtDocumentController::markDirty() {
    if (dirty_) return;
    dirty_ = true;
    emit dirtyChanged();
}

void NbtDocumentController::clearItems() {
    tree_model_->setRoot(nullptr);
    items_.clear();
    current_index_ = -1;
    emit itemsChanged();
    emit currentIndexChanged();
}

void NbtDocumentController::replaceItems(std::vector<std::unique_ptr<bl::nbt::compound_tag>> roots, const QStringList& rawKeys, Mode mode,
                                         const QString& path, const QString& title, const NbtItemPresenter& presenter) {
    const QString old_title = documentTitle();
    loading_ = true;
    // Views may read the old rows during modelAboutToBeReset. Their tags must
    // remain alive until the old model has been detached.
    tree_model_->setRoot(nullptr);
    items_.clear();
    items_.reserve(roots.size());
    original_raw_keys_.clear();
    for (size_t i = 0; i < roots.size(); ++i) {
        Item item;
        item.root = std::move(roots[i]);
        item.raw_key = i < static_cast<size_t>(rawKeys.size()) && !rawKeys[static_cast<qsizetype>(i)].isEmpty()
                           ? rawKeys[static_cast<qsizetype>(i)]
                           : (item.root ? QString::fromStdString(item.root->key()) : QString());
        if (!item.raw_key.isEmpty()) original_raw_keys_.append(item.raw_key);
        item.label = QString::number(i);
        if (presenter && item.root) {
            const NbtItemPresentation presentation = presenter(item.root.get(), static_cast<int>(i), item.raw_key);
            if (!presentation.label.isEmpty()) item.label = presentation.label;
            item.icon = presentation.icon;
        }
        items_.push_back(std::move(item));
    }
    current_index_ = items_.empty() ? -1 : 0;
    const bool mode_changed = mode_ != mode;
    const bool path_changed = file_path_ != path;
    mode_ = mode;
    file_path_ = path;
    document_title_ = title;
    tree_model_->setRoot(current_index_ >= 0 ? items_[current_index_].root.get() : nullptr);
    dirty_ = false;
    loading_ = false;
    emit itemsChanged();
    emit currentIndexChanged();
    if (mode_changed) emit modeChanged();
    if (path_changed) emit filePathChanged();
    if (documentTitle() != old_title) emit documentTitleChanged();
    emit dirtyChanged();
}

bool NbtDocumentController::loadFile(const QString& path) {
    const QString local_path = localFilePath(path);
    QFile file(local_path);
    if (!file.open(QIODevice::ReadOnly)) {
        setError(QStringLiteral("Cannot open NBT file: %1").arg(file.errorString()));
        return false;
    }
    const QByteArray bytes = file.readAll();
    if (bytes.isEmpty()) {
        setError(QStringLiteral("The NBT file is empty."));
        return false;
    }
    auto parsed = bl::nbt::parse_all(reinterpret_cast<const char*>(bytes.constData()), static_cast<size_t>(bytes.size()));
    if (parsed.empty()) {
        setError(QStringLiteral("No NBT roots were found in the file."));
        return false;
    }
    std::vector<std::unique_ptr<bl::nbt::compound_tag>> roots;
    roots.reserve(parsed.size());
    for (auto* root : parsed) roots.emplace_back(root);
    replaceItems(std::move(roots), {}, Mode::File, local_path);
    setError({});
    return true;
}

void NbtDocumentController::loadMemory(std::vector<std::unique_ptr<bl::nbt::compound_tag>> roots, const QStringList& rawKeys,
                                       const QString& title, const NbtItemPresenter& presenter) {
    replaceItems(std::move(roots), rawKeys, Mode::Memory, {}, title.isEmpty() ? QStringLiteral("Memory NBT") : title, presenter);
    setError({});
}

std::unique_ptr<bl::nbt::compound_tag> NbtDocumentController::itemCopy(int index) const {
    if (index < 0 || index >= static_cast<int>(items_.size()) || !items_[static_cast<size_t>(index)].root) return nullptr;
    return std::unique_ptr<bl::nbt::compound_tag>(
        static_cast<bl::nbt::compound_tag*>(items_[static_cast<size_t>(index)].root->copy()));
}

bool NbtDocumentController::isEmpty() const {
    for (const auto& item : items_) {
        if (item.root && !item.root->value.empty()) return false;
    }
    return true;
}

std::unordered_map<std::string, std::string> NbtDocumentController::levelDbChanges() const {
    std::unordered_map<std::string, std::string> result;
    if (mode_ != Mode::Memory || !dirty_) return result;

    QSet<QString> current_keys;
    for (const auto& item : items_) {
        if (!item.root || item.raw_key.isEmpty()) continue;
        current_keys.insert(item.raw_key);
        result[item.raw_key.toStdString()] = item.root->serialize();
    }
    for (const auto& key : original_raw_keys_) {
        if (!current_keys.contains(key)) result[key.toStdString()] = {};
    }
    return result;
}

void NbtDocumentController::markPersisted() {
    if (!dirty_) return;
    dirty_ = false;
    original_raw_keys_.clear();
    for (const auto& item : items_) {
        if (!item.raw_key.isEmpty()) original_raw_keys_.append(item.raw_key);
    }
    emit dirtyChanged();
}

bool NbtDocumentController::save() {
    if (mode_ == Mode::Memory) {
        setError(QStringLiteral("In-memory NBT documents are committed by their owner."));
        return false;
    }
    if (file_path_.isEmpty()) {
        setError(QStringLiteral("Choose a destination with Save As."));
        return false;
    }
    return saveAs(file_path_);
}

bool NbtDocumentController::saveAs(const QString& path) {
    if (mode_ == Mode::Memory) {
        setError(QStringLiteral("In-memory NBT documents are committed by their owner."));
        return false;
    }
    const QString local_path = localFilePath(path);
    QByteArray output;
    for (const auto& item : items_) {
        if (!item.root) continue;
        const std::string data = item.root->serialize();
        output.append(data.data(), static_cast<qsizetype>(data.size()));
    }
    QFile file(local_path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        setError(QStringLiteral("Cannot write NBT file: %1").arg(file.errorString()));
        return false;
    }
    if (file.write(output) != output.size()) {
        setError(QStringLiteral("The NBT file could not be written completely."));
        return false;
    }
    const QString old_title = documentTitle();
    const bool path_changed = file_path_ != local_path;
    file_path_ = local_path;
    // "Untitled" (and other creation-time labels) must not outlive the first
    // successful save; documentTitle() then derives the name from the file.
    document_title_.clear();
    dirty_ = false;
    setError({});
    if (path_changed) emit filePathChanged();
    if (documentTitle() != old_title) emit documentTitleChanged();
    emit dirtyChanged();
    return true;
}

bool NbtDocumentController::exportFile(const QString& path) {
    const QString local_path = localFilePath(path);
    QByteArray output;
    for (const auto& item : items_) {
        if (!item.root) continue;
        const std::string data = item.root->serialize();
        output.append(data.data(), static_cast<qsizetype>(data.size()));
    }
    if (output.isEmpty()) {
        setError(QStringLiteral("There is no NBT data to export."));
        return false;
    }
    QFile file(local_path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        setError(QStringLiteral("Cannot write NBT file: %1").arg(file.errorString()));
        return false;
    }
    if (file.write(output) != output.size()) {
        setError(QStringLiteral("The NBT file could not be written completely."));
        return false;
    }
    setError({});
    return true;
}

bool NbtDocumentController::importFile(const QString& path) {
    if (mode_ == Mode::File) return loadFile(path);

    const QString local_path = localFilePath(path);
    QFile file(local_path);
    if (!file.open(QIODevice::ReadOnly)) {
        setError(QStringLiteral("Cannot open NBT file: %1").arg(file.errorString()));
        return false;
    }
    const QByteArray bytes = file.readAll();
    if (bytes.isEmpty()) {
        setError(QStringLiteral("The NBT file is empty."));
        return false;
    }
    auto parsed = bl::nbt::parse_all(reinterpret_cast<const char*>(bytes.constData()), static_cast<size_t>(bytes.size()));
    if (parsed.empty()) {
        setError(QStringLiteral("No NBT roots were found in the file."));
        return false;
    }

    QStringList old_raw_keys;
    old_raw_keys.reserve(static_cast<qsizetype>(items_.size()));
    for (const auto& item : items_) old_raw_keys.append(item.raw_key);
    const QStringList old_original_keys = original_raw_keys_;
    const QString title = documentTitle();

    std::vector<std::unique_ptr<bl::nbt::compound_tag>> roots;
    roots.reserve(parsed.size());
    for (auto* root : parsed) roots.emplace_back(root);
    QStringList raw_keys;
    raw_keys.reserve(static_cast<qsizetype>(roots.size()));
    for (qsizetype i = 0; i < static_cast<qsizetype>(roots.size()); ++i)
        raw_keys.append(i < old_raw_keys.size() ? old_raw_keys[i] : QString());

    replaceItems(std::move(roots), raw_keys, Mode::Memory, {}, title);

    QSet<QString> original;
    for (const auto& key : old_original_keys) {
        if (!key.isEmpty()) original.insert(key);
    }
    for (const auto& key : raw_keys) {
        if (!key.isEmpty()) original.insert(key);
    }
    original_raw_keys_ = original.values();
    markDirty();
    setError({});
    return true;
}

void NbtDocumentController::createNew() {
    auto root = std::make_unique<bl::nbt::compound_tag>("root");
    std::vector<std::unique_ptr<bl::nbt::compound_tag>> roots;
    roots.push_back(std::move(root));
    replaceItems(std::move(roots), {}, Mode::File, {}, QStringLiteral("Untitled"));
    dirty_ = true;
    emit dirtyChanged();
}

void NbtDocumentController::selectItem(int index) {
    if (index < 0 || index >= static_cast<int>(items_.size()) || index == current_index_) return;
    loading_ = true;
    current_index_ = index;
    tree_model_->setRoot(items_[index].root.get());
    loading_ = false;
    emit currentIndexChanged();
}

int NbtDocumentController::addItem() {
    Item item;
    item.root = std::make_unique<bl::nbt::compound_tag>("root");
    item.label = QString::number(items_.size());
    items_.push_back(std::move(item));
    current_index_ = static_cast<int>(items_.size()) - 1;
    loading_ = true;
    tree_model_->setRoot(items_[current_index_].root.get());
    loading_ = false;
    markDirty();
    emit itemsChanged();
    emit currentIndexChanged();
    return current_index_;
}

bool NbtDocumentController::removeItem(int index) {
    if (index < 0 || index >= static_cast<int>(items_.size()) || items_.size() <= 1) return false;
    items_.erase(items_.begin() + index);
    if (current_index_ >= static_cast<int>(items_.size()))
        current_index_ = static_cast<int>(items_.size()) - 1;
    else if (current_index_ > index)
        --current_index_;
    loading_ = true;
    tree_model_->setRoot(current_index_ >= 0 ? items_[current_index_].root.get() : nullptr);
    loading_ = false;
    markDirty();
    emit itemsChanged();
    emit currentIndexChanged();
    return true;
}

bool NbtDocumentController::clearAllItems() {
    if (read_only_ || items_.empty()) return false;
    clearItems();
    markDirty();
    return true;
}

bool NbtDocumentController::clearContents() {
    if (read_only_) return false;

    // Clear every root. Keep original_raw_keys_ so a memory document still
    // emits deletions for all LevelDB keys on commit.
    clearItems();
    markDirty();
    setError({});
    return true;
}

void NbtDocumentController::setReadOnly(bool read_only) {
    if (read_only_ == read_only) return;
    read_only_ = read_only;
    tree_model_->setEditable(!read_only_);
    emit readOnlyChanged();
}
