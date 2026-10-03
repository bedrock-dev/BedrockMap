#include "datakeymodel.h"

#include <QVariant>

#include <array>
#include <algorithm>
#include <string>
#include <set>

namespace {

    [[nodiscard]] QString categoryTitle(DataKeyCategory category) {
        switch (category) {
            case DataKeyCategory::Chunks:
                return QStringLiteral("chunks");
            case DataKeyCategory::Villages:
                return QStringLiteral("villages");
            case DataKeyCategory::Players:
                return QStringLiteral("players");
            case DataKeyCategory::MapItems:
                return QStringLiteral("map items");
            case DataKeyCategory::Actors:
                return QStringLiteral("actors");
            case DataKeyCategory::Digp:
                return QStringLiteral("digp");
            case DataKeyCategory::Others:
                return QStringLiteral("others");
        }
        return QStringLiteral("others");
    }

    [[nodiscard]] bool hasHierarchy(DataKeyCategory category) noexcept {
        return category == DataKeyCategory::Chunks || category == DataKeyCategory::Villages;
    }

    [[nodiscard]] QString dimensionTitle(std::int32_t dimension) { return QStringLiteral("dim %1").arg(dimension); }

    [[nodiscard]] QString entryLabel(const DataKeyEntry& entry) {
        return QString::fromUtf8(entry.label.data(), static_cast<int>(entry.label.size()));
    }

}  // namespace

DataKeyModel::DataKeyModel(const DataKeyIndex* index, QObject* parent) : QAbstractItemModel(parent), index_(index) {
    resetIndex(index);
}

void DataKeyModel::resetIndex(const DataKeyIndex* index) {
    beginResetModel();
    index_ = index;
    root_ = std::make_unique<Node>();

    static constexpr std::array<DataKeyCategory, 7> categories = {DataKeyCategory::Chunks, DataKeyCategory::Villages, DataKeyCategory::Players,
                                                                   DataKeyCategory::MapItems, DataKeyCategory::Actors, DataKeyCategory::Digp,
                                                                   DataKeyCategory::Others};
    for (const auto category : categories) {
        auto child = std::make_unique<Node>();
        child->kind = Kind::Category;
        child->parent = root_.get();
        child->category = category;
        child->label = categoryTitle(category);
        child->value = index_ ? QString::number(static_cast<qulonglong>(index_->categoryCount(category))) : QStringLiteral("0");
        root_->children.push_back(std::move(child));
    }
    endResetModel();
}

DataKeyModel::Node* DataKeyModel::nodeForIndex(const QModelIndex& index) const {
    return index.isValid() ? static_cast<Node*>(index.internalPointer()) : root_.get();
}

QModelIndex DataKeyModel::indexForNode(const Node* node) const {
    if (!node || node == root_.get() || !node->parent) return {};
    const auto it = std::find_if(node->parent->children.begin(), node->parent->children.end(),
                                 [node](const std::unique_ptr<Node>& child) { return child.get() == node; });
    if (it == node->parent->children.end()) return {};
    return createIndex(static_cast<int>(std::distance(node->parent->children.begin(), it)), 0, const_cast<Node*>(node));
}

QModelIndex DataKeyModel::index(int row, int column, const QModelIndex& parent) const {
    if (row < 0 || column < 0 || column >= 2) return {};
    const Node* parent_node = nodeForIndex(parent);
    if (!parent_node || row >= static_cast<int>(parent_node->children.size())) return {};
    return createIndex(row, column, parent_node->children[static_cast<std::size_t>(row)].get());
}

QModelIndex DataKeyModel::parent(const QModelIndex& child) const {
    if (!child.isValid()) return {};
    return indexForNode(nodeForIndex(child)->parent);
}

int DataKeyModel::rowCount(const QModelIndex& parent) const {
    if (parent.isValid() && parent.column() != 0) return 0;
    const Node* node = nodeForIndex(parent);
    return node ? static_cast<int>(node->children.size()) : 0;
}

int DataKeyModel::columnCount(const QModelIndex&) const { return 2; }

