#version 450
layout(location=0) in vec2 vUV;
layout(location=0) out vec4 FragColor;
layout(set=2,binding=0) uniform sampler2D Processed;
layout(set=2,binding=1) uniform sampler2D UserInterface;
void main() {
    vec4 guest=texture(Processed,vUV);
    // SDL's transparent UI target contains premultiplied color.
    vec4 ui=texture(UserInterface,vUV);
    FragColor=vec4(ui.rgb+guest.rgb*(1.0-ui.a),1.0);
}
