cbuffer Context : register(b0, space3)
{
    float4 _192_u[18] : packoffset(c0);
};

Texture2D<float4> A2TextureCurrent : register(t0, space2);
SamplerState _A2TextureCurrent_sampler : register(s0, space2);
Texture2D<float4> PreviousFrame : register(t1, space2);
SamplerState _PreviousFrame_sampler : register(s1, space2);

static float4 gl_FragCoord;
static float2 vUV;
static float4 FragColor;

struct SPIRV_Cross_Input
{
    float2 vUV : TEXCOORD0;
    float4 gl_FragCoord : SV_Position;
};

struct SPIRV_Cross_Output
{
    float4 FragColor : SV_Target0;
};

static float3x3 PAL;
static float3x3 NTSC;
static float3x3 NTSC_J;
static float2 TexCoords;

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

uint2 spvTextureSize(Texture2D<float4> Tex, uint Level, out uint Param)
{
    uint2 ret;
    Tex.GetDimensions(Level, ret.x, ret.y, Param);
    return ret;
}

float4 CurrentBorderTexel(int2 at, int level)
{
    uint _158_dummy_parameter;
    int2 size = int2(spvTextureSize(A2TextureCurrent, uint(level), _158_dummy_parameter));
    float2 center = (float2(at) + 0.5f.xx) / float2(size);
    bool _174 = any(bool2(at.x < int2(0, 0).x, at.y < int2(0, 0).y));
    bool _182;
    if (!_174)
    {
        _182 = any(bool2(at.x >= size.x, at.y >= size.y));
    }
    else
    {
        _182 = _174;
    }
    bool _200;
    if (!_182)
    {
        _200 = any(bool2(center.x < _192_u[11].xy.x, center.y < _192_u[11].xy.y));
    }
    else
    {
        _200 = _182;
    }
    bool _214;
    if (!_200)
    {
        float2 _211 = _192_u[11].xy + _192_u[11].zw;
        _214 = any(bool2(center.x >= _211.x, center.y >= _211.y));
    }
    else
    {
        _214 = _200;
    }
    if (_214)
    {
        return 0.0f.xxxx;
    }
    return A2TextureCurrent.Load(int3(at, level));
}

float4 CurrentBorderLinear(float2 uv, int level)
{
    uint _232_dummy_parameter;
    float2 at = (uv * float2(int2(spvTextureSize(A2TextureCurrent, uint(level), _232_dummy_parameter)))) - 0.5f.xx;
    int2 low = int2(floor(at));
    float2 weight = frac(at);
    int2 param = low;
    int param_1 = level;
    int2 param_2 = low + int2(1, 0);
    int param_3 = level;
    int2 param_4 = low + int2(0, 1);
    int param_5 = level;
    int2 param_6 = low + int2(1, 1);
    int param_7 = level;
    return lerp(lerp(CurrentBorderTexel(param, param_1), CurrentBorderTexel(param_2, param_3), weight.x.xxxx), lerp(CurrentBorderTexel(param_4, param_5), CurrentBorderTexel(param_6, param_7), weight.x.xxxx), weight.y.xxxx);
}

