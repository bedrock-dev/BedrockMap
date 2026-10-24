#include "voxelquickitem.h"

#include <QFile>
#include <QMatrix4x4>
#include <QQuaternion>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <rhi/qrhi.h>
#include <rhi/qshader.h>

namespace {
    constexpr int vertexStride = 10 * sizeof(float);
    constexpr int uniformSize = 64;

    QShader loadShader(const QString& path) {
        QFile file(path);
        return file.open(QIODevice::ReadOnly) ? QShader::fromSerialized(file.readAll()) : QShader{};
    }

    QMatrix4x4 cameraMatrix(const VoxelRhiCameraState& camera, const QVector3D& size, const QSizeF& viewport) {
        QMatrix4x4 projection;
        const float aspect = viewport.height() > 0
                                 ? static_cast<float>(viewport.width() / viewport.height()) : 1.0f;
        if (camera.orthoMode) {
            // Match the perspective view at the model's center plane.
            constexpr float halfHeight = 26.509668f;  // 64 * tan(22.5 degrees)
            projection.ortho(-halfHeight * aspect, halfHeight * aspect, -halfHeight, halfHeight, 0.1f, 1000.0f);
        } else {
            projection.perspective(45.0f, aspect, 0.1f, 1000.0f);
        }
        QMatrix4x4 view;
        view.lookAt(QVector3D(0, 0, 64), QVector3D(), QVector3D(0, 1, 0));
        const float fit = 24.0f / std::max({1.0f, size.x(), size.y(), size.z()});
        QMatrix4x4 model;
        model.translate(camera.pan);
        model.rotate(camera.pitch, 1, 0, 0);
        model.rotate(camera.yaw, 0, 1, 0);
        model.scale(std::max(0.05f, fit * camera.zoom));
        model.translate(-size * 0.5f);
        return projection * view * model;
    }

    class VoxelRhiRenderer final : public QQuickRhiItemRenderer {
       public:
        ~VoxelRhiRenderer() override {
            delete selection_vertex_buffer_;
            delete selection_fill_pipeline_;
            delete selection_line_pipeline_;
            delete selection_handle_pipeline_;
            delete transparent_index_buffer_;
            delete transparent_vertex_buffer_;
            delete opaque_index_buffer_;
            delete opaque_vertex_buffer_;
            delete uniform_buffer_;
            delete shader_resources_;
            delete transparent_pipeline_;
            delete opaque_pipeline_;
        }

       protected:
        void initialize(QRhiCommandBuffer*) override {
            QRhi* rhi = this->rhi();
            if (!rhi || !renderTarget() || !renderTarget()->renderPassDescriptor()) return;

            const QShader vertexShader = loadShader(QStringLiteral(":/bedrockmap/shaders/src/qml/shaders/voxelrhi.vert.qsb"));
            const QShader fragmentShader = loadShader(QStringLiteral(":/bedrockmap/shaders/src/qml/shaders/voxelrhi.frag.qsb"));
            if (!vertexShader.isValid() || !fragmentShader.isValid()) return;

            if (!uniform_buffer_) {
                uniform_buffer_ = rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, uniformSize);
                uniform_buffer_->create();
                shader_resources_ = rhi->newShaderResourceBindings();
                shader_resources_->setBindings({
                    QRhiShaderResourceBinding::uniformBuffer(0, QRhiShaderResourceBinding::VertexStage, uniform_buffer_, 0,
                                                             uniformSize),
                });
                shader_resources_->create();
            }

            // The item can initialize again when its render target is resized.
            // Rebuild the pipelines for that target without leaking old ones.
            delete opaque_pipeline_;
            delete transparent_pipeline_;
            delete selection_fill_pipeline_;
            delete selection_line_pipeline_;
            delete selection_handle_pipeline_;

            QRhiVertexInputLayout layout;
            layout.setBindings({QRhiVertexInputBinding(vertexStride)});
            layout.setAttributes({
                QRhiVertexInputAttribute(0, 0, QRhiVertexInputAttribute::Float3, 0),
                QRhiVertexInputAttribute(0, 1, QRhiVertexInputAttribute::Float3, 3 * sizeof(float)),
                QRhiVertexInputAttribute(0, 2, QRhiVertexInputAttribute::Float4, 6 * sizeof(float)),
            });

