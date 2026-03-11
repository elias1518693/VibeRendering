#version 450

// Only position is needed for the depth-only shadow pass.
// The full vertex buffer is still bound (stride = 48 bytes) — other attributes are ignored.
layout(location = 0) in vec3 inPosition;
layout(location = 1) in float inUvX;
layout(location = 2) in vec3 inNormal;
layout(location = 3) in float inUvY;
layout(location = 4) in vec4 inColor;

layout(push_constant) uniform PushConstants {
    mat4 lightMVP;
} push;

void main()
{
    gl_Position = push.lightMVP * vec4(inPosition, 1.0);
}
