#ifndef VOXELWIDGET_H
#define VOXELWIDGET_H

#include <QColor>
#include <QMatrix4x4>
#include <QOpenGLFunctions_3_3_Core>
#include <QOpenGLShaderProgram>
#include <QOpenGLWidget>
#include <QVector3D>
#include <vector>

#include "chunk.h"

// 体素数据结构：恢复transparent参数，双判定（优先级：transparent > alpha）
struct Voxel {
    QColor color;              // 颜色（alpha通道辅助判定）
    bool transparent = false;  // 主透明标记（true=透明，false=不透明）
    Voxel() : color(Qt::white) {}
    Voxel(const QColor& c, bool trans = false) : color(c), transparent(trans) {}
};

class VoxelWidget : public QOpenGLWidget, protected QOpenGLFunctions_3_3_Core {
    Q_OBJECT

   public:
    explicit VoxelWidget(QWidget* parent = nullptr);
    ~VoxelWidget() override;

    void setLayer(int startLayer, int endLayer);
    void updateVoxelData(const std::vector<std::vector<std::vector<Voxel>>>& newData);
    void forceInitAndRender();

    static std::vector<std::vector<std::vector<Voxel>>> createVoxelDataFromChunks(const std::vector<std::vector<bl::chunk*>>& chunks);

   protected:
    void initializeGL() override;
    void resizeGL(int w, int h) override;
    void paintGL() override;

    void mousePressEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void wheelEvent(QWheelEvent* e) override;
    void showEvent(QShowEvent* e) override;
    void keyPressEvent(QKeyEvent* e) override;
    void keyReleaseEvent(QKeyEvent* e) override;

   private:
    // 核心：邻居判定（同时考虑transparent和alpha）
    bool hasNeighbor(int layer, int x, int z, int dLayer, int dX, int dZ);
    void buildVoxelVertices();
    void addFaceVertices(int layer, int x, int z, const Voxel& voxel, const std::vector<float>& faceVertices, const QVector3D& normal);
    void initOpenGLBuffers();

    // OpenGL 资源（分离不透明/半透明）
    QOpenGLShaderProgram* m_shaderProgram = nullptr;
    GLuint m_vaoOpaque = 0, m_vaoTransparent = 0;
    GLuint m_vboOpaque = 0, m_vboTransparent = 0;
    GLuint m_eboOpaque = 0, m_eboTransparent = 0;

    // 顶点数据（格式：3位置 + 3法线 + 4颜色(RGBA)）
    std::vector<float> m_verticesOpaque;
    std::vector<float> m_verticesTransparent;
    std::vector<GLuint> m_indicesOpaque;
    std::vector<GLuint> m_indicesTransparent;

    // 体素数据
    std::vector<std::vector<std::vector<Voxel>>> m_voxelData;
    int m_startLayer = 0;
    int m_endLayer = 0;

    // 视角控制
    QPoint m_lastMousePos;
    float m_rotateX = 0.0f;
    float m_rotateY = 0.0f;
    float m_scale = 1.0f;
    float m_voxelSize = 1.0f;

    QVector3D m_cameraTranslate;   // 相机平移偏移（X/Y/Z轴）
    bool m_isPanDragging{false};   // 是否正在平移拖拽
    QPoint m_panStartPos;          // 平移起始鼠标位置
    float m_panSensitivity{0.3};   // 平移灵敏度
    bool m_isShiftPressed{false};  // Shift键是否按下（区分前后/左右平移）

    // 矩阵
    QMatrix4x4 m_projection;
    QMatrix4x4 m_view;
    QMatrix4x4 m_model;

    // 光照参数
    QVector3D m_lightPos = QVector3D(10.0f, 20.0f, 10.0f);
    QVector3D m_lightColor = QVector3D(1.0f, 1.0f, 1.0f);
    QVector3D m_ambientLight = QVector3D(0.3f, 0.3f, 0.3f);

    // 立方体面模板和法线
    static const std::vector<std::vector<float>> m_faceTemplates;
    static const std::vector<QVector3D> m_faceNormals;
};

#endif  // VOXELWIDGET_H