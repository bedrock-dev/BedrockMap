#include "controller/globaldatacontroller.h"

#include <QCoreApplication>
#include <QMetaObject>
#include <QUuid>
#include <QtConcurrent>
#include <algorithm>
#include <utility>

#include "controller/mcstructurevolumesource.h"
#include "global.h"
#include "loader/asynclevelloader.h"

namespace {
    NbtItemPresenter rootKeyPresenter(QString icon) {
        return [icon = std::move(icon)](const bl::nbt::compound_tag*, int, const QString& raw_key) {
            return NbtItemPresentation{raw_key, icon};
        };
    }

    QString keyTagText(const bl::nbt::abstract_tag* tag) {
        if (!tag) return {};
        if (const auto* value = tag->as<const bl::nbt::string_tag*>()) return QString::fromStdString(value->value).trimmed();
        if (const auto* value = tag->as<const bl::nbt::byte_tag*>()) return QString::number(value->value);
        if (const auto* value = tag->as<const bl::nbt::short_tag*>()) return QString::number(value->value);
        if (const auto* value = tag->as<const bl::nbt::int_tag*>()) return QString::number(value->value);
        if (const auto* value = tag->as<const bl::nbt::long_tag*>()) return QString::number(value->value);
        return {};
    }

    NbtKeyResolver playerKeyResolver() {
        return [](bl::nbt::compound_tag* root, int, const QString& existing) {
            if (existing == QStringLiteral("~local_player") || existing.startsWith(QStringLiteral("player_"))) return existing;
            const QString server_id = keyTagText(root->get_by_path("ServerId"));
            return server_id.startsWith(QStringLiteral("player_")) ? server_id : QString{};
        };
    }

    NbtKeyResolver mapKeyResolver() {
        return [](bl::nbt::compound_tag* root, int, const QString& existing) {
            if (existing.startsWith(QStringLiteral("map_"))) return existing;
            const QString map_id = keyTagText(root->get_by_path("mapId"));
            return map_id.isEmpty() ? QString{} : QStringLiteral("map_") + map_id;
        };
    }

    NbtKeyResolver structureKeyResolver() {
        return [](bl::nbt::compound_tag* root, int, const QString& existing) {
            if (existing.startsWith(QStringLiteral("structuretemplate_"))) return existing;
            if (!root->get_by_path("format_version") || !root->get_by_path("size")) return QString{};
            return QStringLiteral("structuretemplate_new_") + QUuid::createUuid().toString(QUuid::WithoutBraces);
        };
    }

    NbtKeyResolver otherKeyResolver() {
        return [](bl::nbt::compound_tag*, int, const QString& existing) {
            return !existing.isEmpty() && bl::global_key::is_other_key(existing.toStdString()) ? existing : QString{};
        };
    }
}  // namespace

GlobalNbtSection::GlobalNbtSection(GlobalNbtPolicy policy, std::vector<std::unique_ptr<bl::nbt::compound_tag>> roots,
                                   const QStringList& raw_keys, AppContext& context, QObject* parent)
    : QObject(parent),
      title_(std::move(policy.title)),
      short_label_(std::move(policy.shortLabel)),
      icon_name_(std::move(policy.iconName)),
      count_(static_cast<int>(roots.size())),
      document_(new NbtDocumentController(this)),
      context_(context) {
    document_->loadMemory(std::move(roots), raw_keys, title_, policy.presenter);
    document_->setReadOnly(policy.readOnly);
    document_->setSingleRecord(policy.singleRecord);
    document_->setSingleRecordKey(std::move(policy.fixedKey));
    document_->setKeyResolver(std::move(policy.keyResolver));
    connect(document_, &NbtDocumentController::dirtyChanged, this, &GlobalNbtSection::dirtyChanged);
    if (policy.structurePreview) enableStructurePreview();
    if (policy.mapPreview) enableMapPreview();
}

void GlobalNbtSection::enableMapPreview() {
    if (map_preview_) return;
    map_preview_ = std::make_unique<MapItemController>(document_);
    connect(document_, &NbtDocumentController::currentIndexChanged, this, &GlobalNbtSection::reloadMapPreview);
    connect(document_->treeModel(), &NbtTreeModel::documentChanged, this, &GlobalNbtSection::reloadMapPreview);
    reloadMapPreview();
}

