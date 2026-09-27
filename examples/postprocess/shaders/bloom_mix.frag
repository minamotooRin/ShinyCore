#version 330
in vec2 fragTexCoord;
uniform sampler2D texture0;
uniform sampler2D sc_scene;
uniform float strength;
out vec4 finalColor;
void main() {
    vec4 original=texture(sc_scene,fragTexCoord);
    finalColor=vec4(original.rgb+texture(texture0,fragTexCoord).rgb*strength,original.a);
}