            const auto createPipeline = [&](bool transparent, bool depthTest, QRhiGraphicsPipeline::Topology topology,
                                            QRhiGraphicsPipeline::CompareOp depthOp) {
                auto* pipeline = rhi->newGraphicsPipeline();
                pipeline->setShaderStages({
                    QRhiShaderStage(QRhiShaderStage::Vertex, vertexShader),
                    QRhiShaderStage(QRhiShaderStage::Fragment, fragmentShader),
                });
                pipeline->setVertexInputLayout(layout);
                pipeline->setShaderResourceBindings(shader_resources_);
                pipeline->setRenderPassDescriptor(renderTarget()->renderPassDescriptor());
                pipeline->setSampleCount(renderTarget()->sampleCount());
                pipeline->setTopology(topology);
                pipeline->setCullMode(QRhiGraphicsPipeline::None);
                pipeline->setDepthTest(depthTest);
                pipeline->setDepthWrite(!transparent);
                pipeline->setDepthOp(depthOp);
                if (topology == QRhiGraphicsPipeline::Lines && rhi->isFeatureSupported(QRhi::WideLines))
                    pipeline->setLineWidth(2.5f);
                QRhiGraphicsPipeline::TargetBlend blend;
                blend.enable = transparent;
                blend.srcColor = QRhiGraphicsPipeline::SrcAlpha;
                blend.dstColor = QRhiGraphicsPipeline::OneMinusSrcAlpha;
                blend.opColor = QRhiGraphicsPipeline::Add;
                pipeline->setTargetBlends({blend});
                if (pipeline->create()) return pipeline;
                delete pipeline;
                return static_cast<QRhiGraphicsPipeline*>(nullptr);
            };
            opaque_pipeline_ = createPipeline(false, true, QRhiGraphicsPipeline::Triangles, QRhiGraphicsPipeline::Less);
            transparent_pipeline_ = createPipeline(true, true, QRhiGraphicsPipeline::Triangles, QRhiGraphicsPipeline::Less);
            selection_fill_pipeline_ = createPipeline(true, true, QRhiGraphicsPipeline::Triangles, QRhiGraphicsPipeline::LessOrEqual);
            selection_line_pipeline_ = createPipeline(true, false, QRhiGraphicsPipeline::Lines, QRhiGraphicsPipeline::Always);
            selection_handle_pipeline_ = createPipeline(true, false, QRhiGraphicsPipeline::Triangles, QRhiGraphicsPipeline::Always);
        }

        void synchronize(QQuickRhiItem* item) override {
            auto* voxelItem = static_cast<VoxelQuickItem*>(item);
            scene_ = voxelItem->currentScene();
            camera_ = voxelItem->cameraState();
            selection_ = voxelItem->selectionState();
            logical_size_ = QSizeF(item->width(), item->height());
            render_size_ = item->effectiveColorBufferSize();
        }

