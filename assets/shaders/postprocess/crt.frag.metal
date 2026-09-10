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
float4 CurrentBorderTexel(thread const int2& at, thread const int& level0, texture2d<float> A2TextureCurrent, sampler A2TextureCurrentSmplr, constant Context& _192)
{
    int2 size = int2(A2TextureCurrent.get_width(level0), A2TextureCurrent.get_height(level0));
    float2 center = (float2(at) + float2(0.5)) / float2(size);
    bool _174 = any(at < int2(0));
    bool _182;
    if (!_174)
    {
        _182 = any(at >= size);
    }
    else
    {
        _182 = _174;
    }
    bool _200;
    if (!_182)
    {
        _200 = any(center < _192.u[11].xy);
    }
    else
    {
        _200 = _182;
    }
    bool _214;
    if (!_200)
    {
        _214 = any(center >= (_192.u[11].xy + _192.u[11].zw));
    }
    else
    {
        _214 = _200;
    }
    if (_214)
    {
        return float4(0.0);
    }
    return A2TextureCurrent.read(uint2(at), level0);
}

static inline __attribute__((always_inline))
float4 CurrentBorderLinear(thread const float2& uv, thread const int& level0, texture2d<float> A2TextureCurrent, sampler A2TextureCurrentSmplr, constant Context& _192)
{
    float2 at = (uv * float2(int2(A2TextureCurrent.get_width(level0), A2TextureCurrent.get_height(level0)))) - float2(0.5);
    int2 low = int2(floor(at));
    float2 weight = fract(at);
    int2 param = low;
    int param_1 = level0;
    int2 param_2 = low + int2(1, 0);
    int param_3 = level0;
    int2 param_4 = low + int2(0, 1);
    int param_5 = level0;
    int2 param_6 = low + int2(1);
    int param_7 = level0;
    return mix(mix(CurrentBorderTexel(param, param_1, A2TextureCurrent, A2TextureCurrentSmplr, _192), CurrentBorderTexel(param_2, param_3, A2TextureCurrent, A2TextureCurrentSmplr, _192), float4(weight.x)), mix(CurrentBorderTexel(param_4, param_5, A2TextureCurrent, A2TextureCurrentSmplr, _192), CurrentBorderTexel(param_6, param_7, A2TextureCurrent, A2TextureCurrentSmplr, _192), float4(weight.x)), float4(weight.y));
}

static inline __attribute__((always_inline))
float4 CurrentAtLod(thread const float2& p, thread const float& requested_lod, texture2d<float> A2TextureCurrent, sampler A2TextureCurrentSmplr, constant Context& _192)
{
    float2 uv = _192.u[11].xy + (p * _192.u[11].zw);
    if (requested_lod <= 0.0)
    {
        bool _304 = any(p < float2(0.0));
        bool _313;
        if (!_304)
        {
            _313 = any(p >= float2(1.0));
        }
        else
        {
            _313 = _304;
        }
        if (_313)
        {
            return float4(0.0);
        }
        return A2TextureCurrent.sample(A2TextureCurrentSmplr, uv, level(0.0));
    }
    float last = floor(log2(float(max(int2(A2TextureCurrent.get_width(), A2TextureCurrent.get_height()).x, int2(A2TextureCurrent.get_width(), A2TextureCurrent.get_height()).y))));
    float lod = fast::min(requested_lod, last);
    float2 coarse = float2(int2(A2TextureCurrent.get_width(int(ceil(lod))), A2TextureCurrent.get_height(int(ceil(lod))))) * _192.u[11].zw;
    bool _355 = all((p * coarse) >= float2(0.5));
    bool _365;
    if (_355)
    {
        _365 = all(((float2(1.0) - p) * coarse) >= float2(0.5));
    }
    else
    {
        _365 = _355;
    }
    if (_365)
    {
        return A2TextureCurrent.sample(A2TextureCurrentSmplr, uv, level(lod));
    }
    int low = int(floor(lod));
    int high = min((low + 1), int(last));
    float2 param = uv;
    int param_1 = low;
    float2 param_2 = uv;
    int param_3 = high;
    return mix(CurrentBorderLinear(param, param_1, A2TextureCurrent, A2TextureCurrentSmplr, _192), CurrentBorderLinear(param_2, param_3, A2TextureCurrent, A2TextureCurrentSmplr, _192), float4(fract(lod)));
}

