#include "terrainmaterial.h"

#include <rhi/qrhi.h>

#include <QFile>
#include <QMatrix4x4>
#include <QMutexLocker>
#include <QSGMaterialShader>
#include <algorithm>
#include <array>
#include <cstring>
#include <utility>

#include "loguru/loguru.hpp"

// --- TerrainAtlasTexture -----------------------------------------------------

TerrainAtlasTexture::TerrainAtlasTexture(const QImage& image, QObject* parent)
    : QSGDynamicTexture(), size_(image.size()), pending_image_(image.convertToFormat(QImage::Format_RGBA8888)) {
    Q_UNUSED(parent);
}

TerrainAtlasTexture::TerrainAtlasTexture(const QSize& atlas_size, int array_size, QObject* parent)
    : QSGDynamicTexture(), size_(atlas_size), array_size_(std::max(1, array_size)), pending_full_upload_(false) {
    Q_UNUSED(parent);
}

TerrainAtlasTexture::~TerrainAtlasTexture() { delete texture_; }

qint64 TerrainAtlasTexture::comparisonKey() const { return reinterpret_cast<qint64>(this); }

QRhiTexture* TerrainAtlasTexture::rhiTexture() const { return texture_; }

QSize TerrainAtlasTexture::textureSize() const { return size_; }

bool TerrainAtlasTexture::updateTexture() {
    const QMutexLocker lock(&mutex_);
    return pending_full_upload_ || !pending_uploads_.isEmpty();
}

void TerrainAtlasTexture::queueUploads(QVector<TerrainAtlasUpload> uploads) {
    if (uploads.isEmpty()) return;
    for (auto& upload : uploads) {
        if (upload.image.format() != QImage::Format_RGBA8888) upload.image = upload.image.convertToFormat(QImage::Format_RGBA8888);
    }
    const QMutexLocker lock(&mutex_);
    pending_uploads_.append(std::move(uploads));
}

bool TerrainAtlasTexture::hasPendingUploads() const {
    const QMutexLocker lock(&mutex_);
    return pending_full_upload_ || !pending_uploads_.isEmpty();
}

void TerrainAtlasTexture::commitTextureOperations(QRhi* rhi, QRhiResourceUpdateBatch* updates) {
    const RenderTimingScope timing(timings_.get(), QStringLiteral("RHI texture commit"));
    // Only the first commit needs pending_image_: it is the whole atlas. Later
    // commits carry sub-region uploads only, so they must not be skipped just
    // because the full image has already been released.
    if (!rhi || !updates) return;
    const QMutexLocker lock(&mutex_);
    if (!texture_) {
        if (size_.isEmpty()) return;
        texture_ =
            array_size_ > 1 ? rhi->newTextureArray(QRhiTexture::RGBA8, array_size_, size_) : rhi->newTexture(QRhiTexture::RGBA8, size_);
        if (!texture_ || !texture_->create()) {
            LOG_F(ERROR, "Terrain atlas texture creation failed: %d x %d, backend %d, maximum texture size %d", size_.width(),
                  size_.height(), static_cast<int>(rhi->backend()), rhi->resourceLimit(QRhi::TextureSizeMax));
            delete texture_;
            texture_ = nullptr;
            pending_image_ = {};
            pending_uploads_.clear();
            pending_full_upload_ = false;
            return;
        }
        pending_full_upload_ = true;
    }

    if (!pending_full_upload_ && pending_uploads_.isEmpty()) return;
    // Qt 6.11 stores 16 upload entries inline. Crash dumps point to sizedFree
    // during growth of larger descriptions. Keep each description inline.
    constexpr qsizetype uploadBatchSize = 16;
    const bool fullImage = pending_full_upload_ && !pending_image_.isNull();
    const qsizetype entryCount = pending_uploads_.size() + (fullImage ? 1 : 0);
    for (qsizetype first = 0; first < entryCount; first += uploadBatchSize) {
        RenderTimingScope stage(timings_.get(), QStringLiteral("Build upload batch"));
        const qsizetype count = std::min(uploadBatchSize, entryCount - first);
        std::array<QRhiTextureUploadEntry, uploadBatchSize> entries;
        for (qsizetype i = 0; i < count; ++i) {
            const qsizetype index = first + i;
            const bool isFull = fullImage && index == 0;
            const QImage& image = isFull ? pending_image_ : pending_uploads_[index - (fullImage ? 1 : 0)].image;
            QRhiTextureSubresourceUploadDescription subresource(image);
            subresource.setDestinationTopLeft(isFull ? QPoint(0, 0) : pending_uploads_[index - (fullImage ? 1 : 0)].destination);
            subresource.setSourceSize(image.size());
            const int layer = isFull ? 0 : pending_uploads_[index - (fullImage ? 1 : 0)].layer;
            entries[i] = QRhiTextureUploadEntry(layer, 0, subresource);
        }
        stage.next(QStringLiteral("Enqueue upload batch"));
        QRhiTextureUploadDescription description;
        description.setEntries(entries.cbegin(), entries.cbegin() + count);
        updates->uploadTexture(texture_, description);
    }
    const RenderTimingScope release(timings_.get(), QStringLiteral("Release upload queue"));
    pending_image_ = {};
    pending_uploads_.clear();
    pending_full_upload_ = false;
}

