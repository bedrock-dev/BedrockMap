#ifndef BEDROCKMAP_NBTRECORDFILTERMODEL_H
#define BEDROCKMAP_NBTRECORDFILTERMODEL_H

#include <QAbstractListModel>
#include <QPointer>
#include <QRegularExpression>
#include <QString>
#include <QTimer>
#include <QVariantList>
#include <QVector>

class NbtDocumentController;

/// Filters the root records of an NBT document without rebuilding JavaScript
/// arrays in QML on every keystroke. Source presentation data is cached once per
/// document item change; filter updates are debounced by the model.
class NbtRecordFilterModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(NbtDocumentController* document READ document WRITE setDocument NOTIFY documentChanged)
    Q_PROPERTY(QString filterText READ filterText WRITE setFilterText NOTIFY filterChanged)
    Q_PROPERTY(bool filterRegex READ filterRegex WRITE setFilterRegex NOTIFY filterChanged)
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(int currentIndex READ currentIndex NOTIFY currentIndexChanged)
    Q_PROPERTY(QString currentIcon READ currentIcon NOTIFY currentIconChanged)

   public:
    enum Role {
        LabelRole = Qt::UserRole + 1,
        IconRole,
        SourceIndexRole,
    };
    Q_ENUM(Role)

    explicit NbtRecordFilterModel(QObject* parent = nullptr);

    [[nodiscard]] NbtDocumentController* document() const { return document_; }
    void setDocument(NbtDocumentController* document);
    [[nodiscard]] QString filterText() const { return filter_text_; }
    void setFilterText(const QString& text);
    [[nodiscard]] bool filterRegex() const { return filter_regex_; }
    void setFilterRegex(bool enable);

    [[nodiscard]] int count() const { return static_cast<int>(filtered_indices_.size()); }
    [[nodiscard]] int currentIndex() const;
    [[nodiscard]] QString currentIcon() const;
    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

    [[nodiscard]] Q_INVOKABLE int sourceIndexAt(int filteredIndex) const;
    [[nodiscard]] Q_INVOKABLE QString iconAt(int filteredIndex) const;
    [[nodiscard]] Q_INVOKABLE QVariantList dialogItems() const;
    Q_INVOKABLE void flushFilter();

   signals:
    void documentChanged();
    void filterChanged();
    void countChanged();
    void currentIndexChanged();
    void currentIconChanged();

   private:
    struct SourceEntry {
        QString label;
        QString icon;
        QString search_text;
    };

    void rebuildSourceEntries();
    void scheduleFilter();
    void rebuildFilter();
    void updateCurrentIndex();

    QPointer<NbtDocumentController> document_;
    QString filter_text_;
    bool filter_regex_{false};
    QVector<SourceEntry> source_entries_;
    QVector<int> filtered_indices_;
    QTimer filter_timer_;
    int current_filtered_index_{-1};
};

#endif  // BEDROCKMAP_NBTRECORDFILTERMODEL_H
