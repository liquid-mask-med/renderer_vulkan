#version 450

layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 color;

layout(set = 0, binding = 0) uniform sampler3D volumeTexture;
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
    float uAspect = float(params.viewportWindow.x) / float(params.viewportWindow.y);
    float minU = params.uvBounds.x;
    float maxU = params.uvBounds.y;
    float minV = params.uvBounds.z;
    float maxV = params.uvBounds.w;

    float centerU = (maxU + minU) * 0.5;
    float centerV = (maxV + minV) * 0.5;
    float halfU = max((maxU - minU) * 0.5, 1e-5);
    float halfV = max((maxV - minV) * 0.5, 1e-5);
    float rangeAspect = halfU / halfV;

    if (uAspect > rangeAspect) {
        halfU = halfV * uAspect;
    } else {
        halfV = halfU / uAspect;
    }

    float uOffset = centerU + (uv.x - 0.5) * 2.0 * halfU;
    float vOffset = centerV + ((1.0 - uv.y) - 0.5) * 2.0 * halfV;
    vec3 worldPos = params.origin.xyz + params.axisU.xyz * uOffset + params.axisV.xyz * vOffset;
    vec3 coord = worldPos / params.volumePhysicalSize.xyz + vec3(0.5);

    if (coord.x <= 0.0 || coord.y <= 0.0 || coord.z <= 0.0 ||
        coord.x >= 1.0 || coord.y >= 1.0 || coord.z >= 1.0) {
        discard;
    }

    float sampledValue = texture(volumeTexture, coord).r;
    int i16 = int(round(sampledValue * 65535.0));
    int hu = i16 - 1024;
    int windowMin = params.viewportWindow.z - params.viewportWindow.w / 2;
    float finalVal = clamp(float(hu - windowMin) / float(params.viewportWindow.w), 0.0, 1.0);
    color = vec4(vec3(finalVal), 1.0);
}
