#version 410 core
// The glass of a mirror: the picture the reflected pass drew, found by where this pixel is on the screen, and the
// glare of the hand light on it (the torch or the lighter): the spot where its beam strikes the glass, a streak
// through it, and the lens glint of the light's reflection with a few ghosts along the line through the glass centre.
in vec3 vWorld;
uniform sampler2D uTex;
uniform vec2 uRes;      // size of the main target
uniform vec3 uTint;
uniform float uAlpha;
uniform float uFlare;       // 0 none .. 1: how strong the glare is (0 while no hand light is on)
uniform vec3 uFlareOrigin;  // the hand light's world position (the torch's tip)
uniform vec3 uFlareDir;     // the way the beam points
uniform vec3 uFlareColor;
uniform vec3 uEye;
uniform vec3 uGlassCenter;
uniform vec3 uGlassNormal;  // horizontal, into the room
uniform vec3 uGlassAlong;   // horizontal, along the glass
out vec4 outColor;

float spot(vec2 d, float r) { return exp(-dot(d, d) / (r * r)); }

void main() {
    vec3 c = texture(uTex, gl_FragCoord.xy / uRes).rgb * uTint;
    vec3 glare = vec3(0.0);
    if (uFlare > 0.001) {
        vec3 rel = vWorld - uGlassCenter;
        vec2 p = vec2(dot(rel, uGlassAlong), rel.y);  // this pixel on the glass, metres
        vec2 centre = vec2(0.0);
        // Where the beam strikes the glass plane (it must be going into the glass).
        float facing = dot(uFlareDir, uGlassNormal);
        if (facing < -0.05) {
            float t = dot(uGlassCenter - uFlareOrigin, uGlassNormal) / facing;
            if (t > 0.0) {
                vec3 hit = uFlareOrigin + uFlareDir * t;
                vec3 hr = hit - uGlassCenter;
                vec2 h = vec2(dot(hr, uGlassAlong), hr.y);
                float reach = 1.0 / (1.0 + 0.35 * t);
                float aim = clamp((-facing - 0.05) / 0.75, 0.0, 1.0);  // squarer on means brighter
                glare += uFlareColor * (spot(p - h, 0.16 + 0.04 * t) * 0.9 + spot(p - h, 0.45) * 0.25) * reach * aim;
                // an anamorphic streak through the spot, along the glass
                glare += uFlareColor * exp(-abs(p.y - h.y) / 0.012) * exp(-abs(p.x - h.x) / 0.55) * 0.35 * reach * aim;
                // ghosts: soft discs on the line from the spot through the glass centre
                glare += uFlareColor * vec3(0.7, 0.9, 1.0) * spot(p - (centre - (h - centre) * 0.6), 0.07) * 0.22 * reach * aim;
                glare += uFlareColor * vec3(1.0, 0.7, 0.5) * spot(p - (centre - (h - centre) * 1.2), 0.11) * 0.14 * reach * aim;
            }
        }
        // The light's own reflection: its image lies behind the glass; seen from the eye it is where the line to
        // that image crosses the glass.
        vec3 image = uFlareOrigin - 2.0 * dot(uFlareOrigin - uGlassCenter, uGlassNormal) * uGlassNormal;
        float den = dot(image - uEye, uGlassNormal);
        if (abs(den) > 1e-4) {
            float t2 = dot(uGlassCenter - uEye, uGlassNormal) / den;
            if (t2 > 0.0 && t2 < 1.0) {
                vec3 ip = uEye + (image - uEye) * t2 - uGlassCenter;
                vec2 g = vec2(dot(ip, uGlassAlong), ip.y);
                glare += uFlareColor * (spot(p - g, 0.05) * 1.1 + spot(p - g, 0.2) * 0.22);
                glare += uFlareColor * exp(-abs(p.y - g.y) / 0.01) * exp(-abs(p.x - g.x) / 0.4) * 0.3;
            }
        }
    }
    outColor = vec4(c + glare * uFlare, uAlpha);
}
