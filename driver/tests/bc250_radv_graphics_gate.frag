#version 450

/*
 * BC-250 RADV Graphics Gate - Fragment Shader (MARCO3_GRAPHICS_DRAW)
 * Target: AMD BC-250 APU (1002:13FE, Cyan Skillfish 2 / GFX1013)
 * Output Format: VK_FORMAT_B8G8R8A8_UNORM (2560 x 1080)
 *
 * Fully conforms to the Byte Proof contract across 8 sampling zones:
 * Frame 0: Strict static SMPTE calibration pattern + BC250 Signature Gold badge
 * Frame > 0: Multi-act dynamic animation (DVD bounce, Carousel, Volumetric wave)
 */

layout(location = 0) in vec2 in_uv;
layout(location = 0) out vec4 out_color;

layout(push_constant) uniform PushConstants {
    uint frame_index;
    uint total_frames;
} pc;

uint get_smpte_color(uint barIndex)
{
    switch (barIndex) {
        case 0u: return 0xFFFFFFFFu; // White   (B=FF, G=FF, R=FF, A=FF)
        case 1u: return 0xFF00FFFFu; // Yellow  (B=00, G=FF, R=FF, A=FF)
        case 2u: return 0xFFFFFF00u; // Cyan    (B=FF, G=FF, R=00, A=FF)
        case 3u: return 0xFF00FF00u; // Green   (B=00, G=FF, R=00, A=FF)
        case 4u: return 0xFFFF00FFu; // Magenta (B=FF, G=00, R=FF, A=FF)
        case 5u: return 0xFF0000FFu; // Red     (B=00, G=00, R=FF, A=FF)
        case 6u: return 0xFFFF0000u; // Blue    (B=FF, G=00, R=00, A=FF)
        default: return 0xFF000000u;
    }
}

uint get_badge_gold(uint frame)
{
    if (frame == 0u) {
        return 0xFFBC2501u; // R=0x25, G=0xBC, B=0x01, A=0xFF
    }
    uint pulse = (frame * 4u) % 120u;
    uint wave = (pulse < 60u) ? pulse : (120u - pulse); // 0..60
    uint g_comp = 0xBCu + (wave / 3u); // 188..208
    uint b_comp = 0x01u + (wave / 2u); // 1..31
    return 0xFF000000u | (0x25u << 16u) | (g_comp << 8u) | b_comp;
}

vec4 unpack_color(uint c)
{
    float b = float( c         & 0xFFu) / 255.0;
    float g = float((c >> 8u)  & 0xFFu) / 255.0;
    float r = float((c >> 16u) & 0xFFu) / 255.0;
    float a = float((c >> 24u) & 0xFFu) / 255.0;
    return vec4(r, g, b, a);
}

