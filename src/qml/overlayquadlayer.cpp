#include "overlayquadlayer.h"

#include <QColor>
#include <QSGGeometry>
#include <QSGGeometryNode>
#include <QSGNode>
#include <QSGVertexColorMaterial>
#include <algorithm>

namespace {
    int quadVertexCount(const OverlayQuad& quad) {
        if (quad.rect.isEmpty()) return 0;
        int count = quad.fill.isValid() && quad.fill.alpha() > 0 ? 6 : 0;
        if (quad.outline.isValid() && quad.outline.alpha() > 0 && quad.outlineWidth > 0.0) count += 24;
        return count;
    }
}  // namespace

void OverlayQuadLayer::update(QSGNode* root, const QVector<OverlayQuad>& quads, QSGNode* before, bool prepend) {
    if (!root) return;

    int required_vertices = 0;
    for (const auto& quad : quads) required_vertices += quadVertexCount(quad);
    if (required_vertices == 0) {
        if (node_ && node_->parent()) root->removeChildNode(node_);
        return;
    }

    if (!node_) {
        auto* geometry = new QSGGeometry(QSGGeometry::defaultAttributes_ColoredPoint2D(), 0);
        geometry->setDrawingMode(QSGGeometry::DrawTriangles);
        geometry->setVertexDataPattern(QSGGeometry::DynamicPattern);
        node_ = new QSGGeometryNode;
        node_->setGeometry(geometry);
        node_->setFlag(QSGNode::OwnsGeometry, true);
        auto* material = new QSGVertexColorMaterial;
        material->setFlag(QSGMaterial::Blending, true);
        node_->setMaterial(material);
        node_->setFlag(QSGNode::OwnsMaterial, true);
    }
    if (!node_->parent()) {
        if (prepend)
            root->prependChildNode(node_);
        else if (before && before->parent() == root)
            root->insertChildNodeBefore(node_, before);
        else
            root->appendChildNode(node_);
    }

    auto* geometry = node_->geometry();
    if (vertex_capacity_ < required_vertices) {
        geometry = new QSGGeometry(QSGGeometry::defaultAttributes_ColoredPoint2D(), required_vertices);
        geometry->setDrawingMode(QSGGeometry::DrawTriangles);
        geometry->setVertexDataPattern(QSGGeometry::DynamicPattern);
        node_->setGeometry(geometry);
        vertex_capacity_ = required_vertices;
    }

    auto* vertices = geometry->vertexDataAsColoredPoint2D();
    int vertex = 0;
    const auto addQuad = [&](const QRectF& rect, const QColor& color) {
        if (rect.isEmpty() || !color.isValid() || color.alpha() == 0) return;
        const auto alpha = static_cast<uchar>(color.alpha());
        const auto premultiply = [alpha](int channel) { return static_cast<uchar>((channel * alpha + 127) / 255); };
        const auto red = premultiply(color.red());
        const auto green = premultiply(color.green());
        const auto blue = premultiply(color.blue());
        const float left = static_cast<float>(rect.left());
        const float top = static_cast<float>(rect.top());
        const float right = static_cast<float>(rect.right());
        const float bottom = static_cast<float>(rect.bottom());
        vertices[vertex++].set(left, top, red, green, blue, alpha);
        vertices[vertex++].set(right, top, red, green, blue, alpha);
        vertices[vertex++].set(left, bottom, red, green, blue, alpha);
        vertices[vertex++].set(right, top, red, green, blue, alpha);
        vertices[vertex++].set(right, bottom, red, green, blue, alpha);
        vertices[vertex++].set(left, bottom, red, green, blue, alpha);
    };

    for (const auto& quad : quads) {
        const QRectF rect = quad.rect.normalized();
        if (rect.isEmpty()) continue;
        addQuad(rect, quad.fill);
        if (!quad.outline.isValid() || quad.outline.alpha() == 0 || quad.outlineWidth <= 0.0) continue;
        const qreal border = std::min(quad.outlineWidth, std::min(rect.width(), rect.height()) / 2.0);
        addQuad(QRectF(rect.left(), rect.top(), rect.width(), border), quad.outline);
        addQuad(QRectF(rect.left(), rect.bottom() - border, rect.width(), border), quad.outline);
        addQuad(QRectF(rect.left(), rect.top() + border, border, rect.height() - border * 2.0), quad.outline);
        addQuad(QRectF(rect.right() - border, rect.top() + border, border, rect.height() - border * 2.0), quad.outline);
    }
    geometry->setVertexCount(vertex);
    geometry->markVertexDataDirty();
    node_->markDirty(QSGNode::DirtyGeometry);
}

void OverlayQuadLayer::resetSceneGraph() {
    node_ = nullptr;
    vertex_capacity_ = 0;
}

QSGNode* OverlayQuadLayer::node() const { return node_; }
