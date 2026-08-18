#include "voxelwidget.h"

#include <QGuiApplication>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QLabel>
#include <QResizeEvent>
#include <QScreen>
#include <QShowEvent>
#include <QToolButton>
#include <QVBoxLayout>
#include <QVector4D>
#include <QWheelEvent>
#include <algorithm>
#include <array>
#include <cmath>

namespace {
// Keep panning consistent with the projection setup used by VoxelWidget.
constexpr float kViewHalfHeight = 20.710678f;
constexpr float kSelectionHandleScreenSizePx = 12.0f;
constexpr float kSelectionHandlePickRadius = kSelectionHandleScreenSizePx * 1.35f;
}  // namespace

void VoxelWidget::setupShortcutHelpButton() {
    shortcut_help_button_ = new QToolButton(this);
    shortcut_help_button_->setText(QStringLiteral("?"));
    shortcut_help_button_->setToolTip(tr("voxelWidget.shortcuts.buttonTooltip"));
    shortcut_help_button_->setFocusPolicy(Qt::NoFocus);
    shortcut_help_button_->setCursor(Qt::ArrowCursor);
    shortcut_help_button_->setStyleSheet(QStringLiteral(
        "QToolButton {"
        " background-color: rgba(16, 20, 24, 170);"
        " border: 1px solid rgba(255, 255, 255, 80);"
        " border-radius: 4px;"
        " color: rgba(255, 255, 255, 235);"
        " font-weight: 700;"
        "}"
        "QToolButton:hover {"
        " background-color: rgba(255, 255, 255, 48);"
        "}"));
    connect(shortcut_help_button_, &QToolButton::clicked, this, &VoxelWidget::showShortcutHelp);
    shortcut_help_popup_ = new QWidget(this, Qt::Popup | Qt::FramelessWindowHint);
    shortcut_help_popup_->setObjectName(QStringLiteral("voxelShortcutHelpPopup"));
    shortcut_help_popup_->setStyleSheet(QStringLiteral(
        "QWidget#voxelShortcutHelpPopup {"
        " background-color: rgba(16, 20, 24, 235);"
        " border: 1px solid rgba(255, 255, 255, 72);"
        " border-radius: 6px;"
        "}"
        "QWidget#voxelShortcutHelpPopup QLabel {"
        " color: rgba(255, 255, 255, 235);"
        " background: transparent;"
        "}"));
    auto* popupLayout = new QVBoxLayout(shortcut_help_popup_);
    popupLayout->setContentsMargins(12, 10, 12, 10);
    popupLayout->setSpacing(6);
    auto* helpLabel = new QLabel(tr("voxelWidget.shortcuts.content"), shortcut_help_popup_);
    helpLabel->setObjectName(QStringLiteral("shortcutHelpLabel"));
    helpLabel->setWordWrap(true);
    helpLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    helpLabel->setMinimumWidth(270);
    helpLabel->setMaximumWidth(340);
    popupLayout->addWidget(helpLabel);
    shortcut_help_popup_->hide();
    updateShortcutHelpButtonGeometry();
}

void VoxelWidget::updateShortcutHelpButtonGeometry() {
    if (!shortcut_help_button_) return;

    constexpr int buttonSize = 28;
    constexpr int margin = 8;
    shortcut_help_button_->setGeometry(std::max(margin, width() - buttonSize - margin), margin, buttonSize, buttonSize);
    if (shortcut_help_popup_ && shortcut_help_popup_->isVisible()) {
        shortcut_help_popup_->adjustSize();
        const QPoint anchor = shortcut_help_button_->mapToGlobal(QPoint(0, shortcut_help_button_->height() + 6));
        QPoint popupPos = anchor;
        if (auto* screen = QGuiApplication::screenAt(anchor)) {
            const QRect available = screen->availableGeometry();
            popupPos.setX(std::clamp(popupPos.x(), available.left() + 8, available.right() - shortcut_help_popup_->width() - 8));
            popupPos.setY(std::clamp(popupPos.y(), available.top() + 8, available.bottom() - shortcut_help_popup_->height() - 8));
        }
        shortcut_help_popup_->move(popupPos);
    }
}

