#ifndef LEVELOPERATOR_H
#define LEVELOPERATOR_H

#include <qcontainerfwd.h>

#include <QPair>
#include <QString>
#include <QVector>

#include "data_3d.h"

struct NewLevelParams {
    QString levelName;
    QString path;
    QString version;
    int gameMode = 0;
    int difficulty = 1;
    bool dayNightCycle = true;
    bool weatherCycle = true;
    bool mobSpawning = true;
    bool flat = false;
    bl::biome biome = bl::biome::plains;
    QVector<QPair<QString, int>> flatBlocks;

    /// Validate parameters. Returns true if valid; on failure sets msg and returns false.
    bool check(QString &msg) const;

    /// Generate flat world settings JSON string from flatBlocks and biome.
    QString toFlatJson() const;
};

/// Static helpers for creating new Minecraft Bedrock levels.
class LevelOperator {
   public:
    /// Create a new empty level. Returns true on success.
    static bool newLevel(const NewLevelParams &p);

    static bool compressionLevel(const QString &path) { return true; }

    static bool backupLevel() { return true; }
};

#endif  // LEVELOPERATOR_H