void main()
{
    // gl_FragCoord.xy are pixel center coordinates [0.5 .. width-0.5]
    uint x = uint(gl_FragCoord.x);
    uint y = uint(gl_FragCoord.y);

    if (x >= 2560u || y >= 1080u) {
        discard;
    }

    uint color = 0xFF000000u; // Default Black

    // =========================================================================
    // FRAME 0: CALIBRATION PATTERN (Byte Proof 8/8 zones exact contract)
    // =========================================================================
    if (pc.frame_index == 0u) {
        if (x >= 1130u && x <= 1430u && y >= 440u && y <= 640u) {
            if (x <= 1134u || x >= 1426u || y <= 444u || y >= 636u) {
                color = 0xFF000000u; // Black border
            } else if (x <= 1138u || x >= 1422u || y <= 448u || y >= 632u) {
                color = 0xFFFFFFFFu; // White inner border
            } else {
                color = 0xFFBC2501u; // BC250 Gold core
            }
        } else if (y < 810u) {
            uint barWidth = 2560u / 7u; // ~365 pixels
            uint barIndex = x / barWidth;
            if (barIndex > 6u) barIndex = 6u;
            color = get_smpte_color(barIndex);
        } else {
            uint intensity = (x * 255u) / 2560u;
            color = 0xFF000000u | (intensity << 16u) | (intensity << 8u) | intensity;
        }

        out_color = unpack_color(color);
        return;
    }

    // =========================================================================
    // MULTI-ACT CONTINUOUS TECH DEMO (pc.frame_index > 0u)
    // =========================================================================
    uint total = max(pc.total_frames, 1u);
    uint phase_len = max(total / 3u, 1u);
    uint phase = min((pc.frame_index - 1u) / phase_len, 2u);
    uint local_frame = (pc.frame_index - 1u) % phase_len;

    // --- ACT 0: BOUNCING LOGO (DVD Screensaver over clean static SMPTE bars) ---
    if (phase == 0u) {
        uint period_x = 2260u * 2u;
        uint period_y = 880u * 2u;
        uint step_x = (local_frame * 8u) % period_x;
        uint bx = (step_x <= 2260u) ? step_x : (period_x - step_x);
        uint step_y = (local_frame * 5u) % period_y;
        uint by = (step_y <= 880u) ? step_y : (period_y - step_y);

        if (x >= bx && x < (bx + 300u) && y >= by && y < (by + 200u)) {
            uint rel_x = x - bx;
            uint rel_y = y - by;
            if (rel_x < 4u || rel_x >= 296u || rel_y < 4u || rel_y >= 196u) {
                color = 0xFF000000u;
            } else if (rel_x < 8u || rel_x >= 292u || rel_y < 8u || rel_y >= 192u) {
                color = 0xFFFFFFFFu;
            } else {
                color = get_badge_gold(local_frame);
            }
        } else if (y < 810u) {
            uint barIndex = (x * 7u) / 2560u;
            if (barIndex > 6u) barIndex = 6u;
            color = get_smpte_color(barIndex);
        } else {
            uint intensity = (x * 255u) / 2560u;
            color = 0xFF000000u | (intensity << 16u) | (intensity << 8u) | intensity;
        }
    }
    // --- ACT 1: SEAMLESS CAROUSEL (Smooth continuous subpixel scrolling) ---
    else if (phase == 1u) {
        if (x >= 1130u && x <= 1430u && y >= 440u && y <= 640u) {
            if (x <= 1134u || x >= 1426u || y <= 444u || y >= 636u) {
                color = 0xFF000000u;
            } else if (x <= 1138u || x >= 1422u || y <= 448u || y >= 632u) {
                color = 0xFFFFFFFFu;
            } else {
                color = get_badge_gold(local_frame);
            }
        } else if (y < 810u) {
            float scroll_px = float((local_frame * 8u) % 2560u);
            float u = fract((float(x) + (2560.0 - scroll_px)) / 2560.0);
            uint barIndex = uint(u * 7.0);
            if (barIndex > 6u) barIndex = 6u;
            color = get_smpte_color(barIndex);
        } else {
            float scroll_px = float((local_frame * 8u) % 2560u);
            float u = fract((float(x) + (2560.0 - scroll_px)) / 2560.0);
            uint intensity = uint(u * 255.0);
            color = 0xFF000000u | (intensity << 16u) | (intensity << 8u) | intensity;
        }
    }
    // --- ACT 2: VOLUMETRIC NEON WAVE (Soft glowing light wave over static bars) ---
    else {
        if (x >= 1130u && x <= 1430u && y >= 440u && y <= 640u) {
            if (x <= 1134u || x >= 1426u || y <= 444u || y >= 636u) {
                color = 0xFF000000u;
            } else if (x <= 1138u || x >= 1422u || y <= 448u || y >= 632u) {
                color = 0xFFFFFFFFu;
            } else {
                color = get_badge_gold(local_frame);
            }
        } else if (y < 810u) {
            uint barIndex = (x * 7u) / 2560u;
            if (barIndex > 6u) barIndex = 6u;
            color = get_smpte_color(barIndex);
        } else {
            uint intensity = (x * 255u) / 2560u;
            color = 0xFF000000u | (intensity << 16u) | (intensity << 8u) | intensity;
        }

        float wave_center = float((local_frame * 12u) % 2560u);
        float dist = abs(float(x) - wave_center);
        dist = min(dist, 2560.0 - dist);
        if (dist < 220.0) {
            float glow = 0.5 + 0.5 * cos(dist * 3.14159265 / 220.0);
            uint r = (color >> 16) & 0xFFu;
            uint g = (color >> 8) & 0xFFu;
            uint b = color & 0xFFu;
            r = min(255u, r + uint(float(255u - r) * 0.7 * glow));
            g = min(255u, g + uint(float(255u - g) * 0.7 * glow));
            b = min(255u, b + uint(float(255u - b) * 0.7 * glow));
            color = 0xFF000000u | (r << 16) | (g << 8) | b;
        }
    }

    out_color = unpack_color(color);
}
