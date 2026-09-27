#version 330
in vec2 fragTexCoord;
uniform sampler2D texture0;
uniform vec3 gain;
uniform float gamma;
out vec4 finalColor;
void main() {
    vec4 color=texture(texture0,fragTexCoord);
    finalColor=vec4(pow(clamp(color.rgb*gain,0.0,1.0),vec3(1.0/gamma)),color.a);
}
