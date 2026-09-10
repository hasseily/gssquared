#version 450
layout(location=0) in vec2 vUV;
layout(location=0) out vec4 FragColor;
layout(set=2,binding=0) uniform sampler2D Source;
layout(std140,set=3,binding=0) uniform Context { vec4 parameters; };

// Preserve the existing Lightweight CRT effect and its F7 toggle.
// https://godotshaders.com/shader/lightweight-crt-effect/
void main() {
    vec4 color=texture(Source,vUV);
    if(parameters.z>0.5) {
        vec2 uv=vUV;
        vec2 delta=uv-0.5;
        uv+=delta*dot(delta,delta)*0.20;
        float scanline=sin(uv.y*parameters.y*3.14159265359)*0.5+0.5;
        scanline=mix(1.0,scanline,0.25);
        float grille=mod(uv.x*parameters.x,3.0)<1.5?0.95:1.05;
        grille=mix(1.0,grille,0.10);
        vec2 edge=uv*(1.0-uv);
        float vignette=mix(1.0,edge.x*edge.y*15.0,0.07);
        color.rgb*=scanline*grille*vignette*1.2;
    }
    FragColor=color;
}