        void render(QRhiCommandBuffer* command_buffer) override {
            QRhi* rhi = this->rhi();
            if (!rhi || !renderTarget()) return;

            QRhiResourceUpdateBatch* updates = rhi->nextResourceUpdateBatch();
            if (scene_) {
                if (scene_.get() != uploaded_scene_.get()) {
                    uploaded_scene_ = scene_;
                    uploadMesh(updates, scene_->opaqueVertices, scene_->opaqueIndices, false);
                    uploadMesh(updates, scene_->transparentVertices, scene_->transparentIndices, true);
                }

                const QMatrix4x4 matrix = cameraMatrix(camera_, scene_->modelSize, logical_size_);
                const QMatrix4x4 mvp = rhi->clipSpaceCorrMatrix() * matrix;
                uploadSelection(updates, {matrix, logical_size_, 1.0f});
                updates->updateDynamicBuffer(uniform_buffer_, 0, uniformSize, mvp.constData());
            }

            if (!scene_) {
                opaque_index_count_ = 0;
                transparent_index_count_ = 0;
                selection_vertex_count_ = 0;
                uploaded_scene_.reset();
            }
            command_buffer->beginPass(renderTarget(), QColor(25, 30, 38), QRhiDepthStencilClearValue(1.0f, 0), updates);
            command_buffer->setViewport(QRhiViewport(0, 0, static_cast<float>(render_size_.width()),
                                                    static_cast<float>(render_size_.height())));
            if (opaque_pipeline_ && opaque_vertex_buffer_ && opaque_index_buffer_ && opaque_index_count_ > 0) {
                const QRhiCommandBuffer::VertexInput input(opaque_vertex_buffer_, 0);
                command_buffer->setGraphicsPipeline(opaque_pipeline_);
                command_buffer->setShaderResources(shader_resources_);
                command_buffer->setVertexInput(0, 1, &input, opaque_index_buffer_, 0, QRhiCommandBuffer::IndexUInt32);
                command_buffer->drawIndexed(opaque_index_count_);
            }
            if (transparent_pipeline_ && transparent_vertex_buffer_ && transparent_index_buffer_ && transparent_index_count_ > 0) {
                const QRhiCommandBuffer::VertexInput input(transparent_vertex_buffer_, 0);
                command_buffer->setGraphicsPipeline(transparent_pipeline_);
                command_buffer->setShaderResources(shader_resources_);
                command_buffer->setVertexInput(0, 1, &input, transparent_index_buffer_, 0, QRhiCommandBuffer::IndexUInt32);
                command_buffer->drawIndexed(transparent_index_count_);
            }
            if (scene_ && selection_vertex_buffer_ && selection_vertex_count_ > 0) {
                const QRhiCommandBuffer::VertexInput input(selection_vertex_buffer_, 0);
                const auto drawSelection = [&](QRhiGraphicsPipeline* pipeline, quint32 count, quint32 first) {
                    if (!pipeline || !count) return;
                    command_buffer->setGraphicsPipeline(pipeline);
                    command_buffer->setShaderResources(shader_resources_);
                    command_buffer->setVertexInput(0, 1, &input);
                    command_buffer->draw(count, 1, first);
                };
                drawSelection(selection_fill_pipeline_, selection_fill_count_, 0);
                drawSelection(selection_line_pipeline_, selection_line_count_, selection_fill_count_);
                drawSelection(selection_handle_pipeline_, selection_vertex_count_ - selection_fill_count_ - selection_line_count_,
                              selection_fill_count_ + selection_line_count_);
            }
            command_buffer->endPass();
        }

