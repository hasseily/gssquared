#version 330
#ifdef GL_ARB_shading_language_420pack
#extension GL_ARB_shading_language_420pack : require
#endif

layout(binding = 0, std140) uniform Context
{
    vec4 parameters;
} _22;

layout(binding = 0) uniform sampler2D Source;

in vec2 vUV;
layout(location = 0) out vec4 FragColor;

void main()
{
    vec4 color = texture(Source, vUV);
    if (_22.parameters.z > 0.5)
    {
        vec2 uv = vUV;
        vec2 delta = uv - vec2(0.5);
        uv += ((delta * dot(delta, delta)) * 0.20000000298023223876953125);
        float scanline = (sin((uv.y * _22.parameters.y) * 3.1415927410125732421875) * 0.5) + 0.5;
        scanline = mix(1.0, scanline, 0.25);
        float grille = (mod(uv.x * _22.parameters.x, 3.0) < 1.5) ? 0.949999988079071044921875 : 1.0499999523162841796875;
        grille = mix(1.0, grille, 0.100000001490116119384765625);
        vec2 edge = uv * (vec2(1.0) - uv);
        float vignette = mix(1.0, (edge.x * edge.y) * 15.0, 0.070000000298023223876953125);
        vec4 _109 = color;
        vec3 _111 = _109.xyz * (((scanline * grille) * vignette) * 1.2000000476837158203125);
        color.x = _111.x;
        color.y = _111.y;
        color.z = _111.z;
    }
    FragColor = color;
}
