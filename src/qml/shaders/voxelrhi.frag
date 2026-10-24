#version 440
layout(location = 0) in vec4 vColor;
layout(location = 1) in vec3 vNormal;
layout(location = 0) out vec4 fragColor;

float faceShade(vec3 n) {
    n = normalize(n);
    if (abs(n.y) >= abs(n.x) && abs(n.y) >= abs(n.z))
        return n.y > 0.0 ? 1.00 : 0.64;
    if (abs(n.x) >= abs(n.z))
        return 0.78;
    return 0.91;
}

void main() {
    if (vColor.a <= 0.0)
        discard;
    float shade = dot(vNormal, vNormal) > 0.000001 ? faceShade(vNormal) : 1.0;
    fragColor = vec4(vColor.rgb * shade, vColor.a);
}
