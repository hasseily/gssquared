#version 330
#ifdef GL_ARB_shading_language_420pack
#extension GL_ARB_shading_language_420pack : require
#endif

layout(binding = 0, std140) uniform Context
{
    vec4 u[18];
} _58;

layout(binding = 0) uniform sampler2D Processed;
layout(binding = 2) uniform sampler2D Bezel;
layout(binding = 1) uniform sampler2D Source;
layout(binding = 3) uniform sampler2D Glass;
layout(binding = 4) uniform sampler2D UserInterface;

in vec2 vUV;
layout(location = 0) out vec4 FragColor;

vec4 over(vec4 a, vec4 b)
{
    return vec4((a.xyz * a.w) + (b.xyz * (1.0 - a.w)), 1.0);
}

void main()
{
    vec4 color = texture(Processed, vUV);
    vec2 uv = (((vUV - vec2(0.5)) / _58.u[15].xy) + vec2(0.5)) - _58.u[15].zw;
    bool _79 = _58.u[17].y > 0.5;
    bool _88;
    if (_79)
    {
        _88 = all(greaterThanEqual(uv, vec2(0.0)));
    }
    else
    {
        _88 = _79;
    }
    bool _95;
    if (_88)
    {
        _95 = all(lessThanEqual(uv, vec2(1.0)));
    }
    else
    {
        _95 = _88;
    }
    if (_95)
    {
        vec4 bezel = texture(Bezel, uv);
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
            vec2 r = (((uv - vec2(0.5)) * _58.u[14].xy) + vec2(0.5)) + _58.u[14].zw;
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
            vec2 reflected = vec2(1.0) - abs(mod(r, vec2(2.0)) - vec2(1.0));
            vec4 image = textureLod(Source, _58.u[11].xy + (reflected * _58.u[11].zw), _58.u[13].y + _58.u[17].x);
            float amount = (1.0 - _58.u[13].x) * image.w;
            bezel = vec4(mix(image.xyz, bezel.xyz, vec3(amount)), 1.0);
            if ((_58.u[13].w > 0.5) && outline)
            {
                bezel = vec4(1.0, 0.0, 0.0, 1.0);
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
            vec4 glass = texture(Glass, uv);
            glass.w *= _58.u[13].z;
            float alpha = glass.w + (bezel.w * (1.0 - glass.w));
            vec3 rgb = (glass.xyz * glass.w) + ((bezel.xyz * bezel.w) * (1.0 - glass.w));
            vec3 _305;
            if (alpha > 0.0)
            {
                _305 = rgb / vec3(alpha);
            }
            else
            {
                _305 = vec3(0.0);
            }
            bezel = vec4(_305, alpha);
        }
        vec4 param = clamp(bezel, vec4(0.0), vec4(1.0));
        vec4 param_1 = color;
        color = over(param, param_1);
    }
    vec4 ui = texture(UserInterface, vUV);
    FragColor = vec4(ui.xyz + (color.xyz * (1.0 - ui.w)), 1.0);
}
