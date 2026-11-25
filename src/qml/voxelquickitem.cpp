#include "voxelquickitem.h"

#include <rhi/qrhi.h>
#include <rhi/qshader.h>

#include <QFile>
#include <QMatrix4x4>
#include <algorithm>
#include <array>
#include <cstring>
#include <memory>

namespace {
    constexpr int vertexStride = VOXEL_VERTEX_FLOATS * sizeof(float);
    constexpr int uniformSize = 80;

    QShader loadShader(const QString& path) {
        QFile file(path);
        return file.open(QIODevice::ReadOnly) ? QShader::fromSerialized(file.readAll()) : QShader{};
    }

    using RhiBuffer = std::unique_ptr<QRhiBuffer>;
    using RhiPipeline = std::unique_ptr<QRhiGraphicsPipeline>;

    RhiBuffer createBuffer(QRhi* rhi, QRhiBuffer::Type type, QRhiBuffer::UsageFlags usage, quint32 bytes) {
        RhiBuffer buffer(rhi->newBuffer(type, usage, bytes));
        return buffer->create() ? std::move(buffer) : RhiBuffer{};
    }

    /// Immutable indexed triangles in the voxel vertex layout.
    struct IndexedMesh {
        RhiBuffer vertices;
        RhiBuffer indices;
        quint32 count{0};

        void upload(QRhi* rhi, QRhiResourceUpdateBatch* updates, const std::vector<float>& vertexData,
                    const std::vector<std::uint32_t>& indexData) {
            *this = {};
            if (vertexData.empty() || indexData.empty()) return;
            vertices =
                createBuffer(rhi, QRhiBuffer::Immutable, QRhiBuffer::VertexBuffer, static_cast<quint32>(vertexData.size() * sizeof(float)));
            indices = createBuffer(rhi, QRhiBuffer::Immutable, QRhiBuffer::IndexBuffer,
                                   static_cast<quint32>(indexData.size() * sizeof(std::uint32_t)));
            if (!vertices || !indices) return;
            updates->uploadStaticBuffer(vertices.get(), vertexData.data());
            updates->uploadStaticBuffer(indices.get(), indexData.data());
            count = static_cast<quint32>(indexData.size());
        }
    };

    /// Growable dynamic buffer holding one VoxelOverlayMeshBuffers batch.
    struct OverlayBuffer {
        RhiBuffer buffer;
        quint32 fillCount{0};
        quint32 lineCount{0};
        quint32 vertexCount{0};

        void clear() { fillCount = lineCount = vertexCount = 0; }

        void upload(QRhi* rhi, QRhiResourceUpdateBatch* updates, const VoxelOverlayMeshBuffers& mesh) {
            clear();
            const auto bytes = static_cast<quint32>(mesh.vertices.size() * sizeof(float));
            if (!bytes) return;
            if (!buffer || buffer->size() < bytes) buffer = createBuffer(rhi, QRhiBuffer::Dynamic, QRhiBuffer::VertexBuffer, bytes);
            if (!buffer) return;
            updates->updateDynamicBuffer(buffer.get(), 0, bytes, mesh.vertices.data());
            fillCount = mesh.fillCount;
            lineCount = mesh.lineCount;
            vertexCount = mesh.vertexCount();
        }
    };

    /// One uniform buffer plus its bindings; the layout matches voxelrhi.vert.
    struct UniformBinding {
        RhiBuffer buffer;
        std::unique_ptr<QRhiShaderResourceBindings> bindings;

        void create(QRhi* rhi) {
            if (bindings) return;
            buffer = createBuffer(rhi, QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, uniformSize);
            if (!buffer) return;
            bindings.reset(rhi->newShaderResourceBindings());
            bindings->setBindings({QRhiShaderResourceBinding::uniformBuffer(
                0, QRhiShaderResourceBinding::VertexStage | QRhiShaderResourceBinding::FragmentStage, buffer.get(), 0, uniformSize)});
            if (!bindings->create()) bindings.reset();
        }

