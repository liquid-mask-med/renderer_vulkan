layout(std430, binding = 0) readonly buffer VolumeData { uint volume[]; };
layout(std430, binding = 1) readonly buffer LutData { vec4 lut[]; };
layout(std430, binding = 2) writeonly buffer OutputData { uint outputPixels[]; };

layout(std140, binding = 3) uniform Params {
    mat4 inverseViewProjection;
    vec4 volumeSize;
    vec4 origin;
    vec4 axisU;
    vec4 axisV;
    vec4 uvBounds;
    ivec4 dimensions;
    ivec4 viewportWindow;
} params;

uint volumeAt(ivec3 p) {
    p = clamp(p, ivec3(0), params.dimensions.xyz - 1);
    return volume[p.x + p.y * params.dimensions.x + p.z * params.dimensions.x * params.dimensions.y];
}

float sampleVolume(vec3 uvw) {
    vec3 p = clamp(uvw, vec3(0), vec3(1)) * vec3(params.dimensions.xyz - 1);
    ivec3 a = ivec3(floor(p));
    ivec3 b = min(a + 1, params.dimensions.xyz - 1);
    vec3 f = fract(p);
    float c00 = mix(float(volumeAt(ivec3(a.x,a.y,a.z))), float(volumeAt(ivec3(b.x,a.y,a.z))), f.x);
    float c10 = mix(float(volumeAt(ivec3(a.x,b.y,a.z))), float(volumeAt(ivec3(b.x,b.y,a.z))), f.x);
    float c01 = mix(float(volumeAt(ivec3(a.x,a.y,b.z))), float(volumeAt(ivec3(b.x,a.y,b.z))), f.x);
    float c11 = mix(float(volumeAt(ivec3(a.x,b.y,b.z))), float(volumeAt(ivec3(b.x,b.y,b.z))), f.x);
    return mix(mix(c00,c10,f.y), mix(c01,c11,f.y), f.z);
}

uint bgra(vec3 rgb) {
    return packUnorm4x8(vec4(rgb.b, rgb.g, rgb.r, 1.0));
}