float4 CurrentAtLod(float2 p, float requested_lod)
{
    float2 uv = _192_u[11].xy + (p * _192_u[11].zw);
    if (requested_lod <= 0.0f)
    {
        bool _304 = any(bool2(p.x < 0.0f.xx.x, p.y < 0.0f.xx.y));
        bool _313;
        if (!_304)
        {
            _313 = any(bool2(p.x >= 1.0f.xx.x, p.y >= 1.0f.xx.y));
        }
        else
        {
            _313 = _304;
        }
        if (_313)
        {
            return 0.0f.xxxx;
        }
        return A2TextureCurrent.SampleLevel(_A2TextureCurrent_sampler, uv, 0.0f);
    }
    uint _324_dummy_parameter;
    uint _328_dummy_parameter;
    float last = floor(log2(float(max(int2(spvTextureSize(A2TextureCurrent, uint(0), _324_dummy_parameter)).x, int2(spvTextureSize(A2TextureCurrent, uint(0), _328_dummy_parameter)).y))));
    float lod = min(requested_lod, last);
    uint _344_dummy_parameter;
    float2 coarse = float2(int2(spvTextureSize(A2TextureCurrent, uint(int(ceil(lod))), _344_dummy_parameter))) * _192_u[11].zw;
    float2 _352 = p * coarse;
    bool _355 = all(bool2(_352.x >= 0.5f.xx.x, _352.y >= 0.5f.xx.y));
    bool _365;
    if (_355)
    {
        float2 _362 = (1.0f.xx - p) * coarse;
        _365 = all(bool2(_362.x >= 0.5f.xx.x, _362.y >= 0.5f.xx.y));
    }
    else
    {
        _365 = _355;
    }
    if (_365)
    {
        return A2TextureCurrent.SampleLevel(_A2TextureCurrent_sampler, uv, lod);
    }
    int low = int(floor(lod));
    int high = min((low + 1), int(last));
    float2 param = uv;
    int param_1 = low;
    float2 param_2 = uv;
    int param_3 = high;
    return lerp(CurrentBorderLinear(param, param_1), CurrentBorderLinear(param_2, param_3), frac(lod).xxxx);
}

float4 SampleCurrent(float2 p)
{
    float2 uv = _192_u[11].xy + (p * _192_u[11].zw);
    uint _412_dummy_parameter;
    float2 size = float2(int2(spvTextureSize(A2TextureCurrent, uint(0), _412_dummy_parameter)));
    float2 gradient_x = ddx(uv);
    float2 gradient_y = ddy(uv);
    float2 dx = gradient_x * size;
    float2 dy = gradient_y * size;
    float lod = 0.5f * log2(max(max(dot(dx, dx), dot(dy, dy)), 9.9999996826552253889678874634872e-21f));
    float last = floor(log2(max(size.x, size.y)));
    uint _456_dummy_parameter;
    float2 coarse = float2(int2(spvTextureSize(A2TextureCurrent, uint(int(ceil(clamp(lod, 0.0f, last)))), _456_dummy_parameter))) * _192_u[11].zw;
    float2 _464 = p * coarse;
    bool _466 = all(bool2(_464.x >= 0.5f.xx.x, _464.y >= 0.5f.xx.y));
    bool _476;
    if (_466)
    {
        float2 _473 = (1.0f.xx - p) * coarse;
        _476 = all(bool2(_473.x >= 0.5f.xx.x, _473.y >= 0.5f.xx.y));
    }
    else
    {
        _476 = _466;
    }
    if (_476)
    {
        return A2TextureCurrent.SampleGrad(_A2TextureCurrent_sampler, uv, gradient_x, gradient_y);
    }
    float2 param = p;
    float param_1 = lod;
    return CurrentAtLod(param, param_1);
}

float3 s2l(float3 c)
{
    return lerp(c / 12.9200000762939453125f.xxx, pow((max(c, 0.0f.xxx) + 0.054999999701976776123046875f.xxx) / 1.05499994754791259765625f.xxx, 2.400000095367431640625f.xxx), step(0.040449999272823333740234375f.xxx, c));
}

float4 SamplePrevious(float2 p)
{
    return PreviousFrame.Sample(_PreviousFrame_sampler, _192_u[12].xy + (p * _192_u[12].zw));
}

float4 HalveFrameRate(float2 coords, float4 currentColor)
{
    bool _675 = _192_u[16].x > 0.5f;
    bool _683;
    if (_675)
    {
        _683 = (int(_192_u[1].z) & 1) == 1;
    }
    else
    {
        _683 = _675;
    }
    if (_683)
    {
        float2 param = coords;
        float3 param_1 = SamplePrevious(param).xyz;
        float3 previousColor = s2l(param_1);
        float3 linearMix = (currentColor.xyz + previousColor) * 0.5f;
        return float4(linearMix, 1.0f);
    }
    return currentColor;
}

