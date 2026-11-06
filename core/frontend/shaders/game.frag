#version 450
// Game picture shader (our own). Runs as an SDL3 GPU-renderer custom render state: SDL's vertex shader
// feeds v_color (location 0) and v_uv (location 1, texture space 0..1, so scanlines follow the game's
// rows whatever the rotation). The game texture is sampled bilinear; every mode reconstructs from it.
//   mode 0 sharp:      sharp-bilinear (clean pixels at any, including non-integer, scale)
//   mode 1 crt:        gaussian scanline beams + aperture-grille mask + gamma
//   mode 2 crt curved: mode 1 on a curved tube with rounded corners and a soft vignette
// GPL-2.0-or-later.
layout(location = 0) in vec4 v_color;
layout(location = 1) in vec2 v_uv;
layout(location = 0) out vec4 o_color;
layout(set = 2, binding = 0) uniform sampler2D u_tex;
layout(set = 3, binding = 0) uniform Params {
    vec4 src;    // texture w, h, 1/w, 1/h
    vec4 dst;    // picture size in output pixels along the texture's u and v axes; z = mask axis (0 = x, 1 = y on screen)
    vec4 opt;    // x mode, y scanline strength, z mask strength, w curvature
};

vec3 sharp(vec2 uv)
{
    vec2 texel = uv * src.xy;
    vec2 scale = max(dst.xy * src.zw, vec2(1.0));
    vec2 base = floor(texel), d = texel - base - 0.5;
    vec2 range = 0.5 - 0.5 / scale;
    vec2 f = (d - clamp(d, -range, range)) * scale + 0.5;
    return texture(u_tex, (base + f) * src.zw).rgb;
}

vec3 lin(vec3 c) { return pow(c, vec3(2.4)); }

vec3 crt(vec2 uv)
{
    vec2 texel = uv * src.xy;
    float row = floor(texel.y), fy = texel.y - row - 0.5;          // -0.5..0.5 from the row centre
    float nrow = row + (fy < 0.0 ? -1.0 : 1.0);
    vec3 c0 = lin(sharp(vec2(uv.x, (row + 0.5) * src.w)));
    vec3 c1 = lin(sharp(vec2(uv.x, (clamp(nrow, 0.0, src.y - 1.0) + 0.5) * src.w)));
    float d0 = abs(fy), d1 = 1.0 - d0;
    // brighter lines bloom wider, like a real beam
    vec3 s0 = mix(vec3(0.26), vec3(0.44), sqrt(c0)), s1 = mix(vec3(0.26), vec3(0.44), sqrt(c1));
    vec3 w0 = exp(-0.5 * d0 * d0 / (s0 * s0)), w1 = exp(-0.5 * d1 * d1 / (s1 * s1));
    vec3 beam = c0 * w0 + c1 * w1;
    vec3 flt = mix(c0, c1, smoothstep(0.0, 1.0, d0) * 0.5);
    vec3 c = mix(flt, beam * 1.45, opt.y);
    // aperture grille, perpendicular to the scanlines on screen
    float p = mod(floor(dst.z > 0.5 ? gl_FragCoord.y : gl_FragCoord.x), 3.0);
    vec3 m = p < 1.0 ? vec3(1.0, 0.7, 0.7) : p < 2.0 ? vec3(0.7, 1.0, 0.7) : vec3(0.7, 0.7, 1.0);
    c *= mix(vec3(1.0), m * 1.18, opt.z);
    return pow(clamp(c, 0.0, 1.0), vec3(1.0 / 2.2));
}

void main()
{
    vec2 uv = v_uv;
    float edge = 1.0;
    if (opt.x > 1.5) {                                                // curved tube
        vec2 cc = uv * 2.0 - 1.0;
        vec2 k = vec2(0.045, 0.060) * opt.w;
        cc *= vec2(1.0 + cc.y * cc.y * k.x, 1.0 + cc.x * cc.x * k.y);
        uv = cc * 0.5 + 0.5;
        vec2 q = abs(cc) - (1.0 - 0.06);                              // rounded corners
        float dist = length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - 0.06;
        edge = clamp(-dist * 120.0, 0.0, 1.0);
        edge *= mix(1.0, 0.82, dot(cc, cc) * 0.5);                    // vignette
    }
    vec3 c = opt.x > 0.5 ? crt(uv) : sharp(uv);
    o_color = vec4(c * edge, 1.0) * v_color;
}
