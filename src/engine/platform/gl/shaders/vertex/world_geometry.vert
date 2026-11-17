#version 410 core
in vec3 aPos;
in vec3 aNormal;
in vec2 aUV;
uniform mat4 uView;
uniform mat4 uProj;
uniform mat4 uModel;
out vec3 vWorld;
out vec3 vNormal;
out vec2 vUV;
void main() {
    vec4 w = uModel * vec4(aPos, 1.0);
    vWorld = w.xyz;
    vNormal = mat3(uModel) * aNormal;
    vUV = aUV;
    gl_Position = uProj * uView * w;
}
