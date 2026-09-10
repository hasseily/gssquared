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
    float4 u[18];
};

struct main0_out
{
    float4 FragColor [[color(0)]];
};

struct main0_in
{
    float2 vUV [[user(locn0)]];
};

static inline __attribute__((always_inline))
float4 over(thread const float4& a, thread const float4& b)
{
    return float4((a.xyz * a.w) + (b.xyz * (1.0 - a.w)), 1.0);
}

fragment main0_out main0(main0_in in [[stage_in]], constant Context& _58 [[buffer(0)]], texture2d<float> Processed [[texture(0)]], texture2d<float> Source [[texture(1)]], texture2d<float> Bezel [[texture(2)]], texture2d<float> Glass [[texture(3)]], texture2d<float> UserInterface [[texture(4)]], sampler ProcessedSmplr [[sampler(0)]], sampler SourceSmplr [[sampler(1)]], sampler BezelSmplr [[sampler(2)]], sampler GlassSmplr [[sampler(3)]], sampler UserInterfaceSmplr [[sampler(4)]])
{
    main0_out out = {};
    float4 color = Processed.sample(ProcessedSmplr, in.vUV);
    float2 uv = (((in.vUV - float2(0.5)) / _58.u[15].xy) + float2(0.5)) - _58.u[15].zw;
    bool _79 = _58.u[17].y > 0.5;
    bool _88;
    if (_79)
    {
        _88 = all(uv >= float2(0.0));
    }
    else
    {
        _88 = _79;
    }
    bool _95;
    if (_88)
    {
        _95 = all(uv <= float2(1.0));
    }
    else
    {
        _95 = _88;
    }
    if (_95)
    {
        float4 bezel = Bezel.sample(BezelSmplr, uv);
        bool outlined = false;
        bool _111 = _58.u[13].x > 9.9999997473787516355514526367188e-06;
        bool _117;
        if (_111)
        {
            _117 = bezel.w > 0.0;
        }
        else
        {
            _117 = _111;
        }
        bool _123;
        if (_117)
        {
            _123 = bezel.w < 1.0;
        }
        else
        {
            _123 = _117;
        }
        if (_123)
        {
            float2 r = (((uv - float2(0.5)) * _58.u[14].xy) + float2(0.5)) + _58.u[14].zw;
            bool _144 = r.x > 0.0;
            bool _151;
            if (_144)
            {
                _151 = r.x < 0.00999999977648258209228515625;
            }
            else
            {
                _151 = _144;
            }
            bool _164;
            if (!_151)
            {
                bool _157 = r.y > 0.0;
                bool _163;
                if (_157)
                {
                    _163 = r.y < 0.00999999977648258209228515625;
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
                bool _171 = r.x > 0.9900000095367431640625;
                bool _177;
                if (_171)
                {
                    _177 = r.x < 1.0;
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
                bool _184 = r.y > 0.9900000095367431640625;
                bool _190;
                if (_184)
                {
                    _190 = r.y < 1.0;
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
            float2 reflected = float2(1.0) - abs(mod(r, float2(2.0)) - float2(1.0));
            float4 image = Source.sample(SourceSmplr, (_58.u[11].xy + (reflected * _58.u[11].zw)), level(_58.u[13].y + _58.u[17].x));
            float amount = (1.0 - _58.u[13].x) * image.w;
            bezel = float4(mix(image.xyz, bezel.xyz, float3(amount)), 1.0);
            if ((_58.u[13].w > 0.5) && outline)
            {
                bezel = float4(1.0, 0.0, 0.0, 1.0);
                outlined = true;
            }
        }
        bool _249 = !outlined;
        bool _256;
        if (_249)
        {
            _256 = _58.u[17].z > 0.5;
        }
        else
        {
            _256 = _249;
        }
        bool _262;
        if (_256)
        {
            _262 = _58.u[13].z > 0.0;
        }
        else
        {
            _262 = _256;
        }
        if (_262)
        {
            float4 glass = Glass.sample(GlassSmplr, uv);
            glass.w *= _58.u[13].z;
            float alpha = glass.w + (bezel.w * (1.0 - glass.w));
            float3 rgb = (glass.xyz * glass.w) + ((bezel.xyz * bezel.w) * (1.0 - glass.w));
            float3 _305;
            if (alpha > 0.0)
            {
                _305 = rgb / float3(alpha);
            }
            else
            {
                _305 = float3(0.0);
            }
            bezel = float4(_305, alpha);
        }
        float4 param = fast::clamp(bezel, float4(0.0), float4(1.0));
        float4 param_1 = color;
        color = over(param, param_1);
    }
    float4 ui = UserInterface.sample(UserInterfaceSmplr, in.vUV);
    out.FragColor = float4(ui.xyz + (color.xyz * (1.0 - ui.w)), 1.0);
    return out;
}