float3 l2s(float3 c)
{
    return lerp(c * 12.9200000762939453125f, (pow(max(c, 0.0f.xxx), 0.4166666567325592041015625f.xxx) * 1.05499994754791259765625f) - 0.054999999701976776123046875f.xxx, step(0.003130800090730190277099609375f.xxx, c));
}

float cbrt1(float x)
{
    return sign(x) * pow(abs(x), 0.3333333432674407958984375f);
}

float3 cbrt3(float3 v)
{
    float param = v.x;
    float param_1 = v.y;
    float param_2 = v.z;
    return float3(cbrt1(param), cbrt1(param_1), cbrt1(param_2));
}

float3 l2oklab(float3 rgb)
{
    if (!(_192_u[2].w > 0.5f))
    {
        return rgb;
    }
    float3 lms = mul(rgb, float3x3(float3(0.4122214615345001220703125f, 0.21190349757671356201171875f, 0.0883024632930755615234375f), float3(0.536332547664642333984375f, 0.680699527263641357421875f, 0.28171885013580322265625f), float3(0.0514459945261478424072265625f, 0.10739696025848388671875f, 0.629978716373443603515625f)));
    float3 param = lms;
    return mul(cbrt3(param), float3x3(float3(0.2104542553424835205078125f, 1.9779984951019287109375f, 0.025904037058353424072265625f), float3(0.793617784976959228515625f, -2.428592205047607421875f, 0.782771766185760498046875f), float3(-0.004072046838700771331787109375f, 0.4505937099456787109375f, -0.8086757659912109375f)));
}

float3 oklab2l(float3 lab)
{
    if (!(_192_u[2].w > 0.5f))
    {
        return lab;
    }
    float3 lms = mul(lab, float3x3(1.0f.xxx, float3(0.3963377773761749267578125f, -0.1055613458156585693359375f, -0.089484177529811859130859375f), float3(0.21580375730991363525390625f, -0.06385417282581329345703125f, -1.2914855480194091796875f)));
    return mul((lms * lms) * lms, float3x3(float3(4.076741695404052734375f, -1.26843798160552978515625f, 0.0041960864327847957611083984375f), float3(-3.30771160125732421875f, 2.60975742340087890625f, -0.70341861248016357421875f), float3(0.2309699356555938720703125f, -0.341319382190704345703125f, 1.7076146602630615234375f)));
}

float4 GenerateGhosting(float2 coords, inout float4 currentColor)
{
    if (_192_u[16].x < 0.5f)
    {
        return currentColor;
    }
    float ghosting = _192_u[2].x / 100.0f;
    float4 blended = 0.0f.xxxx;
    float2 param = coords;
    float4 previousColor = SamplePrevious(param);
    float3 param_1 = previousColor.xyz;
    float3 _728 = s2l(param_1);
    previousColor.x = _728.x;
    previousColor.y = _728.y;
    previousColor.z = _728.z;
    if (_192_u[2].w > 0.5f)
    {
        float3 param_2 = previousColor.xyz;
        float3 _743 = l2oklab(param_2);
        previousColor.x = _743.x;
        previousColor.y = _743.y;
        previousColor.z = _743.z;
        float3 param_3 = currentColor.xyz;
        float3 _753 = l2oklab(param_3);
        currentColor.x = _753.x;
        currentColor.y = _753.y;
        currentColor.z = _753.z;
        if (currentColor.x > previousColor.x)
        {
            blended = lerp(currentColor, previousColor, 0.00999999977648258209228515625f.xxxx);
        }
        else
        {
            float cdist = length(previousColor - currentColor);
            float t = smoothstep(0.02999999932944774627685546875f, 0.0500000007450580596923828125f, cdist);
            ghosting = lerp(cdist, ghosting, t);
            blended = lerp(currentColor, previousColor, ghosting.xxxx);
        }
        float3 param_4 = blended.xyz;
        float3 _795 = oklab2l(param_4);
        blended.x = _795.x;
        blended.y = _795.y;
        blended.z = _795.z;
    }
    else
    {
        float currentIntensity = dot(currentColor.xyz, float3(0.2125999927520751953125f, 0.715200006961822509765625f, 0.072200000286102294921875f));
        float previousIntensity = dot(previousColor.xyz, float3(0.2125999927520751953125f, 0.715200006961822509765625f, 0.072200000286102294921875f));
        if (currentIntensity > previousIntensity)
        {
            blended = lerp(currentColor, previousColor, 0.00999999977648258209228515625f.xxxx);
        }
        else
        {
            if ((previousIntensity - currentIntensity) < (ghosting * 0.0350000001490116119384765625f))
            {
                blended = lerp(currentColor, previousColor, (ghosting / 2.0f).xxxx);
            }
            else
            {
                blended = lerp(currentColor, previousColor, ghosting.xxxx);
            }
        }
    }
    return blended;
}