       private:
        void uploadSelection(QRhiResourceUpdateBatch* updates, const VoxelSelectionView& view) {
            selection_vertex_count_ = 0;
            if (!selection_.enabled || !selection_.bounds.is_valid()) return;
            const auto mesh = voxel_mesh::buildSelectionMesh(selection_.bounds, view, selection_.color, selection_.active);
            std::vector<float> vertices;
            vertices.reserve(mesh.vertices.size() / 7 * 10);
            for (size_t offset = 0; offset < mesh.vertices.size(); offset += 7) {
                vertices.insert(vertices.end(), mesh.vertices.begin() + offset, mesh.vertices.begin() + offset + 3);
                // Zero normals select unlit colors in the shared voxel shader.
                vertices.insert(vertices.end(), {0, 0, 0});
                vertices.insert(vertices.end(), mesh.vertices.begin() + offset + 3, mesh.vertices.begin() + offset + 7);
            }
            const quint32 bytes = static_cast<quint32>(vertices.size() * sizeof(float));
            if (!bytes) return;
            if (!selection_vertex_buffer_ || selection_vertex_buffer_->size() < bytes) {
                delete selection_vertex_buffer_;
                selection_vertex_buffer_ = rhi()->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::VertexBuffer, bytes);
                if (!selection_vertex_buffer_->create()) {
                    delete selection_vertex_buffer_;
                    selection_vertex_buffer_ = nullptr;
                    return;
                }
            }
            updates->updateDynamicBuffer(selection_vertex_buffer_, 0, bytes, vertices.data());
            selection_vertex_count_ = static_cast<quint32>(vertices.size() / 10);
            selection_fill_count_ = mesh.fillCount;
            selection_line_count_ = mesh.lineCount;
        }

        void uploadMesh(QRhiResourceUpdateBatch* updates, const std::vector<float>& vertices,
                        const std::vector<std::uint32_t>& indices, bool transparent) {
            QRhiBuffer*& vertexBuffer = transparent ? transparent_vertex_buffer_ : opaque_vertex_buffer_;
            QRhiBuffer*& indexBuffer = transparent ? transparent_index_buffer_ : opaque_index_buffer_;
            quint32& indexCount = transparent ? transparent_index_count_ : opaque_index_count_;
            delete indexBuffer;
            delete vertexBuffer;
            indexBuffer = nullptr;
            vertexBuffer = nullptr;
            indexCount = 0;
            if (vertices.empty() || indices.empty()) return;

            vertexBuffer = rhi()->newBuffer(QRhiBuffer::Immutable, QRhiBuffer::VertexBuffer,
                                            static_cast<quint32>(vertices.size() * sizeof(float)));
            vertexBuffer->create();
            indexBuffer = rhi()->newBuffer(QRhiBuffer::Immutable, QRhiBuffer::IndexBuffer,
                                           static_cast<quint32>(indices.size() * sizeof(std::uint32_t)));
            indexBuffer->create();
            updates->uploadStaticBuffer(vertexBuffer, vertices.data());
            updates->uploadStaticBuffer(indexBuffer, indices.data());
            indexCount = static_cast<quint32>(indices.size());
        }

        std::shared_ptr<const VoxelSceneData> scene_;
        std::shared_ptr<const VoxelSceneData> uploaded_scene_;
        VoxelRhiCameraState camera_;
        QSize render_size_;
        QSizeF logical_size_;
        VoxelRhiSelectionState selection_;
        QRhiBuffer* selection_vertex_buffer_{nullptr};
        QRhiGraphicsPipeline* selection_fill_pipeline_{nullptr};
        QRhiGraphicsPipeline* selection_line_pipeline_{nullptr};
        QRhiGraphicsPipeline* selection_handle_pipeline_{nullptr};
        quint32 selection_vertex_count_{0};
        quint32 selection_fill_count_{0};
        quint32 selection_line_count_{0};
        QRhiBuffer* uniform_buffer_{nullptr};
        QRhiShaderResourceBindings* shader_resources_{nullptr};
        QRhiGraphicsPipeline* opaque_pipeline_{nullptr};
        QRhiGraphicsPipeline* transparent_pipeline_{nullptr};
        QRhiBuffer* opaque_vertex_buffer_{nullptr};
        QRhiBuffer* opaque_index_buffer_{nullptr};
        quint32 opaque_index_count_{0};
        QRhiBuffer* transparent_vertex_buffer_{nullptr};
        QRhiBuffer* transparent_index_buffer_{nullptr};
        quint32 transparent_index_count_{0};
    };
}  // namespace

VoxelQuickItem::VoxelQuickItem(QQuickItem* parent) : QQuickRhiItem(parent) {
    setSampleCount(4);
    setAlphaBlending(false);
    setAcceptedMouseButtons(Qt::LeftButton | Qt::RightButton);
}

void VoxelQuickItem::setController(VoxelSceneController* controller) {
    if (controller_ == controller) return;
    if (scene_connection_) disconnect(scene_connection_);
    controller_ = controller;
    if (controller_)
        scene_connection_ = connect(controller_, &VoxelSceneController::voxelChanged, this, &VoxelQuickItem::syncScene);
    syncScene();
    emit controllerChanged();
}

std::shared_ptr<const VoxelSceneData> VoxelQuickItem::currentScene() const {
    const QMutexLocker lock(&state_mutex_);
    return scene_;
}

