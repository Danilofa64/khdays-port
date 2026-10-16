// The studio's 3D previews (models, rooms; studio/app/preview3d.cpp): positions through view and
// projection, the texture sampled as the DS does times the vertex colour, a light from the viewer's
// side, alpha cut and blended. SDL3's resource layout as the port's (port/tools/build_shaders.py).

cbuffer VertexUniforms : register(b0, space1)
{
    float4x4 view_projection;  // column-major, OpenGL-style depth (-1..1)
    float4 light;              // xyz: where light comes from (unit); w unused
};

struct VertexIn
{
    float3 position : TEXCOORD0;
    float3 normal : TEXCOORD1;
    float2 uv : TEXCOORD2;
    float4 colour : TEXCOORD3;
};

struct VertexOut
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
    float4 colour : TEXCOORD1;
};

VertexOut vs_main(VertexIn input)
{
    VertexOut output;
    float4 p = mul(view_projection, float4(input.position, 1.0));
    p.z = (p.z + p.w) * 0.5;  // to the graphics card's 0..1 depth
    output.position = p;
    output.uv = input.uv;
    float shade = 1.0;
    if (dot(input.normal, input.normal) > 1e-12)
    {
        shade = 0.35 + 0.65 * abs(dot(normalize(input.normal), light.xyz));
    }
    output.colour = float4(input.colour.rgb * shade, input.colour.a);
    return output;
}

[[vk::combinedImageSampler]][[vk::binding(0, 2)]]
Texture2D<float4> picture : register(t0, space2);
[[vk::combinedImageSampler]][[vk::binding(0, 2)]]
SamplerState picture_sampler : register(s0, space2);

cbuffer PixelUniforms : register(b0, space3)
{
    float4 cut;  // x: the alpha below which a pixel is not drawn
};

float4 ps_main(VertexOut input) : SV_Target
{
    float4 c = input.colour * picture.Sample(picture_sampler, input.uv);
    if (c.a < cut.x)
    {
        discard;
    }
    return c;
}