void VoxelWidget::showShortcutHelp() {
    if (!shortcut_help_popup_) return;
    if (shortcut_help_popup_->isVisible()) {
        shortcut_help_popup_->hide();
        return;
    }
    if (auto* label = shortcut_help_popup_->findChild<QLabel*>(QStringLiteral("shortcutHelpLabel"))) {
        label->setText(tr("voxelWidget.shortcuts.content"));
    }
    shortcut_help_popup_->adjustSize();
    const QPoint anchor = shortcut_help_button_ ? shortcut_help_button_->mapToGlobal(QPoint(0, shortcut_help_button_->height() + 6))
                                                : mapToGlobal(QPoint(8, 8));
    QPoint popupPos = anchor;
    if (auto* screen = QGuiApplication::screenAt(anchor)) {
        const QRect available = screen->availableGeometry();
        popupPos.setX(std::clamp(popupPos.x(), available.left() + 8, available.right() - shortcut_help_popup_->width() - 8));
        popupPos.setY(std::clamp(popupPos.y(), available.top() + 8, available.bottom() - shortcut_help_popup_->height() - 8));
    }
    shortcut_help_popup_->move(popupPos);
    shortcut_help_popup_->show();
}

float VoxelWidget::pixelsPerWorldUnitAt(const QVector3D& point) const {
    bool originVisible = false;
    const QPointF origin = projectToWidget(point, &originVisible);
    if (!originVisible) return 0.0f;

    float totalPixelsPerWorld = 0.0f;
    int sampleCount = 0;
    const QVector3D axes[] = {
        QVector3D(voxel_size_, 0.0f, 0.0f),
        QVector3D(0.0f, voxel_size_, 0.0f),
        QVector3D(0.0f, 0.0f, voxel_size_),
    };
    for (const auto& axis : axes) {
        bool axisVisible = false;
        const QPointF projected = projectToWidget(point + axis, &axisVisible);
        if (!axisVisible) continue;
        const float dx = static_cast<float>(projected.x() - origin.x());
        const float dy = static_cast<float>(projected.y() - origin.y());
        const float pixels = std::sqrt(dx * dx + dy * dy);
        if (pixels <= 1e-3f) continue;
        totalPixelsPerWorld += pixels / voxel_size_;
        ++sampleCount;
    }
    if (sampleCount == 0) return 0.0f;
    return totalPixelsPerWorld / static_cast<float>(sampleCount);
}

float VoxelWidget::selectionHandleHalfSizeAt(const QVector3D& point) const {
    const float pixelsPerWorld = pixelsPerWorldUnitAt(point);
    if (pixelsPerWorld <= 1e-3f) {
        return 0.20f * voxel_size_;
    }
    return (kSelectionHandleScreenSizePx * 0.5f) / pixelsPerWorld;
}

QVector3D VoxelWidget::selectionHandleAxis(SelectionHandle handle) const {
    switch (handle) {
        case SelectionHandle::MinX:
        case SelectionHandle::MaxX:
            return {1.0f, 0.0f, 0.0f};
        case SelectionHandle::MinY:
        case SelectionHandle::MaxY:
            return {0.0f, 1.0f, 0.0f};
        case SelectionHandle::MinZ:
        case SelectionHandle::MaxZ:
            return {0.0f, 0.0f, 1.0f};
        case SelectionHandle::None:
            return {};
    }
    return {};
}

QPointF VoxelWidget::projectToWidget(const QVector3D& point, bool* visible) const {
    const QVector4D clip = m_projection * m_view * m_model * QVector4D(point, 1.0f);
    if (std::abs(clip.w()) < 1e-6f) {
        if (visible) *visible = false;
        return {};
    }

    const QVector3D ndc = clip.toVector3D() / clip.w();
    if (visible) {
        *visible = clip.w() > 0.0f && ndc.z() >= -1.0f && ndc.z() <= 1.0f;
    }
    return {(ndc.x() + 1.0f) * 0.5f * width(), (1.0f - ndc.y()) * 0.5f * height()};
}

