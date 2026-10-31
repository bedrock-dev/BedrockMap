#version 440
layout(location = 0) in vec3 position;
layout(location = 1) in vec3 normal;
layout(location = 2) in vec4 color;

layout(location = 0) out vec4 vColor;
layout(location = 1) out vec3 vNormal;

layout(std140, binding = 0) uniform buf {
    mat4 uMvp;
    vec4 uTone;
};

void main() {
    vColor = color;
    vNormal = normal;
    gl_Position = uMvp * vec4(position, 1.0);
}