float2 Warp(inout float2 pos)
{
    pos = (pos * 2.0f) - 1.0f.xx;
    pos *= float2(1.0f + ((pos.y * pos.y) * _192_u[10].z), 1.0f + ((pos.x * pos.x) * _192_u[10].w));
    pos = (pos * 0.5f) + 0.5f.xx;
    return pos;
}

float2 BarrelDistortion(float2 uv)
{
    float2 delta = uv - 0.5f.xx;
    float delta2 = dot(delta, delta);
    float delta4 = delta2 * delta2;
    float delta_offset = delta4 * _192_u[3].z;
    float2 warped = uv + (delta * delta_offset);
    return ((warped - 0.5f.xx) / lerp(1.0f, 1.2000000476837158203125f, _192_u[3].z / 5.0f).xx) + 0.5f.xx;
}

float roundCorners(float2 p, float2 b, float r)
{
    return length(max((abs(p) - b) + r.xx, 0.0f.xx)) - r;
}

float4 SampleCurrentLod(float2 p, float lod)
{
    float2 param = p;
    float param_1 = lod + _192_u[17].x;
    return CurrentAtLod(param, param_1);
}

float4 PhosphorBlur(Texture2D<float4> tex, SamplerState _tex_sampler, float2 uv, float2 resolution, float blurAmount)
{
    float2 param = uv;
    float param_1 = blurAmount * 4.0f;
    float4 color = SampleCurrentLod(param, param_1);
    float3 param_2 = color.xyz;
    float3 _861 = s2l(param_2);
    color.x = _861.x;
    color.y = _861.y;
    color.z = _861.z;
    if (_192_u[2].z > 0.5f)
    {
        float4 _873 = color;
        float2 param_3 = uv;
        float3 param_4 = SampleCurrent(param_3).xyz;
        float3 _883 = lerp(_873.xyz, s2l(param_4), 0.300000011920928955078125f.xxx);
        color.x = _883.x;
        color.y = _883.y;
        color.z = _883.z;
    }
    return clamp(color, 0.0f.xxxx, 1.0f.xxxx);
}

float rand(float2 co)
{
    float a = 12.98980045318603515625f;
    float b = 78.233001708984375f;
    float c = 43758.546875f;
    float dt = dot(co, float2(a, b));
    float sn = mod(dt, 3.1400001049041748046875f);
    return frac(sin(sn) * c);
}