void GlobalNbtSection::reloadMapPreview() {
    if (map_preview_) map_preview_->reload();
}

void GlobalNbtSection::enableStructurePreview() {
    if (structure_preview_) return;
    structure_preview_ = std::make_unique<BlockVolumeController>(std::make_unique<McstructureVolumeSource>(), context_);
    connect(document_, &NbtDocumentController::currentIndexChanged, this, &GlobalNbtSection::reloadStructurePreview);
    connect(structure_preview_.get(), &VoxelSceneController::loadingChanged, this, [this] {
        if (structure_preview_->loading() || !pending_structure_preview_) return;
        QMetaObject::invokeMethod(
            this,
            [this] {
                if (structure_preview_->loading() || !pending_structure_preview_) return;
                QByteArray raw = std::move(*pending_structure_preview_);
                pending_structure_preview_.reset();
                if (raw.isEmpty()) {
                    structure_preview_->clearPreview();
                } else {
                    structure_preview_->loadBytes(raw);
                }
            },
            Qt::QueuedConnection);
    });
    reloadStructurePreview();
}

void GlobalNbtSection::reloadStructurePreview() {
    if (!structure_preview_) return;
    const auto root = document_->itemCopy(document_->currentIndex());
    QByteArray raw;
    if (root) {
        const std::string serialized = root->serialize();
        raw = QByteArray(serialized.data(), static_cast<qsizetype>(serialized.size()));
    }
    if (structure_preview_->loading()) {
        pending_structure_preview_ = std::move(raw);
        return;
    }
    pending_structure_preview_.reset();
    if (raw.isEmpty()) {
        structure_preview_->clearPreview();
    } else {
        structure_preview_->loadBytes(raw);
    }
}

