cbuffer Context : register(b0, space3)
{
    float4 _22_parameters : packoffset(c0);
};

Texture2D<float4> Source : register(t0, space2);
SamplerState _Source_sampler : register(s0, space2);

static float2 vUV;
static float4 FragColor;

struct SPIRV_Cross_Input
{
    float2 vUV : TEXCOORD0;
};

struct SPIRV_Cross_Output
{
    float4 FragColor : SV_Target0;
};

float mod(float x, float y)
{
    return x - y * floor(x / y);
}

float2 mod(float2 x, float2 y)
{
    return x - y * floor(x / y);
}

float3 mod(float3 x, float3 y)
{
    return x - y * floor(x / y);
}

float4 mod(float4 x, float4 y)
{
    return x - y * floor(x / y);
}

void frag_main()
{
    float4 color = Source.Sample(_Source_sampler, vUV);
    if (_22_parameters.z > 0.5f)
    {
        float2 uv = vUV;
        float2 delta = uv - 0.5f.xx;
        uv += ((delta * dot(delta, delta)) * 0.20000000298023223876953125f);
        float scanline = (sin((uv.y * _22_parameters.y) * 3.1415927410125732421875f) * 0.5f) + 0.5f;
        scanline = lerp(1.0f, scanline, 0.25f);
        float grille = (mod(uv.x * _22_parameters.x, 3.0f) < 1.5f) ? 0.949999988079071044921875f : 1.0499999523162841796875f;
        grille = lerp(1.0f, grille, 0.100000001490116119384765625f);
        float2 edge = uv * (1.0f.xx - uv);
        float vignette = lerp(1.0f, (edge.x * edge.y) * 15.0f, 0.070000000298023223876953125f);
        float4 _109 = color;
        float3 _111 = _109.xyz * (((scanline * grille) * vignette) * 1.2000000476837158203125f);
        color.x = _111.x;
        color.y = _111.y;
        color.z = _111.z;
    }
    FragColor = color;
}

SPIRV_Cross_Output main(SPIRV_Cross_Input stage_input)
{
    vUV = stage_input.vUV;
    frag_main();
    SPIRV_Cross_Output stage_output;
    stage_output.FragColor = FragColor;
    return stage_output;
}
