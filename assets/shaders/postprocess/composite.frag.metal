#include <metal_stdlib>
#include <simd/simd.h>

using namespace metal;

struct main0_out
{
    float4 FragColor [[color(0)]];
};

struct main0_in
{
    float2 vUV [[user(locn0)]];
};

fragment main0_out main0(main0_in in [[stage_in]], texture2d<float> Processed [[texture(0)]], texture2d<float> UserInterface [[texture(1)]], sampler ProcessedSmplr [[sampler(0)]], sampler UserInterfaceSmplr [[sampler(1)]])
{
    main0_out out = {};
    float4 guest = Processed.sample(ProcessedSmplr, in.vUV);
    float4 ui = UserInterface.sample(UserInterfaceSmplr, in.vUV);
    out.FragColor = float4(ui.xyz + (guest.xyz * (1.0 - ui.w)), 1.0);
    return out;
}