        void update(QRhiResourceUpdateBatch* updates, const QMatrix4x4& mvp, float brightness, float saturation) const {
            if (!buffer) return;
            std::array<float, uniformSize / sizeof(float)> data{};
            std::memcpy(data.data(), mvp.constData(), 16 * sizeof(float));
            data[16] = brightness;
            data[17] = saturation;
            updates->updateDynamicBuffer(buffer.get(), 0, uniformSize, data.data());
        }
    };

    class VoxelRhiRenderer final : public QQuickRhiItemRenderer {
       protected:
        void initialize(QRhiCommandBuffer*) override {
            QRhi* rhi = this->rhi();
            if (!rhi || !renderTarget() || !renderTarget()->renderPassDescriptor()) return;

            const QShader vertexShader = loadShader(QStringLiteral(":/bedrockmap/shaders/voxelrhi.vert.qsb"));
            const QShader fragmentShader = loadShader(QStringLiteral(":/bedrockmap/shaders/voxelrhi.frag.qsb"));
            if (!vertexShader.isValid() || !fragmentShader.isValid()) return;

            scene_uniforms_.create(rhi);
            import_uniforms_.create(rhi);
            if (!scene_uniforms_.bindings) return;

            QRhiVertexInputLayout layout;
            layout.setBindings({QRhiVertexInputBinding(vertexStride)});
            layout.setAttributes({
                QRhiVertexInputAttribute(0, 0, QRhiVertexInputAttribute::Float3, 0),
                QRhiVertexInputAttribute(0, 1, QRhiVertexInputAttribute::Float3, 3 * sizeof(float)),
                QRhiVertexInputAttribute(0, 2, QRhiVertexInputAttribute::Float4, 6 * sizeof(float)),
            });

            // The item can initialize again when its render target is resized;
            // assigning a new pipeline releases the one built for the old target.
            const auto createPipeline = [&](bool blend, bool depthTest, QRhiGraphicsPipeline::Topology topology,
                                            QRhiGraphicsPipeline::CompareOp depthOp) {
                RhiPipeline pipeline(rhi->newGraphicsPipeline());
                pipeline->setShaderStages({
                    QRhiShaderStage(QRhiShaderStage::Vertex, vertexShader),
                    QRhiShaderStage(QRhiShaderStage::Fragment, fragmentShader),
                });
                pipeline->setVertexInputLayout(layout);
                pipeline->setShaderResourceBindings(scene_uniforms_.bindings.get());
                pipeline->setRenderPassDescriptor(renderTarget()->renderPassDescriptor());
                pipeline->setSampleCount(renderTarget()->sampleCount());
                pipeline->setTopology(topology);
                pipeline->setCullMode(QRhiGraphicsPipeline::None);
                pipeline->setDepthTest(depthTest);
                pipeline->setDepthWrite(!blend);
                pipeline->setDepthOp(depthOp);
                if (topology == QRhiGraphicsPipeline::Lines && rhi->isFeatureSupported(QRhi::WideLines)) pipeline->setLineWidth(2.5f);
                QRhiGraphicsPipeline::TargetBlend targetBlend;
                targetBlend.enable = blend;
                targetBlend.srcColor = QRhiGraphicsPipeline::SrcAlpha;
                targetBlend.dstColor = QRhiGraphicsPipeline::OneMinusSrcAlpha;
                targetBlend.opColor = QRhiGraphicsPipeline::Add;
                pipeline->setTargetBlends({targetBlend});
                return pipeline->create() ? std::move(pipeline) : RhiPipeline{};
            };
            opaque_pipeline_ = createPipeline(false, true, QRhiGraphicsPipeline::Triangles, QRhiGraphicsPipeline::Less);
            transparent_pipeline_ = createPipeline(true, true, QRhiGraphicsPipeline::Triangles, QRhiGraphicsPipeline::Less);
            fill_pipeline_ = createPipeline(true, true, QRhiGraphicsPipeline::Triangles, QRhiGraphicsPipeline::LessOrEqual);
            line_pipeline_ = createPipeline(true, false, QRhiGraphicsPipeline::Lines, QRhiGraphicsPipeline::Always);
            grid_pipeline_ = createPipeline(true, true, QRhiGraphicsPipeline::Lines, QRhiGraphicsPipeline::LessOrEqual);
            handle_pipeline_ = createPipeline(true, false, QRhiGraphicsPipeline::Triangles, QRhiGraphicsPipeline::Always);
        }

