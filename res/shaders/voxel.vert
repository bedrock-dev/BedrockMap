#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;
layout (location = 2) in vec4 aColor;

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;

out vec4 vColor;
out vec3 vNormal;

void main()
{
    // The normal is passed through in model space on purpose: faceShade() in the fragment
    // shader is a fixed per-direction brightness (the game's convention), so a face has to keep
    // its shade no matter how the camera is turned. Rotating it here would make the shading
    // follow the view instead.
    vNormal = aNormal;
    vColor = aColor;
    gl_Position = projection * view * model * vec4(aPos, 1.0);
}