VoxelWidget::SelectionHandle VoxelWidget::pickSelectionHandle(const QPointF& position) const {
    if (!selection_enabled_ || !selection_.isValid()) return SelectionHandle::None;

    constexpr std::array<SelectionHandle, 6> handles = {
        SelectionHandle::MinX, SelectionHandle::MaxX, SelectionHandle::MinY,
        SelectionHandle::MaxY, SelectionHandle::MinZ, SelectionHandle::MaxZ,
    };
    SelectionHandle bestHandle = SelectionHandle::None;
    float bestDistanceSquared = kSelectionHandlePickRadius * kSelectionHandlePickRadius;
    for (const SelectionHandle handle : handles) {
        bool visible = false;
        const QPointF projected = projectToWidget(selectionHandlePosition(handle), &visible);
        if (!visible) continue;
        const QPointF delta = position - projected;
        const float distanceSquared = static_cast<float>(delta.x() * delta.x() + delta.y() * delta.y());
        if (distanceSquared <= bestDistanceSquared) {
            bestDistanceSquared = distanceSquared;
            bestHandle = handle;
        }
    }
    return bestHandle;
}

void VoxelWidget::updateSelectionFromDrag(const QPointF& position) {
    if (active_selection_handle_ == SelectionHandle::None) return;

    const float axisLengthSquared = static_cast<float>(selection_drag_axis_screen_.x() * selection_drag_axis_screen_.x() +
                                                       selection_drag_axis_screen_.y() * selection_drag_axis_screen_.y());
    if (axisLengthSquared < 1e-10f) return;
    const QPointF mouseDelta = position - selection_drag_start_;
    const float voxelDelta = static_cast<float>((mouseDelta.x() * selection_drag_axis_screen_.x() +
                                                 mouseDelta.y() * selection_drag_axis_screen_.y()) /
                                                axisLengthSquared);
    const int value = qRound(static_cast<float>(selection_drag_start_value_) + voxelDelta);

    const int sizeX = static_cast<int>(voxel_data_[0].size());
    const int sizeY = static_cast<int>(voxel_data_.size());
    const int sizeZ = static_cast<int>(voxel_data_[0][0].size());
    bool changed = false;
    auto assign = [&changed](float& target, int newValue) {
        if (qRound(target) == newValue) return;
        target = static_cast<float>(newValue);
        changed = true;
    };

    float minimumX = selection_.minimum.x();
    float minimumY = selection_.minimum.y();
    float minimumZ = selection_.minimum.z();
    float maximumX = selection_.maximum.x();
    float maximumY = selection_.maximum.y();
    float maximumZ = selection_.maximum.z();
    switch (active_selection_handle_) {
        case SelectionHandle::MinX:
            assign(minimumX, std::clamp(value, 0, qRound(maximumX) - 1));
            break;
        case SelectionHandle::MaxX:
            assign(maximumX, std::clamp(value, qRound(minimumX) + 1, sizeX));
            break;
        case SelectionHandle::MinY:
            assign(minimumY, std::clamp(value, 0, qRound(maximumY) - 1));
            break;
        case SelectionHandle::MaxY:
            assign(maximumY, std::clamp(value, qRound(minimumY) + 1, sizeY));
            break;
        case SelectionHandle::MinZ:
            assign(minimumZ, std::clamp(value, 0, qRound(maximumZ) - 1));
            break;
        case SelectionHandle::MaxZ:
            assign(maximumZ, std::clamp(value, qRound(minimumZ) + 1, sizeZ));
            break;
        case SelectionHandle::None:
            break;
    }
    if (!changed) return;

    selection_.minimum = QVector3D(minimumX, minimumY, minimumZ);
    selection_.maximum = QVector3D(maximumX, maximumY, maximumZ);
    buildSelectionVertices();
    if (gl_initialized_) {
        makeCurrent();
        updateSelectionOpenGLBuffer();
        doneCurrent();
    }
    update();
}

