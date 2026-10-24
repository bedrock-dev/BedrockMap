#include "controller/globaldatacontroller.h"

#include <QtConcurrent>
#include <algorithm>
#include <utility>

#include "global.h"
#include "loader/asynclevelloader.h"

GlobalNbtSection::GlobalNbtSection(QString title, QString short_label, QString icon_name,
                                   std::vector<std::unique_ptr<bl::nbt::compound_tag>> roots, const QStringList& raw_keys,
                                   bool read_only, bool single_record, QObject* parent, const NbtItemPresenter& presenter)
    : QObject(parent),
      title_(std::move(title)),
      short_label_(std::move(short_label)),
      icon_name_(std::move(icon_name)),
      count_(static_cast<int>(roots.size())),
      document_(new NbtDocumentController(this)),
      single_record_(single_record) {
    document_->loadMemory(std::move(roots), raw_keys, title_, presenter);
    document_->setReadOnly(read_only);
    connect(document_, &NbtDocumentController::dirtyChanged, this, &GlobalNbtSection::dirtyChanged);
}

GlobalDataController::GlobalDataController(QObject* parent) : QObject(parent) {
    connect(&watcher_, &QFutureWatcher<void>::finished, this, [this] {
        if (stop_.load(std::memory_order_acquire) || !result_) return;
        publish(result_);
        result_.reset();
        loading_ = false;
        loaded_ = true;
        emit loadingChanged();
        emit loadedChanged();
    });
}

GlobalDataController::~GlobalDataController() { stopAndWait(); }

void GlobalDataController::clearSections() {
    sections_storage_.clear();
    level_dat_section_ = nullptr;
    sections_.clear();
    village_rects_.clear();
    emit sectionsChanged();
    emit dirtyChanged();
}

void GlobalDataController::start(AsyncLevelLoader* loader, bl::nbt::compound_tag* level_dat_root) {
    if (!loader || loading_) return;
    stopAndWait();
    stop_.store(false, std::memory_order_release);
    clearSections();
    level_dat_root_.reset(level_dat_root ? static_cast<bl::nbt::compound_tag*>(level_dat_root->copy()) : nullptr);
    result_ = std::make_shared<GlobalNBTLoadResult>();
    loading_ = true;
    loaded_ = false;
    error_.clear();
    emit loadingChanged();
    emit loadedChanged();
    emit errorChanged();
    watcher_.setFuture(QtConcurrent::run([loader, result = result_, stop = &stop_] {
        try {
            loader->loadGlobalData(*result, *stop);
        } catch (...) {
        }
    }));
}

void GlobalDataController::stopAndWait() {
    stop_.store(true, std::memory_order_release);
    if (watcher_.isRunning()) watcher_.waitForFinished();
    result_.reset();
    loading_ = false;
}

