#ifndef BEDROCKMAP_BLOCKVOLUMEMUTATION_H
#define BEDROCKMAP_BLOCKVOLUMEMUTATION_H

#include <QRegion>
#include <QString>

struct BlockVolumeMutationResult {
    bool success{false};
    QRegion region;
    int dimension{0};
    QString error;
};

#endif  // BEDROCKMAP_BLOCKVOLUMEMUTATION_H
