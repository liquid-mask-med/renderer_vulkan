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
    vec4 volumePixelSize;
    vec4 renderControls;
} params;

vec3 GetGradient(vec3 coord);
float InterleavedGradientNoise(vec2 pixel);

void main() {
    float stepSize = params.renderControls.x;
    vec3 rayStep = params.viewRay.xyz * stepSize;
    vec3 pos = volumePos + rayStep * InterleavedGradientNoise(gl_FragCoord.xy);
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
        float alpha = 1.0 - pow(max(1.0 - sampledColor.a, 0.0), stepSize);

        if (alpha > 0.001) {
            vec3 N = -GetGradient(coord);
            N.z = -N.z;
            vec3 L = normalize(-params.viewRay.xyz);
            float ndotl = max(dot(N, L), 0.0);
            sampledColor.rgb *= 0.2 + 1.0 * ndotl;
        }

        accumulatedColor.rgb += (1.0 - accumulatedColor.a) * sampledColor.rgb * alpha;
        accumulatedColor.a += (1.0 - accumulatedColor.a) * alpha;

        if (accumulatedColor.a > 0.98) {
            break;
        }

        pos += rayStep;
    }

    //vec3 bgColor = vec3(0.55, 0.58, 0.78);
    vec3 bgColor = vec3(0, 0, 0);
    vec3 finalRgb = accumulatedColor.rgb + (1.0 - accumulatedColor.a) * bgColor;
    color = vec4(finalRgb, 1.0);
}

float InterleavedGradientNoise(vec2 pixel)
{
    return fract(52.9829189 * fract(dot(pixel, vec2(0.06711056, 0.00583715))));
}

vec3 GetGradient(vec3 coord)
{
    vec3 cellStep = 1.0 / params.volumePixelSize.xyz;

    float xp = texture(volumeTexture, coord + vec3(cellStep.x, 0.0, 0.0)).r * 65535.0;
    float xm = texture(volumeTexture, coord - vec3(cellStep.x, 0.0, 0.0)).r * 65535.0;

    float yp = texture(volumeTexture, coord + vec3(0.0, cellStep.y, 0.0)).r * 65535.0;
    float ym = texture(volumeTexture, coord - vec3(0.0, cellStep.y, 0.0)).r * 65535.0;

    float zp = texture(volumeTexture, coord + vec3(0.0, 0.0, cellStep.z)).r * 65535.0;
    float zm = texture(volumeTexture, coord - vec3(0.0, 0.0, cellStep.z)).r * 65535.0;

    vec3 spacing = params.volumePhysicalSize.xyz / params.volumePixelSize.xyz;
    vec3 grad = vec3(
        (xp - xm) / spacing.x,
        (yp - ym) / spacing.y,
        (zp - zm) / spacing.z
    );

    float len = length(grad);
    return len > 0.0 ? grad / len : vec3(0.0);
}
