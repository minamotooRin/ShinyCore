#version 330
in vec2 fragTexCoord;
in vec4 fragColor;
uniform sampler2D texture0;
uniform vec4 colDiffuse;
uniform vec3 tint;
uniform float strength;
out vec4 finalColor;
void main() {
    vec4 texel=texture(texture0,fragTexCoord);
    finalColor=vec4(mix(texel.rgb,tint,strength),texel.a)*fragColor*colDiffuse;
}