        void synchronize(QQuickRhiItem* item) override {
            auto* voxelItem = static_cast<VoxelQuickItem*>(item);
            scene_ = voxelItem->currentScene();
            import_scene_ = voxelItem->currentOverlayScene();
            hsa_areas_ = voxelItem->hsaAreasState();
            camera_ = voxelItem->cameraState();
            selection_ = voxelItem->selectionState();
            saturation_ = voxelItem->saturation();
            brightness_ = voxelItem->brightness();
            show_axes_ = voxelItem->showAxes();
            logical_size_ = QSizeF(item->width(), item->height());
            render_size_ = item->effectiveColorBufferSize();
        }

        void render(QRhiCommandBuffer* command_buffer) override {
            QRhi* rhi = this->rhi();
            if (!rhi || !renderTarget()) return;

            QRhiResourceUpdateBatch* updates = rhi->nextResourceUpdateBatch();
            if (scene_) {
                prepareScene(rhi, updates);
            } else {
                releaseSceneMeshes();
            }
            command_buffer->beginPass(renderTarget(), QColor(25, 30, 38), QRhiDepthStencilClearValue(1.0f, 0), updates);
            command_buffer->setViewport(
                QRhiViewport(0, 0, static_cast<float>(render_size_.width()), static_cast<float>(render_size_.height())));
            if (scene_) {
                auto* sceneBindings = scene_uniforms_.bindings.get();
                drawIndexed(command_buffer, opaque_pipeline_.get(), sceneBindings, opaque_);
                drawIndexed(command_buffer, transparent_pipeline_.get(), sceneBindings, transparent_);
                if (import_scene_) drawIndexed(command_buffer, transparent_pipeline_.get(), import_uniforms_.bindings.get(), import_);
                drawGrid(command_buffer, grid_);
                drawOverlay(command_buffer, hsa_);
                drawOverlay(command_buffer, axes_);
                drawOverlay(command_buffer, selection_mesh_);
            }
            command_buffer->endPass();
        }

       private:
        void prepareScene(QRhi* rhi, QRhiResourceUpdateBatch* updates) {
            if (scene_ != uploaded_scene_) {
                uploaded_scene_ = scene_;
                opaque_.upload(rhi, updates, scene_->mesh.opaqueVertices, scene_->mesh.opaqueIndices);
                transparent_.upload(rhi, updates, scene_->mesh.transparentVertices, scene_->mesh.transparentIndices);
            }

            const QMatrix4x4 matrix = camera_.matrix(scene_->modelSize, logical_size_);
            scene_uniforms_.update(updates, rhi->clipSpaceCorrMatrix() * matrix, brightness_, saturation_);

            if (import_scene_) {
                if (import_scene_ != uploaded_import_scene_) uploadImport(rhi, updates);
                QMatrix4x4 importMatrix = matrix;
                importMatrix.translate(selection_.bounds.min_pos.x, selection_.bounds.min_pos.y, selection_.bounds.min_pos.z);
                import_uniforms_.update(updates, rhi->clipSpaceCorrMatrix() * importMatrix, brightness_, saturation_);
            } else if (uploaded_import_scene_) {
                import_ = {};
                uploaded_import_scene_.reset();
            }

            if (!show_axes_) {
                axes_.clear();
                axes_model_size_ = {};
            } else if (axes_model_size_ != scene_->modelSize || !axes_.vertexCount) {
                axes_model_size_ = scene_->modelSize;
                axes_.upload(rhi, updates, VoxelOverlayMeshBuffers::makeAxes(scene_->modelSize));
            }

            if (grid_model_size_ != scene_->modelSize || !grid_.vertexCount) {
                grid_model_size_ = scene_->modelSize;
                grid_.upload(rhi, updates, VoxelOverlayMeshBuffers::makeChunkGrid(scene_->modelSize));
            }

            if (!hsa_areas_ || hsa_areas_->empty()) {
                hsa_.clear();
                uploaded_hsa_areas_.reset();
            } else if (hsa_areas_ != uploaded_hsa_areas_) {
                uploaded_hsa_areas_ = hsa_areas_;
                hsa_.upload(rhi, updates, VoxelOverlayMeshBuffers::makeHsaAreas(*hsa_areas_));
            }

            // Handle sizes depend on the camera, so the selection is rebuilt every frame.
            if (selection_.enabled && selection_.bounds.is_valid())
                selection_mesh_.upload(rhi, updates,
                                       VoxelOverlayMeshBuffers::makeSelection(selection_.bounds, {matrix, logical_size_},
                                                                              selection_.color, selection_.active));
            else
                selection_mesh_.clear();
        }

