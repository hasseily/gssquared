cbuffer Context : register(b0, space3)
{
    float4 _58_u[18] : packoffset(c0);
};

Texture2D<float4> Processed : register(t0, space2);
SamplerState _Processed_sampler : register(s0, space2);
Texture2D<float4> Bezel : register(t2, space2);
SamplerState _Bezel_sampler : register(s2, space2);
Texture2D<float4> Source : register(t1, space2);
SamplerState _Source_sampler : register(s1, space2);
Texture2D<float4> Glass : register(t3, space2);
SamplerState _Glass_sampler : register(s3, space2);
Texture2D<float4> UserInterface : register(t4, space2);
SamplerState _UserInterface_sampler : register(s4, space2);

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

float4 over(float4 a, float4 b)
{
    return float4((a.xyz * a.w) + (b.xyz * (1.0f - a.w)), 1.0f);
}

void frag_main()
{
    float4 color = Processed.Sample(_Processed_sampler, vUV);
    float2 uv = (((vUV - 0.5f.xx) / _58_u[15].xy) + 0.5f.xx) - _58_u[15].zw;
    bool _79 = _58_u[17].y > 0.5f;
    bool _88;
    if (_79)
    {
        _88 = all(bool2(uv.x >= 0.0f.xx.x, uv.y >= 0.0f.xx.y));
    }
    else
    {
        _88 = _79;
    }
    bool _95;
    if (_88)
    {
        _95 = all(bool2(uv.x <= 1.0f.xx.x, uv.y <= 1.0f.xx.y));
    }
    else
    {
        _95 = _88;
    }
    if (_95)
    {
        float4 bezel = Bezel.Sample(_Bezel_sampler, uv);
        bool outlined = false;
        bool _111 = _58_u[13].x > 9.9999997473787516355514526367188e-06f;
        bool _117;
        if (_111)
        {
            _117 = bezel.w > 0.0f;
        }
        else
        {
            _117 = _111;
        }
        bool _123;
        if (_117)
        {
            _123 = bezel.w < 1.0f;
        }
        else
        {
            _123 = _117;
        }
        if (_123)
        {
            float2 r = (((uv - 0.5f.xx) * _58_u[14].xy) + 0.5f.xx) + _58_u[14].zw;
            bool _144 = r.x > 0.0f;
            bool _151;
            if (_144)
            {
                _151 = r.x < 0.00999999977648258209228515625f;
            }
            else
            {
                _151 = _144;
            }
            bool _164;
            if (!_151)
            {
                bool _157 = r.y > 0.0f;
                bool _163;
                if (_157)
                {
                    _163 = r.y < 0.00999999977648258209228515625f;
                }
                else
                {
                    _163 = _157;
                }
                _164 = _163;
            }
            else
            {
                _164 = _151;
            }
            bool _178;
            if (!_164)
            {
                bool _171 = r.x > 0.9900000095367431640625f;
                bool _177;
                if (_171)
                {
                    _177 = r.x < 1.0f;
                }
                else
                {
                    _177 = _171;
                }
                _178 = _177;
            }
            else
            {
                _178 = _164;
            }
            bool _191;
            if (!_178)
            {
                bool _184 = r.y > 0.9900000095367431640625f;
                bool _190;
                if (_184)
                {
                    _190 = r.y < 1.0f;
                }
                else
                {
                    _190 = _184;
                }
                _191 = _190;
            }
            else
            {
                _191 = _178;
            }
            bool outline = _191;
            float2 reflected = 1.0f.xx - abs(mod(r, 2.0f.xx) - 1.0f.xx);
            float4 image = Source.SampleLevel(_Source_sampler, _58_u[11].xy + (reflected * _58_u[11].zw), _58_u[13].y + _58_u[17].x);
            float amount = (1.0f - _58_u[13].x) * image.w;
            bezel = float4(lerp(image.xyz, bezel.xyz, amount.xxx), 1.0f);
            if ((_58_u[13].w > 0.5f) && outline)
            {
                bezel = float4(1.0f, 0.0f, 0.0f, 1.0f);
                outlined = true;
            }
        }
        bool _249 = !outlined;
        bool _256;
        if (_249)
        {
            _256 = _58_u[17].z > 0.5f;
        }
        else
        {
            _256 = _249;
        }
        bool _262;
        if (_256)
        {
            _262 = _58_u[13].z > 0.0f;
        }
        else
        {
            _262 = _256;
        }
        if (_262)
        {
            float4 glass = Glass.Sample(_Glass_sampler, uv);
            glass.w *= _58_u[13].z;
            float alpha = glass.w + (bezel.w * (1.0f - glass.w));
            float3 rgb = (glass.xyz * glass.w) + ((bezel.xyz * bezel.w) * (1.0f - glass.w));
            float3 _305;
            if (alpha > 0.0f)
            {
                _305 = rgb / alpha.xxx;
            }
            else
            {
                _305 = 0.0f.xxx;
            }
            bezel = float4(_305, alpha);
        }
        float4 param = clamp(bezel, 0.0f.xxxx, 1.0f.xxxx);
        float4 param_1 = color;
        color = over(param, param_1);
    }
    float4 ui = UserInterface.Sample(_UserInterface_sampler, vUV);
    FragColor = float4(ui.xyz + (color.xyz * (1.0f - ui.w)), 1.0f);
}

SPIRV_Cross_Output main(SPIRV_Cross_Input stage_input)
{
    vUV = stage_input.vUV;
    frag_main();
    SPIRV_Cross_Output stage_output;
    stage_output.FragColor = FragColor;
    return stage_output;
}
