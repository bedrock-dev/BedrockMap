#include "controller/nbtrecordfiltermodel.h"

#include <QVariantMap>
#include <utility>

#include "controller/nbtdocumentcontroller.h"

namespace {
    constexpr int kFilterDelayMs = 180;
}

NbtRecordFilterModel::NbtRecordFilterModel(QObject* parent) : QAbstractListModel(parent) {
    filter_timer_.setSingleShot(true);
    filter_timer_.setInterval(kFilterDelayMs);
    connect(&filter_timer_, &QTimer::timeout, this, &NbtRecordFilterModel::rebuildFilter);
}

void NbtRecordFilterModel::setDocument(NbtDocumentController* document) {
    if (document_ == document) return;
    if (document_) disconnect(document_, nullptr, this, nullptr);
    document_ = document;
    if (document_) {
        connect(document_, &NbtDocumentController::itemsChanged, this, [this] { rebuildSourceEntries(); });
        connect(document_, &NbtDocumentController::currentIndexChanged, this, &NbtRecordFilterModel::updateCurrentIndex);
    }
    emit documentChanged();
    rebuildSourceEntries();
}

void NbtRecordFilterModel::setFilterText(const QString& text) {
    if (filter_text_ == text) return;
    filter_text_ = text;
    emit filterChanged();
    scheduleFilter();
}

void NbtRecordFilterModel::setFilterRegex(bool enable) {
    if (filter_regex_ == enable) return;
    filter_regex_ = enable;
    emit filterChanged();
    scheduleFilter();
}

int NbtRecordFilterModel::currentIndex() const {
    if (!document_) return -1;
    return filtered_indices_.indexOf(document_->currentIndex());
}

QString NbtRecordFilterModel::currentIcon() const {
    const int filtered_index = currentIndex();
    return filtered_index >= 0 ? iconAt(filtered_index) : QString{};
}

int NbtRecordFilterModel::rowCount(const QModelIndex& parent) const { return parent.isValid() ? 0 : count(); }

QVariant NbtRecordFilterModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= filtered_indices_.size()) return {};
    const SourceEntry& entry = source_entries_.at(filtered_indices_.at(index.row()));
    switch (role) {
        case Qt::DisplayRole:
        case LabelRole:
            return entry.label;
        case IconRole:
            return entry.icon;
        case SourceIndexRole:
            return filtered_indices_.at(index.row());
        default:
            return {};
    }
}

QHash<int, QByteArray> NbtRecordFilterModel::roleNames() const {
    return {{LabelRole, "label"}, {IconRole, "itemIcon"}, {SourceIndexRole, "sourceIndex"}};
}

int NbtRecordFilterModel::sourceIndexAt(int filteredIndex) const {
    if (filteredIndex < 0 || filteredIndex >= filtered_indices_.size()) return -1;
    return filtered_indices_.at(filteredIndex);
}

QString NbtRecordFilterModel::iconAt(int filteredIndex) const {
    const int source_index = sourceIndexAt(filteredIndex);
    return source_index >= 0 && source_index < source_entries_.size() ? source_entries_.at(source_index).icon : QString{};
}

QVariantList NbtRecordFilterModel::dialogItems() const {
    QVariantList result;
    result.reserve(filtered_indices_.size());
    for (const int source_index : filtered_indices_) {
        const SourceEntry& entry = source_entries_.at(source_index);
        result.append(QVariantMap{{QStringLiteral("text"), entry.label}, {QStringLiteral("value"), source_index}});
    }
    return result;
}

void NbtRecordFilterModel::flushFilter() {
    if (!filter_timer_.isActive()) return;
    filter_timer_.stop();
    rebuildFilter();
}

void NbtRecordFilterModel::rebuildSourceEntries() {
    filter_timer_.stop();
    source_entries_.clear();
    if (document_) {
        source_entries_.reserve(document_->rootCount());
        for (int index = 0; index < document_->rootCount(); ++index) {
            const NbtItemPresentation presentation = document_->itemPresentation(index);
            source_entries_.push_back({presentation.label, presentation.icon, presentation.label.toCaseFolded()});
        }
    }
    rebuildFilter();
}

void NbtRecordFilterModel::scheduleFilter() { filter_timer_.start(); }

void NbtRecordFilterModel::rebuildFilter() {
    QVector<int> next_filtered;
    const QString needle = filter_text_.trimmed().toCaseFolded();
    QRegularExpression expression;
    bool valid_expression = true;
    if (filter_regex_ && !needle.isEmpty()) {
        expression.setPattern(filter_text_.trimmed());
        expression.setPatternOptions(QRegularExpression::CaseInsensitiveOption);
        valid_expression = expression.isValid();
    }

    if (valid_expression) {
        next_filtered.reserve(source_entries_.size());
        for (int index = 0; index < source_entries_.size(); ++index) {
            const SourceEntry& entry = source_entries_.at(index);
            const bool match =
                needle.isEmpty() || (filter_regex_ ? expression.match(entry.label).hasMatch() : entry.search_text.contains(needle));
            if (match) next_filtered.append(index);
        }
    }

    beginResetModel();
    filtered_indices_ = std::move(next_filtered);
    endResetModel();
    emit countChanged();
    updateCurrentIndex();
}

void NbtRecordFilterModel::updateCurrentIndex() {
    const int next_index = currentIndex();
    if (current_filtered_index_ == next_index) {
        emit currentIconChanged();
        return;
    }
    current_filtered_index_ = next_index;
    emit currentIndexChanged();
    emit currentIconChanged();
}
