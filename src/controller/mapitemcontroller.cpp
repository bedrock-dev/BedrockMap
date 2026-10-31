#include "controller/mapitemcontroller.h"

#include <QBuffer>
#include <QImage>
#include <QImageReader>
#include <QImageWriter>
#include <algorithm>
#include <cstring>
#include <utility>

#include "controller/nbtdocumentcontroller.h"
#include "io/documentio.h"
#include "nbt.h"

MapItemController::MapItemController(NbtDocumentController* document, QObject* parent) : QObject(parent), document_(document) {}

void MapItemController::reload() {
    QString source;
    image_ = {};
    const auto* root = document_ ? document_->treeModel()->root() : nullptr;
    const auto* tag = root ? root->get("colors") : nullptr;
    const auto* colors = tag ? tag->as<const bl::nbt::byte_array_tag*>() : nullptr;
    const int side = colors && colors->value.size() == 256 * 256 * 4 ? 256 : 128;
    const int bytes_per_line = side * 4;
    if (colors && colors->value.size() == static_cast<size_t>(side * bytes_per_line)) {
        // Bedrock map pixels are stored row by row as RGBA bytes, including alpha.
        image_ = QImage(reinterpret_cast<const uchar*>(colors->value.data()), side, side, bytes_per_line, QImage::Format_RGBA8888).copy();
        QByteArray png;
        QBuffer buffer(&png);
        if (buffer.open(QIODevice::WriteOnly) && image_.save(&buffer, "PNG")) {
            source = QStringLiteral("data:image/png;base64,") + QString::fromLatin1(png.toBase64());
        }
    }
    const auto* lock_tag = root ? root->get("mapLocked") : nullptr;
    const auto* lock = lock_tag ? lock_tag->as<const bl::nbt::byte_tag*>() : nullptr;
    const bool locked = lock && lock->value != 0;
    if (locked_ != locked) {
        locked_ = locked;
        emit lockedChanged();
    }
    if (image_source_ == source) return;
    image_source_ = std::move(source);
    emit imageSourceChanged();
}

void MapItemController::setError(const QString& error) {
    if (last_error_ == error) return;
    last_error_ = error;
    emit lastErrorChanged();
}

bl::nbt::compound_tag* MapItemController::editableRoot() {
    auto* root = document_ && !document_->readOnly() ? document_->treeModel()->root() : nullptr;
    if (!root) setError(tr("No editable map record is selected."));
    return root;
}

bool MapItemController::saveImage(const QUrl& url) {
    if (url.isEmpty()) return false;
    if (image_.isNull()) {
        setError(tr("No map image is available to save."));
        return false;
    }
    const QString error = io::DocumentIo::writeStream(url, [this](QIODevice& device) {
        QImageWriter writer(&device, "PNG");
        return writer.write(image_) ? QString() : writer.errorString();
    });
    setError(error.isEmpty() ? QString() : tr("Could not save map image: %1").arg(error));
    return error.isEmpty();
}

bool MapItemController::replaceImage(const QUrl& url) {
    if (url.isEmpty()) return false;
    const auto* root = editableRoot();
    if (!root) return false;
    const auto* tag = root->get("colors");
    if (!tag || !tag->as<const bl::nbt::byte_array_tag*>()) {
        setError(tr("The selected map has no colors byte array."));
        return false;
    }
    auto input = io::DocumentIo::openRead(url);
    if (!input.ok()) {
        setError(tr("Could not load map image: %1").arg(input.error));
        return false;
    }
    QImageReader reader(input.file.get());
    reader.setAutoTransform(true);
    const QImage source = reader.read();
    if (source.isNull()) {
        setError(tr("Could not load map image: %1").arg(reader.errorString()));
        return false;
    }
    const int crop_side = std::min(source.width(), source.height());
    constexpr int target_side = 256;
    const QImage image = source.copy((source.width() - crop_side) / 2, (source.height() - crop_side) / 2, crop_side, crop_side)
                             .scaled(target_side, target_side, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
                             .convertToFormat(QImage::Format_RGBA8888);
    if (image.isNull()) {
        setError(tr("Could not prepare map image."));
        return false;
    }
    auto colors = std::make_unique<bl::nbt::byte_array_tag>("colors");
    constexpr int stride = target_side * 4;
    colors->value.resize(target_side * stride);
    for (int y = 0; y < target_side; ++y) {
        std::memcpy(colors->value.data() + y * stride, image.constScanLine(y), stride);
    }
    if (!document_->treeModel()->setRootChild(std::move(colors))) {
        setError(tr("Could not update the map record."));
        return false;
    }
    setError({});
    return true;
}

bool MapItemController::lockMap() {
    const auto* root = editableRoot();
    if (!root) return false;
    const auto* tag = root->get("mapLocked");
    const auto* lock = tag ? tag->as<const bl::nbt::byte_tag*>() : nullptr;
    if (lock && lock->value == 1) {
        setError({});
        return true;
    }
    if (!document_->treeModel()->setRootChild(std::make_unique<bl::nbt::byte_tag>("mapLocked", 1))) {
        setError(tr("Could not update the map record."));
        return false;
    }
    setError({});
    return true;
}
