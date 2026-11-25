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
uniform sampler2D uMorphTex;  // morph targets: two texels (position, normal delta) a vertex a target, 2048 to a row
uniform int uMorphCount;      // targets the batch blends (0: none)
uniform int uMorphVerts;      // vertices in the batch (a target's stride)
uniform float uMorphW[8];     // their weights
uniform sampler2D uBoneTex;  // uSkinned 2: 64 matrices a row, a row per instance of a run
uniform mat4 uView;
uniform mat4 uProj;
uniform mat4 uModel;
out vec3 vWorld;
out vec3 vNormal;
out vec2 vUV;
mat4 boneAt(int bone) {
    int x = bone * 4, y = gl_InstanceID;
    return mat4(texelFetch(uBoneTex, ivec2(x, y), 0), texelFetch(uBoneTex, ivec2(x + 1, y), 0),
                texelFetch(uBoneTex, ivec2(x + 2, y), 0), texelFetch(uBoneTex, ivec2(x + 3, y), 0));
}
void main() {
    vec4 p = vec4(aPos, 1.0);
    vec3 n = aNormal;
    // Morph targets move the vertex before the bones do.
    for (int t = 0; t < 8; ++t) {
        if (t >= uMorphCount) break;
        float w = uMorphW[t];
        if (w == 0.0) continue;
        int i = (t * uMorphVerts + gl_VertexID) * 2;
        p.xyz += w * texelFetch(uMorphTex, ivec2(i % 2048, i / 2048), 0).xyz;
        i += 1;
        n += w * texelFetch(uMorphTex, ivec2(i % 2048, i / 2048), 0).xyz;
    }
    if (uSkinned == 1) {
        mat4 s = aWeights.x * uBones[int(aJoints.x)] +
                 aWeights.y * uBones[int(aJoints.y)] +
                 aWeights.z * uBones[int(aJoints.z)] +
                 aWeights.w * uBones[int(aJoints.w)];
        p = s * p;
        n = mat3(s) * n;
    } else if (uSkinned == 2) {
        mat4 s = aWeights.x * boneAt(int(aJoints.x)) +
                 aWeights.y * boneAt(int(aJoints.y)) +
                 aWeights.z * boneAt(int(aJoints.z)) +
                 aWeights.w * boneAt(int(aJoints.w));
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