static inline __attribute__((always_inline))
float4 SampleCurrent(thread const float2& p, texture2d<float> A2TextureCurrent, sampler A2TextureCurrentSmplr, constant Context& _192)
{
    float2 uv = _192.u[11].xy + (p * _192.u[11].zw);
    float2 size = float2(int2(A2TextureCurrent.get_width(), A2TextureCurrent.get_height()));
    float2 gradient_x = dfdx(uv);
    float2 gradient_y = dfdy(uv);
    float2 dx = gradient_x * size;
    float2 dy = gradient_y * size;
    float lod = 0.5 * log2(fast::max(fast::max(dot(dx, dx), dot(dy, dy)), 9.9999996826552253889678874634872e-21));
    float last = floor(log2(fast::max(size.x, size.y)));
    float2 coarse = float2(int2(A2TextureCurrent.get_width(int(ceil(fast::clamp(lod, 0.0, last)))), A2TextureCurrent.get_height(int(ceil(fast::clamp(lod, 0.0, last)))))) * _192.u[11].zw;
    bool _466 = all((p * coarse) >= float2(0.5));
    bool _476;
    if (_466)
    {
        _476 = all(((float2(1.0) - p) * coarse) >= float2(0.5));
    }
    else
    {
        _476 = _466;
    }
    if (_476)
    {
        return A2TextureCurrent.sample(A2TextureCurrentSmplr, uv, gradient2d(gradient_x, gradient_y));
    }
    float2 param = p;
    float param_1 = lod;
    return CurrentAtLod(param, param_1, A2TextureCurrent, A2TextureCurrentSmplr, _192);
}

static inline __attribute__((always_inline))
float3 s2l(thread const float3& c)
{
    return mix(c / float3(12.9200000762939453125), powr((fast::max(c, float3(0.0)) + float3(0.054999999701976776123046875)) / float3(1.05499994754791259765625), float3(2.400000095367431640625)), step(float3(0.040449999272823333740234375), c));
}

static inline __attribute__((always_inline))
float4 SamplePrevious(thread const float2& p, constant Context& _192, texture2d<float> PreviousFrame, sampler PreviousFrameSmplr)
{
    return PreviousFrame.sample(PreviousFrameSmplr, (_192.u[12].xy + (p * _192.u[12].zw)));
}

static inline __attribute__((always_inline))
float4 HalveFrameRate(thread const float2& coords, thread const float4& currentColor, constant Context& _192, texture2d<float> PreviousFrame, sampler PreviousFrameSmplr)
{
    bool _675 = _192.u[16].x > 0.5;
    bool _683;
    if (_675)
    {
        _683 = (int(_192.u[1].z) & 1) == 1;
    }
    else
    {
        _683 = _675;
    }
    if (_683)
    {
        float2 param = coords;
        float3 param_1 = SamplePrevious(param, _192, PreviousFrame, PreviousFrameSmplr).xyz;
        float3 previousColor = s2l(param_1);
        float3 linearMix = (currentColor.xyz + previousColor) * 0.5;
        return float4(linearMix, 1.0);
    }
    return currentColor;
}

static inline __attribute__((always_inline))
float3 l2s(thread const float3& c)
{
    return mix(c * 12.9200000762939453125, (powr(fast::max(c, float3(0.0)), float3(0.4166666567325592041015625)) * 1.05499994754791259765625) - float3(0.054999999701976776123046875), step(float3(0.003130800090730190277099609375), c));
}

static inline __attribute__((always_inline))
float cbrt1(thread const float& x)
{
    return sign(x) * powr(abs(x), 0.3333333432674407958984375);
}

static inline __attribute__((always_inline))
float3 cbrt3(thread const float3& v)
{
    float param = v.x;
    float param_1 = v.y;
    float param_2 = v.z;
    return float3(cbrt1(param), cbrt1(param_1), cbrt1(param_2));
}

