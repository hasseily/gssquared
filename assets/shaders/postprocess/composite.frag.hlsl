Texture2D<float4> Processed : register(t0, space2);
SamplerState _Processed_sampler : register(s0, space2);
Texture2D<float4> UserInterface : register(t1, space2);
SamplerState _UserInterface_sampler : register(s1, space2);

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

void frag_main()
{
    float4 guest = Processed.Sample(_Processed_sampler, vUV);
    float4 ui = UserInterface.Sample(_UserInterface_sampler, vUV);
    FragColor = float4(ui.xyz + (guest.xyz * (1.0f - ui.w)), 1.0f);
}

SPIRV_Cross_Output main(SPIRV_Cross_Input stage_input)
{
    vUV = stage_input.vUV;
    frag_main();
    SPIRV_Cross_Output stage_output;
    stage_output.FragColor = FragColor;
    return stage_output;
}
