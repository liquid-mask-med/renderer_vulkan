#version 450

layout(location = 0) in vec3 position;
layout(location = 0) out vec3 volumePos;

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

void main() {
    volumePos = position;
    gl_Position = params.projectMatrix * params.viewMatrix * params.modelMatrix * vec4(position, 1.0);
    gl_Position.z = (gl_Position.z + gl_Position.w) * 0.5;
}