        /// The import preview is one translucent mesh: both halves merged, alpha scaled down.
        void uploadImport(QRhi* rhi, QRhiResourceUpdateBatch* updates) {
            uploaded_import_scene_ = import_scene_;
            constexpr float IMPORT_ALPHA = 0.42f;
            std::vector<float> vertices;
            std::vector<std::uint32_t> indices;
            const auto append = [&](const std::vector<float>& sourceVertices, const std::vector<std::uint32_t>& sourceIndices) {
                const auto base = static_cast<std::uint32_t>(vertices.size() / VOXEL_VERTEX_FLOATS);
                const size_t first = vertices.size();
                vertices.insert(vertices.end(), sourceVertices.begin(), sourceVertices.end());
                for (size_t alpha = first + 9; alpha < vertices.size(); alpha += VOXEL_VERTEX_FLOATS) vertices[alpha] *= IMPORT_ALPHA;
                for (const auto index : sourceIndices) indices.push_back(base + index);
            };
            const auto& mesh = import_scene_->mesh;
            append(mesh.opaqueVertices, mesh.opaqueIndices);
            append(mesh.transparentVertices, mesh.transparentIndices);
            import_.upload(rhi, updates, vertices, indices);
        }

        void drawIndexed(QRhiCommandBuffer* command_buffer, QRhiGraphicsPipeline* pipeline, QRhiShaderResourceBindings* bindings,
                         const IndexedMesh& mesh) const {
            if (!pipeline || !bindings || !mesh.count) return;
            const QRhiCommandBuffer::VertexInput input(mesh.vertices.get(), 0);
            command_buffer->setGraphicsPipeline(pipeline);
            command_buffer->setShaderResources(bindings);
            command_buffer->setVertexInput(0, 1, &input, mesh.indices.get(), 0, QRhiCommandBuffer::IndexUInt32);
            command_buffer->drawIndexed(mesh.count);
        }

        void drawOverlay(QRhiCommandBuffer* command_buffer, const OverlayBuffer& overlay) const {
            if (!overlay.vertexCount) return;
            const QRhiCommandBuffer::VertexInput input(overlay.buffer.get(), 0);
            const auto draw = [&](QRhiGraphicsPipeline* pipeline, quint32 count, quint32 first) {
                if (!pipeline || !count) return;
                command_buffer->setGraphicsPipeline(pipeline);
                command_buffer->setShaderResources(scene_uniforms_.bindings.get());
                command_buffer->setVertexInput(0, 1, &input);
                command_buffer->draw(count, 1, first);
            };
            draw(fill_pipeline_.get(), overlay.fillCount, 0);
            draw(line_pipeline_.get(), overlay.lineCount, overlay.fillCount);
            draw(handle_pipeline_.get(), overlay.vertexCount - overlay.fillCount - overlay.lineCount,
                 overlay.fillCount + overlay.lineCount);
        }

