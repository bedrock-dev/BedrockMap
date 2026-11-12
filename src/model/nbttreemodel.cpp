#include "model/nbttreemodel.h"

#include <QRegularExpression>
#include <algorithm>
#include <cstdint>
#include <limits>
#include <span>
#include <string>

#include "base/binaryclipboard.h"

using namespace bl::nbt;

namespace {
    template <typename T>
    using number_limits = std::numeric_limits<T>;

    bool parseIntegerValues(tag_type type, const QString& text, std::vector<int64_t>& values) {
        const auto range = [type]() -> std::pair<int64_t, int64_t> {
            switch (type) {
                case Byte:
                case ByteArray:
                    return {number_limits<int8_t>::min(), number_limits<int8_t>::max()};
                case Short:
                    return {number_limits<int16_t>::min(), number_limits<int16_t>::max()};
                case Int:
                case IntArray:
                    return {number_limits<int32_t>::min(), number_limits<int32_t>::max()};
                case Long:
                case LongArray:
                    return {number_limits<int64_t>::min(), number_limits<int64_t>::max()};
                default:
                    return {0, -1};
            }
        }();
        if (range.second < range.first) return false;

        QStringList parts;
        if (type == ByteArray || type == IntArray || type == LongArray) {
            parts = text.split(QRegularExpression(QStringLiteral("[,\\s]+")), Qt::SkipEmptyParts);
        } else {
            parts = {text.trimmed()};
        }
        values.clear();
        values.reserve(parts.size());
        for (const auto& part : parts) {
            bool ok = false;
            const auto value = part.toLongLong(&ok);
            if (!ok || value < range.first || value > range.second) return false;
            values.push_back(value);
        }
        return !values.empty() || type == ByteArray || type == IntArray || type == LongArray;
    }

    QString arrayPreview(const QString& text, int limit = 180) {
        if (text.size() <= limit) return text;
        return text.left(limit) + QStringLiteral(" ...");
    }
}  // namespace

NbtTreeModel::NbtTreeModel(QObject* parent) : QAbstractListModel(parent) {}
NbtTreeModel::~NbtTreeModel() = default;

void NbtTreeModel::setRoot(compound_tag* root) {
    root_ = root;
    beginResetModel();
    search_collapsed_.clear();
    const auto previous = rebuild();
    updateMatches();
    visible_ = collectVisible();
    endResetModel();
    emit countChanged();
}

bool NbtTreeModel::setRootChild(std::unique_ptr<abstract_tag> tag) {
    if (!root_ || !editable_ || !tag || tag->key().empty()) return false;
    root_->put(tag.release());
    commitStructureChange();
    return true;
}

int NbtTreeModel::rowCount(const QModelIndex& parent) const {
    if (parent.isValid()) return 0;
    return static_cast<int>(visible_.size());
}

QHash<int, QByteArray> NbtTreeModel::roleNames() const {
    return {{KeyRole, "key"},       {TypeRole, "tagType"},    {TypeNameRole, "typeName"},       {ValueRole, "value"},
            {DepthRole, "depth"},   {HasChildrenRole, "hasChildren"}, {ExpandedRole, "expanded"}, {CanAddRole, "canAdd"},
            {CanEditRole, "canEdit"}, {CanRemoveRole, "canRemove"}, {CanClearRole, "canClear"}};
}

QVariant NbtTreeModel::data(const QModelIndex& index, int role) const {
    const Node* node = nodeAt(index.row());
    if (!node || !node->tag) return {};
    const auto* tag = node->tag;
    const bool container = tag->type() == Compound || tag->type() == List;
    switch (role) {
        case KeyRole: {
            QString key = QString::fromStdString(tag->key());
            if (tag->type() != Compound && tag->type() != List && key.isEmpty()) key = QStringLiteral("[%1]").arg(node->row_in_parent);
            if (node->parent == nullptr && key.isEmpty()) key = QStringLiteral("<root>");
            return key;
        }
        case TypeRole:
            return static_cast<int>(tag->type());
        case TypeNameRole:
            return QString::fromStdString(tag_type_to_str(tag->type()));
        case ValueRole:
            return valueText(tag);
        case DepthRole:
            return depthOf(node);
        case HasChildrenRole:
            return hasVisibleChildren(node);
        case ExpandedRole:
            return isExpanded(node);
        case CanAddRole:
            return editable_ && !hex_mode_ && container;
        case CanEditRole:
            return editable_ && !hex_mode_ && !container;
        case CanRemoveRole:
            return editable_ && !hex_mode_ && node->parent != nullptr;
        case CanClearRole:
            return editable_ && !hex_mode_ && container && !node->children.empty();
        default:
            return {};
    }
}

