#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in float inUvX;
layout(location = 2) in vec3 inNormal;
layout(location = 3) in float inUvY;
layout(location = 4) in vec4 inColor;

// Vertex stage uses offset 0..63 (mat4 lightMVP)
layout(push_constant) uniform PC {
    mat4 lightMVP;
} push;

layout(location = 0) out vec3 outWorldPos;

void main()
{
    gl_Position = push.lightMVP * vec4(inPosition, 1.0);
    outWorldPos = inPosition; // Sponza has identity model matrix
}