float3 Mask(float2 pos, float CGWG)
{
    if (int(_192_u[10].x) == 0)
    {
        return 1.0f.xxx;
    }
    float3 mask = CGWG.xxx;
    if (int(_192_u[10].x) == 1)
    {
        if (false)
        {
            return (((1.0f - CGWG) * sin(pos.x * 3.1415927410125732421875f)) + CGWG).xxx;
        }
        else
        {
            float m = frac(pos.x * 0.5f);
            if (m < 0.5f)
            {
                mask.x = 1.0f;
                mask.z = 1.0f;
            }
            else
            {
                mask.y = 1.0f;
            }
            return mask;
        }
    }
    if (int(_192_u[10].x) == 2)
    {
        if (false)
        {
            return (((1.0f - CGWG) * sin((pos.x * 3.1415927410125732421875f) * 0.66670000553131103515625f)) + CGWG).xxx;
        }
        else
        {
            float m_1 = frac(pos.x * 0.33329999446868896484375f);
            if (m_1 < 0.33329999446868896484375f)
            {
                float3 _982;
                if (_192_u[3].w == 0.0f)
                {
                    _982 = float3(mask.x, mask.y, 1.0f);
                }
                else
                {
                    _982 = float3(1.0f, mask.y, mask.z);
                }
                mask = _982;
            }
            else
            {
                if (m_1 < 0.6665999889373779296875f)
                {
                    mask.y = 1.0f;
                }
                else
                {
                    float3 _1008;
                    if (_192_u[3].w == 0.0f)
                    {
                        _1008 = float3(1.0f, mask.y, mask.z);
                    }
                    else
                    {
                        _1008 = float3(mask.x, mask.y, 1.0f);
                    }
                    mask = _1008;
                }
            }
            return mask;
        }
    }
    return 1.0f.xxx;
}

float3 slot(float2 pos)
{
    float h = frac(pos.x / _192_u[9].y);
    float v = frac(pos.y);
    float odd;
    if (v < 0.5f)
    {
        odd = 0.0f;
    }
    else
    {
        odd = 1.0f;
    }
    if (odd == 0.0f)
    {
        if (h < 0.5f)
        {
            return 0.5f.xxx;
        }
        else
        {
            return 1.5f.xxx;
        }
    }
    else
    {
        if (h < 0.5f)
        {
            return 1.5f.xxx;
        }
        else
        {
            return 0.5f.xxx;
        }
    }
}

