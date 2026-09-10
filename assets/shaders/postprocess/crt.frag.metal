#pragma clang diagnostic ignored "-Wmissing-prototypes"

#include <metal_stdlib>
#include <simd/simd.h>

using namespace metal;

// Implementation of the GLSL mod() function, which is slightly different than Metal fmod()
template<typename Tx, typename Ty>
inline Tx mod(Tx x, Ty y)
{
    return x - y * floor(x / y);
}

struct Context
{
    float4 parameters;
};

struct main0_out
{
    float4 FragColor [[color(0)]];
};

struct main0_in
{
    float2 vUV [[user(locn0)]];
};

fragment main0_out main0(main0_in in [[stage_in]], constant Context& _22 [[buffer(0)]], texture2d<float> Source [[texture(0)]], sampler SourceSmplr [[sampler(0)]])
{
    main0_out out = {};
    float4 color = Source.sample(SourceSmplr, in.vUV);
    if (_22.parameters.z > 0.5)
    {
        float2 uv = in.vUV;
        float2 delta = uv - float2(0.5);
        uv += ((delta * dot(delta, delta)) * 0.20000000298023223876953125);
        float scanline = (sin((uv.y * _22.parameters.y) * 3.1415927410125732421875) * 0.5) + 0.5;
        scanline = mix(1.0, scanline, 0.25);
        float grille = (mod(uv.x * _22.parameters.x, 3.0) < 1.5) ? 0.949999988079071044921875 : 1.0499999523162841796875;
        grille = mix(1.0, grille, 0.100000001490116119384765625);
        float2 edge = uv * (float2(1.0) - uv);
        float vignette = mix(1.0, (edge.x * edge.y) * 15.0, 0.070000000298023223876953125);
        float4 _109 = color;
        float3 _111 = _109.xyz * (((scanline * grille) * vignette) * 1.2000000476837158203125);
        color.x = _111.x;
        color.y = _111.y;
        color.z = _111.z;
    }
    out.FragColor = color;
    return out;
}