void GlobalDataController::appendGeneralSection(const QString& title, const QString& short_label, const QString& icon_name,
                                                const std::unordered_map<std::string, bl::nbt::compound_tag*>& data,
                                                bool skip_portals, bool read_only, bool single_record,
                                                const NbtItemPresenter& presenter) {
    std::vector<std::pair<std::string, bl::nbt::compound_tag*>> ordered;
    ordered.reserve(data.size());
    for (const auto& [key, tag] : data) {
        if (!tag || (skip_portals && key == "portals")) continue;
        ordered.emplace_back(key, tag);
    }
    std::sort(ordered.begin(), ordered.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    std::vector<std::unique_ptr<bl::nbt::compound_tag>> roots;
    QStringList raw_keys;
    roots.reserve(ordered.size());
    raw_keys.reserve(static_cast<qsizetype>(ordered.size()));
    for (const auto& [key, tag] : ordered) {
        auto* copy = static_cast<bl::nbt::compound_tag*>(tag->copy());
        copy->set_key(key);
        roots.emplace_back(copy);
        raw_keys.append(QString::fromStdString(key));
    }
    auto section =
        std::make_unique<GlobalNbtSection>(title, short_label, icon_name, std::move(roots), raw_keys, read_only,
                                           single_record, this, presenter);
    connect(section.get(), &GlobalNbtSection::dirtyChanged, this, &GlobalDataController::dirtyChanged);
    sections_storage_.push_back(std::move(section));
    sections_.push_back(QVariant::fromValue(static_cast<QObject*>(sections_storage_.back().get())));
}

void GlobalDataController::appendVillageSection(std::shared_ptr<GlobalNBTLoadResult> result) {
    struct VillageRecord {
        std::string display_key;
        std::string raw_key;
        bl::nbt::compound_tag* tag;
    };
    std::vector<VillageRecord> ordered;
    for (size_t dim = 0; dim < result->villageData.data().size(); ++dim) {
        for (const auto& [uuid, records] : result->villageData.data()[dim]) {
            for (size_t type = 0; type < records.size(); ++type) {
                auto* tag = records[type];
                if (!tag) continue;
                const auto type_name = bl::village_key::village_key_type_to_str(static_cast<bl::village_key::key_type>(type));
                ordered.push_back(VillageRecord{
                    QStringLiteral("%1/%2/%3").arg(dim).arg(QString::fromStdString(uuid)).arg(QString::fromStdString(type_name)).toStdString(),
                    "VILLAGE_" + uuid + "_" + type_name, tag});
            }
        }
    }
    std::sort(ordered.begin(), ordered.end(), [](const auto& a, const auto& b) { return a.display_key < b.display_key; });
    std::vector<std::unique_ptr<bl::nbt::compound_tag>> roots;
    QStringList raw_keys;
    roots.reserve(ordered.size());
    raw_keys.reserve(static_cast<qsizetype>(ordered.size()));
    for (const auto& record : ordered) {
        auto* copy = static_cast<bl::nbt::compound_tag*>(record.tag->copy());
        copy->set_key(record.display_key);
        roots.emplace_back(copy);
        raw_keys.append(QString::fromStdString(record.raw_key));
    }
    auto section = std::make_unique<GlobalNbtSection>(QStringLiteral("Villages"), QStringLiteral("V"),
                                                      QStringLiteral("villager"), std::move(roots), raw_keys, false, false,
                                                      this,
                                                      [](const bl::nbt::compound_tag*, int, const QString& raw_key) {
                                                          QString label = raw_key;
                                                          if (label.startsWith(QStringLiteral("VILLAGE_")))
                                                              label.remove(0, 8);
                                                          const qsizetype separator = label.lastIndexOf(QLatin1Char('_'));
                                                          QString icon = QStringLiteral("qrc:/res/icons/village/info.png");
                                                          if (separator >= 0) {
                                                              const QString type = label.mid(separator + 1);
                                                              const QString normalized = type.toLower();
                                                              if (normalized == QStringLiteral("dwellers"))
                                                                  icon = QStringLiteral("qrc:/res/icons/village/dwellers.png");
                                                              else if (normalized == QStringLiteral("players"))
                                                                  icon = QStringLiteral("qrc:/res/icons/village/players.png");
                                                              else if (normalized == QStringLiteral("poi"))
                                                                  icon = QStringLiteral("qrc:/res/icons/village/poi.png");
                                                              label = label.left(separator) + QStringLiteral(" / ") + type;
                                                          }
                                                          return NbtItemPresentation{label, icon};
                                                      });
    connect(section.get(), &GlobalNbtSection::dirtyChanged, this, &GlobalDataController::dirtyChanged);
    sections_storage_.push_back(std::move(section));
    sections_.push_back(QVariant::fromValue(static_cast<QObject*>(sections_storage_.back().get())));
}

void GlobalDataController::appendLevelDatSection() {
    if (!level_dat_root_) return;
    std::vector<std::unique_ptr<bl::nbt::compound_tag>> roots;
    auto* copy = static_cast<bl::nbt::compound_tag*>(level_dat_root_->copy());
    copy->set_key("level.dat");
    roots.emplace_back(copy);
    auto section = std::make_unique<GlobalNbtSection>(QStringLiteral("level.dat"), QStringLiteral("LD"),
                                                      QStringLiteral("level_dat"), std::move(roots), QStringList{"level.dat"},
                                                      false, true, this);
    level_dat_section_ = section.get();
    connect(section.get(), &GlobalNbtSection::dirtyChanged, this, &GlobalDataController::dirtyChanged);
    sections_storage_.push_back(std::move(section));
    sections_.push_back(QVariant::fromValue(static_cast<QObject*>(sections_storage_.back().get())));
}

void GlobalDataController::collectVillageRects(GlobalNBTLoadResult& result) {
    village_rects_.clear();
    const QColor outline(0, 223, 162, 255);
    QColor fill = outline;
    fill.setAlpha(30);
    for (size_t dim = 0; dim < result.villageData.data().size(); ++dim) {
        for (const auto& [key, records] : result.villageData.data()[dim]) {
            Q_UNUSED(key);
            const auto* info = records[static_cast<size_t>(bl::village_key::INFO)];
            if (!info) continue;
            const auto* x0 = info->get("X0") ? info->get("X0")->as<const bl::nbt::int_tag*>() : nullptr;
            const auto* z0 = info->get("Z0") ? info->get("Z0")->as<const bl::nbt::int_tag*>() : nullptr;
            const auto* x1 = info->get("X1") ? info->get("X1")->as<const bl::nbt::int_tag*>() : nullptr;
            const auto* z1 = info->get("Z1") ? info->get("Z1")->as<const bl::nbt::int_tag*>() : nullptr;
            if (!x0 || !z0 || !x1 || !z1) continue;
            village_rects_.push_back(OverlayRect{static_cast<float>(x0->value), static_cast<float>(z0->value),
                                                 static_cast<float>(x1->value), static_cast<float>(z1->value), static_cast<int>(dim),
                                                 outline, fill});
        }
    }
}

void GlobalDataController::publish(std::shared_ptr<GlobalNBTLoadResult> result) {
    clearSections();
    collectVillageRects(*result);
    appendLevelDatSection();
    appendGeneralSection(
        QStringLiteral("Players"), QStringLiteral("P"), QStringLiteral("players"), result->playerData.data(), false, false, false,
        [](const bl::nbt::compound_tag*, int, const QString& raw_key) {
            return NbtItemPresentation{raw_key, QStringLiteral("qrc:/res/icons/village/players.png")};
        });
    appendVillageSection(result);
    appendGeneralSection(QStringLiteral("Portals"), QStringLiteral("⇄"), QStringLiteral("portal"),
                         result->portalData.data(), false, false, true);
    appendGeneralSection(QStringLiteral("Structures"), QStringLiteral("S"), QStringLiteral("structure"),
                         result->structuresData.data(), false, true);
    appendGeneralSection(QStringLiteral("Maps"), QStringLiteral("M"), QStringLiteral("map_item"), result->mapData.data());
    appendGeneralSection(QStringLiteral("Other"), QStringLiteral("…"), QString(), result->otherData.data(), true);
    emit sectionsChanged();
    emit dirtyChanged();
    level_dat_root_.reset();
}

bool GlobalDataController::isDirty() const {
    return std::any_of(sections_storage_.begin(), sections_storage_.end(),
                       [](const auto& section) { return section && section->isDirty(); });
}

std::unordered_map<std::string, std::string> GlobalDataController::collectLevelDbChanges() const {
    std::unordered_map<std::string, std::string> result;
    for (const auto& section : sections_storage_) {
        if (!section || section.get() == level_dat_section_) continue;
        for (const auto& [key, value] : section->levelDbChanges()) result[key] = value;
    }
    return result;
}

std::unique_ptr<bl::nbt::compound_tag> GlobalDataController::levelDatSnapshot() const {
    return level_dat_section_ && level_dat_section_->isDirty() ? level_dat_section_->document()->itemCopy(0) : nullptr;
}

void GlobalDataController::markSaved() {
    for (const auto& section : sections_storage_) {
        if (section && section->document()) section->document()->markPersisted();
    }
    emit dirtyChanged();
}
