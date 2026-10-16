// The display capture's blend at the port's resolution (host/render/gfx_render.c): a VRAM bank's copy
// becomes engine A's picture (source A) and the bank before (source B) blended as the DS blends them,
// (A * EVA + B * EVB) / 16 (GBATEK, "DS Video Display Capture").

[[vk::image_format("rgba8")]] RWTexture2D<unorm float4> source_a : register(u0, space1);  // read only here
[[vk::image_format("rgba8")]] RWTexture2D<unorm float4> source_b : register(u1, space1);  // read only here
[[vk::image_format("rgba8")]] RWTexture2D<unorm float4> blended : register(u2, space1);

cbuffer Params : register(b0, space2) {
    uint width;
    uint height;
    uint eva;  // 0-16
    uint evb;  // 0-16; 0: source B is not read
};

[numthreads(8, 8, 1)]
void cs_main(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= width || id.y >= height) {
        return;
    }
    float3 color = source_a[id.xy].rgb * (float)eva;
    if (evb != 0) {
        color += source_b[id.xy].rgb * (float)evb;
    }
    blended[id.xy] = float4(min(color / 16.0f, 1.0f), 1.0f);
}