QString NbtTreeModel::hexValueText(const abstract_tag* tag) {
    if (!tag) return {};
    switch (tag->type()) {
        case Byte:
            return QStringLiteral("0x%1")
                .arg(static_cast<uint8_t>(static_cast<const byte_tag*>(tag)->value), 2, 16, QLatin1Char('0'))
                .toUpper();
        case Short:
            return QStringLiteral("0x%1")
                .arg(static_cast<uint16_t>(static_cast<const short_tag*>(tag)->value), 4, 16, QLatin1Char('0'))
                .toUpper();
        case Int:
            return QStringLiteral("0x%1")
                .arg(static_cast<uint32_t>(static_cast<const int_tag*>(tag)->value), 8, 16, QLatin1Char('0'))
                .toUpper();
        case Long:
            return QStringLiteral("0x%1")
                .arg(static_cast<uint64_t>(static_cast<const long_tag*>(tag)->value), 16, 16, QLatin1Char('0'))
                .toUpper();
        case ByteArray: {
            QStringList values;
            const auto& data = static_cast<const byte_array_tag*>(tag)->value;
            for (size_t i = 0; i < std::min<size_t>(data.size(), 16); ++i)
                values << QStringLiteral("%1").arg(static_cast<uint8_t>(data[i]), 2, 16, QLatin1Char('0')).toUpper();
            if (data.size() > 16) values << QStringLiteral("...");
            return values.join(QLatin1Char(' '));
        }
        case IntArray: {
            QStringList values;
            const auto& data = static_cast<const int_array_tag*>(tag)->value;
            for (size_t i = 0; i < std::min<size_t>(data.size(), 16); ++i)
                values << QStringLiteral("%1").arg(static_cast<uint32_t>(data[i]), 8, 16, QLatin1Char('0')).toUpper();
            if (data.size() > 16) values << QStringLiteral("...");
            return values.join(QLatin1Char(' '));
        }
        case LongArray: {
            QStringList values;
            const auto& data = static_cast<const long_array_tag*>(tag)->value;
            for (size_t i = 0; i < std::min<size_t>(data.size(), 16); ++i)
                values << QStringLiteral("%1").arg(static_cast<uint64_t>(data[i]), 16, 16, QLatin1Char('0')).toUpper();
            if (data.size() > 16) values << QStringLiteral("...");
            return values.join(QLatin1Char(' '));
        }
        default:
            return QString::fromStdString(tag->value_string());
    }
}

QString NbtTreeModel::valueText(const abstract_tag* tag) const {
    if (!tag) return {};
    if (hex_mode_) return arrayPreview(hexValueText(tag));
    switch (tag->type()) {
        case Compound:
            return QStringLiteral("{%1}").arg(static_cast<const compound_tag*>(tag)->value.size());
        case List:
            return QStringLiteral("[%1]").arg(static_cast<const list_tag*>(tag)->value.size());
        case ByteArray:
        case IntArray:
        case LongArray:
            return arrayPreview(QString::fromStdString(tag->restricted_value_string()));
        default:
            return QString::fromStdString(tag->value_string());
    }
}

void NbtTreeModel::setHexMode(bool enabled) {
    if (hex_mode_ == enabled) return;
    hex_mode_ = enabled;
    emit hexModeChanged();
    if (!visible_.isEmpty()) emit dataChanged(index(0), index(static_cast<int>(visible_.size()) - 1));
}

