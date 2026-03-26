#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;
layout (location = 2) in vec4 aColor;

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;

out vec4 vColor;
out vec3 vNormal;
out vec3 vFragPos;

void main()
{
    vFragPos = vec3(model * vec4(aPos, 1.0));
    
    vNormal = mat3(transpose(inverse(model))) * aNormal;
    vColor = aColor;
    gl_Position = projection * view * model * vec4(aPos, 1.0);
}