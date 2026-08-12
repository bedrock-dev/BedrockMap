#include "leveloperator.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>

#include "bedrock_level.h"
#include "leveldb/db.h"
#include "loguru/loguru.hpp"
#include "msg.h"
#include "nbt.h"

namespace {

    /// Return the best matching level.dat template for the given version.
    /// Versions with 4 segments (1.xx.x.xx) are truncated to 3 segments (1.xx.x),
    /// because all template files follow the 3-segment naming convention.
    /// Falls back to "res/level_dat/level.dat" when no version-specific file exists.
    QString selectLevelDat(const QString &version) {
        auto parts = version.split('.');
        if (parts.size() == 4) {
            parts.removeLast();
        }
        const QString key = parts.join('.');
        const QString candidate = QStringLiteral(":/res/level_dat/") + key + QStringLiteral(".dat");
        if (QFile::exists(candidate)) {
            return candidate;
        }
        return QStringLiteral(":/res/level_dat/level.dat");
    }

}  // namespace

bool NewLevelParams::check(QString &msg) const {
    // 1. level name non-empty
    if (levelName.trimmed().isEmpty()) {
        msg = ::msg::NEW_LEVEL_NAME_EMPTY();
        return false;
    }

    // 2. path exists and is an empty directory
    QDir dir(path);
    if (!dir.exists()) {
        msg = ::msg::NEW_LEVEL_DIR_NOT_EXIST(path);
        return false;
    }
    QFileInfo fi(path);
    if (!fi.isDir()) {
        msg = ::msg::NEW_LEVEL_PATH_NOT_DIR(path);
        return false;
    }
    if (!dir.isEmpty()) {
        msg = ::msg::NEW_LEVEL_DIR_NOT_EMPTY(path);
        return false;
    }

    // 2. write permission
    if (!fi.isWritable()) {
        msg = ::msg::NEW_LEVEL_NO_WRITE_PERM(path);
        return false;
    }

    // 3. version format: 1.xx.x.xx or 1.xx.x
    static const QRegularExpression versionRe(QStringLiteral(R"(^1\.\d{2}\.\d{1,2}(?:\.\d{1,2})?$)"));
    if (!versionRe.match(version).hasMatch()) {
        msg = ::msg::NEW_LEVEL_INVALID_VERSION(version);
        return false;
    }

    msg = QStringLiteral("OK");
    return true;
}

QString NewLevelParams::toFlatJson() const {
    QJsonArray layers;
    for (const auto &[name, count] : flatBlocks) {
        QJsonObject layer;
        if (name.contains(QLatin1Char(':'))) {
            layer[QStringLiteral("block_name")] = name;
        } else {
            layer[QStringLiteral("block_name")] = QStringLiteral("minecraft:") + name;
        }
        layer[QStringLiteral("count")] = count;
        layers.append(layer);
    }

    QJsonObject root;
    root[QStringLiteral("biome_id")] = static_cast<int>(biome);
    root[QStringLiteral("block_layers")] = layers;
    root[QStringLiteral("encoding_version")] = 6;
    root[QStringLiteral("preset_id")] = QStringLiteral("ClassicFlat");
    root[QStringLiteral("world_version")] = QStringLiteral("version.post_1_18");

    return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

bool LevelOperator::newLevel(const NewLevelParams &params) {
    QString err;
    if (!params.check(err)) {
        LOG_F(ERROR, "can not create level: %s", err.toStdString().c_str());
        return false;
    }

    const QString srcPath = selectLevelDat(params.version);
    const QString dstPath = params.path + QStringLiteral("/level.dat");

    QFile src(srcPath);
    if (!src.open(QIODevice::ReadOnly)) {
        LOG_F(ERROR, "cannot open template %s", srcPath.toStdString().c_str());
        return false;
    }

    QFile dst(dstPath);
    if (!dst.open(QIODevice::WriteOnly)) {
        LOG_F(ERROR, "cannot write %s", dstPath.toStdString().c_str());
        return false;
    }

    dst.write(src.readAll());
    src.close();
    dst.close();

    LOG_F(INFO, "level.dat copied to %s", dstPath.toStdString().c_str());

    QFile nameFile(params.path + QStringLiteral("/levelname.txt"));
    if (nameFile.open(QIODevice::WriteOnly)) {
        nameFile.write(params.levelName.toUtf8());
        nameFile.close();
        LOG_F(INFO, "levelname.txt created");
    } else {
        LOG_F(ERROR, "cannot create levelname.txt");
        return false;
    }

    QDir dir(params.path);
    if (!dir.mkpath(QStringLiteral("db"))) {
        LOG_F(ERROR, "cannot create db directory");
        return false;
    }

    leveldb::Options opts;
    opts.create_if_missing = true;

    const auto dbPath = dir.absoluteFilePath(QStringLiteral("db")).toStdString();

    leveldb::DB *db = nullptr;
    leveldb::Status status = leveldb::DB::Open(opts, dbPath, &db);
    if (!status.ok()) {
        LOG_F(ERROR, "LevelDB create failed: %s", status.ToString().c_str());
        return false;
    }
    delete db;

    // change level data settings
    bl::bedrock_level level;
    if (!level.open(params.path.toStdString())) {
        LOG_F(ERROR, "Open new level failed");
        return false;
    }

    LOG_F(INFO, "flag json: %s", params.toFlatJson().toStdString().c_str());

    auto *nbt = level.dat().root();
    nbt->put(new bl::nbt::int_tag("GameType", params.gameMode));
    nbt->put(new bl::nbt::int_tag("Difficulty", params.difficulty));
    nbt->put(new bl::nbt::byte_tag("dodaylightcycle", params.dayNightCycle));
    nbt->put(new bl::nbt::byte_tag("doweathercycle", params.weatherCycle));
    nbt->put(new bl::nbt::byte_tag("domobspawning", params.mobSpawning));
    nbt->put(new bl::nbt::string_tag("LevelName", params.levelName.toStdString()));
    nbt->put(new bl::nbt::int_tag("Generator", params.flat ? 2 : 1));
    nbt->put(new bl::nbt::string_tag("FlatWorldLayers", params.toFlatJson().toStdString()));
    auto raw = level.dat().header() + nbt->to_raw();
    level.close();
    bl::utils::write_file(dstPath.toStdString(), raw.data(), raw.size());
    return true;
}
