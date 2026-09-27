#version 330
in vec2 fragTexCoord;
uniform sampler2D texture0;
uniform float time;
uniform float amplitude;
out vec4 finalColor;
void main() {
    vec2 uv=fragTexCoord;
    uv.x+=sin(uv.y*24.0+time*3.0)*amplitude;
    finalColor=texture(texture0,clamp(uv,0.0,1.0));
}