void VoxelWidget::mousePressEvent(QMouseEvent* e) {
    setFocus();  // make sure keyboard shortcuts (R / O / arrows) are received
    if (e->button() == Qt::MiddleButton && selection_enabled_) {
        updateModelMatrix();
        active_selection_handle_ = pickSelectionHandle(e->position());
        if (active_selection_handle_ != SelectionHandle::None) {
            selection_drag_start_ = e->position();
            const QVector3D handlePosition = selectionHandlePosition(active_selection_handle_);
            const QVector3D handleAxis = selectionHandleAxis(active_selection_handle_);
            const auto projectedAxisForVoxelStep = [this, &handlePosition, &handleAxis](float voxelStep) {
                return projectToWidget(handlePosition + handleAxis * voxel_size_ * voxelStep) - projectToWidget(handlePosition);
            };
            selection_drag_axis_screen_ =
                projectedAxisForVoxelStep(1.0f);
            const float axisLengthSquared = static_cast<float>(selection_drag_axis_screen_.x() * selection_drag_axis_screen_.x() +
                                                               selection_drag_axis_screen_.y() * selection_drag_axis_screen_.y());
            if (axisLengthSquared < 4.0f) {
                const int sizeX = static_cast<int>(voxel_data_[0].size());
                const int sizeY = static_cast<int>(voxel_data_.size());
                const int sizeZ = static_cast<int>(voxel_data_[0][0].size());
                int axisVoxelCount = 1;
                switch (active_selection_handle_) {
                    case SelectionHandle::MinX:
                    case SelectionHandle::MaxX:
                        axisVoxelCount = sizeX;
                        break;
                    case SelectionHandle::MinY:
                    case SelectionHandle::MaxY:
                        axisVoxelCount = sizeY;
                        break;
                    case SelectionHandle::MinZ:
                    case SelectionHandle::MaxZ:
                        axisVoxelCount = sizeZ;
                        break;
                    case SelectionHandle::None:
                        break;
                }
                const float sampleVoxelStep = static_cast<float>(std::clamp(axisVoxelCount, 1, 256));
                const QPointF sampledAxis = projectedAxisForVoxelStep(sampleVoxelStep);
                const float sampledAxisLengthSquared = static_cast<float>(sampledAxis.x() * sampledAxis.x() + sampledAxis.y() * sampledAxis.y());
                if (sampledAxisLengthSquared > 1e-6f) {
                    selection_drag_axis_screen_ = sampledAxis / sampleVoxelStep;
                } else {
                    // An axis pointing into the camera has no useful screen projection.
                    // In that view, upward motion increases the selected coordinate.
                    selection_drag_axis_screen_ = QPointF(0.0, -12.0);
                }
            }
            switch (active_selection_handle_) {
                case SelectionHandle::MinX:
                    selection_drag_start_value_ = qRound(selection_.minimum.x());
                    break;
                case SelectionHandle::MaxX:
                    selection_drag_start_value_ = qRound(selection_.maximum.x());
                    break;
                case SelectionHandle::MinY:
                    selection_drag_start_value_ = qRound(selection_.minimum.y());
                    break;
                case SelectionHandle::MaxY:
                    selection_drag_start_value_ = qRound(selection_.maximum.y());
                    break;
                case SelectionHandle::MinZ:
                    selection_drag_start_value_ = qRound(selection_.minimum.z());
                    break;
                case SelectionHandle::MaxZ:
                    selection_drag_start_value_ = qRound(selection_.maximum.z());
                    break;
                case SelectionHandle::None:
                    break;
            }
            setCursor(Qt::ClosedHandCursor);
            buildSelectionVertices();
            if (gl_initialized_) {
                makeCurrent();
                updateSelectionOpenGLBuffer();
                doneCurrent();
            }
            update();
        }
        e->accept();
        return;
    }

    if (e->button() == Qt::LeftButton) {
        if (rotation_locked_) {
            e->accept();
            return;
        }
        m_lastMousePos = e->pos();
    } else if (e->button() == Qt::RightButton) {
        m_panStartPos = e->pos();
        m_isPanDragging = true;
    }
    e->accept();
}

void VoxelWidget::mouseReleaseEvent(QMouseEvent* e) {
    if (e->button() == Qt::MiddleButton && active_selection_handle_ != SelectionHandle::None) {
        active_selection_handle_ = SelectionHandle::None;
        unsetCursor();
        buildSelectionVertices();
        if (gl_initialized_) {
            makeCurrent();
            updateSelectionOpenGLBuffer();
            doneCurrent();
        }
        update();
    } else if (e->button() == Qt::RightButton) {
        m_isPanDragging = false;
    }
    e->accept();
}