QVariant DataKeyModel::headerData(int section, Qt::Orientation orientation, int role) const {
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole) return {};
    if (section == 0) return QStringLiteral("Key");
    if (section == 1) return QStringLiteral("Value Size / Count");
    return {};
}

QVariant DataKeyModel::data(const QModelIndex& model_index, int role) const {
    if (!model_index.isValid()) return {};
    const auto* node = nodeForIndex(model_index);
    if (role == Qt::DisplayRole) return model_index.column() == 0 ? node->label : node->value;
    if (role == Qt::TextAlignmentRole && model_index.column() == 1) return static_cast<int>(Qt::AlignRight | Qt::AlignVCenter);
    return {};
}

Qt::ItemFlags DataKeyModel::flags(const QModelIndex& model_index) const {
    if (!model_index.isValid()) return Qt::NoItemFlags;
    return QAbstractItemModel::flags(model_index);
}

bool DataKeyModel::hasChildren(const QModelIndex& parent) const {
    const Node* node = nodeForIndex(parent);
    if (!node) return false;
    if (node->kind == Kind::Entry || node->kind == Kind::LoadMore) return false;
    return !node->children.empty() || !node->children_loaded || node->has_more;
}

bool DataKeyModel::canFetchMore(const QModelIndex& parent) const {
    const Node* node = nodeForIndex(parent);
    return node && node->kind != Kind::Entry && node->kind != Kind::LoadMore && !node->children_loaded;
}

void DataKeyModel::fetchMore(const QModelIndex& parent) {
    Node* node = nodeForIndex(parent);
    if (!node || node->kind == Kind::Entry || node->kind == Kind::LoadMore) return;
    appendPage(node);
}

bool DataKeyModel::nodeHasMore(const Node* node) const {
    if (!index_ || !node) return false;
    const auto& entries = index_->entries();
    if (node->kind == Kind::Category && hasHierarchy(node->category)) {
        for (std::size_t i = node->scan_position; i < entries.size(); ++i) {
            if (!matches(node, entries[i])) continue;
            bool exists = false;
            for (const auto& child : node->children) {
                if (child->kind == Kind::Dimension && child->dimension == entries[i].dimension) {
                    exists = true;
                    break;
                }
            }
            if (!exists) return true;
        }
        return false;
    }
    if (node->kind == Kind::Dimension) {
        for (std::size_t i = node->scan_position; i < entries.size(); ++i) {
            if (!matches(node, entries[i])) continue;
            const QString group = QString::fromStdString(entries[i].group);
            bool exists = false;
            for (const auto& child : node->children) {
                if (child->kind == Kind::Group && child->group == group) {
                    exists = true;
                    break;
                }
            }
            if (!exists) return true;
        }
        return false;
    }
    for (std::size_t i = node->scan_position; i < entries.size(); ++i) {
        if (matches(node, entries[i])) return true;
    }
    return false;
}

bool DataKeyModel::matches(const Node* node, const DataKeyEntry& entry) const {
    if (node->kind == Kind::Category) return entry.category == node->category;
    if (node->kind == Kind::Dimension) return entry.category == node->category && entry.has_dimension && entry.dimension == node->dimension;
    if (node->kind == Kind::Group)
        return entry.category == node->category && entry.has_dimension && entry.dimension == node->dimension &&
               QString::fromStdString(entry.group) == node->group;
    return false;
}

QString DataKeyModel::entryLabel(const DataKeyEntry& entry) const { return ::entryLabel(entry); }

QString DataKeyModel::entryValueSize(std::size_t entry_index) const {
    // Values are intentionally read only when a page is materialized. The key
    // list remains lazy, while the displayed size stays useful for each page.
    return QStringLiteral("-");
}

void DataKeyModel::appendChild(Node* parent_node, std::unique_ptr<Node> child) {
    child->parent = parent_node;
    parent_node->children.push_back(std::move(child));
}

void DataKeyModel::removeLoadMore(Node* node) {
    if (node->children.empty() || node->children.back()->kind != Kind::LoadMore) return;
    const int row = static_cast<int>(node->children.size() - 1);
    beginRemoveRows(indexForNode(node), row, row);
    node->children.pop_back();
    endRemoveRows();
}