void NbtTreeModel::setEditable(bool editable) {
    if (editable_ == editable) return;
    editable_ = editable;
    emit editableChanged();
    if (!visible_.isEmpty()) emit dataChanged(index(0), index(static_cast<int>(visible_.size()) - 1));
}

void NbtTreeModel::setSearchText(const QString& text) {
    const QString needle = text.trimmed().toLower();
    if (search_text_ == needle) return;
    search_text_ = needle;
    search_collapsed_.clear();
    updateMatches();
    applyVisible(collectVisible(), true);
    emit searchTextChanged();
}

std::unique_ptr<NbtTreeModel::Node> NbtTreeModel::rebuild(const abstract_tag* renamed_from, const abstract_tag* renamed_to) {
    // Old tags may already be deleted. Their addresses are only compared,
    // never dereferenced.
    QSet<const abstract_tag*> expanded;
    const auto collect = [&expanded](auto&& self, const Node* node) -> void {
        if (node->expanded) expanded.insert(node->tag);
        for (const auto& child : node->children) self(self, child.get());
    };
    if (root_node_) collect(collect, root_node_.get());
    if (renamed_from && expanded.contains(renamed_from)) expanded.insert(renamed_to);
    QSet<const abstract_tag*> collapsed;
    if (renamed_from && search_collapsed_.contains(renamed_from)) collapsed.insert(renamed_to);

    auto previous = std::move(root_node_);
    root_node_ = std::make_unique<Node>();
    root_node_->tag = root_;
    root_node_->expanded = true;
    if (root_) {
        const auto build = [&](auto&& self, Node* node) -> void {
            if (expanded.contains(node->tag)) node->expanded = true;
            if (search_collapsed_.contains(node->tag)) collapsed.insert(node->tag);
            const auto add_child = [&](abstract_tag* child, int row_in_parent) {
                auto next = std::make_unique<Node>();
                next->tag = child;
                next->parent = node;
                next->row_in_parent = row_in_parent;
                node->children.emplace_back(std::move(next));
                self(self, node->children.back().get());
            };
            if (node->tag->type() == Compound) {
                auto* compound = static_cast<compound_tag*>(node->tag);
                node->children.reserve(compound->value.size());
                for (const auto& [key, child] : compound->value) {
                    Q_UNUSED(key);
                    if (child) add_child(child, static_cast<int>(node->children.size()));
                }
            } else if (node->tag->type() == List) {
                auto* list = static_cast<list_tag*>(node->tag);
                node->children.reserve(list->value.size());
                for (size_t i = 0; i < list->value.size(); ++i)
                    if (list->value[i]) add_child(list->value[i], static_cast<int>(i));
            }
        };
        build(build, root_node_.get());
    }
    search_collapsed_ = std::move(collapsed);
    return previous;
}

void NbtTreeModel::updateMatches() {
    filtered_nodes_.clear();
    search_parents_.clear();
    if (!search_text_.isEmpty() && root_node_) Q_UNUSED(markMatches(root_node_.get()));
}

bool NbtTreeModel::markMatches(Node* node) {
    if (!node || !node->tag) return false;
    bool child_matched = false;
    for (auto& child : node->children) {
        if (markMatches(child.get())) child_matched = true;
    }
    if (child_matched) search_parents_.insert(node);
    if (!child_matched) {
        const QString key = QString::fromStdString(node->tag->key()).toLower();
        if (!key.contains(search_text_) && !valueText(node->tag).toLower().contains(search_text_)) return false;
    }
    filtered_nodes_.insert(node);
    return true;
}

bool NbtTreeModel::isExpanded(const Node* node) const {
    if (search_text_.isEmpty()) return node->expanded;
    return search_parents_.contains(node) && !search_collapsed_.contains(node->tag);
}

bool NbtTreeModel::hasVisibleChildren(const Node* node) const {
    return search_text_.isEmpty() ? !node->children.empty() : search_parents_.contains(node);
}

QVector<NbtTreeModel::Node*> NbtTreeModel::collectVisible() const {
    QVector<Node*> rows;
    if (root_node_ && root_node_->tag) appendVisible(root_node_.get(), rows);
    return rows;
}

