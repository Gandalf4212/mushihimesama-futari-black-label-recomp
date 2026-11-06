#version 450
// Procedural bezel (our own design, no artwork): a dark cabinet surround with a bevelled frame around the
// screen and an ambient glow taken from the colours near the picture's edges. Drawn over the whole output
// with the game texture bound, before the picture itself is drawn on top. v_uv runs 0..1 over the output.
// GPL-2.0-or-later.
layout(location = 0) in vec4 v_color;
layout(location = 1) in vec2 v_uv;
layout(location = 0) out vec4 o_color;
layout(set = 2, binding = 0) uniform sampler2D u_tex;
layout(set = 3, binding = 0) uniform Params {
    vec4 rect;   // picture rect in output uv: x, y, w, h
    vec4 outp;   // output w, h (pixels); z rotation (0 none, 1 = 270 cw / yoko, 2 = 180); w glow strength
};

// output-local coordinates t (0..1 inside the picture rect) -> game texture uv
vec2 game_uv(vec2 t)
{
    t = clamp(t, 0.0, 1.0);
    if (outp.z > 1.5) return 1.0 - t;
    if (outp.z > 0.5) return vec2(1.0 - t.y, t.x);
    return t;
}

float rbox(vec2 p, vec2 b, float r) { vec2 q = abs(p) - b + r; return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - r; }

void main()
{
    vec2 px = v_uv * outp.xy;                                   // output pixels
    vec2 c = (rect.xy + rect.zw * 0.5) * outp.xy, half_ = rect.zw * 0.5 * outp.xy;
    float unit = outp.y / 1080.0;                               // sizes scale with the output height
    float d = rbox(px - c, half_ + 14.0 * unit, 10.0 * unit);   // distance outside the frame's inner edge

    // cabinet: near-black with a soft vertical falloff and a faint diagonal sheen
    float g = 0.050 - 0.020 * abs(v_uv.y - 0.45) + 0.006 * sin((v_uv.x + v_uv.y) * 3.0);
    vec3 col = vec3(g, g, g * 1.08);

    // bevelled frame: 14 px gap (black glass), then a 22 px rounded rim lit from above
    float rim = smoothstep(0.0, 1.5 * unit, d) * (1.0 - smoothstep(22.0 * unit, 23.5 * unit, d));
    float light = 0.5 + 0.5 * clamp(-(px.y - c.y) / (half_.y + 40.0 * unit), -1.0, 1.0);
    vec3 rimc = vec3(0.10, 0.10, 0.115) + 0.10 * light * smoothstep(20.0 * unit, 2.0 * unit, d);
    col = mix(col, rimc, rim);
    if (d < 0.0) col = vec3(0.006);                             // glass between picture and rim

    // ambient glow: average of the picture near the closest edge point, fading with distance
    vec2 t = (px - (c - half_)) / (2.0 * half_);
    vec2 e = clamp(t, 0.0, 1.0);
    // wide, soft average along the nearest edge (a 7x3 grid over +-35 % of the edge, 12 % deep), so the
    // glow follows the picture's overall colour there instead of streaking out each row
    vec3 a = vec3(0.0);
    bool side = abs(t.x - e.x) > abs(t.y - e.y);
    for (int i = -3; i <= 3; i++)
        for (int j = 0; j < 3; j++) {
            float along = float(i) * 0.115, depth = 0.02 + float(j) * 0.05;
            vec2 q = side ? vec2(e.x < 0.5 ? depth : 1.0 - depth, e.y + along) : vec2(e.x + along, e.y < 0.5 ? depth : 1.0 - depth);
            a += texture(u_tex, game_uv(q)).rgb;
        }
    a /= 21.0;
    float dist = length((t - e) * 2.0 * half_) / outp.y;
    col += a * outp.w * exp(-dist * 6.0) * (d > 22.0 * unit ? 1.0 : 0.35);

    o_color = vec4(col, 1.0) * v_color;
}
