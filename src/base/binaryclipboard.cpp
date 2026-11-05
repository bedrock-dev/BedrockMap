#include "base/binaryclipboard.h"

#include <QClipboard>
#include <QGuiApplication>
#include <QMimeData>
#include <QUuid>
#include <utility>

#include "base/clipboarddata.h"

namespace {
    QString kindName(BinaryClipboard::PayloadKind kind) {
        return kind == BinaryClipboard::PayloadKind::ChunkRegion ? QStringLiteral("region")
                                                                 : QStringLiteral("mcstructure");
    }

    class SystemBinaryClipboard final : public BinaryClipboard {
       public:
        bool write(PayloadKind kind, const QByteArray& data) override {
            auto* clipboard = QGuiApplication::clipboard();
            if (!clipboard || data.isEmpty()) return false;

            auto mime_data = std::make_unique<QMimeData>();
            if (kind == PayloadKind::ChunkRegion)
                clipboard_data::writeRegion(*mime_data, data);
            else
                clipboard_data::writeMcstructure(*mime_data, data);
            clipboard->setMimeData(mime_data.release(), QClipboard::Clipboard);
            return true;
        }

        [[nodiscard]] QByteArray read(PayloadKind kind) const override {
            const auto* clipboard = QGuiApplication::clipboard();
            if (!clipboard) return {};
            const auto* mime_data = clipboard->mimeData(QClipboard::Clipboard);
            return kind == PayloadKind::ChunkRegion ? clipboard_data::readRegion(mime_data)
                                                    : clipboard_data::readMcstructure(mime_data);
        }
    };

    class MemoryBinaryClipboard final : public BinaryClipboard {
       public:
        bool write(PayloadKind kind, const QByteArray& data) override {
            auto* clipboard = QGuiApplication::clipboard();
            if (!clipboard || data.isEmpty()) return false;

            const QString next_marker =
                QStringLiteral("BedrockMap/clipboard:v2:%1:%2")
                    .arg(kindName(kind), QUuid::createUuid().toString(QUuid::WithoutBraces));
            data_ = data;
            kind_ = kind;
            marker_ = next_marker;
            clipboard->setText(marker_, QClipboard::Clipboard);
            return true;
        }

        [[nodiscard]] QByteArray read(PayloadKind kind) const override {
            const auto* clipboard = QGuiApplication::clipboard();
            if (!clipboard) return {};
            const auto* mime_data = clipboard->mimeData(QClipboard::Clipboard);

            if (kind == kind_ && !marker_.isEmpty() && clipboard->text(QClipboard::Clipboard) == marker_) return data_;

            // Keep older Base64 text payloads readable after upgrading.
            return kind == PayloadKind::ChunkRegion ? clipboard_data::readRegion(mime_data)
                                                    : clipboard_data::readMcstructure(mime_data);
        }

       private:
        QByteArray data_;
        PayloadKind kind_{PayloadKind::ChunkRegion};
        QString marker_;
    };
}  // namespace

std::unique_ptr<BinaryClipboard> BinaryClipboard::create() {
#ifdef Q_OS_ANDROID
    return std::make_unique<MemoryBinaryClipboard>();
#else
    return std::make_unique<SystemBinaryClipboard>();
#endif
}
