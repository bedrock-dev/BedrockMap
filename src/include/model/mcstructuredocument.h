#ifndef BEDROCKMAP_MCSTRUCTUREDOCUMENT_H
#define BEDROCKMAP_MCSTRUCTUREDOCUMENT_H

#include <QByteArray>
#include <QString>
#include <limits>
#include <memory>

#include "mcstructure.h"

namespace mcstructure_document {
    struct Data {
        QByteArray raw;
        std::shared_ptr<bl::mcstructure> structure;
        std::unique_ptr<bl::nbt::compound_tag> root;
        QString error;
        [[nodiscard]] bool valid() const { return structure && error.isEmpty(); }
    };

    /// Crop in model-local coordinates, preserving both block layers and block entities.
    [[nodiscard]] QByteArray exportData(const bl::mcstructure& structure, const bl::block_box& selection,
                                       bool exportEntities, bool useNewFormat);
    [[nodiscard]] Data parse(QByteArray raw, qsizetype treeLimit = std::numeric_limits<qsizetype>::max());
    [[nodiscard]] Data load(const QString& path, qsizetype treeLimit = std::numeric_limits<qsizetype>::max());
}  // namespace mcstructure_document

#endif  // BEDROCKMAP_MCSTRUCTUREDOCUMENT_H