        void drawGrid(QRhiCommandBuffer* command_buffer, const OverlayBuffer& grid) const {
            if (!grid_pipeline_ || !grid.lineCount) return;
            const QRhiCommandBuffer::VertexInput input(grid.buffer.get(), 0);
            command_buffer->setGraphicsPipeline(grid_pipeline_.get());
            command_buffer->setShaderResources(scene_uniforms_.bindings.get());
            command_buffer->setVertexInput(0, 1, &input);
            command_buffer->draw(grid.lineCount, 1, 0);
        }

        void releaseSceneMeshes() {
            opaque_ = {};
            transparent_ = {};
            import_ = {};
            hsa_.clear();
            axes_.clear();
            grid_.clear();
            selection_mesh_.clear();
            uploaded_scene_.reset();
            uploaded_import_scene_.reset();
            uploaded_hsa_areas_.reset();
            axes_model_size_ = {};
            grid_model_size_ = {};
        }

        // State copied from the item in synchronize().
        std::shared_ptr<const VoxelSceneData> scene_;
        std::shared_ptr<const VoxelSceneData> import_scene_;
        std::shared_ptr<const std::vector<VoxelHsaArea>> hsa_areas_;
        VoxelCameraState camera_;
        VoxelSelectionState selection_;
        float saturation_{1.0f};
        float brightness_{1.0f};
        bool show_axes_{false};
        QSize render_size_;
        QSizeF logical_size_;

        // What the GPU buffers currently hold.
        std::shared_ptr<const VoxelSceneData> uploaded_scene_;
        std::shared_ptr<const VoxelSceneData> uploaded_import_scene_;
        std::shared_ptr<const std::vector<VoxelHsaArea>> uploaded_hsa_areas_;
        QVector3D axes_model_size_;
        QVector3D grid_model_size_;

        UniformBinding scene_uniforms_;
        UniformBinding import_uniforms_;
        RhiPipeline opaque_pipeline_;
        RhiPipeline transparent_pipeline_;
        RhiPipeline fill_pipeline_;
        RhiPipeline line_pipeline_;
        RhiPipeline grid_pipeline_;
        RhiPipeline handle_pipeline_;
        IndexedMesh opaque_;
        IndexedMesh transparent_;
        IndexedMesh import_;
        OverlayBuffer hsa_;
        OverlayBuffer axes_;
        OverlayBuffer grid_;
        OverlayBuffer selection_mesh_;
    };
}  // namespace

VoxelQuickItem::VoxelQuickItem(QQuickItem* parent) : QQuickRhiItem(parent) {
    setSampleCount(4);
    setAlphaBlending(false);
}

void VoxelQuickItem::setController(VoxelSceneController* controller) {
    if (controller_ == controller) return;
    if (scene_connection_) disconnect(scene_connection_);
    if (overlay_connection_) disconnect(overlay_connection_);
    controller_ = controller;
    if (controller_) {
        scene_connection_ = connect(controller_, &VoxelSceneController::voxelChanged, this, &VoxelQuickItem::syncScene);
        overlay_connection_ = connect(controller_, &VoxelSceneController::overlayChanged, this, &VoxelQuickItem::syncOverlay);
    }
    syncScene();
    syncOverlay();
    emit controllerChanged();
}

std::shared_ptr<const VoxelSceneData> VoxelQuickItem::currentScene() const { return scene_; }

std::shared_ptr<const VoxelSceneData> VoxelQuickItem::currentOverlayScene() const { return overlay_scene_; }

bool VoxelQuickItem::orthoMode() const { return view_state_.orthoMode(); }

void VoxelQuickItem::setOrthoMode(bool ortho) {
    if (!view_state_.setOrthoMode(ortho)) return;
    emit viewOptionsChanged();
    update();
}

bool VoxelQuickItem::rotationLocked() const { return view_state_.rotationLocked(); }

