#version 410 core
in vec3 aPos;
in vec2 aUV;      // the corner of the sprite, 0..1
in vec4 aColor;
in float aShape;  // 0 soft, 1 spark, 2 smoke
uniform mat4 uView;
uniform mat4 uProj;
out vec2 vUV;
out vec4 vColor;
flat out float vShape;
void main() {
    vec4 viewPos = uView * vec4(aPos, 1.0);
    // Fade out close to the eye, so a particle never pops across the lens.
    float fade = smoothstep(0.15, 0.7, -viewPos.z);
    vUV = aUV;
    vColor = vec4(aColor.rgb, aColor.a * fade);
    vShape = aShape;
    gl_Position = uProj * viewPos;
}
