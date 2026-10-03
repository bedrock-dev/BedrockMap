#ifndef BEDROCKMAP_DATAKEYMODEL_H
#define BEDROCKMAP_DATAKEYMODEL_H

#include <QAbstractItemModel>
#include <QModelIndex>

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

#include "datakeyindex.h"

/// Lazy tree model for the archive key browser.
///
/// Only the expanded branch and one page of children are represented by model
/// nodes. The model deliberately does not create one Qt item per database key.
class DataKeyModel final : public QAbstractItemModel {
   public:
    explicit DataKeyModel(const DataKeyIndex* index, QObject* parent = nullptr);

    void resetIndex(const DataKeyIndex* index);
    [[nodiscard]] std::optional<std::size_t> activate(const QModelIndex& index);
    [[nodiscard]] std::optional<std::size_t> entryIndex(const QModelIndex& index) const;

    [[nodiscard]] QModelIndex index(int row, int column, const QModelIndex& parent = {}) const override;
    [[nodiscard]] QModelIndex parent(const QModelIndex& child) const override;
    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] int columnCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    [[nodiscard]] Qt::ItemFlags flags(const QModelIndex& index) const override;
    [[nodiscard]] bool hasChildren(const QModelIndex& parent = {}) const override;
    [[nodiscard]] bool canFetchMore(const QModelIndex& parent) const override;
    void fetchMore(const QModelIndex& parent) override;

   private:
    enum class Kind : std::uint8_t { Root, Category, Dimension, Group, Entry, LoadMore };

    struct Node {
        Kind kind{Kind::Root};
        Node* parent{nullptr};
        std::vector<std::unique_ptr<Node>> children;
        DataKeyCategory category{DataKeyCategory::Unknown};
        std::int32_t dimension{0};
        QString group;
        QString label;
        QString value;
        std::size_t entry_index{0};
        std::size_t scan_position{0};
        bool children_loaded{false};
        bool has_more{false};
        bool chunk_missing_main_key{false};
        bool value_empty{false};
    };

    static constexpr std::size_t PAGE_SIZE = 500;

    [[nodiscard]] Node* nodeForIndex(const QModelIndex& index) const;
    [[nodiscard]] QModelIndex indexForNode(const Node* node) const;
    [[nodiscard]] bool nodeHasMore(const Node* node) const;
    void appendPage(Node* node);
    void appendLoadMore(Node* node);
    void removeLoadMore(Node* node);
    void appendChild(Node* parent, std::unique_ptr<Node> child);
    [[nodiscard]] QString entryLabel(const DataKeyEntry& entry) const;
    [[nodiscard]] QString entryValueSize(std::size_t entry_index) const;
    [[nodiscard]] bool matches(const Node* node, const DataKeyEntry& entry) const;

    const DataKeyIndex* index_{nullptr};
    std::unique_ptr<Node> root_;
};

#endif  // BEDROCKMAP_DATAKEYMODEL_H