void NbtTreeModel::appendVisible(Node* node, QVector<Node*>& rows) const {
    if (!search_text_.isEmpty() && !filtered_nodes_.contains(node)) return;
    rows.append(node);
    if (!isExpanded(node)) return;
    for (auto& child : node->children) appendVisible(child.get(), rows);
}

void NbtTreeModel::applyVisible(QVector<Node*> next, bool refresh_all) {
    // Rows are matched by tag because a rebuild replaces every Node.
    const qsizetype old_size = visible_.size();
    const qsizetype new_size = next.size();
    qsizetype prefix = 0;
    while (prefix < old_size && prefix < new_size && visible_[prefix]->tag == next[prefix]->tag) ++prefix;
    qsizetype suffix = 0;
    while (suffix < old_size - prefix && suffix < new_size - prefix &&
           visible_[old_size - 1 - suffix]->tag == next[new_size - 1 - suffix]->tag)
        ++suffix;
    const int first = static_cast<int>(prefix);
    const int removed = static_cast<int>(old_size - prefix - suffix);
    const int inserted = static_cast<int>(new_size - prefix - suffix);

    if (removed > 0) {
        beginRemoveRows({}, first, first + removed - 1);
        visible_.remove(prefix, removed);
        endRemoveRows();
    }
    if (inserted > 0) {
        beginInsertRows({}, first, first + inserted - 1);
        visible_ = std::move(next);
        endInsertRows();
    } else {
        visible_ = std::move(next);
    }
    if (refresh_all && !visible_.isEmpty()) emit dataChanged(index(0), index(static_cast<int>(visible_.size()) - 1));
    if (old_size != new_size) emit countChanged();
}

void NbtTreeModel::commitStructureChange(const abstract_tag* renamed_from, const abstract_tag* renamed_to) {
    // visible_ still points into the previous tree until applyVisible() returns.
    const auto previous = rebuild(renamed_from, renamed_to);
    updateMatches();
    applyVisible(collectVisible(), true);
    emit documentChanged();
}

NbtTreeModel::Node* NbtTreeModel::nodeAt(int row) const {
    if (row < 0 || row >= visible_.size()) return nullptr;
    return visible_[row];
}

void NbtTreeModel::setError(const QString& message) {
    if (last_error_ == message) return;
    last_error_ = message;
    emit lastErrorChanged();
}

int NbtTreeModel::depthOf(const Node* node) const {
    int depth = 0;
    while (node && node->parent) {
        ++depth;
        node = node->parent;
    }
    return depth;
}

bool NbtTreeModel::toggleExpanded(int row) {
    Node* node = nodeAt(row);
    if (!node || !hasVisibleChildren(node)) return false;
    if (search_text_.isEmpty())
        node->expanded = !node->expanded;
    else if (!search_collapsed_.remove(node->tag))
        search_collapsed_.insert(node->tag);
    applyVisible(collectVisible(), false);
    emit dataChanged(index(row), index(row), {ExpandedRole});
    return true;
}

void NbtTreeModel::expandAll() {
    if (!root_node_) return;
    const auto expand = [](auto&& self, Node* node) -> void {
        node->expanded = true;
        for (auto& child : node->children) self(self, child.get());
    };
    expand(expand, root_node_.get());
    search_collapsed_.clear();
    applyVisible(collectVisible(), true);
}

void NbtTreeModel::collapseAll() {
    if (!root_node_) return;
    root_node_->expanded = true;
    const auto collapse = [](auto&& self, Node* node) -> void {
        for (auto& child : node->children) {
            child->expanded = false;
            self(self, child.get());
        }
    };
    collapse(collapse, root_node_.get());
    for (const Node* node : std::as_const(search_parents_))
        if (node != root_node_.get()) search_collapsed_.insert(node->tag);
    applyVisible(collectVisible(), true);
}

