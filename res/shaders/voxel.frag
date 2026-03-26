#version 330 core
in vec4 vColor;
in vec3 vNormal;
in vec3 vFragPos;

uniform vec3 lightPos;
uniform vec3 lightColor;
uniform vec3 ambientLight;

out vec4 FragColor;

void main()
{
    vec3 ambient = ambientLight * vColor.rgb;

    vec3 norm = normalize(vNormal);
    vec3 lightDir = normalize(lightPos - vFragPos);
    float diff = max(dot(norm, lightDir), 0.0);
    vec3 diffuse = diff * lightColor * vColor.rgb * 0.8;

    vec3 result = ambient + diffuse;
    FragColor = vec4(result, vColor.a);

    if (FragColor.a <= 0.0) discard;
}