static inline __attribute__((always_inline))
float3 l2oklab(thread const float3& rgb, constant Context& _192)
{
    if (!(_192.u[2].w > 0.5))
    {
        return rgb;
    }
    float3 lms = float3x3(float3(0.4122214615345001220703125, 0.21190349757671356201171875, 0.0883024632930755615234375), float3(0.536332547664642333984375, 0.680699527263641357421875, 0.28171885013580322265625), float3(0.0514459945261478424072265625, 0.10739696025848388671875, 0.629978716373443603515625)) * rgb;
    float3 param = lms;
    return float3x3(float3(0.2104542553424835205078125, 1.9779984951019287109375, 0.025904037058353424072265625), float3(0.793617784976959228515625, -2.428592205047607421875, 0.782771766185760498046875), float3(-0.004072046838700771331787109375, 0.4505937099456787109375, -0.8086757659912109375)) * cbrt3(param);
}

static inline __attribute__((always_inline))
float3 oklab2l(thread const float3& lab, constant Context& _192)
{
    if (!(_192.u[2].w > 0.5))
    {
        return lab;
    }
    float3 lms = float3x3(float3(1.0), float3(0.3963377773761749267578125, -0.1055613458156585693359375, -0.089484177529811859130859375), float3(0.21580375730991363525390625, -0.06385417282581329345703125, -1.2914855480194091796875)) * lab;
    return float3x3(float3(4.076741695404052734375, -1.26843798160552978515625, 0.0041960864327847957611083984375), float3(-3.30771160125732421875, 2.60975742340087890625, -0.70341861248016357421875), float3(0.2309699356555938720703125, -0.341319382190704345703125, 1.7076146602630615234375)) * ((lms * lms) * lms);
}

static inline __attribute__((always_inline))
float4 GenerateGhosting(thread const float2& coords, thread float4& currentColor, constant Context& _192, texture2d<float> PreviousFrame, sampler PreviousFrameSmplr)
{
    if (_192.u[16].x < 0.5)
    {
        return currentColor;
    }
    float ghosting = _192.u[2].x / 100.0;
    float4 blended = float4(0.0);
    float2 param = coords;
    float4 previousColor = SamplePrevious(param, _192, PreviousFrame, PreviousFrameSmplr);
    float3 param_1 = previousColor.xyz;
    float3 _728 = s2l(param_1);
    previousColor.x = _728.x;
    previousColor.y = _728.y;
    previousColor.z = _728.z;
    if (_192.u[2].w > 0.5)
    {
        float3 param_2 = previousColor.xyz;
        float3 _743 = l2oklab(param_2, _192);
        previousColor.x = _743.x;
        previousColor.y = _743.y;
        previousColor.z = _743.z;
        float3 param_3 = currentColor.xyz;
        float3 _753 = l2oklab(param_3, _192);
        currentColor.x = _753.x;
        currentColor.y = _753.y;
        currentColor.z = _753.z;
        if (currentColor.x > previousColor.x)
        {
            blended = mix(currentColor, previousColor, float4(0.00999999977648258209228515625));
        }
        else
        {
            float cdist = length(previousColor - currentColor);
            float t = smoothstep(0.02999999932944774627685546875, 0.0500000007450580596923828125, cdist);
            ghosting = mix(cdist, ghosting, t);
            blended = mix(currentColor, previousColor, float4(ghosting));
        }
        float3 param_4 = blended.xyz;
        float3 _795 = oklab2l(param_4, _192);
        blended.x = _795.x;
        blended.y = _795.y;
        blended.z = _795.z;
    }
    else
    {
        float currentIntensity = dot(currentColor.xyz, float3(0.2125999927520751953125, 0.715200006961822509765625, 0.072200000286102294921875));
        float previousIntensity = dot(previousColor.xyz, float3(0.2125999927520751953125, 0.715200006961822509765625, 0.072200000286102294921875));
        if (currentIntensity > previousIntensity)
        {
            blended = mix(currentColor, previousColor, float4(0.00999999977648258209228515625));
        }
        else
        {
            if ((previousIntensity - currentIntensity) < (ghosting * 0.0350000001490116119384765625))
            {
                blended = mix(currentColor, previousColor, float4(ghosting / 2.0));
            }
            else
            {
                blended = mix(currentColor, previousColor, float4(ghosting));
            }
        }
    }
    return blended;
}

