#ifndef BEDROCKMAP_GLOBALDATACONTROLLER_H
#define BEDROCKMAP_GLOBALDATACONTROLLER_H

#include <QFutureWatcher>
#include <QObject>
#include <QPointer>
#include <QVariantList>
#include <atomic>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "controller/nbtdocumentcontroller.h"
#include "nbt.h"
#include "render/overlay.h"

class AsyncLevelLoader;
struct GlobalNBTLoadResult;

/// One expandable category in the right-hand global-data inspector. Its root is
/// a compound whose children are the original global records, so the standard
/// NbtTreeView can display and edit the stack without a second view type.
class GlobalNbtSection final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString title READ title CONSTANT)
    Q_PROPERTY(QString shortLabel READ shortLabel CONSTANT)
    Q_PROPERTY(QString iconName READ iconName CONSTANT)
    Q_PROPERTY(int count READ count CONSTANT)
    Q_PROPERTY(NbtDocumentController* document READ document CONSTANT)
    Q_PROPERTY(bool dirty READ isDirty NOTIFY dirtyChanged)
    Q_PROPERTY(bool singleRecord READ singleRecord CONSTANT)

   public:
    GlobalNbtSection(QString title, QString short_label, QString icon_name,
                     std::vector<std::unique_ptr<bl::nbt::compound_tag>> roots, const QStringList& raw_keys, bool read_only,
                     bool single_record, QObject* parent = nullptr, const NbtItemPresenter& presenter = {});

    [[nodiscard]] QString title() const { return title_; }
    [[nodiscard]] QString shortLabel() const { return short_label_; }
    [[nodiscard]] QString iconName() const { return icon_name_; }
    [[nodiscard]] int count() const { return count_; }
    [[nodiscard]] NbtDocumentController* document() const { return document_; }
    [[nodiscard]] bool isDirty() const { return document_ && document_->dirty(); }
    [[nodiscard]] bool singleRecord() const { return single_record_; }
    [[nodiscard]] std::unordered_map<std::string, std::string> levelDbChanges() const {
        return document_ ? document_->levelDbChanges() : std::unordered_map<std::string, std::string>{};
    }

   signals:
    void dirtyChanged();

   private:
    QString title_;
    QString short_label_;
    QString icon_name_;
    int count_{0};
    NbtDocumentController* document_{nullptr};
    bool single_record_{false};
};

/// Loads the world's global NBT records once per session and publishes them as
/// independent section models. Saving and specialized editors are intentionally
/// outside this controller for now.
class GlobalDataController : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList sections READ sections NOTIFY sectionsChanged)
    Q_PROPERTY(bool loading READ loading NOTIFY loadingChanged)
    Q_PROPERTY(bool loaded READ loaded NOTIFY loadedChanged)
    Q_PROPERTY(QString error READ error NOTIFY errorChanged)
    Q_PROPERTY(bool dirty READ isDirty NOTIFY dirtyChanged)

   public:
    explicit GlobalDataController(QObject* parent = nullptr);
    ~GlobalDataController() override;

    [[nodiscard]] QVariantList sections() const { return sections_; }
    [[nodiscard]] bool loading() const { return loading_; }
    [[nodiscard]] bool loaded() const { return loaded_; }
    [[nodiscard]] QString error() const { return error_; }
    [[nodiscard]] const std::vector<OverlayRect>& villageRects() const { return village_rects_; }
    [[nodiscard]] bool isDirty() const;
    [[nodiscard]] std::unordered_map<std::string, std::string> collectLevelDbChanges() const;
    [[nodiscard]] std::unique_ptr<bl::nbt::compound_tag> levelDatSnapshot() const;
    void markSaved();

    void start(AsyncLevelLoader* loader, bl::nbt::compound_tag* level_dat_root);
    void stopAndWait();

   signals:
    void sectionsChanged();
    void loadingChanged();
    void loadedChanged();
    void errorChanged();
    void dirtyChanged();

   private:
    void clearSections();
    void publish(std::shared_ptr<GlobalNBTLoadResult> result);
    void appendGeneralSection(const QString& title, const QString& short_label, const QString& icon_name,
                              const std::unordered_map<std::string, bl::nbt::compound_tag*>& data, bool skip_portals = false,
                              bool read_only = false, bool single_record = false, const NbtItemPresenter& presenter = {});
    void appendVillageSection(std::shared_ptr<GlobalNBTLoadResult> result);
    void appendLevelDatSection();
    void collectVillageRects(GlobalNBTLoadResult& result);

    QFutureWatcher<void> watcher_;
    std::shared_ptr<GlobalNBTLoadResult> result_;
    std::unique_ptr<bl::nbt::compound_tag> level_dat_root_;
    std::vector<std::unique_ptr<GlobalNbtSection>> sections_storage_;
    GlobalNbtSection* level_dat_section_{nullptr};
    QVariantList sections_;
    std::atomic_bool stop_{false};
    bool loading_{false};
    bool loaded_{false};
    QString error_;
    std::vector<OverlayRect> village_rects_;
};

#endif  // BEDROCKMAP_GLOBALDATACONTROLLER_H
