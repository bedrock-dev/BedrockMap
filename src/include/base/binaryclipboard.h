#ifndef BEDROCKMAP_BINARYCLIPBOARD_H
#define BEDROCKMAP_BINARYCLIPBOARD_H

#include <QByteArray>
#include <memory>

/// Shared transport for BedrockMap's binary clipboard payloads.
///
/// Desktop implementations use the system clipboard directly. Mobile
/// implementations may keep the payload in application memory and publish a
/// short marker instead.
class BinaryClipboard {
   public:
    enum class PayloadKind { ChunkRegion, Mcstructure };

    virtual ~BinaryClipboard() = default;

    virtual bool write(PayloadKind kind, const QByteArray& data) = 0;
    [[nodiscard]] virtual QByteArray read(PayloadKind kind) const = 0;

    [[nodiscard]] static std::unique_ptr<BinaryClipboard> create();
};

#endif  // BEDROCKMAP_BINARYCLIPBOARD_H