static inline __attribute__((always_inline))
float2 Warp(thread float2& pos, constant Context& _192)
{
    pos = (pos * 2.0) - float2(1.0);
    pos *= float2(1.0 + ((pos.y * pos.y) * _192.u[10].z), 1.0 + ((pos.x * pos.x) * _192.u[10].w));
    pos = (pos * 0.5) + float2(0.5);
    return pos;
}

static inline __attribute__((always_inline))
float2 BarrelDistortion(thread const float2& uv, constant Context& _192)
{
    float2 delta = uv - float2(0.5);
    float delta2 = dot(delta, delta);
    float delta4 = delta2 * delta2;
    float delta_offset = delta4 * _192.u[3].z;
    float2 warped = uv + (delta * delta_offset);
    return ((warped - float2(0.5)) / float2(mix(1.0, 1.2000000476837158203125, _192.u[3].z / 5.0))) + float2(0.5);
}

static inline __attribute__((always_inline))
float roundCorners(thread const float2& p, thread const float2& b, thread const float& r)
{
    return length(fast::max((abs(p) - b) + float2(r), float2(0.0))) - r;
}

static inline __attribute__((always_inline))
float4 SampleCurrentLod(thread const float2& p, thread const float& lod, texture2d<float> A2TextureCurrent, sampler A2TextureCurrentSmplr, constant Context& _192)
{
    float2 param = p;
    float param_1 = lod + _192.u[17].x;
    return CurrentAtLod(param, param_1, A2TextureCurrent, A2TextureCurrentSmplr, _192);
}

static inline __attribute__((always_inline))
float4 PhosphorBlur(texture2d<float> tex, sampler texSmplr, thread const float2& uv, thread const float2& resolution, thread const float& blurAmount, texture2d<float> A2TextureCurrent, sampler A2TextureCurrentSmplr, constant Context& _192)
{
    float2 param = uv;
    float param_1 = blurAmount * 4.0;
    float4 color = SampleCurrentLod(param, param_1, A2TextureCurrent, A2TextureCurrentSmplr, _192);
    float3 param_2 = color.xyz;
    float3 _861 = s2l(param_2);
    color.x = _861.x;
    color.y = _861.y;
    color.z = _861.z;
    if (_192.u[2].z > 0.5)
    {
        float4 _873 = color;
        float2 param_3 = uv;
        float3 param_4 = SampleCurrent(param_3, A2TextureCurrent, A2TextureCurrentSmplr, _192).xyz;
        float3 _883 = mix(_873.xyz, s2l(param_4), float3(0.300000011920928955078125));
        color.x = _883.x;
        color.y = _883.y;
        color.z = _883.z;
    }
    return fast::clamp(color, float4(0.0), float4(1.0));
}

static inline __attribute__((always_inline))
float rand(thread const float2& co)
{
    float a = 12.98980045318603515625;
    float b = 78.233001708984375;
    float c = 43758.546875;
    float dt = dot(co, float2(a, b));
    float sn = mod(dt, 3.1400001049041748046875);
    return fract(sin(sn) * c);
}