bool NbtTreeModel::applyValue(abstract_tag* tag, const QString& text) {
    if (!tag) return false;
    const QString trimmed = text.trimmed();
    switch (tag->type()) {
        case String:
            static_cast<string_tag*>(tag)->value = text.toStdString();
            return true;
        case Compound:
        case List:
            return true;
        case Byte:
        case Short:
        case Int:
        case Long:
        case ByteArray:
        case IntArray:
        case LongArray: {
            std::vector<int64_t> values;
            if (!parseIntegerValues(tag->type(), trimmed, values)) return false;
            switch (tag->type()) {
                case Byte:
                    static_cast<byte_tag*>(tag)->value = static_cast<int8_t>(values.front());
                    break;
                case Short:
                    static_cast<short_tag*>(tag)->value = static_cast<int16_t>(values.front());
                    break;
                case Int:
                    static_cast<int_tag*>(tag)->value = static_cast<int32_t>(values.front());
                    break;
                case Long:
                    static_cast<long_tag*>(tag)->value = static_cast<int64_t>(values.front());
                    break;
                case ByteArray: {
                    auto* array = static_cast<byte_array_tag*>(tag);
                    array->value.clear();
                    for (const auto value : values) array->value.push_back(static_cast<int8_t>(value));
                    break;
                }
                case IntArray: {
                    auto* array = static_cast<int_array_tag*>(tag);
                    array->value.clear();
                    for (const auto value : values) array->value.push_back(static_cast<int32_t>(value));
                    break;
                }
                case LongArray: {
                    auto* array = static_cast<long_array_tag*>(tag);
                    array->value.clear();
                    for (const auto value : values) array->value.push_back(value);
                    break;
                }
                default:
                    break;
            }
            return true;
        }
        case Float: {
            bool ok = false;
            const float value = trimmed.toFloat(&ok);
            if (ok) static_cast<float_tag*>(tag)->value = value;
            return ok;
        }
        case Double: {
            bool ok = false;
            const double value = trimmed.toDouble(&ok);
            if (ok) static_cast<double_tag*>(tag)->value = value;
            return ok;
        }
        case End:
            return false;
    }
    return false;
}

abstract_tag* NbtTreeModel::createTag(int type, const QString& key, const QString& value) {
    const auto tag_name = key.toStdString();
    switch (static_cast<tag_type>(type)) {
        case Compound:
            return new compound_tag(tag_name);
        case List:
            return new list_tag(tag_name);
        case String:
            return new string_tag(tag_name, value.toStdString());
        case Byte:
        case Short:
        case Int:
        case Long:
        case ByteArray:
        case IntArray:
        case LongArray: {
            std::vector<int64_t> values;
            if (!parseIntegerValues(static_cast<tag_type>(type), value.trimmed(), values)) return nullptr;
            switch (static_cast<tag_type>(type)) {
                case Byte:
                    return new byte_tag(tag_name, static_cast<int8_t>(values.front()));
                case Short:
                    return new short_tag(tag_name, static_cast<int16_t>(values.front()));
                case Int:
                    return new int_tag(tag_name, static_cast<int32_t>(values.front()));
                case Long:
                    return new long_tag(tag_name, values.front());
                case ByteArray: {
                    auto* array = new byte_array_tag(tag_name);
                    for (const auto v : values) array->value.push_back(static_cast<int8_t>(v));
                    return array;
                }
                case IntArray: {
                    auto* array = new int_array_tag(tag_name);
                    for (const auto v : values) array->value.push_back(static_cast<int32_t>(v));
                    return array;
                }
                case LongArray: {
                    auto* array = new long_array_tag(tag_name);
                    for (const auto v : values) array->value.push_back(v);
                    return array;
                }
                default:
                    return nullptr;
            }
        }
        case Float: {
            bool ok = false;
            const float v = value.trimmed().toFloat(&ok);
            return ok ? new float_tag(tag_name, v) : nullptr;
        }
        case Double: {
            bool ok = false;
            const double v = value.trimmed().toDouble(&ok);
            return ok ? new double_tag(tag_name, v) : nullptr;
        }
        case End:
            return nullptr;
    }
    return nullptr;
}

