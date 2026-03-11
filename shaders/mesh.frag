#version 450

layout(location = 0) in vec3 inNormal;
layout(location = 1) in vec2 inUV;
layout(location = 2) in vec4 inColor;

layout(set = 0, binding = 0) uniform sampler2D albedoTex;

layout(location = 0) out vec4 outColor;

void main()
{
    vec4 texSample = texture(albedoTex, inUV);

    // Simple directional light in world space
    vec3  lightDir = normalize(vec3(1.0, 3.0, 2.0));
    float ambient  = 0.20;
    float diffuse  = max(dot(normalize(inNormal), lightDir), 0.0);
    float lighting = ambient + diffuse * 0.85;

    outColor = vec4(texSample.rgb * lighting, texSample.a);
}