static inline __attribute__((always_inline))
float3 Mask(thread const float2& pos, thread const float& CGWG, constant Context& _192)
{
    if (int(_192.u[10].x) == 0)
    {
        return float3(1.0);
    }
    float3 mask = float3(CGWG);
    if (int(_192.u[10].x) == 1)
    {
        if (false)
        {
            return float3(((1.0 - CGWG) * sin(pos.x * 3.1415927410125732421875)) + CGWG);
        }
        else
        {
            float m = fract(pos.x * 0.5);
            if (m < 0.5)
            {
                mask.x = 1.0;
                mask.z = 1.0;
            }
            else
            {
                mask.y = 1.0;
            }
            return mask;
        }
    }
    if (int(_192.u[10].x) == 2)
    {
        if (false)
        {
            return float3(((1.0 - CGWG) * sin((pos.x * 3.1415927410125732421875) * 0.66670000553131103515625)) + CGWG);
        }
        else
        {
            float m_1 = fract(pos.x * 0.33329999446868896484375);
            if (m_1 < 0.33329999446868896484375)
            {
                float3 _982;
                if (_192.u[3].w == 0.0)
                {
                    _982 = float3(mask.x, mask.y, 1.0);
                }
                else
                {
                    _982 = float3(1.0, mask.y, mask.z);
                }
                mask = _982;
            }
            else
            {
                if (m_1 < 0.6665999889373779296875)
                {
                    mask.y = 1.0;
                }
                else
                {
                    float3 _1008;
                    if (_192.u[3].w == 0.0)
                    {
                        _1008 = float3(1.0, mask.y, mask.z);
                    }
                    else
                    {
                        _1008 = float3(mask.x, mask.y, 1.0);
                    }
                    mask = _1008;
                }
            }
            return mask;
        }
    }
    return float3(1.0);
}

static inline __attribute__((always_inline))
float3 slot(thread const float2& pos, constant Context& _192)
{
    float h = fract(pos.x / _192.u[9].y);
    float v = fract(pos.y);
    float odd;
    if (v < 0.5)
    {
        odd = 0.0;
    }
    else
    {
        odd = 1.0;
    }
    if (odd == 0.0)
    {
        if (h < 0.5)
        {
            return float3(0.5);
        }
        else
        {
            return float3(1.5);
        }
    }
    else
    {
        if (h < 0.5)
        {
            return float3(1.5);
        }
        else
        {
            return float3(0.5);
        }
    }
}