void frag_main()
{
    PAL = float3x3(float3(1.07400000095367431640625f, -0.0573999993503093719482421875f, -0.011900000274181365966796875f), float3(0.038400001823902130126953125f, 0.96990001201629638671875f, -0.005900000222027301788330078125f), float3(-0.0078999996185302734375f, 0.02040000073611736297607421875f, 0.988399982452392578125f));
    NTSC = float3x3(float3(0.93180000782012939453125f, 0.0412000007927417755126953125f, 0.02170000039041042327880859375f), float3(0.013500000350177288055419921875f, 0.97109997272491455078125f, 0.014800000004470348358154296875f), float3(0.0054999999701976776123046875f, -0.0142999999225139617919921875f, 1.00849997997283935546875f));
    NTSC_J = float3x3(float3(0.950100004673004150390625f, -0.04309999942779541015625f, 0.085699997842311859130859375f), float3(0.02649999968707561492919921875f, 0.927799999713897705078125f, 0.04320000112056732177734375f), float3(0.0010999999940395355224609375f, -0.02060000039637088775634765625f, 1.31529998779296875f));
    TexCoords = (vUV - _192_u[12].xy) / _192_u[12].zw;
    FragColor = float4(0.0f, 0.0f, 0.0f, 1.0f);
    bool _1193 = any(bool2(TexCoords.x < 0.0f.xx.x, TexCoords.y < 0.0f.xx.y));
    bool _1200;
    if (!_1193)
    {
        _1200 = any(bool2(TexCoords.x > 1.0f.xx.x, TexCoords.y > 1.0f.xx.y));
    }
    else
    {
        _1200 = _1193;
    }
    if (_1200)
    {
        return;
    }
    if (int(_192_u[1].w) <= 1)
    {
        float2 param = TexCoords;
        float4 color = SampleCurrent(param);
        if (int(_192_u[1].w) == 1)
        {
            float4 _1229 = color;
            float3 _1231 = _1229.xyz * (1.0f - mod(floor(TexCoords.y * _192_u[0].y), 2.0f));
            color.x = _1231.x;
            color.y = _1231.y;
            color.z = _1231.z;
        }
        float3 param_1 = color.xyz;
        float3 _1241 = s2l(param_1);
        color.x = _1241.x;
        color.y = _1241.y;
        color.z = _1241.z;
        if (_192_u[16].y > 0.5f)
        {
            float2 param_2 = TexCoords;
            float4 param_3 = color;
            color = HalveFrameRate(param_2, param_3);
        }
        float3 param_4 = color.xyz;
        FragColor = float4(l2s(param_4), 1.0f);
        return;
    }
    float2 q = (TexCoords * _192_u[0].xy) / _192_u[0].xy;
    float2 uv = q;
    float o = (2.0f * mod(gl_FragCoord.y, 2.0f)) / _192_u[0].z;
    bool _1292 = uv.x < 0.0f;
    bool _1299;
    if (!_1292)
    {
        _1299 = uv.x > 1.0f;
    }
    else
    {
        _1299 = _1292;
    }
    if (_1299)
    {
        discard;
    }
    bool _1305 = uv.y < 0.0f;
    bool _1312;
    if (!_1305)
    {
        _1312 = uv.y > 1.0f;
    }
    else
    {
        _1312 = _1305;
    }
    if (_1312)
    {
        discard;
    }
    if (int(_192_u[1].w) == 1)
    {
        float2 param_5 = TexCoords;
        FragColor = SampleCurrent(param_5);
        float4 _1325 = FragColor;
        float3 _1335 = _1325.xyz * (1.0f - mod(floor(TexCoords.y * _192_u[0].y), 2.0f));
        FragColor.x = _1335.x;
        FragColor.y = _1335.y;
        FragColor.z = _1335.z;
        if (_192_u[16].y > 0.5f)
        {
            float2 param_6 = TexCoords;
            float4 param_7 = FragColor;
            FragColor = HalveFrameRate(param_6, param_7);
        }
        if (_192_u[2].x > 9.9999997473787516355514526367188e-05f)
        {
            float2 param_8 = TexCoords;
            float4 param_9 = FragColor;
            float4 _1363 = GenerateGhosting(param_8, param_9);
            FragColor = _1363;
        }
        return;
    }
    if (int(_192_u[10].y) == 1)
    {
        if (mod(floor(TexCoords.y * _192_u[0].y), 2.0f) > 0.89999997615814208984375f)
        {
            discard;
        }
    }
    float3x3 hue = float3x3(float3(1.0f, _192_u[7].x, _192_u[6].w), float3(-_192_u[7].x, 1.0f, _192_u[6].z), float3(-_192_u[6].w, -_192_u[6].z, 1.0f));
    float2 param_10 = TexCoords;
    float2 _1409 = Warp(param_10);
    float2 pos = _1409;
    float2 param_11 = pos;
    pos = BarrelDistortion(param_11);
    float2 bpos = pos;
    float2 dx = float2((1.0f.xx / _192_u[0].xy).x, 0.0f);
    float2 ogl2 = pos * _192_u[0].xy;
    float2 i = floor(pos * _192_u[0].xy) + 0.5f.xx;
    float f = ogl2.y - i.y;
    pos.y = (i.y + (((4.0f * f) * f) * f)) * (1.0f.xx / _192_u[0].xy).y;
    pos.x = lerp(pos.x, i.x * (1.0f.xx / _192_u[0].xy).x, 0.20000000298023223876953125f);
    float corn = 1.0f;
    if (_192_u[5].w > 9.9999999747524270787835121154785e-07f)
    {
        float2 halfRes = _192_u[0].zw * 0.5f;
        float2 param_12 = (pos * _192_u[0].zw) - halfRes;
        float2 param_13 = halfRes;
        float param_14 = abs((_192_u[5].w * _192_u[0].z) * 30.0f);
        float b = 1.0f - roundCorners(param_12, param_13, param_14);
        if (_192_u[3].x > 0.5f)
        {
            corn = b / 10.0f;
        }
        else
        {
            if (b < _192_u[5].w)
            {
                discard;
            }
        }
    }
    float4 res0;
    if (_192_u[2].y > 0.001000000047497451305389404296875f)
    {
        float2 param_15 = pos;
        float2 param_16 = _192_u[0].xy;
        float param_17 = _192_u[2].y;
        res0 = PhosphorBlur(A2TextureCurrent, _A2TextureCurrent_sampler, param_15, param_16, param_17);
    }
    else
    {
        float2 param_18 = pos;
        res0 = SampleCurrent(param_18);
        float3 param_19 = res0.xyz;
        float3 _1550 = s2l(param_19);
        res0.x = _1550.x;
        res0.y = _1550.y;
        res0.z = _1550.z;
    }
    float3 res = res0.xyz;
    if (res0.w <= 0.0f)
    {
        return;
    }
    if (_192_u[6].x > 9.9999997473787516355514526367188e-05f)
    {
        if (((abs(_192_u[5].z) + abs(_192_u[5].y)) + abs(_192_u[5].x)) > 0.001000000047497451305389404296875f)
        {
            float2 param_20 = pos + (dx * _192_u[5].z);
            float resr = SampleCurrent(param_20).x;
            float2 param_21 = pos + (dx * _192_u[5].y);
            float resg = SampleCurrent(param_21).y;
            float2 param_22 = pos + (dx * _192_u[5].x);
            float resb = SampleCurrent(param_22).z;
            res = float3((res0.x * (1.0f - _192_u[6].x)) + (resr * _192_u[6].x), (res0.y * (1.0f - _192_u[6].x)) + (resg * _192_u[6].x), (res0.z * (1.0f - _192_u[6].x)) + (resb * _192_u[6].x));
        }
    }
    float l = dot(_192_u[4].y.xxx, res);
    float CGWG = 0.300000011920928955078125f;
    if (_192_u[3].y > 0.5f)
    {
        CGWG = lerp(_192_u[7].z, _192_u[7].y, l);
    }
    if (int(_192_u[10].y) == 2)
    {
        if (_192_u[9].z > 9.9999997473787516355514526367188e-06f)
        {
            float vig = 0.0f + ((((16.0f * pos.x) * pos.y) * (1.0f - pos.x)) * (1.0f - pos.y));
            vig = pow(vig, _192_u[9].z);
            res *= vig.xxx;
        }
        if (_192_u[8].y > 9.9999997473787516355514526367188e-06f)
        {
            float scans = clamp(0.3499999940395355224609375f + (0.1500000059604644775390625f * sin((2.0f * ((-_192_u[16].z) * _192_u[8].z)) + (((pos.y * float(uint(_192_u[1].x))) * 8.0f) / 2.5499999523162841796875f))), 0.0f, 1.0f);
            float s = pow(scans, _192_u[8].y);
            s = pow(s, _192_u[8].y);
            res *= s.xxx;
        }
        res *= (1.0f + (_192_u[9].x * sin(300.0f * _192_u[16].z)));
        float2 param_23 = pos + (9.9999997473787516355514526367188e-05f * _192_u[16].z).xx;
        float2 param_24 = (pos + (9.9999997473787516355514526367188e-05f * _192_u[16].z).xx) + 0.300000011920928955078125f.xx;
        float2 param_25 = (pos + (9.9999997473787516355514526367188e-05f * _192_u[16].z).xx) + 0.5f.xx;
        res *= (1.0f.xxx - (float3(rand(param_23), rand(param_24), rand(param_25)) * _192_u[8].w));
    }
    float2 xy = (TexCoords * _192_u[0].zw) / _192_u[7].w.xx;
    float2 param_26 = xy;
    float param_27 = CGWG;
    res *= Mask(param_26, param_27);
    if (_192_u[3].y > 0.5f)
    {
        float2 param_28 = xy / 2.0f.xx;
        res *= lerp(slot(param_28), 1.0f.xxx, CGWG.xxx);
    }
    res = (res - _192_u[4].x.xxx) / (1.0f - _192_u[4].x).xxx;
    float3 param_29 = res;
    res = l2oklab(param_29);
    if (_192_u[2].w > 0.5f)
    {
        float c = cos(_192_u[6].y);
        float s_1 = sin(_192_u[6].y);
        float2 ab = float2(res.y, res.z);
        ab = mul(ab, float2x2(float2(c, -s_1), float2(s_1, c)));
        res.y = ab.x;
        res.z = ab.y;
        float3 _1879 = res;
        float2 _1881 = _1879.yz * _192_u[8].x;
        res.y = _1881.x;
        res.z = _1881.y;
        res.x = ((res.x - 0.5f) * _192_u[4].w) + 0.5f;
        res.x *= _192_u[4].z;
    }
    else
    {
        res = mul(hue, res);
        float slum = dot(float3(0.2899999916553497314453125f, 0.60000002384185791015625f, 0.10999999940395355224609375f), res);
        res = lerp(slum.xxx, res, _192_u[8].x.xxx);
        float lum = dot(float3(0.2125999927520751953125f, 0.715200006961822509765625f, 0.072200000286102294921875f), res);
        float lum2 = ((lum - 0.180000007152557373046875f) * _192_u[4].w) + 0.180000007152557373046875f;
        float _1932;
        if (lum > 9.9999999747524270787835121154785e-07f)
        {
            _1932 = lum2 / lum;
        }
        else
        {
            _1932 = 0.0f;
        }
        float scale = _1932;
        res *= scale;
        res = clamp(res, 0.0f.xxx, 1.0f.xxx);
        res *= _192_u[4].z;
    }
    FragColor = float4(res, corn);
    float3 param_30 = FragColor.xyz;
    float3 _1960 = oklab2l(param_30);
    FragColor.x = _1960.x;
    FragColor.y = _1960.y;
    FragColor.z = _1960.z;
    if (int(_192_u[9].w) != 0)
    {
        float3 clr = FragColor.xyz;
        if (int(_192_u[9].w) == 1)
        {
            clr = mul(PAL, clr);
        }
        if (int(_192_u[9].w) == 2)
        {
            clr = mul(NTSC, clr);
        }
        if (int(_192_u[9].w) == 3)
        {
            clr = mul(NTSC_J, clr);
        }
        clr /= float3(0.23999999463558197021484375f, 0.689999997615814208984375f, 0.070000000298023223876953125f);
        clr *= float3(0.2899999916553497314453125f, 0.60000002384185791015625f, 0.10999999940395355224609375f);
        FragColor.x = clr.x;
        FragColor.y = clr.y;
        FragColor.z = clr.z;
    }
    if (_192_u[16].y > 0.5f)
    {
        float2 param_31 = TexCoords;
        float4 param_32 = FragColor;
        FragColor = HalveFrameRate(param_31, param_32);
    }
    if (_192_u[2].x > 9.9999997473787516355514526367188e-05f)
    {
        float2 param_33 = TexCoords;
        float4 param_34 = FragColor;
        float4 _2037 = GenerateGhosting(param_33, param_34);
        FragColor = _2037;
    }
    float3 param_35 = FragColor.xyz;
    float3 _2041 = l2s(param_35);
    FragColor.x = _2041.x;
    FragColor.y = _2041.y;
    FragColor.z = _2041.z;
}

SPIRV_Cross_Output main(SPIRV_Cross_Input stage_input)
{
    gl_FragCoord = stage_input.gl_FragCoord;
    gl_FragCoord.w = 1.0 / gl_FragCoord.w;
    vUV = stage_input.vUV;
    frag_main();
    SPIRV_Cross_Output stage_output;
    stage_output.FragColor = FragColor;
    return stage_output;
}
