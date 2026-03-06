#version 450

// Per-instance data
layout(location = 0) in vec2 inPosition;    // Quad position in virtual pixels
layout(location = 1) in vec2 inUVOffset;    // Top-left UV in spritesheet
layout(location = 2) in vec2 inUVSize;      // UV width/height
layout(location = 3) in vec2 inQuadSize;    // Quad width/height in virtual pixels
layout(location = 4) in vec4 inColor;       // Color tint / text color

// Push constant: orthographic projection
layout(push_constant) uniform PushConstants {
    mat4 projection;
} pc;

layout(location = 0) out vec2 fragTexCoord;
layout(location = 1) out vec4 fragColor;

// Generate quad vertices from gl_VertexIndex (0-5 for two triangles)
vec2 positions[6] = vec2[](
    vec2(0.0, 0.0), vec2(1.0, 0.0), vec2(0.0, 1.0),
    vec2(1.0, 0.0), vec2(1.0, 1.0), vec2(0.0, 1.0)
);

void main() {
    vec2 pos = positions[gl_VertexIndex];
    vec2 worldPos = inPosition + pos * inQuadSize;
    gl_Position = pc.projection * vec4(worldPos, 0.0, 1.0);
    fragTexCoord = inUVOffset + pos * inUVSize;
    fragColor = inColor;
}