void VoxelQuickItem::setRotationLocked(bool locked) {
    if (!view_state_.setRotationLocked(locked)) return;
    emit viewOptionsChanged();
    update();
}

bool VoxelQuickItem::showAxes() const { return show_axes_; }

void VoxelQuickItem::setShowAxes(bool enabled) {
    if (show_axes_ == enabled) return;
    show_axes_ = enabled;
    emit axesChanged();
    update();
}

QVector3D VoxelQuickItem::modelSize() const { return scene_ ? scene_->modelSize : QVector3D{}; }

QVector3D VoxelQuickItem::modelOrigin() const { return scene_ ? scene_->origin : QVector3D{}; }

VoxelSelectionState VoxelQuickItem::selectionState() const { return view_state_.selection(); }

bool VoxelQuickItem::selectionEnabled() const { return view_state_.selection().enabled; }

void VoxelQuickItem::setSelectionEnabled(bool enabled) {
    if (!view_state_.setSelectionEnabled(enabled)) return;
    emit selectionOptionsChanged();
    emit selectionInteractionChanged();
    update();
}

bool VoxelQuickItem::selectionMoveMode() const { return view_state_.selectionMoveMode(); }

void VoxelQuickItem::setSelectionMoveMode(bool enabled) {
    if (!view_state_.setSelectionMoveMode(enabled)) return;
    emit selectionOptionsChanged();
    emit selectionInteractionChanged();
    update();
}

QColor VoxelQuickItem::selectionColor() const { return view_state_.selection().color; }

void VoxelQuickItem::setSelectionColor(const QColor& color) {
    if (!view_state_.setSelectionColor(color)) return;
    emit selectionOptionsChanged();
    update();
}

QVector3D VoxelQuickItem::selectionMinimum() const {
    const auto& p = view_state_.selection().bounds.min_pos;
    return QVector3D(p.x, p.y, p.z);
}

QVector3D VoxelQuickItem::selectionMaximum() const {
    const auto& p = view_state_.selection().bounds.max_pos;
    return QVector3D(p.x, p.y, p.z);
}

bool VoxelQuickItem::selectionDragging() const { return view_state_.selection().active != VoxelSelectionHandle::None; }

void VoxelQuickItem::setHsaAreas(const QVariantList& values) {
    std::vector<VoxelHsaArea> areas;
    areas.reserve(static_cast<size_t>(values.size()));
    for (const auto& value : values) {
        const auto area = value.toMap();
        const bl::block_box bounds{
            bl::block_pos{
                area.value(QStringLiteral("minX")).toInt(),
                area.value(QStringLiteral("minY")).toInt(),
                area.value(QStringLiteral("minZ")).toInt(),
            },
            bl::block_pos{
                area.value(QStringLiteral("maxX")).toInt(),
                area.value(QStringLiteral("maxY")).toInt(),
                area.value(QStringLiteral("maxZ")).toInt(),
            },
        };
        const QColor color = area.value(QStringLiteral("color")).value<QColor>();
        if (!bounds.is_valid() || !color.isValid()) continue;
        areas.push_back(VoxelHsaArea{bounds, color});
    }
    hsa_areas_qml_ = values;
    auto state = std::make_shared<std::vector<VoxelHsaArea>>(std::move(areas));
    hsa_areas_state_ = std::move(state);
    emit hsaAreasChanged();
    update();
}

float VoxelQuickItem::saturation() const { return saturation_; }

void VoxelQuickItem::setSaturation(float saturation) {
    saturation = std::clamp(saturation, 0.0f, 2.0f);
    if (qFuzzyCompare(saturation_, saturation)) return;
    saturation_ = saturation;
    emit toneChanged();
    update();
}

float VoxelQuickItem::brightness() const { return brightness_; }

void VoxelQuickItem::setBrightness(float brightness) {
    brightness = std::clamp(brightness, 0.0f, 2.0f);
    if (qFuzzyCompare(brightness_, brightness)) return;
    brightness_ = brightness;
    emit toneChanged();
    update();
}

