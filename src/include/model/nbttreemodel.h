#ifndef BEDROCKMAP_NBTTREEMODEL_H
#define BEDROCKMAP_NBTTREEMODEL_H

#include <QAbstractListModel>
#include <QSet>
#include <QString>
#include <QVariantMap>
#include <QVector>
#include <memory>
#include <vector>

#include "nbt.h"

/// Flat presentation model for one NBT compound tree. Rows are expanded nodes
/// in display order, which keeps the QML view simple and mobile friendly while
/// the C++ tree still provides parent/list semantics for edits.
class NbtTreeModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(bool hexMode READ hexMode WRITE setHexMode NOTIFY hexModeChanged)
    Q_PROPERTY(bool editable READ editable WRITE setEditable NOTIFY editableChanged)
    Q_PROPERTY(QString searchText READ searchText WRITE setSearchText NOTIFY searchTextChanged)
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)

   public:
    enum Role {
        KeyRole = Qt::UserRole + 1,
        TypeRole,
        TypeNameRole,
        ValueRole,
        DepthRole,
        HasChildrenRole,
        ExpandedRole,
        CanAddRole,
        CanEditRole,
        CanRemoveRole,
        CanClearRole,
    };
    Q_ENUM(Role)

    explicit NbtTreeModel(QObject* parent = nullptr);
    ~NbtTreeModel() override;

    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] int count() const { return rowCount(); }
    [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

    void setRoot(bl::nbt::compound_tag* root);
    /// Insert or replace a top-level tag and notify the owning document of the edit.
    bool setRootChild(std::unique_ptr<bl::nbt::abstract_tag> tag);
    [[nodiscard]] bl::nbt::compound_tag* root() const { return root_; }
    [[nodiscard]] bool hexMode() const { return hex_mode_; }
    [[nodiscard]] bool editable() const { return editable_; }
    [[nodiscard]] QString searchText() const { return search_text_; }
    [[nodiscard]] QString lastError() const { return last_error_; }
    void setHexMode(bool enabled);
    void setEditable(bool editable);
    void setSearchText(const QString& text);

    Q_INVOKABLE bool toggleExpanded(int row);
    Q_INVOKABLE void expandAll();
    Q_INVOKABLE void collapseAll();
    Q_INVOKABLE bool updateNode(int row, const QString& key, const QString& value);
    Q_INVOKABLE bool addChild(int row, int type, const QString& key, const QString& value);
    Q_INVOKABLE bool removeNode(int row);
    Q_INVOKABLE bool clearChildren(int row);
    Q_INVOKABLE int childTypeForRow(int row) const;
    [[nodiscard]] Q_INVOKABLE QVariantMap tagInfo(int row) const;

   signals:
    void hexModeChanged();
    void editableChanged();
    void searchTextChanged();
    void lastErrorChanged();
    void documentChanged();
    void countChanged();

   private:
    struct Node {
        bl::nbt::abstract_tag* tag{nullptr};
        Node* parent{nullptr};
        int row_in_parent{0};
        bool expanded{false};
        std::vector<std::unique_ptr<Node>> children;
    };

    void rebuild();
    void rebuildVisible();
    void appendNode(Node* node);
    [[nodiscard]] bool markMatches(Node* node, const QString& needle);
    void appendFiltered(Node* node);
    [[nodiscard]] Node* nodeAt(int row) const;
    void setError(const QString& message);
    [[nodiscard]] int depthOf(const Node* node) const;
    [[nodiscard]] QString valueText(const bl::nbt::abstract_tag* tag) const;
    [[nodiscard]] static QString hexValueText(const bl::nbt::abstract_tag* tag);
    [[nodiscard]] bool applyValue(bl::nbt::abstract_tag* tag, const QString& text);
    [[nodiscard]] bl::nbt::abstract_tag* createTag(int type, const QString& key, const QString& value);

    bl::nbt::compound_tag* root_{nullptr};
    std::unique_ptr<Node> root_node_;
    QVector<Node*> visible_;
    QSet<Node*> filtered_nodes_;
    bool hex_mode_{false};
    bool editable_{true};
    QString last_error_;
    bool rebuilding_{false};
    QString search_text_;
};

#endif  // BEDROCKMAP_NBTTREEMODEL_H
