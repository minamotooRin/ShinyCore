#version 330
in vec2 fragTexCoord;
uniform sampler2D texture0;
uniform vec2 sc_resolution;
out vec4 finalColor;
void main() {
    float weights[5]=float[](0.227027,0.1945946,0.1216216,0.054054,0.016216);
    vec3 color=texture(texture0,fragTexCoord).rgb*weights[0];
    for(int i=1;i<5;++i) {
        vec2 offset=vec2(0.0,float(i)/sc_resolution.y);
        color+=(texture(texture0,fragTexCoord+offset).rgb+texture(texture0,fragTexCoord-offset).rgb)*weights[i];
    }
    finalColor=vec4(color,1.0);
}
