#version 450
layout(set=3,binding=0,std140) uniform Context { vec4 u[18]; };
layout(set=2,binding=0) uniform sampler2D Processed;
layout(set=2,binding=1) uniform sampler2D Source;
layout(set=2,binding=2) uniform sampler2D Bezel;
layout(set=2,binding=3) uniform sampler2D Glass;
layout(set=2,binding=4) uniform sampler2D UserInterface;
layout(location=0) in vec2 vUV;
layout(location=0) out vec4 FragColor;
vec4 over(vec4 a,vec4 b) { return vec4(a.rgb*a.a+b.rgb*(1.0-a.a),1.0); }
void main() {
    vec4 color=texture(Processed,vUV);
    vec2 uv=(vUV-0.5)/u[15].xy+0.5-u[15].zw;
    if (u[17].y > 0.5 && all(greaterThanEqual(uv,vec2(0.0))) && all(lessThanEqual(uv,vec2(1.0)))) {
        vec4 bezel=texture(Bezel,uv);
        bool outlined=false;
        if (u[13].x > 0.00001 && bezel.a > 0.0 && bezel.a < 1.0) {
            vec2 r=(uv-0.5)*u[14].xy+0.5+u[14].zw;
            bool outline=(r.x>0.0&&r.x<0.01)||(r.y>0.0&&r.y<0.01)||
                (r.x>0.99&&r.x<1.0)||(r.y>0.99&&r.y<1.0);
            vec2 reflected=1.0-abs(mod(r,2.0)-1.0);
            vec4 image=textureLod(Source,u[11].xy+reflected*u[11].zw,u[13].y+u[17].x);
            float amount=(1.0-u[13].x)*image.a;
            bezel=vec4(mix(image.rgb,bezel.rgb,amount),1.0);
            if (u[13].w > 0.5 && outline) { bezel=vec4(1.0,0.0,0.0,1.0); outlined=true; }
        }
        if (!outlined && u[17].z > 0.5 && u[13].z > 0.0) {
            // Preserve the source glass-thickness extrapolation, then apply
            // the normalized render-target clamp before framebuffer blending.
            vec4 glass=texture(Glass,uv); glass.a*=u[13].z;
            float alpha=glass.a+bezel.a*(1.0-glass.a);
            vec3 rgb=glass.rgb*glass.a+bezel.rgb*bezel.a*(1.0-glass.a);
            bezel=vec4(alpha>0.0?rgb/alpha:vec3(0.0),alpha);
        }
        color=over(clamp(bezel,0.0,1.0),color);
    }
    // SDL uses straight-alpha input with source-over into a transparent UI
    // target, so its stored RGB is already premultiplied.
    vec4 ui=texture(UserInterface,vUV);
    FragColor=vec4(ui.rgb+color.rgb*(1.0-ui.a),1.0);
}