bool NbtTreeModel::updateNode(int row, const QString& key, const QString& value) {
    Node* node = nodeAt(row);
    if (!node || !node->tag) return false;
    if (!editable_ || hex_mode_) {
        setError(tr("The current view is read-only."));
        return false;
    }
    auto* tag = node->tag;
    const QString new_key = key;
    const bool key_changed = new_key.toStdString() != tag->key();
    if (node->parent && node->parent->tag && node->parent->tag->type() == Compound && key_changed) {
        auto* parent = static_cast<compound_tag*>(node->parent->tag);
        if (parent->get(new_key.toStdString())) {
            setError(tr("A tag with this key already exists."));
            return false;
        }
        auto* clone = tag->copy();
        clone->set_key(new_key.toStdString());
        if (!applyValue(clone, value)) {
            delete clone;
            setError(tr("Invalid value for this tag type."));
            return false;
        }
        parent->remove(tag->key());
        parent->put(clone);
        commitStructureChange(tag, clone);
        return true;
    }
    if (!applyValue(tag, value)) {
        setError(tr("Invalid value for this tag type."));
        return false;
    }
    if (key_changed) tag->set_key(new_key.toStdString());
    emit dataChanged(index(row), index(row));
    emit documentChanged();
    return true;
}

bool NbtTreeModel::addChild(int row, int type, const QString& key, const QString& value) {
    Node* node = nodeAt(row);
    if (!node || !node->tag || !editable_ || hex_mode_) return false;
    if (node->tag->type() != Compound && node->tag->type() != List) {
        setError(tr("This tag cannot contain children."));
        return false;
    }
    if (node->tag->type() == List) {
        auto* list = static_cast<list_tag*>(node->tag);
        if (!list->value.empty() && list->value.front()->type() != static_cast<tag_type>(type)) {
            setError(tr("All items in this list must have the same type."));
            return false;
        }
    } else {
        auto* compound = static_cast<compound_tag*>(node->tag);
        if (compound->get(key.toStdString())) {
            setError(tr("A tag with this key already exists."));
            return false;
        }
    }
    auto* child = createTag(type, key, value);
    if (!child) {
        setError(tr("Invalid value for this tag type."));
        return false;
    }
    if (node->tag->type() == Compound)
        static_cast<compound_tag*>(node->tag)->put(child);
    else
        static_cast<list_tag*>(node->tag)->push_back(child);
    node->expanded = true;
    search_collapsed_.remove(node->tag);
    commitStructureChange();
    return true;
}

bool NbtTreeModel::removeNode(int row) {
    Node* node = nodeAt(row);
    if (!node || !node->parent || !editable_ || hex_mode_) return false;
    const auto parent_type = node->parent->tag->type();
    if (parent_type == Compound) {
        static_cast<compound_tag*>(node->parent->tag)->remove(node->tag->key());
    } else if (parent_type == List) {
        static_cast<list_tag*>(node->parent->tag)->remove(static_cast<size_t>(node->row_in_parent));
    } else {
        return false;
    }
    commitStructureChange();
    return true;
}

bool NbtTreeModel::clearChildren(int row) {
    Node* node = nodeAt(row);
    if (!node || !node->tag || !editable_ || hex_mode_) return false;
    if (node->tag->type() == Compound) {
        static_cast<compound_tag*>(node->tag)->value.clear();
    } else if (node->tag->type() == List) {
        auto* list = static_cast<list_tag*>(node->tag);
        for (auto* child : list->value) delete child;
        list->value.clear();
    } else {
        return false;
    }
    commitStructureChange();
    return true;
}

bool NbtTreeModel::copyNode(int row) {
    const Node* node = nodeAt(row);
    if (!node || !node->tag) return false;
    if (!clipboard_) {
        setError(tr("The clipboard is not available."));
        return false;
    }
    // NBT payloads always start with a compound, so the copied tag is wrapped
    // in an unnamed compound to keep its own type and key.
    compound_tag wrapper("");
    wrapper.put(node->tag->copy());
    const std::string raw = wrapper.serialize();
    if (!clipboard_->write(BinaryClipboard::PayloadKind::NbtTag, QByteArray(raw.data(), static_cast<qsizetype>(raw.size())))) {
        setError(tr("Failed to write the clipboard."));
        return false;
    }
    return true;
}

