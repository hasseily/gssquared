#version 330
#ifdef GL_ARB_shading_language_420pack
#extension GL_ARB_shading_language_420pack : require
#endif

layout(binding = 0) uniform sampler2D Processed;
layout(binding = 1) uniform sampler2D UserInterface;

in vec2 vUV;
layout(location = 0) out vec4 FragColor;

void main()
{
    vec4 guest = texture(Processed, vUV);
    vec4 ui = texture(UserInterface, vUV);
    FragColor = vec4(ui.xyz + (guest.xyz * (1.0 - ui.w)), 1.0);
}