void VoxelQuickItem::setSelectionBoundary(int axis, bool maximum, int value) {
    if (!scene_ || !view_state_.setSelectionBoundary(axis, maximum, value, scene_->modelSize)) return;
    emit selectionChanged();
    emit selectionInteractionChanged();
    update();
}

void VoxelQuickItem::setSelectionBounds(const QVector3D& minimum, const QVector3D& maximum, bool allowOutside) {
    if (!scene_ || !view_state_.setSelectionBounds(minimum, maximum, allowOutside, scene_->modelSize)) return;
    emit selectionChanged();
    emit selectionInteractionChanged();
    update();
}

void VoxelQuickItem::activateImportSelection(const QVector3D& size) {
    if (!scene_ || !view_state_.activateImportSelection(size)) return;
    emit selectionOptionsChanged();
    emit selectionChanged();
    emit selectionInteractionChanged();
    update();
}

void VoxelQuickItem::deactivateImportSelection() {
    if (!view_state_.deactivateImportSelection()) return;
    emit selectionOptionsChanged();
    emit selectionChanged();
    emit selectionInteractionChanged();
    update();
}

bool VoxelQuickItem::beginSelectionDrag(const QPointF& position, qreal pickRadius) {
    if (!scene_ || width() <= 0 || height() <= 0 ||
        !view_state_.beginSelectionDrag(position, pickRadius, scene_->modelSize, QSizeF(width(), height())))
        return false;
    emit selectionInteractionChanged();
    update();
    return true;
}

void VoxelQuickItem::dragSelection(const QPointF& position) {
    if (!scene_ || !view_state_.dragSelection(position, scene_->modelSize)) return;
    emit selectionChanged();
    update();
}

void VoxelQuickItem::endSelectionDrag() {
    if (!view_state_.endSelectionDrag()) return;
    emit selectionInteractionChanged();
    update();
}

void VoxelQuickItem::rotateView(float yawDegrees, float pitchDegrees) {
    view_state_.rotate(yawDegrees, pitchDegrees);
    update();
}

void VoxelQuickItem::focusFrontFace() {
    view_state_.focusFrontFace();
    update();
}

QQuickRhiItemRenderer* VoxelQuickItem::createRenderer() { return new VoxelRhiRenderer; }

void VoxelQuickItem::syncScene() {
    std::shared_ptr<const VoxelSceneData> next;
    if (controller_) next = controller_->voxelScene();
    // A rebuilt scene of the same volume, such as after an edit, keeps the
    // camera and selection the user is working with.
    const bool same_volume = scene_ && next && scene_->modelSize == next->modelSize && scene_->origin == next->origin;
    scene_ = std::move(next);
    if (same_volume)
        view_state_.cancelSelectionDrag();
    else
        view_state_.resetForScene(scene_ ? scene_->voxels.bounds().value_or(bl::block_box{}) : bl::block_box{});
    emit sceneChanged();
    emit selectionChanged();
    emit selectionInteractionChanged();
    update();
}

void VoxelQuickItem::syncOverlay() {
    std::shared_ptr<const VoxelSceneData> next;
    if (controller_) next = controller_->voxelOverlayScene();
    overlay_scene_ = std::move(next);
    update();
}

void VoxelQuickItem::resetView() {
    view_state_.resetCamera();
    update();
    emit viewOptionsChanged();
}

void VoxelQuickItem::beginRotate(const QPointF& position) { view_state_.beginRotate(position); }

void VoxelQuickItem::beginPan(const QPointF& position) { view_state_.beginPan(position); }

void VoxelQuickItem::dragView(const QPointF& position) {
    if (view_state_.dragView(position, height())) update();
}

void VoxelQuickItem::endView() { view_state_.endView(); }

void VoxelQuickItem::zoomBy(qreal factor) {
    if (view_state_.zoomBy(factor)) update();
}