bool NbtTreeModel::pasteInto(int row) {
    Node* node = nodeAt(row);
    if (!node || !node->tag) return false;
    if (!editable_ || hex_mode_) {
        setError(tr("The current view is read-only."));
        return false;
    }
    const auto target_type = node->tag->type();
    if (target_type != Compound && target_type != List) {
        setError(tr("Tags can only be pasted into a compound or list."));
        return false;
    }
    if (!clipboard_) {
        setError(tr("The clipboard is not available."));
        return false;
    }
    const QByteArray raw = clipboard_->read(BinaryClipboard::PayloadKind::NbtTag);
    if (raw.isEmpty()) {
        setError(tr("The clipboard does not contain a copied NBT tag."));
        return false;
    }
    const auto parsed = parse_one(std::span(reinterpret_cast<const byte_t*>(raw.constData()), static_cast<size_t>(raw.size())));
    const std::unique_ptr<compound_tag> wrapper(parsed.root);
    if (!parsed || !wrapper || wrapper->value.size() != 1 || parsed.consumed_length != static_cast<size_t>(raw.size())) {
        setError(tr("The clipboard does not contain a valid NBT tag."));
        return false;
    }
    std::unique_ptr<abstract_tag> tag(wrapper->value.begin()->second->copy());

    if (target_type == List) {
        auto* list = static_cast<list_tag*>(node->tag);
        if (!list->value.empty() && list->value.front()->type() != tag->type()) {
            setError(tr("All items in this list must have the same type."));
            return false;
        }
        tag->set_key("");
        list->push_back(tag.release());
    } else {
        auto* compound = static_cast<compound_tag*>(node->tag);
        if (tag->key().empty()) tag->set_key(tag_type_to_str(tag->type()));
        if (compound->get(tag->key())) {
            setError(tr("A tag with this key already exists."));
            return false;
        }
        compound->put(tag.release());
    }
    node->expanded = true;
    search_collapsed_.remove(node->tag);
    commitStructureChange();
    return true;
}

int NbtTreeModel::childTypeForRow(int row) const {
    const Node* node = nodeAt(row);
    if (!node || !node->tag) return -1;
    if (node->tag->type() == List) {
        const auto* list = static_cast<const list_tag*>(node->tag);
        return list->value.empty() ? -1 : static_cast<int>(list->value.front()->type());
    }
    return -1;
}

QVariantMap NbtTreeModel::tagInfo(int row) const {
    const Node* node = nodeAt(row);
    if (!node || !node->tag) return {};
    const auto* tag = node->tag;
    const bool container = tag->type() == Compound || tag->type() == List;
    const bool has_children = container && !node->children.empty();
    const bool is_root = node->parent == nullptr;
    const bool can_add = editable_ && !hex_mode_ && container;
    // Root containers have no editable value, but their key is still the
    // document name and must remain renameable.
    const bool can_edit = editable_ && !hex_mode_ && (!container || is_root);
    const bool can_remove = editable_ && !hex_mode_ && node->parent != nullptr;
    const bool can_clear = editable_ && !hex_mode_ && has_children;
    return {{QStringLiteral("key"), QString::fromStdString(tag->key())},
            {QStringLiteral("type"), static_cast<int>(tag->type())},
            {QStringLiteral("typeName"), QString::fromStdString(tag_type_to_str(tag->type()))},
            {QStringLiteral("value"), QString::fromStdString(tag->restricted_value_string())},
            {QStringLiteral("isContainer"), container},
            {QStringLiteral("hasChildren"), has_children},
            {QStringLiteral("isRoot"), is_root},
            {QStringLiteral("childType"), childTypeForRow(row)},
            {QStringLiteral("canAdd"), can_add},
            {QStringLiteral("canEdit"), can_edit},
            {QStringLiteral("canRemove"), can_remove},
            {QStringLiteral("canClear"), can_clear},
            {QStringLiteral("canCopy"), clipboard_ != nullptr},
            {QStringLiteral("canPaste"), clipboard_ != nullptr && can_add}};
}
