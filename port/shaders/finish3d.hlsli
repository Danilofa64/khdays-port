// Finishes the 3D raster3d.hlsl drew, as port/gpu/gpu3d.c does: edge
// marking, then fog. It is shared by the compose shaders. The includer
// declares color3d and attr3d (raster3d's targets) and, in its constant
// buffer, width, height, disp3dcnt, swap_params, clear_color, clear_depth,
// fog_color, fog_offset, edge_colors, fog_table and fine (1 when the 3D's
// channels have 8 bits: raster3d.hlsl's fine).

int expand5(int c)
{
    return c != 0 ? c * 2 + 1 : 0;
}

// A 5-bit colour channel at the 3D's precision.
int finish_channel5(int c)
{
    return fine != 0u ? (c << 3) | (c >> 2) : expand5(c);
}

uint edge_color(uint index)
{
    const uint word = edge_colors[index >> 1];
    return (index & 1u) != 0u ? word >> 16 : word & 0xffffu;
}

uint fog_density(uint index)
{
    return (fog_table[index >> 4][(index >> 2) & 3u] >> ((index & 3u) * 8u)) & 0x7fu;
}

// RGB 6 bits each (8 when fine) in bits 0-7/8-15/16-23, alpha 0-31 in 24-28.
uint finished3d(int x, int y)
{
    const uint c = color3d[uint2(x, y)];
    const uint att = attr3d[uint2(x, y)];
    const uint cmask = fine != 0u ? 255u : 63u;
    int r = (int)(c & cmask), g = (int)((c >> 8) & cmask), b = (int)((c >> 16) & cmask), a = (int)((c >> 24) & 31u);
    const uint translucent = c >> 31;
    const uint depth = att & 0xffffffu, id = (att >> 24) & 63u, edge = (att >> 30) & 1u, fog = att >> 31;
    if ((disp3dcnt & 0x20u) != 0u && edge != 0u && translucent == 0u) {
        const uint cd = clear_depth & 0x7fffu;
        const uint clear_d = cd * 0x200u + ((cd + 1u) / 0x8000u) * 0x1ffu;
        const uint clear_id = (clear_color >> 24) & 63u;
        const int dx[4] = {-1, 1, 0, 0}, dy[4] = {0, 0, -1, 1};
        for (int k = 0; k < 4; ++k) {
            const int nx = x + dx[k], ny = y + dy[k];
            uint nid, nd;
            if (nx < 0 || nx >= (int)width || ny < 0 || ny >= (int)height) {
                nid = clear_id;
                nd = clear_d;
            } else {
                const uint natt = attr3d[uint2(nx, ny)];
                nid = (natt >> 24) & 63u;
                nd = natt & 0xffffffu;
            }
            if (nid != id && depth < nd) {
                const uint e = edge_color(id >> 3);
                r = finish_channel5((int)(e & 31u));
                g = finish_channel5((int)((e >> 5) & 31u));
                b = finish_channel5((int)((e >> 10) & 31u));
                break;
            }
        }
    }
    if ((disp3dcnt & 0x80u) != 0u && fog != 0u) {
        const uint wbuffer = (swap_params >> 1) & 1u;
        const uint offset = fog_offset & 0x7fffu;
        const uint shift = (disp3dcnt >> 8) & 15u;
        const uint step = 0x400u >> shift;
        const int fr = finish_channel5((int)(fog_color & 31u)), fg = finish_channel5((int)((fog_color >> 5) & 31u)),
                  fb = finish_channel5((int)((fog_color >> 10) & 31u));
        const int fa = (int)((fog_color >> 16) & 31u);
        const uint d = wbuffer != 0u ? depth : depth >> 9; // the 15-bit scale of FOG_OFFSET
        int density;
        if (step == 0u || d <= offset + step) {
            density = (int)fog_density(0u);
        } else if (d >= offset + step * 32u) {
            density = (int)fog_density(31u);
        } else {
            const uint n = (d - offset) / step - 1u;
            const uint lo = offset + step * (n + 1u);
            const int d0 = (int)fog_density(n), d1 = (int)fog_density(n + 1u);
            density = d0 + (d1 - d0) * (int)(d - lo) / (int)step;
        }
        if (density == 127) density = 128;
        if ((disp3dcnt & 0x40u) == 0u) {
            r = (fr * density + r * (128 - density)) / 128;
            g = (fg * density + g * (128 - density)) / 128;
            b = (fb * density + b * (128 - density)) / 128;
        }
        a = (fa * density + a * (128 - density)) / 128;
    }
    return (uint)r | ((uint)g << 8) | ((uint)b << 16) | ((uint)a << 24) | (c & 0x20000000u); // over the 2D
}
