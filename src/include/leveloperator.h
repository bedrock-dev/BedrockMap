#ifndef LEVELOPERATOR_H
#define LEVELOPERATOR_H

#include <QString>

/// Static helpers for creating new Minecraft Bedrock levels.
class LevelOperator {
   public:
    /// Create a new empty level at the given path. Returns true on success.
    static bool newLevel(const QString &path, const QString &version, bool flat, const QString &flatBlocks);
};

#endif  // LEVELOPERATOR_H
