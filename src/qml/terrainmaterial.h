#ifndef BEDROCKMAP_TERRAINMATERIAL_H
#define BEDROCKMAP_TERRAINMATERIAL_H

#include <QImage>
#include <QMutex>
#include <QPoint>
#include <QSize>
#include <QSGMaterial>
#include <QSGTexture>
#include <QVector>
#include <array>
#include <memory>

#include "render/rendertimings.h"

class QRhi;
class QRhiResourceUpdateBatch;
class QRhiTexture;

struct TerrainAtlasUpload {
    QImage image;
    QPoint destination;
    int layer{0};
};

struct TerrainArrayUniformBody {
    std::array<float, 4> view{0.0f, 0.0f, 1.0f, 1.0f};
    std::array<float, 4> params{128.0f, 256.0f, 0.0f, 1.0f};
    std::array<float, 4> shade{-1.0f, -1.0f, 0.0f, 8.0f};
    std::array<float, 4> shadow{0.45f, 0.7f, 0.25f, 0.0f};
    std::array<float, 4> ao{0.15f, 8.0f, 8.0f, 16.0f};
    std::array<float, 4> tint{1.0f, 1.0f, 1.0f, 0.0f};
    std::array<float, 4> water{0.64f, 0.64f, 0.64f, 0.0f};
    std::array<float, 4> grass{80.0f, 320.0f, 0.0f, 8.0f};
    std::array<float, 4> grass_color{1.0f, 0.0f, 0.0f, 0.0f};
    std::array<float, 4> overview{0.0f, 0.0f, 0.0f, 0.0f};
    std::array<float, 4> checker{20.0f / 255.0f, 20.0f / 255.0f, 20.0f / 255.0f, 1.0f};
    std::array<float, 4> checker_alternate{40.0f / 255.0f, 40.0f / 255.0f, 40.0f / 255.0f, 1.0f};
    std::array<float, 4> void_options{0.0f, 0.0f, 0.0f, 0.0f};
};

class TerrainArrayMaterial final : public QSGMaterial {
   public:
    TerrainArrayMaterial(QSGTexture* color, QSGTexture* height, QSGTexture* material, QSGTexture* page_table,
                         QSGTexture* overview, QSGTexture* palette);
    ~TerrainArrayMaterial() override;
    [[nodiscard]] QSGTexture* texture(int binding) const;
    void setUniforms(const TerrainArrayUniformBody& body) { body_ = body; }
    [[nodiscard]] const TerrainArrayUniformBody& uniforms() const { return body_; }
    QSGMaterialType* type() const override;
    QSGMaterialShader* createShader(QSGRendererInterface::RenderMode) const override;

   private:
    TerrainArrayUniformBody body_;
    QSGTexture* color_{nullptr};
    QSGTexture* height_{nullptr};
    QSGTexture* material_{nullptr};
    QSGTexture* page_table_{nullptr};
    QSGTexture* overview_{nullptr};
    QSGTexture* palette_{nullptr};
};

/// QSG texture backed by a QRhi texture with queued sub-region uploads.
///
/// Qt never commits a texture that carries a custom RHI texture by itself: the
/// owning material calls commitTextureOperations() from updateSampledImage(), as
/// QSGTexture's documentation requires, which Qt calls again on every frame the
/// material is drawn. updateTexture() is therefore only the "is there pending
/// work" query here. The commit runs on the scene graph's render thread while
/// queueUploads() runs during updatePaintNode() on the render thread as well.
/// The pending queue retains shared QImages until the RHI batch owns them.
class TerrainAtlasTexture final : public QSGDynamicTexture {
   public:
    TerrainAtlasTexture(const QImage& image, QObject* parent = nullptr);
    explicit TerrainAtlasTexture(const QSize& atlas_size, int array_size = 1, QObject* parent = nullptr);
    void setTimings(const std::shared_ptr<RenderTimings>& timings) { timings_ = timings; }
    ~TerrainAtlasTexture() override;

    qint64 comparisonKey() const override;
    QRhiTexture* rhiTexture() const override;
    QSize textureSize() const override;
    bool hasAlphaChannel() const override { return false; }
    bool hasMipmaps() const override { return false; }
    bool updateTexture() override;
    void commitTextureOperations(QRhi* rhi, QRhiResourceUpdateBatch* updates) override;

    /// Transfer complete tile images with one lock per texture (render thread).
    void queueUploads(QVector<TerrainAtlasUpload> uploads);
    /// Drop queued tile uploads that belong to a renderer state being replaced.
    /// The GPU texture keeps its previous contents; the caller must cover every
    /// slot it is about to sample before the next commit.
    void discardPendingUploads();

    /// True while uploads are queued but not committed to the GPU yet. The
    /// owning node keeps its material dirty for as long as this holds, because
    /// the commit only happens when the scene graph re-queries the samplers.
    [[nodiscard]] bool hasPendingUploads() const;
    /// Queued sub-region uploads not committed yet.
    [[nodiscard]] int pendingUploadCount() const;

   private:
    QSize size_;
    int array_size_{1};
    std::shared_ptr<RenderTimings> timings_;
    QRhiTexture* texture_{nullptr};
    mutable QMutex mutex_;
    QImage pending_image_;
    bool pending_full_upload_{true};
    QVector<TerrainAtlasUpload> pending_uploads_;
};

#endif  // BEDROCKMAP_TERRAINMATERIAL_H
