#include "leveloperator.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>

#include "bedrock_level.h"
#include "leveldb/db.h"
#include "loguru/loguru.hpp"

bool LevelOperator::newLevel(const QString &path, const QString &version, bool flat, const QString &flatBlocks) {
    Q_UNUSED(version);
    Q_UNUSED(flat);
    Q_UNUSED(flatBlocks);

    QDir dir(path);
    if (!dir.exists()) {
        LOG_F(WARNING, "LevelOperator::newLevel: directory does not exist: %s", path.toStdString().c_str());
        return false;
    }
    if (!dir.isEmpty()) {
        LOG_F(WARNING, "LevelOperator::newLevel: directory is not empty: %s", path.toStdString().c_str());
        return false;
    }

    // Copy level.dat template
    QFile templateFile("../res/level.dat");
    if (!templateFile.open(QIODevice::ReadOnly)) {
        LOG_F(WARNING, "LevelOperator::newLevel: cannot open ../res/level.dat");
        return false;
    }
    QFile target(path + "/level.dat");
    if (!target.open(QIODevice::WriteOnly)) {
        LOG_F(WARNING, "LevelOperator::newLevel: cannot write level.dat");
        templateFile.close();
        return false;
    }

    target.write(templateFile.readAll());
    templateFile.close();
    target.close();

    // Create db directory
    if (!dir.mkpath("db")) {
        LOG_F(WARNING, "LevelOperator::newLevel: cannot create db directory");
        return false;
    }

    // Create a new LevelDB database
    leveldb::Options opts;
    opts.create_if_missing = true;
    leveldb::DB *db = nullptr;
    leveldb::Status status = leveldb::DB::Open(opts, dir.absoluteFilePath("db").toStdString(), &db);
    if (!status.ok()) {
        LOG_F(WARNING, "LevelOperator::newLevel: LevelDB create failed: %s", status.ToString().c_str());
        return false;
    }
    delete db;
    LOG_F(INFO, "LevelOperator::newLevel: level created at %s", path.toStdString().c_str());

    bl::bedrock_level level;
    if (!level.open(path.toStdString())) {
        LOG_F(WARNING, "Can not open the new level, created failed");
    }

    //    auto *dat = level.dat().root();

    level.close();
    return true;
}