namespace {
    class TerrainArrayMaterialShader final : public QSGMaterialShader {
       public:
        TerrainArrayMaterialShader() {
            setShaderFileName(VertexStage, QStringLiteral(":/bedrockmap/shaders/terrainarray.vert.qsb"));
            setShaderFileName(FragmentStage, QStringLiteral(":/bedrockmap/shaders/terrainarray.frag.qsb"));
        }

        bool updateUniformData(RenderState& state, QSGMaterial* newMaterial, QSGMaterial*) override {
            auto* material = static_cast<TerrainArrayMaterial*>(newMaterial);
            QByteArray* data = state.uniformData();
            if (!data) return false;
            constexpr int matrixBytes = 16 * static_cast<int>(sizeof(float));
            const int bodyBytes = static_cast<int>(sizeof(TerrainArrayUniformBody));
            if (data->size() < matrixBytes + bodyBytes) return false;
            const QMatrix4x4 matrix = state.combinedMatrix();
            std::memcpy(data->data(), matrix.constData(), matrixBytes);
            std::memcpy(data->data() + matrixBytes, &material->uniforms(), bodyBytes);
            return true;
        }

        void updateSampledImage(RenderState& state, int binding, QSGTexture** texture, QSGMaterial* newMaterial, QSGMaterial*) override {
            auto* material = static_cast<TerrainArrayMaterial*>(newMaterial);
            QSGTexture* bound = material->texture(binding);
            if (bound) bound->commitTextureOperations(state.rhi(), state.resourceUpdateBatch());
            *texture = bound;
        }
    };
    QSGMaterialType arrayType;
}  // namespace

TerrainArrayMaterial::TerrainArrayMaterial(QSGTexture* color, QSGTexture* surface, QSGTexture* page_table, QSGTexture* overview,
                                           QSGTexture* palette)
    : color_(color), surface_(surface), page_table_(page_table), overview_(overview), palette_(palette) {
    setFlag(Blending, false);
}

TerrainArrayMaterial::~TerrainArrayMaterial() {
    delete color_;
    delete surface_;
    delete page_table_;
    delete overview_;
    delete palette_;
}

QSGTexture* TerrainArrayMaterial::texture(int binding) const {
    if (binding == 1) return color_;
    if (binding == 2) return surface_;
    if (binding == 3) return page_table_;
    if (binding == 4) return overview_;
    if (binding == 5) return palette_;
    return nullptr;
}

QSGMaterialType* TerrainArrayMaterial::type() const { return &arrayType; }

QSGMaterialShader* TerrainArrayMaterial::createShader(QSGRendererInterface::RenderMode) const { return new TerrainArrayMaterialShader; }
