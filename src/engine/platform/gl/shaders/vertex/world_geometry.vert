#version 410 core
in vec3 aPos;
in vec3 aNormal;
in vec2 aUV;
in vec4 aJoints;   // skinned batches only: bone indices and weights
in vec4 aWeights;
in mat4 aModel;      // per instance (locations 5..8), read when uInstanced is 1
uniform int uInstanced;
uniform int uSkinned;  // 1 when the batch carries a skin
uniform mat4 uBones[64];
uniform mat4 uView;
uniform mat4 uProj;
uniform mat4 uModel;
out vec3 vWorld;
out vec3 vNormal;
out vec2 vUV;
void main() {
    vec4 p = vec4(aPos, 1.0);
    vec3 n = aNormal;
    if (uSkinned == 1) {
        mat4 s = aWeights.x * uBones[int(aJoints.x)] +
                 aWeights.y * uBones[int(aJoints.y)] +
                 aWeights.z * uBones[int(aJoints.z)] +
                 aWeights.w * uBones[int(aJoints.w)];
        p = s * p;
        n = mat3(s) * n;
    }
    mat4 model = uInstanced == 1 ? aModel : uModel;
    vec4 w = model * p;
    vWorld = w.xyz;
    vNormal = mat3(model) * n;
    vUV = aUV;
    gl_Position = uProj * uView * w;
}