GlobalDataController::GlobalDataController(AppContext& context, QObject* parent) : QObject(parent), context_(context) {
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

GlobalNbtSection* GlobalDataController::appendGeneralSection(
    GlobalNbtPolicy policy, const std::unordered_map<std::string, bl::nbt::compound_tag*>& data, bool skip_portals) {
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
    return addSection(std::make_unique<GlobalNbtSection>(std::move(policy), std::move(roots), raw_keys, context_, this));
}

GlobalNbtSection* GlobalDataController::addSection(std::unique_ptr<GlobalNbtSection> section) {
    auto* section_ptr = section.get();
    connect(section.get(), &GlobalNbtSection::dirtyChanged, this, &GlobalDataController::dirtyChanged);
    sections_storage_.push_back(std::move(section));
    sections_.push_back(QVariant::fromValue(static_cast<QObject*>(sections_storage_.back().get())));
    return section_ptr;
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
                ordered.push_back(VillageRecord{QStringLiteral("%1/%2/%3")
                                                    .arg(dim)
                                                    .arg(QString::fromStdString(uuid))
                                                    .arg(QString::fromStdString(type_name))
                                                    .toStdString(),
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
    GlobalNbtPolicy policy;
    policy.title = QStringLiteral("Villages");
    policy.shortLabel = QStringLiteral("V");
    policy.iconName = QStringLiteral("villager");
    policy.presenter = [](const bl::nbt::compound_tag*, int, const QString& raw_key) {
                                               QString label = raw_key;
                                               if (label.startsWith(QStringLiteral("VILLAGE_"))) label.remove(0, 8);
                                               const qsizetype separator = label.lastIndexOf(QLatin1Char('_'));
                                               QString icon = QStringLiteral("qrc:/bedrockmap/icons/village/info.png");
                                               if (separator >= 0) {
                                                   const QString type = label.mid(separator + 1);
                                                   const QString normalized = type.toLower();
                                                   if (normalized == QStringLiteral("dwellers"))
                                                       icon = QStringLiteral("qrc:/bedrockmap/icons/village/dwellers.png");
                                                   else if (normalized == QStringLiteral("players"))
                                                       icon = QStringLiteral("qrc:/bedrockmap/icons/village/players.png");
                                                   else if (normalized == QStringLiteral("poi"))
                                                       icon = QStringLiteral("qrc:/bedrockmap/icons/village/poi.png");
                                                   label = label.left(separator) + QStringLiteral(" / ") + type;
                                               }
                                               return NbtItemPresentation{label, icon};
                                           };
    addSection(std::make_unique<GlobalNbtSection>(std::move(policy), std::move(roots), raw_keys, context_, this));
}

void GlobalDataController::appendLevelDatSection() {
    if (!level_dat_root_) return;
    std::vector<std::unique_ptr<bl::nbt::compound_tag>> roots;
    auto* copy = static_cast<bl::nbt::compound_tag*>(level_dat_root_->copy());
    copy->set_key("level.dat");
    roots.emplace_back(copy);
    GlobalNbtPolicy policy;
    policy.title = QStringLiteral("level.dat");
    policy.shortLabel = QStringLiteral("LD");
    policy.iconName = QStringLiteral("level_dat");
    policy.singleRecord = true;
    policy.fixedKey = QStringLiteral("level.dat");
    level_dat_section_ = addSection(
        std::make_unique<GlobalNbtSection>(std::move(policy), std::move(roots), QStringList{"level.dat"}, context_, this));
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

    const auto make_policy = [](QString title, QString short_label, QString icon) {
        GlobalNbtPolicy policy;
        policy.title = std::move(title);
        policy.shortLabel = std::move(short_label);
        policy.iconName = std::move(icon);
        return policy;
    };

    auto players = make_policy(QStringLiteral("Players"), QStringLiteral("P"), QStringLiteral("players"));
    players.presenter = rootKeyPresenter(QStringLiteral("qrc:/bedrockmap/icons/village/players.png"));
    players.keyResolver = playerKeyResolver();
    appendGeneralSection(std::move(players), result->playerData.data());

    appendVillageSection(result);

    auto portals = make_policy(QStringLiteral("Portals"), QStringLiteral("⇄"), QStringLiteral("portal"));
    portals.singleRecord = true;
    portals.fixedKey = QStringLiteral("portals");
    appendGeneralSection(std::move(portals), result->portalData.data());

    auto structures = make_policy(QStringLiteral("Structures"), QStringLiteral("S"), QStringLiteral("structure"));
    structures.structurePreview = true;
    structures.presenter = rootKeyPresenter(QStringLiteral("structure"));
    structures.keyResolver = structureKeyResolver();
    appendGeneralSection(std::move(structures), result->structuresData.data());

    auto maps = make_policy(QStringLiteral("Maps"), QStringLiteral("M"), QStringLiteral("map_item"));
    maps.mapPreview = true;
    maps.presenter = rootKeyPresenter(QStringLiteral("map_item"));
    maps.keyResolver = mapKeyResolver();
    appendGeneralSection(std::move(maps), result->mapData.data());

    auto other = make_policy(QStringLiteral("Other"), QStringLiteral("…"), QString());
    other.presenter = rootKeyPresenter({});
    other.keyResolver = otherKeyResolver();
    appendGeneralSection(std::move(other), result->otherData.data(), true);

    emit sectionsChanged();
    emit dirtyChanged();
    level_dat_root_.reset();
}

bool GlobalDataController::isDirty() const {
    return std::any_of(sections_storage_.begin(), sections_storage_.end(),
                       [](const auto& section) { return section && section->isDirty(); });
}

std::unordered_map<std::string, std::string> GlobalDataController::collectLevelDbChanges(QString* error) {
    std::unordered_map<std::string, std::string> result;
    if (error) error->clear();
    for (const auto& section : sections_storage_) {
        if (!section || section.get() == level_dat_section_) continue;
        std::unordered_map<std::string, std::string> section_changes;
        QString section_error;
        if (!section->collectLevelDbChanges(section_changes, section_error)) {
            if (error) {
                *error = QCoreApplication::translate("GlobalDataController", "%1: %2").arg(section->title(), section_error);
            }
            return {};
        }
        for (const auto& [key, value] : section_changes) result[key] = value;
    }
    return result;
}

std::unique_ptr<bl::nbt::compound_tag> GlobalDataController::levelDatSnapshot() const {
    return level_dat_section_ && level_dat_section_->isDirty() ? level_dat_section_->document()->itemCopy(0) : nullptr;
}

void GlobalDataController::markSaved() {
    for (const auto& section : sections_storage_) {
        if (section) section->markPersisted();
    }
    emit dirtyChanged();
}