VoxelRhiCameraState VoxelQuickItem::cameraState() const {
    const QMutexLocker lock(&state_mutex_);
    return {yaw_, pitch_, zoom_, pan_, ortho_mode_};
}

bool VoxelQuickItem::orthoMode() const {
    const QMutexLocker lock(&state_mutex_);
    return ortho_mode_;
}

void VoxelQuickItem::setOrthoMode(bool ortho) {
    {
        const QMutexLocker lock(&state_mutex_);
        if (ortho_mode_ == ortho) return;
        ortho_mode_ = ortho;
    }
    emit viewOptionsChanged();
    update();
}

bool VoxelQuickItem::rotationLocked() const {
    const QMutexLocker lock(&state_mutex_);
    return rotation_locked_;
}

void VoxelQuickItem::setRotationLocked(bool locked) {
    {
        const QMutexLocker lock(&state_mutex_);
        if (rotation_locked_ == locked) return;
        rotation_locked_ = locked;
        if (locked) rotating_ = false;
    }
    emit viewOptionsChanged();
    update();
}

QVector3D VoxelQuickItem::modelSize() const {
    const QMutexLocker lock(&state_mutex_);
    return scene_ ? scene_->modelSize : QVector3D{};
}

QVector3D VoxelQuickItem::modelOrigin() const {
    const QMutexLocker lock(&state_mutex_);
    return scene_ ? scene_->origin : QVector3D{};
}

VoxelRhiSelectionState VoxelQuickItem::selectionState() const {
    const QMutexLocker lock(&state_mutex_);
    return selection_;
}

bool VoxelQuickItem::selectionEnabled() const {
    const QMutexLocker lock(&state_mutex_);
    return selection_.enabled;
}

void VoxelQuickItem::setSelectionEnabled(bool enabled) {
    {
        const QMutexLocker lock(&state_mutex_);
        if (selection_.enabled == enabled) return;
        selection_.enabled = enabled;
        selection_.active = VoxelSelectionHandle::None;
        selection_drag_ = {};
    }
    emit selectionOptionsChanged();
    emit selectionInteractionChanged();
    update();
}

bool VoxelQuickItem::selectionMoveMode() const {
    const QMutexLocker lock(&state_mutex_);
    return selection_move_mode_;
}

void VoxelQuickItem::setSelectionMoveMode(bool enabled) {
    {
        const QMutexLocker lock(&state_mutex_);
        if (selection_move_mode_ == enabled) return;
        selection_move_mode_ = enabled;
        selection_.active = VoxelSelectionHandle::None;
        selection_drag_ = {};
    }
    emit selectionOptionsChanged();
    emit selectionInteractionChanged();
    update();
}

QColor VoxelQuickItem::selectionColor() const {
    const QMutexLocker lock(&state_mutex_);
    return selection_.color;
}

void VoxelQuickItem::setSelectionColor(const QColor& color) {
    if (!color.isValid()) return;
    {
        const QMutexLocker lock(&state_mutex_);
        if (selection_.color == color) return;
        selection_.color = color;
    }
    emit selectionOptionsChanged();
    update();
}

QVector3D VoxelQuickItem::selectionMinimum() const {
    const QMutexLocker lock(&state_mutex_);
    const auto& p = selection_.bounds.min_pos;
    return QVector3D(p.x, p.y, p.z);
}

QVector3D VoxelQuickItem::selectionMaximum() const {
    const QMutexLocker lock(&state_mutex_);
    const auto& p = selection_.bounds.max_pos;
    return QVector3D(p.x, p.y, p.z);
}

bool VoxelQuickItem::selectionDragging() const {
    const QMutexLocker lock(&state_mutex_);
    return selection_.active != VoxelSelectionHandle::None;
}