void VoxelWidget::mouseMoveEvent(QMouseEvent* e) {
    if ((e->buttons() & Qt::MiddleButton) && active_selection_handle_ != SelectionHandle::None) {
        updateSelectionFromDrag(e->position());
        e->accept();
        return;
    }

    if (e->buttons() & Qt::LeftButton && !m_isPanDragging) {
        if (rotation_locked_) {
            e->accept();
            return;
        }
        int dx = e->pos().x() - m_lastMousePos.x();
        int dy = e->pos().y() - m_lastMousePos.y();
        orbitRotate(dx * 0.5f, dy * 0.5f);
        m_lastMousePos = e->pos();
        return;
    }

    if ((e->buttons() & Qt::RightButton) && m_isPanDragging) {
        int deltaX = e->pos().x() - m_panStartPos.x();
        int deltaY = e->pos().y() - m_panStartPos.y();

        // 1:1 world-space drag: the model follows the cursor exactly,
        // independent of the current zoom level.
        const float worldPerPixel = (2.0f * kViewHalfHeight) / std::max(1, height());
        const float sensitivity = worldPerPixel * m_panSensitivity;

        if (!m_isShiftPressed) {
            m_cameraTranslate.setX(m_cameraTranslate.x() + deltaX * sensitivity);
            m_cameraTranslate.setY(m_cameraTranslate.y() - deltaY * sensitivity);
        } else {
            m_cameraTranslate.setZ(m_cameraTranslate.z() + deltaX * sensitivity);
            m_cameraTranslate.setY(m_cameraTranslate.y() - deltaY * sensitivity);
        }

        m_panStartPos = e->pos();
        update();
    }
}

void VoxelWidget::wheelEvent(QWheelEvent* e) {
    int delta = e->angleDelta().y();
    if (delta > 0) {
        m_scale = std::min(m_scale + 0.1f, 10.0f);
    } else {
        m_scale = std::max(m_scale - 0.1f, 0.1f);
    }
    update();
}

void VoxelWidget::resizeEvent(QResizeEvent* e) {
    QOpenGLWidget::resizeEvent(e);
    updateShortcutHelpButtonGeometry();
}

void VoxelWidget::showEvent(QShowEvent* e) { QOpenGLWidget::showEvent(e); }