fragment main0_out main0(main0_in in [[stage_in]], constant Context& _192 [[buffer(0)]], texture2d<float> A2TextureCurrent [[texture(0)]], texture2d<float> PreviousFrame [[texture(1)]], sampler A2TextureCurrentSmplr [[sampler(0)]], sampler PreviousFrameSmplr [[sampler(1)]], float4 gl_FragCoord [[position]])
{
    main0_out out = {};
    float3x3 PAL = float3x3(float3(1.07400000095367431640625, -0.0573999993503093719482421875, -0.011900000274181365966796875), float3(0.038400001823902130126953125, 0.96990001201629638671875, -0.005900000222027301788330078125), float3(-0.0078999996185302734375, 0.02040000073611736297607421875, 0.988399982452392578125));
    float3x3 NTSC = float3x3(float3(0.93180000782012939453125, 0.0412000007927417755126953125, 0.02170000039041042327880859375), float3(0.013500000350177288055419921875, 0.97109997272491455078125, 0.014800000004470348358154296875), float3(0.0054999999701976776123046875, -0.0142999999225139617919921875, 1.00849997997283935546875));
    float3x3 NTSC_J = float3x3(float3(0.950100004673004150390625, -0.04309999942779541015625, 0.085699997842311859130859375), float3(0.02649999968707561492919921875, 0.927799999713897705078125, 0.04320000112056732177734375), float3(0.0010999999940395355224609375, -0.02060000039637088775634765625, 1.31529998779296875));
    float2 TexCoords = (in.vUV - _192.u[12].xy) / _192.u[12].zw;
    out.FragColor = float4(0.0, 0.0, 0.0, 1.0);
    bool _1193 = any(TexCoords < float2(0.0));
    bool _1200;
    if (!_1193)
    {
        _1200 = any(TexCoords > float2(1.0));
    }
    else
    {
        _1200 = _1193;
    }
    if (_1200)
    {
        return out;
    }
    if (int(_192.u[1].w) <= 1)
    {
        float2 param = TexCoords;
        float4 color = SampleCurrent(param, A2TextureCurrent, A2TextureCurrentSmplr, _192);
        if (int(_192.u[1].w) == 1)
        {
            float4 _1229 = color;
            float3 _1231 = _1229.xyz * (1.0 - mod(floor(TexCoords.y * _192.u[0].y), 2.0));
            color.x = _1231.x;
            color.y = _1231.y;
            color.z = _1231.z;
        }
        float3 param_1 = color.xyz;
        float3 _1241 = s2l(param_1);
        color.x = _1241.x;
        color.y = _1241.y;
        color.z = _1241.z;
        if (_192.u[16].y > 0.5)
        {
            float2 param_2 = TexCoords;
            float4 param_3 = color;
            color = HalveFrameRate(param_2, param_3, _192, PreviousFrame, PreviousFrameSmplr);
        }
        float3 param_4 = color.xyz;
        out.FragColor = float4(l2s(param_4), 1.0);
        return out;
    }
    float2 q = (TexCoords * _192.u[0].xy) / _192.u[0].xy;
    float2 uv = q;
    float o = (2.0 * mod(gl_FragCoord.y, 2.0)) / _192.u[0].z;
    bool _1292 = uv.x < 0.0;
    bool _1299;
    if (!_1292)
    {
        _1299 = uv.x > 1.0;
    }
    else
    {
        _1299 = _1292;
    }
    if (_1299)
    {
        discard_fragment();
    }
    bool _1305 = uv.y < 0.0;
    bool _1312;
    if (!_1305)
    {
        _1312 = uv.y > 1.0;
    }
    else
    {
        _1312 = _1305;
    }
    if (_1312)
    {
        discard_fragment();
    }
    if (int(_192.u[1].w) == 1)
    {
        float2 param_5 = TexCoords;
        out.FragColor = SampleCurrent(param_5, A2TextureCurrent, A2TextureCurrentSmplr, _192);
        float4 _1325 = out.FragColor;
        float3 _1335 = _1325.xyz * (1.0 - mod(floor(TexCoords.y * _192.u[0].y), 2.0));
        out.FragColor.x = _1335.x;
        out.FragColor.y = _1335.y;
        out.FragColor.z = _1335.z;
        if (_192.u[16].y > 0.5)
        {
            float2 param_6 = TexCoords;
            float4 param_7 = out.FragColor;
            out.FragColor = HalveFrameRate(param_6, param_7, _192, PreviousFrame, PreviousFrameSmplr);
        }
        if (_192.u[2].x > 9.9999997473787516355514526367188e-05)
        {
            float2 param_8 = TexCoords;
            float4 param_9 = out.FragColor;
            float4 _1363 = GenerateGhosting(param_8, param_9, _192, PreviousFrame, PreviousFrameSmplr);
            out.FragColor = _1363;
        }
        return out;
    }
    if (int(_192.u[10].y) == 1)
    {
        if (mod(floor(TexCoords.y * _192.u[0].y), 2.0) > 0.89999997615814208984375)
        {
            discard_fragment();
        }
    }
    float3x3 hue = float3x3(float3(1.0, _192.u[7].x, _192.u[6].w), float3(-_192.u[7].x, 1.0, _192.u[6].z), float3(-_192.u[6].w, -_192.u[6].z, 1.0));
    float2 param_10 = TexCoords;
    float2 _1409 = Warp(param_10, _192);
    float2 pos = _1409;
    float2 param_11 = pos;
    pos = BarrelDistortion(param_11, _192);
    float2 bpos = pos;
    float2 dx = float2((float2(1.0) / _192.u[0].xy).x, 0.0);
    float2 ogl2 = pos * _192.u[0].xy;
    float2 i = floor(pos * _192.u[0].xy) + float2(0.5);
    float f = ogl2.y - i.y;
    pos.y = (i.y + (((4.0 * f) * f) * f)) * (float2(1.0) / _192.u[0].xy).y;
    pos.x = mix(pos.x, i.x * (float2(1.0) / _192.u[0].xy).x, 0.20000000298023223876953125);
    float corn = 1.0;
    if (_192.u[5].w > 9.9999999747524270787835121154785e-07)
    {
        float2 halfRes = _192.u[0].zw * 0.5;
        float2 param_12 = (pos * _192.u[0].zw) - halfRes;
        float2 param_13 = halfRes;
        float param_14 = abs((_192.u[5].w * _192.u[0].z) * 30.0);
        float b = 1.0 - roundCorners(param_12, param_13, param_14);
        if (_192.u[3].x > 0.5)
        {
            corn = b / 10.0;
        }
        else
        {
            if (b < _192.u[5].w)
            {
                discard_fragment();
            }
        }
    }
    float4 res0;
    if (_192.u[2].y > 0.001000000047497451305389404296875)
    {
        float2 param_15 = pos;
        float2 param_16 = _192.u[0].xy;
        float param_17 = _192.u[2].y;
        res0 = PhosphorBlur(A2TextureCurrent, A2TextureCurrentSmplr, param_15, param_16, param_17, A2TextureCurrent, A2TextureCurrentSmplr, _192);
    }
    else
    {
        float2 param_18 = pos;
        res0 = SampleCurrent(param_18, A2TextureCurrent, A2TextureCurrentSmplr, _192);
        float3 param_19 = res0.xyz;
        float3 _1550 = s2l(param_19);
        res0.x = _1550.x;
        res0.y = _1550.y;
        res0.z = _1550.z;
    }
    float3 res = res0.xyz;
    if (res0.w <= 0.0)
    {
        return out;
    }
    if (_192.u[6].x > 9.9999997473787516355514526367188e-05)
    {
        if (((abs(_192.u[5].z) + abs(_192.u[5].y)) + abs(_192.u[5].x)) > 0.001000000047497451305389404296875)
        {
            float2 param_20 = pos + (dx * _192.u[5].z);
            float resr = SampleCurrent(param_20, A2TextureCurrent, A2TextureCurrentSmplr, _192).x;
            float2 param_21 = pos + (dx * _192.u[5].y);
            float resg = SampleCurrent(param_21, A2TextureCurrent, A2TextureCurrentSmplr, _192).y;
            float2 param_22 = pos + (dx * _192.u[5].x);
            float resb = SampleCurrent(param_22, A2TextureCurrent, A2TextureCurrentSmplr, _192).z;
            res = float3((res0.x * (1.0 - _192.u[6].x)) + (resr * _192.u[6].x), (res0.y * (1.0 - _192.u[6].x)) + (resg * _192.u[6].x), (res0.z * (1.0 - _192.u[6].x)) + (resb * _192.u[6].x));
        }
    }
    float l = dot(float3(_192.u[4].y), res);
    float CGWG = 0.300000011920928955078125;
    if (_192.u[3].y > 0.5)
    {
        CGWG = mix(_192.u[7].z, _192.u[7].y, l);
    }
    if (int(_192.u[10].y) == 2)
    {
        if (_192.u[9].z > 9.9999997473787516355514526367188e-06)
        {
            float vig = 0.0 + ((((16.0 * pos.x) * pos.y) * (1.0 - pos.x)) * (1.0 - pos.y));
            vig = powr(vig, _192.u[9].z);
            res *= float3(vig);
        }
        if (_192.u[8].y > 9.9999997473787516355514526367188e-06)
        {
            float scans = fast::clamp(0.3499999940395355224609375 + (0.1500000059604644775390625 * sin((2.0 * ((-_192.u[16].z) * _192.u[8].z)) + (((pos.y * float(uint(_192.u[1].x))) * 8.0) / 2.5499999523162841796875))), 0.0, 1.0);
            float s = powr(scans, _192.u[8].y);
            s = powr(s, _192.u[8].y);
            res *= float3(s);
        }
        res *= (1.0 + (_192.u[9].x * sin(300.0 * _192.u[16].z)));
        float2 param_23 = pos + float2(9.9999997473787516355514526367188e-05 * _192.u[16].z);
        float2 param_24 = (pos + float2(9.9999997473787516355514526367188e-05 * _192.u[16].z)) + float2(0.300000011920928955078125);
        float2 param_25 = (pos + float2(9.9999997473787516355514526367188e-05 * _192.u[16].z)) + float2(0.5);
        res *= (float3(1.0) - (float3(rand(param_23), rand(param_24), rand(param_25)) * _192.u[8].w));
    }
    float2 xy = (TexCoords * _192.u[0].zw) / float2(_192.u[7].w);
    float2 param_26 = xy;
    float param_27 = CGWG;
    res *= Mask(param_26, param_27, _192);
    if (_192.u[3].y > 0.5)
    {
        float2 param_28 = xy / float2(2.0);
        res *= mix(slot(param_28, _192), float3(1.0), float3(CGWG));
    }
    res = (res - float3(_192.u[4].x)) / float3(1.0 - _192.u[4].x);
    float3 param_29 = res;
    res = l2oklab(param_29, _192);
    if (_192.u[2].w > 0.5)
    {
        float c = cos(_192.u[6].y);
        float s_1 = sin(_192.u[6].y);
        float2 ab = float2(res.y, res.z);
        ab = float2x2(float2(c, -s_1), float2(s_1, c)) * ab;
        res.y = ab.x;
        res.z = ab.y;
        float3 _1879 = res;
        float2 _1881 = _1879.yz * _192.u[8].x;
        res.y = _1881.x;
        res.z = _1881.y;
        res.x = ((res.x - 0.5) * _192.u[4].w) + 0.5;
        res.x *= _192.u[4].z;
    }
    else
    {
        res *= hue;
        float slum = dot(float3(0.2899999916553497314453125, 0.60000002384185791015625, 0.10999999940395355224609375), res);
        res = mix(float3(slum), res, float3(_192.u[8].x));
        float lum = dot(float3(0.2125999927520751953125, 0.715200006961822509765625, 0.072200000286102294921875), res);
        float lum2 = ((lum - 0.180000007152557373046875) * _192.u[4].w) + 0.180000007152557373046875;
        float _1932;
        if (lum > 9.9999999747524270787835121154785e-07)
        {
            _1932 = lum2 / lum;
        }
        else
        {
            _1932 = 0.0;
        }
        float scale = _1932;
        res *= scale;
        res = fast::clamp(res, float3(0.0), float3(1.0));
        res *= _192.u[4].z;
    }
    out.FragColor = float4(res, corn);
    float3 param_30 = out.FragColor.xyz;
    float3 _1960 = oklab2l(param_30, _192);
    out.FragColor.x = _1960.x;
    out.FragColor.y = _1960.y;
    out.FragColor.z = _1960.z;
    if (int(_192.u[9].w) != 0)
    {
        float3 clr = out.FragColor.xyz;
        if (int(_192.u[9].w) == 1)
        {
            clr *= PAL;
        }
        if (int(_192.u[9].w) == 2)
        {
            clr *= NTSC;
        }
        if (int(_192.u[9].w) == 3)
        {
            clr *= NTSC_J;
        }
        clr /= float3(0.23999999463558197021484375, 0.689999997615814208984375, 0.070000000298023223876953125);
        clr *= float3(0.2899999916553497314453125, 0.60000002384185791015625, 0.10999999940395355224609375);
        out.FragColor.x = clr.x;
        out.FragColor.y = clr.y;
        out.FragColor.z = clr.z;
    }
    if (_192.u[16].y > 0.5)
    {
        float2 param_31 = TexCoords;
        float4 param_32 = out.FragColor;
        out.FragColor = HalveFrameRate(param_31, param_32, _192, PreviousFrame, PreviousFrameSmplr);
    }
    if (_192.u[2].x > 9.9999997473787516355514526367188e-05)
    {
        float2 param_33 = TexCoords;
        float4 param_34 = out.FragColor;
        float4 _2037 = GenerateGhosting(param_33, param_34, _192, PreviousFrame, PreviousFrameSmplr);
        out.FragColor = _2037;
    }
    float3 param_35 = out.FragColor.xyz;
    float3 _2041 = l2s(param_35);
    out.FragColor.x = _2041.x;
    out.FragColor.y = _2041.y;
    out.FragColor.z = _2041.z;
    return out;
}