void VoxelQuickItem::setSelectionBoundary(int axis, bool maximum, int value) {
    if (axis < 0 || axis > 2) return;
    {
        const QMutexLocker lock(&state_mutex_);
        if (!scene_ || !selection_.enabled || !selection_.bounds.is_valid()) return;
        auto bounds = selection_.bounds;
        int* low[] = {&bounds.min_pos.x, &bounds.min_pos.y, &bounds.min_pos.z};
        int* high[] = {&bounds.max_pos.x, &bounds.max_pos.y, &bounds.max_pos.z};
        if (maximum)
            *high[axis] = std::clamp(value, *low[axis] + 1, static_cast<int>(scene_->modelSize[axis]));
        else
            *low[axis] = std::clamp(value, 0, *high[axis] - 1);
        selection_.bounds = bounds;
        selection_.active = VoxelSelectionHandle::None;
        selection_drag_ = {};
    }
    emit selectionChanged();
    emit selectionInteractionChanged();
    update();
}

bool VoxelQuickItem::beginSelectionDrag(const QPointF& position, qreal pickRadius) {
    if (!std::isfinite(position.x()) || !std::isfinite(position.y()) ||
        !std::isfinite(pickRadius) || pickRadius <= 0 || width() <= 0 || height() <= 0) return false;
    {
        const QMutexLocker lock(&state_mutex_);
        if (!scene_ || !selection_.enabled) return false;
        const VoxelRhiCameraState camera{yaw_, pitch_, zoom_, pan_, ortho_mode_};
        const VoxelSelectionView view{cameraMatrix(camera, scene_->modelSize, QSizeF(width(), height())),
                                      QSizeF(width(), height()), 1.0f};
        const auto drag = voxel_mesh::beginSelectionDrag(selection_.bounds, scene_->modelSize, view, position,
                                                         selection_move_mode_, static_cast<float>(pickRadius));
        if (drag.handle == VoxelSelectionHandle::None) return false;
        selection_drag_ = drag;
        selection_.active = drag.handle;
        rotating_ = false;
        panning_ = false;
    }
    emit selectionInteractionChanged();
    update();
    return true;
}

void VoxelQuickItem::dragSelection(const QPointF& position) {
    if (!std::isfinite(position.x()) || !std::isfinite(position.y())) return;
    {
        const QMutexLocker lock(&state_mutex_);
        if (!scene_ || !selection_.enabled || selection_.active == VoxelSelectionHandle::None) return;
        const auto bounds = voxel_mesh::dragSelection(selection_drag_, scene_->modelSize, position);
        if (!bounds.is_valid() || (bounds.min_pos == selection_.bounds.min_pos && bounds.max_pos == selection_.bounds.max_pos))
            return;
        selection_.bounds = bounds;
    }
    emit selectionChanged();
    update();
}

void VoxelQuickItem::endSelectionDrag() {
    {
        const QMutexLocker lock(&state_mutex_);
        if (selection_.active == VoxelSelectionHandle::None) return;
        selection_.active = VoxelSelectionHandle::None;
        selection_drag_ = {};
    }
    emit selectionInteractionChanged();
    update();
}

void VoxelQuickItem::rotateView(float yawDegrees, float pitchDegrees) {
    if (!std::isfinite(yawDegrees) || !std::isfinite(pitchDegrees)) return;
    {
        const QMutexLocker lock(&state_mutex_);
        // Explicit buttons remain available while drag rotation is locked,
        // matching the native preview's controls.
        yaw_ = std::remainder(yaw_ + yawDegrees, 360.0f);
        pitch_ = std::remainder(pitch_ + pitchDegrees, 360.0f);
    }
    update();
}

void VoxelQuickItem::focusFrontFace() {
    {
        const QMutexLocker lock(&state_mutex_);
        const QQuaternion current = QQuaternion::fromAxisAndAngle(1, 0, 0, pitch_) *
                                    QQuaternion::fromAxisAndAngle(0, 1, 0, yaw_);
        const QVector3D axes[] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
        QVector3D front;
        float facing = -2.0f;
        for (const auto& axis : axes) {
            const float z = current.rotatedVector(axis).z();
            if (z > facing) {
                facing = z;
                front = axis;
            }
        }
        // Pick the nearest axis-aligned yaw/pitch pose that faces the camera.
        // This preserves the roll-free orbit used by dragging and arrow buttons.
        float bestDot = -1.0f;
        for (const float pitch : {-180.0f, -90.0f, 0.0f, 90.0f}) {
            for (const float yaw : {-180.0f, -90.0f, 0.0f, 90.0f}) {
                const QQuaternion candidate = QQuaternion::fromAxisAndAngle(1, 0, 0, pitch) *
                                              QQuaternion::fromAxisAndAngle(0, 1, 0, yaw);
                if (candidate.rotatedVector(front).z() < 0.999f) continue;
                const float dot = std::abs(QQuaternion::dotProduct(current, candidate));
                if (dot > bestDot) {
                    bestDot = dot;
                    yaw_ = yaw;
                    pitch_ = pitch;
                }
            }
        }
    }
    update();
}

