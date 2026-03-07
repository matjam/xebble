#version 450

// Per-instance data
layout(location = 0) in vec2 inPosition;    // Pivot point in virtual pixels (world space)
layout(location = 1) in vec2 inUVOffset;    // Top-left UV in spritesheet
layout(location = 2) in vec2 inUVSize;      // UV width/height
layout(location = 3) in vec2 inQuadSize;    // Unscaled quad width/height in virtual pixels
layout(location = 4) in vec4 inColor;       // Color tint
layout(location = 5) in float inScale;      // Uniform scale multiplier
layout(location = 6) in float inRotation;   // Rotation in radians (CCW)
layout(location = 7) in vec2 inPivot;       // Pivot point in 0-1 quad-local space

// Push constant: orthographic projection
layout(push_constant) uniform PushConstants {
    mat4 projection;
} pc;

layout(location = 0) out vec2 fragTexCoord;
layout(location = 1) out vec4 fragColor;

// Unit quad corners for two triangles
vec2 corners[6] = vec2[](
    vec2(0.0, 0.0), vec2(1.0, 0.0), vec2(0.0, 1.0),
    vec2(1.0, 0.0), vec2(1.0, 1.0), vec2(0.0, 1.0)
);

void main() {
    vec2 corner = corners[gl_VertexIndex];

    // Scale the quad
    vec2 scaled = corner * inQuadSize * inScale;

    // Pivot in scaled quad space
    vec2 pivot = inPivot * inQuadSize * inScale;

    // Rotate around pivot
    vec2 p = scaled - pivot;
    float c = cos(inRotation);
    float s = sin(inRotation);
    vec2 rotated = vec2(p.x * c - p.y * s, p.x * s + p.y * c);

    // World position: pivot point + rotated offset from pivot
    vec2 worldPos = inPosition + rotated;

    gl_Position  = pc.projection * vec4(worldPos, 0.0, 1.0);
    fragTexCoord = inUVOffset + corner * inUVSize;
    fragColor    = inColor;
}
