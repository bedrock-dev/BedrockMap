#include "controller/nbtdocumentcontroller.h"

#include <QCoreApplication>
#include <QSet>
#include <QUrl>
#include <utility>

#include "io/documentio.h"
#include "model/nbttreemodel.h"
#include "nbt.h"

NbtDocumentController::NbtDocumentController(QObject* parent) : QObject(parent), tree_model_(new NbtTreeModel(this)) {
    connect(tree_model_, &NbtTreeModel::documentChanged, this, [this] {
        if (loading_ || read_only_) return;
        if (mode_ == Mode::Memory && current_index_ >= 0 && current_index_ < static_cast<int>(items_.size())) {
            auto& item = items_[static_cast<size_t>(current_index_)];
            item.modified = true;
            ++item.revision;
        }
        markDirty();
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
    return file_url_.isEmpty() ? QStringLiteral("Untitled") : io::DocumentIo::displayName(file_url_);
}

QString NbtDocumentController::filePath() const { return file_url_.isEmpty() ? QString{} : io::DocumentIo::identifier(file_url_); }

void NbtDocumentController::setError(const QString& message) {
    if (last_error_ == message) return;
    last_error_ = message;
    emit lastErrorChanged();
}

void NbtDocumentController::setDirty(bool dirty) {
    if (dirty_ == dirty) return;
    dirty_ = dirty;
    emit dirtyChanged();
}

void NbtDocumentController::markDirty() { setDirty(true); }

bool NbtDocumentController::hasPendingMemoryChanges() const {
    QSet<QString> removed_keys(original_raw_keys_.begin(), original_raw_keys_.end());
    for (const auto& item : items_) {
        if (!item.root) continue;
        if (item.modified || !item.persisted || item.raw_key.isEmpty()) return true;
        removed_keys.remove(item.raw_key);
    }
    return !removed_keys.isEmpty();
}

void NbtDocumentController::clearItems() {
    tree_model_->setRoot(nullptr);
    items_.clear();
    current_index_ = -1;
    emit itemsChanged();
    emit currentIndexChanged();
}

void NbtDocumentController::setDocumentOpen(bool open) {
    if (document_open_ == open) return;
    document_open_ = open;
    emit documentOpenChanged();
}

void NbtDocumentController::replaceItems(std::vector<std::unique_ptr<bl::nbt::compound_tag>> roots, const QStringList& rawKeys, Mode mode,
                                         const QUrl& url, const QString& title, const NbtItemPresenter& presenter) {
    const QString old_title = documentTitle();
    loading_ = true;
    // Views may read the old rows during modelAboutToBeReset. Their tags must
    // remain alive until the old model has been detached.
    tree_model_->setRoot(nullptr);
    items_.clear();
    storage_.presenter = presenter;
    items_.reserve(roots.size());
    original_raw_keys_.clear();
    for (size_t i = 0; i < roots.size(); ++i) {
        Item item;
        item.id = ++next_item_id_;
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
    const bool path_changed = file_url_ != url;
    mode_ = mode;
    file_url_ = url;
    document_title_ = title;
    tree_model_->setRoot(current_index_ >= 0 ? items_[current_index_].root.get() : nullptr);
    loading_ = false;
    emit itemsChanged();
    emit currentIndexChanged();
    setDocumentOpen(true);
    if (mode_changed) emit modeChanged();
    if (path_changed) emit filePathChanged();
    if (documentTitle() != old_title) emit documentTitleChanged();
    setDirty(false);
}

bool NbtDocumentController::loadFile(const QUrl& url) {
    const auto read = io::DocumentIo::readAll(url);
    if (!read.ok()) {
        setError(QStringLiteral("Cannot open NBT file: %1").arg(read.error));
        return false;
    }
    if (read.data.isEmpty()) {
        replaceItems({}, {}, Mode::File, url, io::DocumentIo::displayName(url));
        setError({});
        return true;
    }
    auto parsed = bl::nbt::parse_all(std::span<const byte_t>(
        reinterpret_cast<const byte_t*>(read.data.constData()), static_cast<size_t>(read.data.size())));
    if (!parsed) {
        setError(QStringLiteral("Cannot parse NBT file: %1").arg(bl::nbt::parse_error_to_string(parsed.error_code)));
        return false;
    }
    if (parsed.roots.empty()) {
        setError(QStringLiteral("No NBT roots were found in the file."));
        return false;
    }
    std::vector<std::unique_ptr<bl::nbt::compound_tag>> roots;
    roots.reserve(parsed.roots.size());
    for (auto* root : parsed.roots) roots.emplace_back(root);
    // Cache provider metadata while the file grant is available. QML title
    // bindings must not query the provider on every evaluation.
    replaceItems(std::move(roots), {}, Mode::File, url, io::DocumentIo::displayName(url));
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
    return std::unique_ptr<bl::nbt::compound_tag>(static_cast<bl::nbt::compound_tag*>(items_[static_cast<size_t>(index)].root->copy()));
}

void NbtDocumentController::setStoragePolicy(NbtStoragePolicy policy) { storage_ = std::move(policy); }

void NbtDocumentController::setKeyResolver(NbtKeyResolver resolver) { storage_.keyResolver = std::move(resolver); }

NbtLevelDbPlan NbtDocumentController::prepareLevelDbChanges() {
    NbtLevelDbPlan plan;
    if (mode_ != Mode::Memory) return plan;

    plan.prepared = true;
    plan.records.reserve(items_.size());
    if (!dirty_) {
        for (const auto& item : items_) plan.records.push_back({item.id, item.revision, item.raw_key});
        return plan;
    }

    QSet<QString> current_keys;
    for (size_t index = 0; index < items_.size(); ++index) {
        const auto& item = items_[index];
        if (!item.root) {
            plan.records.push_back({item.id, item.revision, {}});
            continue;
        }
        QString key = item.raw_key;
        if (!item.persisted || key.isEmpty()) {
            if (!storage_.keyResolver) {
                plan.error = QCoreApplication::translate("NbtDocumentController",
                                                         "No LevelDB key rule is configured for record %1.")
                                 .arg(index);
                setError(plan.error);
                return plan;
            }
            key = storage_.keyResolver(item.root.get(), static_cast<int>(index), key).trimmed();
            if (key.isEmpty()) {
                plan.error = QCoreApplication::translate("NbtDocumentController",
                                                         "Cannot generate a LevelDB key for record %1.")
                                 .arg(item.label.isEmpty() ? QString::number(index) : item.label);
                setError(plan.error);
                return plan;
            }
        }
        if (current_keys.contains(key)) {
            plan.error = QCoreApplication::translate("NbtDocumentController", "Duplicate LevelDB key: %1.").arg(key);
            setError(plan.error);
            return plan;
        }
        current_keys.insert(key);
        plan.records.push_back({item.id, item.revision, key});
        if (item.modified || !item.persisted || item.raw_key.isEmpty()) {
            plan.changes[key.toStdString()] = item.root->serialize();
        }
    }
    for (const auto& key : original_raw_keys_) {
        if (!key.isEmpty() && !current_keys.contains(key)) plan.changes[key.toStdString()] = {};
    }
    setError({});
    return plan;
}

void NbtDocumentController::markPersisted(const NbtLevelDbPlan& plan) {
    if (mode_ == Mode::Memory && plan.prepared) {
        // Acknowledge the saved snapshot by stable identity. Later edits and
        // records added or removed during the write must remain pending.
        std::unordered_map<quint64, const NbtLevelDbPlan::Record*> saved_records;
        original_raw_keys_.clear();
        for (const auto& record : plan.records) {
            saved_records.emplace(record.id, &record);
            if (!record.key.isEmpty()) original_raw_keys_.append(record.key);
        }
        for (auto& item : items_) {
            const auto saved = saved_records.find(item.id);
            if (saved == saved_records.end() || saved->second->key.isEmpty()) continue;
            item.raw_key = saved->second->key;
            item.persisted = true;
            item.modified = item.revision != saved->second->revision;
        }
        setDirty(hasPendingMemoryChanges());
        return;
    }
    if (!dirty_) return;
    original_raw_keys_.clear();
    for (auto& item : items_) {
        item.persisted = true;
        item.modified = false;
        if (!item.raw_key.isEmpty()) original_raw_keys_.append(item.raw_key);
    }
    setDirty(false);
}

bool NbtDocumentController::save() {
    if (mode_ == Mode::Memory) {
        setError(QStringLiteral("In-memory NBT documents are committed by their owner."));
        return false;
    }
    if (file_url_.isEmpty()) {
        setError(QStringLiteral("Choose a destination with Save As."));
        return false;
    }
    return saveAs(file_url_);
}

bool NbtDocumentController::requiresSaveAs() const { return io::DocumentIo::requiresSaveAs(file_url_); }

bool NbtDocumentController::saveAs(const QUrl& url) {
    if (mode_ == Mode::Memory) {
        setError(QStringLiteral("In-memory NBT documents are committed by their owner."));
        return false;
    }
    if (read_only_) return false;
    QByteArray output;
    for (const auto& item : items_) {
        if (!item.root) continue;
        const std::string data = item.root->serialize();
        output.append(data.data(), static_cast<qsizetype>(data.size()));
    }
    const QString error = io::DocumentIo::writeBytes(url, output);
    if (!error.isEmpty()) {
        setError(QStringLiteral("Cannot write NBT file: %1").arg(error));
        return false;
    }
    const QString old_title = documentTitle();
    const bool path_changed = file_url_ != url;
    file_url_ = url;
    // Replace creation-time labels after Save As, and retain the cached title
    // when saving the same provider document again.
    if (path_changed || document_title_.isEmpty()) document_title_ = io::DocumentIo::displayName(url);
    setDirty(false);
    setError({});
    if (path_changed) emit filePathChanged();
    if (documentTitle() != old_title) emit documentTitleChanged();
    emit saved();
    return true;
}

bool NbtDocumentController::exportFile(const QUrl& url) {
    QByteArray output;
    for (const auto& item : items_) {
        if (!item.root) continue;
        const std::string data = item.root->serialize();
        output.append(data.data(), static_cast<qsizetype>(data.size()));
    }
    if (const QString error = io::DocumentIo::writeBytes(url, output); !error.isEmpty()) {
        setError(QStringLiteral("Cannot write NBT file: %1").arg(error));
        return false;
    }
    setError({});
    return true;
}

bool NbtDocumentController::exportCurrent(const QUrl& url) {
    if (current_index_ < 0 || current_index_ >= static_cast<int>(items_.size()) ||
        !items_[static_cast<size_t>(current_index_)].root) {
        setError(QStringLiteral("There is no current NBT record to export."));
        return false;
    }
    const std::string data = items_[static_cast<size_t>(current_index_)].root->serialize();
    const QString error = io::DocumentIo::writeBytes(url, QByteArray(data.data(), static_cast<qsizetype>(data.size())));
    if (!error.isEmpty()) {
        setError(QStringLiteral("Cannot write NBT file: %1").arg(error));
        return false;
    }
    setError({});
    return true;
}

bool NbtDocumentController::importFile(const QUrl& url) {
    if (read_only_) return false;
    const auto read = io::DocumentIo::readAll(url);
    if (!read.ok()) {
        setError(QStringLiteral("Cannot open NBT file: %1").arg(read.error));
        return false;
    }
    if (read.data.isEmpty()) {
        if (storage_.singleRecord) {
            setError(QCoreApplication::translate("NbtDocumentController", "A single-record section requires exactly one NBT root."));
            return false;
        }
        if (mode_ == Mode::File && file_url_.isEmpty())
            replaceItems({}, {}, Mode::File, url, io::DocumentIo::displayName(url));
        else
            setDocumentOpen(true);
        setError({});
        return true;
    }
    auto parsed = bl::nbt::parse_all(std::span<const byte_t>(
        reinterpret_cast<const byte_t*>(read.data.constData()), static_cast<size_t>(read.data.size())));
    if (!parsed) {
        setError(QStringLiteral("Cannot parse NBT file: %1").arg(bl::nbt::parse_error_to_string(parsed.error_code)));
        return false;
    }
    if (parsed.roots.empty()) {
        setError(QStringLiteral("No NBT roots were found in the file."));
        return false;
    }

    std::vector<std::unique_ptr<bl::nbt::compound_tag>> roots;
    roots.reserve(parsed.roots.size());
    for (auto* root : parsed.roots) roots.emplace_back(root);

    if (storage_.singleRecord) {
        if (!items_.empty()) {
            setError(QCoreApplication::translate("NbtDocumentController",
                                                 "A single-record section already contains a record. Clear it before importing."));
            return false;
        }
        if (roots.size() != 1) {
            setError(QCoreApplication::translate("NbtDocumentController",
                                                 "A single-record section requires exactly one NBT root."));
            return false;
        }
        Item item;
        item.id = ++next_item_id_;
        item.modified = mode_ == Mode::Memory;
        item.root = std::move(roots.front());
        item.raw_key = items_.size() == 1 ? items_.front().raw_key : QString{};
        if (item.raw_key.isEmpty()) item.raw_key = storage_.fixedKey;
        if (item.raw_key.isEmpty()) item.raw_key = QString::fromStdString(item.root->key());
        item.persisted = !item.raw_key.isEmpty();
        item.label = QStringLiteral("0");
        if (storage_.presenter && item.root) {
            const NbtItemPresentation presentation = storage_.presenter(item.root.get(), 0, item.raw_key);
            if (!presentation.label.isEmpty()) item.label = presentation.label;
            item.icon = presentation.icon;
        }
        tree_model_->setRoot(nullptr);
        items_.clear();
        items_.push_back(std::move(item));
        current_index_ = 0;
        tree_model_->setRoot(items_.front().root.get());
        emit itemsChanged();
        emit currentIndexChanged();
        setDocumentOpen(true);
        markDirty();
        setError({});
        return true;
    }

    if (mode_ == Mode::File && file_url_.isEmpty() && items_.size() == 1 && items_.front().root &&
        items_.front().root->value.empty()) {
        tree_model_->setRoot(nullptr);
        items_.clear();
    }
    const bool adopt_file = mode_ == Mode::File && items_.empty() && file_url_.isEmpty();
    const int first_new = static_cast<int>(items_.size());
    loading_ = true;
    items_.reserve(items_.size() + roots.size());
    for (auto& root : roots) {
        Item item;
        item.id = ++next_item_id_;
        item.root = std::move(root);
        item.raw_key = QString::fromStdString(item.root->key());
        item.persisted = false;
        item.label = QString::number(items_.size());
        if (storage_.presenter && item.root) {
            const NbtItemPresentation presentation =
                storage_.presenter(item.root.get(), static_cast<int>(items_.size()), item.raw_key);
            if (!presentation.label.isEmpty()) item.label = presentation.label;
            item.icon = presentation.icon;
        }
        items_.push_back(std::move(item));
    }
    current_index_ = first_new;
    tree_model_->setRoot(items_[static_cast<size_t>(current_index_)].root.get());
    loading_ = false;
    const QString old_title = documentTitle();
    if (adopt_file) {
        file_url_ = url;
        document_title_ = io::DocumentIo::displayName(url);
    }
    emit itemsChanged();
    emit currentIndexChanged();
    setDocumentOpen(true);
    if (adopt_file) emit filePathChanged();
    if (documentTitle() != old_title) emit documentTitleChanged();
    markDirty();
    setError({});
    return true;
}

bool NbtDocumentController::replaceFile(const QUrl& url) {
    if (read_only_ || !storage_.singleRecord) return false;
    const auto read = io::DocumentIo::readAll(url);
    if (!read.ok()) {
        setError(QStringLiteral("Cannot open NBT file: %1").arg(read.error));
        return false;
    }
    if (read.data.isEmpty()) {
        setError(QStringLiteral("The NBT file is empty."));
        return false;
    }
    auto parsed = bl::nbt::parse_all(std::span<const byte_t>(
        reinterpret_cast<const byte_t*>(read.data.constData()), static_cast<size_t>(read.data.size())));
    if (!parsed) {
        setError(QStringLiteral("Cannot parse NBT file: %1").arg(bl::nbt::parse_error_to_string(parsed.error_code)));
        return false;
    }
    if (parsed.roots.size() != 1) {
        for (auto* root : parsed.roots) delete root;
        setError(QCoreApplication::translate("NbtDocumentController",
                                             "A single-record section requires exactly one NBT root."));
        return false;
    }

    Item item;
    item.id = ++next_item_id_;
    item.modified = mode_ == Mode::Memory;
    item.root.reset(parsed.roots.front());
    item.raw_key = items_.size() == 1 ? items_.front().raw_key : QString{};
    if (item.raw_key.isEmpty()) item.raw_key = storage_.fixedKey;
    if (item.raw_key.isEmpty()) item.raw_key = QString::fromStdString(item.root->key());
    item.persisted = !item.raw_key.isEmpty();
    item.label = QStringLiteral("0");
    if (storage_.presenter && item.root) {
        const NbtItemPresentation presentation = storage_.presenter(item.root.get(), 0, item.raw_key);
        if (!presentation.label.isEmpty()) item.label = presentation.label;
        item.icon = presentation.icon;
    }
    tree_model_->setRoot(nullptr);
    items_.clear();
    items_.push_back(std::move(item));
    current_index_ = 0;
    tree_model_->setRoot(items_.front().root.get());
    emit itemsChanged();
    emit currentIndexChanged();
    setDocumentOpen(true);
    markDirty();
    setError({});
    return true;
}

void NbtDocumentController::createNew() {
    replaceItems({}, {}, Mode::File, {}, QStringLiteral("Untitled"));
    markDirty();
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
    if (read_only_ || (storage_.singleRecord && !items_.empty())) return -1;
    Item item;
    item.id = ++next_item_id_;
    item.root = std::make_unique<bl::nbt::compound_tag>("root");
    item.persisted = false;
    item.label = QString::number(items_.size());
    items_.push_back(std::move(item));
    current_index_ = static_cast<int>(items_.size()) - 1;
    loading_ = true;
    tree_model_->setRoot(items_[current_index_].root.get());
    loading_ = false;
    markDirty();
    emit itemsChanged();
    emit currentIndexChanged();
    setDocumentOpen(true);
    return current_index_;
}

bool NbtDocumentController::removeItem(int index) {
    if (read_only_ || index < 0 || index >= static_cast<int>(items_.size())) return false;
    loading_ = true;
    // Detach borrowed tags while the old roots are still alive.
    tree_model_->setRoot(nullptr);
    items_.erase(items_.begin() + index);
    if (current_index_ >= static_cast<int>(items_.size()))
        current_index_ = static_cast<int>(items_.size()) - 1;
    else if (current_index_ > index)
        --current_index_;
    tree_model_->setRoot(current_index_ >= 0 ? items_[current_index_].root.get() : nullptr);
    loading_ = false;
    markDirty();
    emit itemsChanged();
    emit currentIndexChanged();
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
