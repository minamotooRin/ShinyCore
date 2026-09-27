#version 330
in vec2 fragTexCoord;
uniform sampler2D texture0;
uniform vec2 sc_resolution;
uniform float threshold;
out vec4 finalColor;
vec3 bright(vec2 uv) { return max(texture(texture0,uv).rgb-vec3(threshold),vec3(0.0)); }
void main() {
    float weights[5]=float[](0.227027,0.1945946,0.1216216,0.054054,0.016216);
    vec3 color=bright(fragTexCoord)*weights[0];
    for(int i=1;i<5;++i) {
        vec2 offset=vec2(float(i)/sc_resolution.x,0.0);
        color+=(bright(fragTexCoord+offset)+bright(fragTexCoord-offset))*weights[i];
    }
    finalColor=vec4(color,1.0);
}
