#version 410 core
in vec3 aPos;
in vec2 aUV;      // metres along the pane and up it: droplets are laid out in them
uniform mat4 uView;
uniform mat4 uProj;
out vec3 vWorld;
out vec2 vUV;
void main() {
    vWorld = aPos;
    vUV = aUV;
    gl_Position = uProj * uView * vec4(aPos, 1.0);
}
