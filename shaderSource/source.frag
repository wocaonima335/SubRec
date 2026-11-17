#version 330 core
in vec2 v_texCoord;

uniform sampler2D yTexture;
uniform sampler2D uTexture;
uniform sampler2D vTexture;

out vec4 FragColor;

void main() {
    // 预览帧是 yuv420p，BT.601 limited range（ddagrab/GpuPreview 的 out_range=tv、swscale 默认）：
    // Y 在 [16, 235]，U/V 在 [16, 240]，中心 128。先展开到 full range，再用 BT.601 系数转 RGB。
    // UV 纹理尺寸是 Y 的 1/2，纹理坐标归一化，所以与 Y 共用同一坐标。
    float y = (texture(yTexture, v_texCoord).r - 16.0 / 255.0) * (255.0 / 219.0);
    float u = (texture(uTexture, v_texCoord).r - 128.0 / 255.0) * (255.0 / 224.0);
    float v = (texture(vTexture, v_texCoord).r - 128.0 / 255.0) * (255.0 / 224.0);

    // BT.601 YCbCr -> RGB（full-range 系数）
    vec3 rgb;
    rgb.r = y + 1.402 * v;
    rgb.g = y - 0.344136 * u - 0.714136 * v;
    rgb.b = y + 1.772 * u;

    FragColor = vec4(clamp(rgb, 0.0, 1.0), 1.0);
}
