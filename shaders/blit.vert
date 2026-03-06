#version 450

layout(location = 0) out vec2 fragTexCoord;

void main() {
    fragTexCoord = vec2(0.0);
    gl_Position = vec4(0.0, 0.0, 0.0, 1.0);
}
