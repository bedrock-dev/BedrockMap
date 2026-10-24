#version 440
layout(location = 0) in vec4 vertex;
layout(location = 1) in vec2 texcoord;
layout(location = 0) out vec2 v_texcoord;
layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    vec4 uView;
    vec4 uParams;
};
void main() {
    v_texcoord = texcoord;
    gl_Position = qt_Matrix * vertex;
}