void DataKeyModel::appendLoadMore(Node* node) {
    auto more = std::make_unique<Node>();
    more->kind = Kind::LoadMore;
    more->parent = node;
    more->label = QStringLiteral("load more...");
    more->value = QStringLiteral("500");
    appendChild(node, std::move(more));
}

void DataKeyModel::appendPage(Node* node) {
    if (!index_) return;
    removeLoadMore(node);
    const QModelIndex parent_index = indexForNode(node);
    const auto& entries = index_->entries();
    const std::size_t first_row = node->children.size();
    std::vector<std::unique_ptr<Node>> pending;
    pending.reserve(PAGE_SIZE);

    if (node->kind == Kind::Category && hasHierarchy(node->category)) {
        const auto& dimensions = index_->categoryDimensions(node->category);
        std::size_t loaded_dimensions = 0;
        for (const auto& child : node->children) {
            if (child->kind == Kind::Dimension) ++loaded_dimensions;
        }
        for (std::size_t i = loaded_dimensions; i < dimensions.size() && pending.size() < PAGE_SIZE; ++i) {
            const auto dimension = dimensions[i];
            auto child = std::make_unique<Node>();
            child->kind = Kind::Dimension;
            child->category = node->category;
            child->dimension = dimension;
            child->label = dimensionTitle(child->dimension);
            pending.push_back(std::move(child));
        }
        node->scan_position = entries.size();
    } else {
        for (std::size_t i = node->scan_position; i < entries.size() && pending.size() < PAGE_SIZE; ++i) {
            if (!matches(node, entries[i])) continue;
            node->scan_position = i + 1;
            if (node->kind == Kind::Dimension) {
                const QString group = QString::fromStdString(entries[i].group);
                bool duplicate = false;
                for (const auto& child : node->children) {
                    if (child->kind == Kind::Group && child->group == group) {
                        duplicate = true;
                        break;
                    }
                }
                for (const auto& child : pending) {
                    if (child->kind == Kind::Group && child->group == group) {
                        duplicate = true;
                        break;
                    }
                }
                if (duplicate) continue;
                auto child = std::make_unique<Node>();
                child->kind = Kind::Group;
                child->category = node->category;
                child->dimension = node->dimension;
                child->group = group;
                child->label = (node->category == DataKeyCategory::Chunks ? QStringLiteral("chunk ") : QStringLiteral("village ")) + group;
                pending.push_back(std::move(child));
            } else {
                auto child = std::make_unique<Node>();
                child->kind = Kind::Entry;
                child->entry_index = i;
                child->label = entryLabel(entries[i]);
                child->value = entryValueSize(i);
                pending.push_back(std::move(child));
            }
        }
    }
    if (!pending.empty()) {
        beginInsertRows(parent_index, static_cast<int>(first_row), static_cast<int>(first_row + pending.size() - 1));
        for (auto& child : pending) {
            child->parent = node;
            node->children.push_back(std::move(child));
        }
        endInsertRows();
    }

    node->children_loaded = true;
    node->has_more = nodeHasMore(node);
    if (node->has_more) {
        const int row = static_cast<int>(node->children.size());
        beginInsertRows(parent_index, row, row);
        appendLoadMore(node);
        endInsertRows();
    }
}

std::optional<std::size_t> DataKeyModel::activate(const QModelIndex& model_index) {
    if (!model_index.isValid()) return std::nullopt;
    Node* node = nodeForIndex(model_index);
    if (!node) return std::nullopt;
    if (node->kind == Kind::LoadMore) {
        Node* parent_node = node->parent;
        fetchMore(indexForNode(parent_node));
        return std::nullopt;
    }
    if (node->kind == Kind::Entry) return node->entry_index;
    return std::nullopt;
}

std::optional<std::size_t> DataKeyModel::entryIndex(const QModelIndex& model_index) const {
    if (!model_index.isValid()) return std::nullopt;
    const Node* node = nodeForIndex(model_index);
    if (!node || node->kind != Kind::Entry) return std::nullopt;
    return node->entry_index;
}
