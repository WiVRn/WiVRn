#version 450

#if DELTA
layout(set = 0, binding = 0) uniform texture2D Y0;
layout(set = 0, binding = 1) uniform texture2D Y1;
#else
layout(set = 0, binding = 0) uniform texture2D Y;
layout(set = 0, binding = 1) uniform texture2D Cb;
layout(set = 0, binding = 2) uniform texture2D Cr;
#endif
layout(set = 0, binding = 3) uniform sampler Samp;

layout(location = 0) out vec3 FragColor;
layout(location = 0) in vec2 vUV;

layout(constant_id = 0) const bool BT2020 = false;
layout(constant_id = 1) const bool FullRange = false;

const mat3 yuv2rgb_bt709 = mat3(
    vec3(1.0, 1.0, 1.0),
    vec3(0.0, -0.13397432 / 0.7152, 1.8556),
    vec3(1.5748, -0.33480248 / 0.7152, 0.0));

const mat3 yuv2rgb_bt2020 = mat3(
    vec3(1.0, 1.0, 1.0),
    vec3(0.0, -0.202008 / 0.587, 1.772),
    vec3(1.402, -0.419198 / 0.587, 0.0));

void main()
{
#if DELTA
    float y0 = textureLod(sampler2D(Y0, Samp), vUV, 0.0).x;
    float y1 = textureLod(sampler2D(Y1, Samp), vUV, 0.0).x;
    FragColor = vec3(abs(y0 - y1) * 10.0);
#else
    float y = textureLod(sampler2D(Y, Samp), vUV, 0.0).x;
    float cb = textureLod(sampler2D(Cb, Samp), vUV, 0.0).x;
    float cr = textureLod(sampler2D(Cr, Samp), vUV, 0.0).x;

    cb -= 0.5;
    cr -= 0.5;

    if (!FullRange)
    {
        y -= 16.0 / 255.0;
        y *= 255.0 / 219.0;
        const float ChromaScale = 255.0 / 224.0;
        cb *= ChromaScale;
        cr *= ChromaScale;
        y = clamp(y, 0.0, 1.0);
        cb = clamp(cb, -0.5, 0.5);
        cr = clamp(cr, -0.5, 0.5);
    }

    if (BT2020)
        FragColor = yuv2rgb_bt2020 * vec3(y, cb, cr);
    else
        FragColor = yuv2rgb_bt709 * vec3(y, cb, cr);
#endif
}
