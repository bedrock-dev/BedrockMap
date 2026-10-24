#ifndef BEDROCKMAP_NBTDOCUMENTCONTROLLER_H
#define BEDROCKMAP_NBTDOCUMENTCONTROLLER_H

#include <QObject>
#include <QStringList>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "model/nbttreemodel.h"

namespace bl::nbt {
    class compound_tag;
}

struct NbtItemPresentation {
    QString label;
    QString icon;
};

using NbtItemPresenter =
    std::function<NbtItemPresentation(const bl::nbt::compound_tag*, int index, const QString& rawKey)>;

/// Owns one editable NBT document. A document can contain several root
/// compounds, matching .nbts files and the palette-style data used by the
/// native editor. Views receive only stable QString/QVariant data through the
/// tree model.
class NbtDocumentController : public QObject {
    Q_OBJECT
    Q_PROPERTY(NbtTreeModel* treeModel READ treeModel CONSTANT)
    Q_PROPERTY(QStringList itemLabels READ itemLabels NOTIFY itemsChanged)
    Q_PROPERTY(QStringList itemIcons READ itemIcons NOTIFY itemsChanged)
    Q_PROPERTY(int currentIndex READ currentIndex WRITE selectItem NOTIFY currentIndexChanged)
    Q_PROPERTY(int itemCount READ itemCount NOTIFY itemsChanged)
    Q_PROPERTY(int mode READ modeValue NOTIFY modeChanged)
    Q_PROPERTY(bool fileBacked READ fileBacked NOTIFY modeChanged)
    Q_PROPERTY(QString filePath READ filePath NOTIFY filePathChanged)
    Q_PROPERTY(QString documentTitle READ documentTitle NOTIFY documentTitleChanged)
    Q_PROPERTY(bool dirty READ dirty NOTIFY dirtyChanged)
    Q_PROPERTY(bool readOnly READ readOnly WRITE setReadOnly NOTIFY readOnlyChanged)
    Q_PROPERTY(bool hasDocument READ hasDocument NOTIFY itemsChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)

   public:
    /// File documents own a filesystem destination. Memory documents belong to
    /// a host such as a world session and must be committed by that host.
    enum class Mode { File, Memory };
    Q_ENUM(Mode)

    explicit NbtDocumentController(QObject* parent = nullptr);
    ~NbtDocumentController() override;

    [[nodiscard]] NbtTreeModel* treeModel() const { return tree_model_; }
    [[nodiscard]] QStringList itemLabels() const;
    [[nodiscard]] QStringList itemIcons() const;
    [[nodiscard]] int currentIndex() const { return current_index_; }
    [[nodiscard]] int itemCount() const { return static_cast<int>(items_.size()); }
    [[nodiscard]] Mode mode() const { return mode_; }
    [[nodiscard]] int modeValue() const { return static_cast<int>(mode_); }
    [[nodiscard]] bool fileBacked() const { return mode_ == Mode::File; }
    [[nodiscard]] QString filePath() const { return file_path_; }
    [[nodiscard]] QString documentTitle() const;
    [[nodiscard]] bool dirty() const { return dirty_; }
    [[nodiscard]] bool readOnly() const { return read_only_; }
    [[nodiscard]] bool hasDocument() const { return !items_.empty(); }
    Q_INVOKABLE [[nodiscard]] bool isEmpty() const;
    [[nodiscard]] QString lastError() const { return last_error_; }

    Q_INVOKABLE bool loadFile(const QString& path);
    /// Load already-parsed in-memory roots. rawKeys preserves the original
    /// LevelDB identity separately from the visible tree key.
    void loadMemory(std::vector<std::unique_ptr<bl::nbt::compound_tag>> roots, const QStringList& rawKeys,
                    const QString& title = {}, const NbtItemPresenter& presenter = {});
    [[nodiscard]] std::unique_ptr<bl::nbt::compound_tag> itemCopy(int index) const;
    [[nodiscard]] std::unordered_map<std::string, std::string> levelDbChanges() const;
    /// Mark a memory document as persisted by its owner.
    void markPersisted();
    Q_INVOKABLE bool save();
    Q_INVOKABLE bool saveAs(const QString& path);
    Q_INVOKABLE bool importFile(const QString& path);
    Q_INVOKABLE bool exportFile(const QString& path);
    Q_INVOKABLE void createNew();
    Q_INVOKABLE void selectItem(int index);
    Q_INVOKABLE int addItem();
    Q_INVOKABLE bool removeItem(int index);
    Q_INVOKABLE bool clearContents();
    Q_INVOKABLE bool clearAllItems();
    void setReadOnly(bool read_only);

   signals:
    void itemsChanged();
    void currentIndexChanged();
    void filePathChanged();
    void documentTitleChanged();
    void modeChanged();
    void dirtyChanged();
    void readOnlyChanged();
    void lastErrorChanged();

   private:
    struct Item {
        std::unique_ptr<bl::nbt::compound_tag> root;
        QString label;
        QString icon;
        QString raw_key;
    };

    void replaceItems(std::vector<std::unique_ptr<bl::nbt::compound_tag>> roots, const QStringList& rawKeys, Mode mode,
                      const QString& path, const QString& title = {}, const NbtItemPresenter& presenter = {});
    void clearItems();
    void setError(const QString& message);
    void markDirty();

    NbtTreeModel* tree_model_{nullptr};
    std::vector<Item> items_;
    QStringList original_raw_keys_;
    int current_index_{-1};
    Mode mode_{Mode::File};
    QString file_path_;
    QString document_title_;
    QString last_error_;
    bool dirty_{false};
    bool read_only_{false};
    bool loading_{false};
};

#endif  // BEDROCKMAP_NBTDOCUMENTCONTROLLER_H