void VoxelWidget::keyPressEvent(QKeyEvent* e) {
    if (e->key() == Qt::Key_Shift) {
        m_isShiftPressed = true;
    } else if (e->key() == Qt::Key_R) {
        // Reset to the default corner view (45 degrees); projection mode is kept.
        m_rotation = QQuaternion::fromAxisAndAngle(1.0f, 0.0f, 0.0f, 45.0f) *
                     QQuaternion::fromAxisAndAngle(0.0f, 1.0f, 0.0f, 45.0f);
        orbit_yaw_degrees_ = 45.0f;
        orbit_pitch_degrees_ = 45.0f;
        m_scale = 1.0f;
        m_cameraTranslate = QVector3D(0.0f, 0.0f, 0.0f);
        updateProjection();
        update();
    } else if (e->key() == Qt::Key_F) {
        // Focus the face that is currently most parallel to the screen: snap it
        // flat and align its four edges with the window axes, choosing the
        // candidate with the smallest rotation from the current orientation.
        const QVector3D local = localFaceClosestTo(QVector3D(0.0f, 0.0f, 1.0f));
        QQuaternion base;
        if (local.x() > 0.5f) {
            base = QQuaternion::fromAxisAndAngle(0.0f, 1.0f, 0.0f, -90.0f);
        } else if (local.x() < -0.5f) {
            base = QQuaternion::fromAxisAndAngle(0.0f, 1.0f, 0.0f, 90.0f);
        } else if (local.y() > 0.5f) {
            base = QQuaternion::fromAxisAndAngle(1.0f, 0.0f, 0.0f, 90.0f);
        } else if (local.y() < -0.5f) {
            base = QQuaternion::fromAxisAndAngle(1.0f, 0.0f, 0.0f, -90.0f);
        } else if (local.z() < -0.5f) {
            base = QQuaternion::fromAxisAndAngle(0.0f, 1.0f, 0.0f, 180.0f);
        } else {
            base = QQuaternion();
        }
        QQuaternion best = base;
        float bestAngle = 1e9f;
        for (float t = 0.0f; t < 360.0f; t += 90.0f) {
            const QQuaternion candidate = QQuaternion::fromAxisAndAngle(0.0f, 0.0f, 1.0f, t) * base;
            const float dot = std::abs(QQuaternion::dotProduct(candidate, m_rotation));
            const float angle = 2.0f * std::acos(std::clamp(dot, -1.0f, 1.0f));
            if (angle < bestAngle) {
                bestAngle = angle;
                best = candidate;
            }
        }
        m_rotation = best.normalized();
        setOrbitAnglesFromRotation();
        update();
    } else if (e->key() == Qt::Key_O) {
        ortho_mode_ = !ortho_mode_;
        updateProjection();
        update();
    } else if (e->key() == Qt::Key_A) {
        axes_visible_ = !axes_visible_;
        update();
    } else if (e->key() == Qt::Key_L) {
        rotation_locked_ = !rotation_locked_;
        update();
    } else if (e->key() == Qt::Key_S) {
        setSelectionEnabled(!selection_enabled_);
        e->accept();
        return;
    } else if (e->key() == Qt::Key_Left) {
        orbitRotate(-90.0f, 0.0f);
    } else if (e->key() == Qt::Key_Right) {
        orbitRotate(90.0f, 0.0f);
    } else if (e->key() == Qt::Key_Up) {
        orbitRotate(0.0f, -90.0f);
    } else if (e->key() == Qt::Key_Down) {
        orbitRotate(0.0f, 90.0f);
    }
    QOpenGLWidget::keyPressEvent(e);
}

void VoxelWidget::keyReleaseEvent(QKeyEvent* e) {
    if (e->key() == Qt::Key_Shift) {
        m_isShiftPressed = false;
    }
    QOpenGLWidget::keyReleaseEvent(e);
}

QVector3D VoxelWidget::localFaceClosestTo(const QVector3D& dir) const {
    QMatrix4x4 rot;
    rot.rotate(m_rotation);
    const QVector3D localAxes[] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
    QVector3D best;
    float bestDot = -2.0f;
    for (const auto& axis : localAxes) {
        const float dot = QVector3D::dotProduct(rot.mapVector(axis), dir);
        if (dot > bestDot) {
            bestDot = dot;
            best = axis;
        }
    }
    return best;
}

void VoxelWidget::setOrbitAnglesFromRotation() {
    QMatrix4x4 rotationMatrix;
    rotationMatrix.rotate(m_rotation);
    constexpr float radiansToDegrees = 57.29577951308232f;
    const float sinPitch = std::clamp(rotationMatrix(2, 1), -1.0f, 1.0f);
    orbit_pitch_degrees_ = std::asin(sinPitch) * radiansToDegrees;
    orbit_yaw_degrees_ = std::atan2(rotationMatrix(0, 2), rotationMatrix(0, 0)) * radiansToDegrees;
    updateRotationFromOrbitAngles();
}

void VoxelWidget::updateRotationFromOrbitAngles() {
    m_rotation = QQuaternion::fromAxisAndAngle(1.0f, 0.0f, 0.0f, orbit_pitch_degrees_) *
                 QQuaternion::fromAxisAndAngle(0.0f, 1.0f, 0.0f, orbit_yaw_degrees_);
    m_rotation.normalize();
}

void VoxelWidget::orbitRotate(float yawDegrees, float pitchDegrees) {
    // Keep the viewer in a roll-free turntable state. Rebuilding from yaw/pitch
    // avoids accumulated quaternion roll and keeps yaw on the model's middle axis.
    orbit_yaw_degrees_ += yawDegrees;
    orbit_pitch_degrees_ += pitchDegrees;
    updateRotationFromOrbitAngles();
    update();
}
