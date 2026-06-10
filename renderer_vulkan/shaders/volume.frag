#version 450

layout(location = 0) in vec3 volumePos;
layout(location = 0) out vec4 color;

layout(set = 0, binding = 0) uniform sampler3D volumeTexture;
layout(set = 0, binding = 1) uniform sampler1D volumeColor;
layout(set = 0, binding = 2, std140) uniform Params {
    mat4 modelMatrix;
    mat4 viewMatrix;
    mat4 projectMatrix;
    vec4 viewRay;
    vec4 volumePhysicalSize;
    vec4 origin;
    vec4 axisU;
    vec4 axisV;
    vec4 uvBounds;
    ivec4 viewportWindow;
    ivec4 dimensions;
} params;

void main() {
    vec3 pos = volumePos;
    vec4 accumulatedColor = vec4(0.0);
    float eps = 1e-4;

    for (int i = 0; i < params.dimensions.w; ++i) {
        vec3 coord = pos / params.volumePhysicalSize.xyz + vec3(0.5);
        coord.z = 1.0 - coord.z;

        if (any(lessThan(coord, vec3(-eps))) || any(greaterThan(coord, vec3(1.0 + eps)))) {
            break;
        }

        float sampledValue = texture(volumeTexture, coord).r;
        vec4 sampledColor = texture(volumeColor, sampledValue * 65535.0 / 4095.0);
        float alpha = sampledColor.a;

        accumulatedColor.rgb += (1.0 - accumulatedColor.a) * sampledColor.rgb * alpha;
        accumulatedColor.a += (1.0 - accumulatedColor.a) * alpha;

        if (accumulatedColor.a > 0.98) {
            break;
        }

        pos += params.viewRay.xyz;
    }

    color = vec4(accumulatedColor.rgb, 1.0);
}
