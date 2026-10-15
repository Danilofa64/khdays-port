// A picture made larger than it is shown, shrunk to the size it is shown at
// (host/render/gfx_render.c): each texel the average of the big picture's area under
// it, each big texel weighted by how much of it the texel covers. The 3D's
// edges, drawn at the larger size, come out smooth (the video setting
// antialiasing), and the window shows the picture at its own pixels instead
// of stretching a smaller one.

[[vk::image_format("rgba8")]] RWTexture2D<unorm float4> big : register(u0, space1);   // read only here
[[vk::image_format("rgba8")]] RWTexture2D<unorm float4> small : register(u1, space1);

cbuffer Params : register(b0, space2) {
    uint width;      // the small picture's texels
    uint height;
    uint big_width;  // the big one's, at least as many
    uint big_height;
};

[numthreads(8, 8, 1)]
void cs_main(uint3 id : SV_DispatchThreadID)
{
    float4 sum = float4(0.0f, 0.0f, 0.0f, 0.0f);
    float total = 0.0f;
    if (id.x >= width || id.y >= height) {
        return;
    }
    {
        const float fx = (float)big_width / (float)width, fy = (float)big_height / (float)height;
        const float x0 = (float)id.x * fx, x1 = x0 + fx, y0 = (float)id.y * fy, y1 = y0 + fy;
        const int bx0 = (int)floor(x0), bx1 = min((int)ceil(x1), (int)big_width);
        const int by0 = (int)floor(y0), by1 = min((int)ceil(y1), (int)big_height);
        for (int y = by0; y < by1; ++y) {
            const float wy = min((float)y + 1.0f, y1) - max((float)y, y0);
            for (int x = bx0; x < bx1; ++x) {
                const float w = (min((float)x + 1.0f, x1) - max((float)x, x0)) * wy;
                if (w > 0.0f) {
                    sum += big[uint2((uint)x, (uint)y)] * w;
                    total += w;
                }
            }
        }
    }
    small[id.xy] = total > 0.0f ? sum / total : float4(0.0f, 0.0f, 0.0f, 1.0f);
}
