#version 450

layout(location = 0) in vec3 inNormal;
layout(location = 1) in vec2 inUV;
layout(location = 2) in vec4 inColor;
layout(location = 3) in vec3 inWorldPos;

layout(set = 0, binding = 0) uniform sampler2D  albedoTex;
layout(set = 0, binding = 1) uniform samplerCube shadowCube;

// Fragment push constants (vertex stage occupies offsets 0–63)
layout(push_constant) uniform PC {
    layout(offset = 64)  vec4  lightPos;
    layout(offset = 80)  float ambient;
    layout(offset = 84)  float diffuse;
    layout(offset = 88)  float shadowBias;
    layout(offset = 92)  float pcfRadius;
    layout(offset = 96)  float attLin;
    layout(offset = 100) float attQuad;
} push;

layout(location = 0) out vec4 outColor;

const float k_farPlane = 1000.0; // must match shadow_cube.frag + Renderer.cpp

// 9-sample PCF on the cubemap
float shadowPCF(vec3 worldPos)
{
    vec3  fragToLight = worldPos - push.lightPos.xyz;
    float currentDist = length(fragToLight) / k_farPlane;

    vec3 offsets[9] = vec3[9](
        vec3( 0.00,  0.00,  0.00),
        vec3( 1.00,  0.00,  0.00) * push.pcfRadius,
        vec3(-1.00,  0.00,  0.00) * push.pcfRadius,
        vec3( 0.00,  1.00,  0.00) * push.pcfRadius,
        vec3( 0.00, -1.00,  0.00) * push.pcfRadius,
        vec3( 0.00,  0.00,  1.00) * push.pcfRadius,
        vec3( 0.00,  0.00, -1.00) * push.pcfRadius,
        vec3( 0.60,  0.60,  0.60) * push.pcfRadius,
        vec3(-0.60, -0.60, -0.60) * push.pcfRadius
    );

    float shadow = 0.0;
    for (int i = 0; i < 9; ++i)
    {
        float storedDist = texture(shadowCube, fragToLight + offsets[i]).r;
        shadow += (currentDist - push.shadowBias < storedDist) ? 1.0 : 0.0;
    }
    return shadow / 9.0;
}

void main()
{
    vec4 texSample = texture(albedoTex, inUV);

    vec3  toLight     = push.lightPos.xyz - inWorldPos;
    float dist        = length(toLight);
    vec3  lightDir    = toLight / dist;
    float attenuation = 1.0 / (1.0 + push.attLin * dist + push.attQuad * dist * dist);

    float lighting = push.ambient + max(dot(normalize(inNormal), lightDir), 0.0)
                     * attenuation * push.diffuse * shadowPCF(inWorldPos);

    outColor = vec4(texSample.rgb * lighting, texSample.a);
}
