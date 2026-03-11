#version 450

layout(location = 0) in vec3 inWorldPos;

// Fragment stage uses offset 64..79 (vec4 lightPos)
layout(push_constant) uniform PC {
    mat4 lightMVP;  // offset 0  — vertex already consumed this
    vec4 lightPos;  // offset 64 — fragment uses this
} push;

layout(location = 0) out float outLinearDist;

const float k_farPlane = 1000.0;

void main()
{
    outLinearDist = length(inWorldPos - push.lightPos.xyz) / k_farPlane;
}
