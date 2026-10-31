#ifndef BEDROCKMAP_GLOBALDATACONTROLLER_H
#define BEDROCKMAP_GLOBALDATACONTROLLER_H

#include <QFutureWatcher>
#include <QObject>
#include <QPointer>
#include <QVariantList>
#include <atomic>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "controller/blockvolumecontroller.h"
#include "controller/mapitemcontroller.h"
#include "controller/nbtdocumentcontroller.h"
#include "nbt.h"
#include "render/overlay.h"

class AsyncLevelLoader;
struct GlobalNBTLoadResult;

struct GlobalNbtPolicy {
    QString title;
    QString shortLabel;
    QString iconName;
    bool readOnly{false};
    bool singleRecord{false};
    bool structurePreview{false};
    bool mapPreview{false};
    QString fixedKey;
    NbtItemPresenter presenter;
    NbtKeyResolver keyResolver;
};

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
    Q_PROPERTY(BlockVolumeController* structurePreview READ structurePreview CONSTANT)
    Q_PROPERTY(MapItemController* mapPreview READ mapPreview CONSTANT)
    Q_PROPERTY(bool dirty READ isDirty NOTIFY dirtyChanged)

   public:
    GlobalNbtSection(GlobalNbtPolicy policy, std::vector<std::unique_ptr<bl::nbt::compound_tag>> roots, const QStringList& raw_keys,
                     QObject* parent = nullptr);

    [[nodiscard]] QString title() const { return title_; }
    [[nodiscard]] QString shortLabel() const { return short_label_; }
    [[nodiscard]] QString iconName() const { return icon_name_; }
    [[nodiscard]] int count() const { return count_; }
    [[nodiscard]] NbtDocumentController* document() const { return document_; }
    [[nodiscard]] BlockVolumeController* structurePreview() const { return structure_preview_.get(); }
    [[nodiscard]] MapItemController* mapPreview() const { return map_preview_.get(); }
    [[nodiscard]] bool isDirty() const { return document_ && document_->dirty(); }
    [[nodiscard]] bool collectLevelDbChanges(std::unordered_map<std::string, std::string>& changes, QString& error) {
        if (!document_) {
            error.clear();
            return true;
        }
        pending_plan_ = document_->prepareLevelDbChanges();
        if (!pending_plan_.error.isEmpty()) {
            error = pending_plan_.error;
            pending_plan_ = {};
            return false;
        }
        for (const auto& [key, value] : pending_plan_.changes) changes[key] = value;
        error.clear();
        return true;
    }
    void markPersisted() {
        if (document_) document_->markPersisted(pending_plan_);
        pending_plan_ = {};
    }
   signals:
    void dirtyChanged();

   private:
    void enableStructurePreview();
    void reloadStructurePreview();
    void enableMapPreview();
    void reloadMapPreview();

    QString title_;
    QString short_label_;
    QString icon_name_;
    int count_{0};
    NbtDocumentController* document_{nullptr};
    std::unique_ptr<MapItemController> map_preview_;
    std::unique_ptr<BlockVolumeController> structure_preview_;
    std::optional<QByteArray> pending_structure_preview_;
    NbtLevelDbPlan pending_plan_;
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
    [[nodiscard]] std::unordered_map<std::string, std::string> collectLevelDbChanges(QString* error = nullptr);
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
    GlobalNbtSection* appendGeneralSection(GlobalNbtPolicy policy,
                                           const std::unordered_map<std::string, bl::nbt::compound_tag*>& data,
                                           bool skip_portals = false);
    GlobalNbtSection* addSection(std::unique_ptr<GlobalNbtSection> section);
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