QQuickRhiItemRenderer* VoxelQuickItem::createRenderer() {
    return new VoxelRhiRenderer;
}

void VoxelQuickItem::syncScene() {
    std::shared_ptr<const VoxelSceneData> next;
    if (controller_) next = controller_->voxelScene();
    {
        const QMutexLocker lock(&state_mutex_);
        scene_ = std::move(next);
        selection_.bounds = scene_ ? bl::block_box::from_min_and_size(
            {0, 0, 0}, static_cast<int>(scene_->modelSize.x()), static_cast<int>(scene_->modelSize.y()),
            static_cast<int>(scene_->modelSize.z())) : bl::block_box{};
        selection_.active = VoxelSelectionHandle::None;
        selection_drag_ = {};
        yaw_ = 45.0f;
        pitch_ = 45.0f;
        zoom_ = 1.0f;
        pan_ = {};
        rotating_ = false;
        panning_ = false;
    }
    emit sceneChanged();
    emit selectionChanged();
    emit selectionInteractionChanged();
    update();
}

void VoxelQuickItem::resetView() {
    yaw_ = 45.0f;
    pitch_ = 45.0f;
    zoom_ = 1.0f;
    pan_ = {};
}

void VoxelQuickItem::beginRotate(const QPointF& position) {
    if (!std::isfinite(position.x()) || !std::isfinite(position.y())) return;
    const QMutexLocker lock(&state_mutex_);
    last_mouse_position_ = position;
    rotating_ = !rotation_locked_;
    panning_ = false;
}

void VoxelQuickItem::beginPan(const QPointF& position) {
    if (!std::isfinite(position.x()) || !std::isfinite(position.y())) return;
    const QMutexLocker lock(&state_mutex_);
    last_mouse_position_ = position;
    panning_ = true;
    rotating_ = false;
}

void VoxelQuickItem::dragView(const QPointF& position) {
    if (!std::isfinite(position.x()) || !std::isfinite(position.y())) return;
    const QMutexLocker lock(&state_mutex_);
    const QPointF delta = position - last_mouse_position_;
    last_mouse_position_ = position;
    if (rotating_ && !rotation_locked_) {
        yaw_ = std::remainder(yaw_ + static_cast<float>(delta.x()) * 0.5f, 360.0f);
        pitch_ = std::remainder(pitch_ + static_cast<float>(delta.y()) * 0.5f, 360.0f);
        update();
    } else if (panning_) {
        // The camera is 64 units from the model with a 45-degree vertical FOV:
        // 2 * 64 * tan(22.5 degrees) screen-space units span the viewport height.
        const float worldPerPixel = 53.019336f / static_cast<float>(std::max(1.0, height()));
        pan_.setX(pan_.x() + static_cast<float>(delta.x()) * worldPerPixel);
        pan_.setY(pan_.y() - static_cast<float>(delta.y()) * worldPerPixel);
        update();
    }
}

void VoxelQuickItem::endView() {
    const QMutexLocker lock(&state_mutex_);
    rotating_ = false;
    panning_ = false;
}

void VoxelQuickItem::zoomBy(qreal factor) {
    if (!std::isfinite(factor) || factor <= 0.0) return;
    const QMutexLocker lock(&state_mutex_);
    zoom_ = std::clamp(zoom_ * static_cast<float>(factor), 0.1f, 10.0f);
    update();
}
