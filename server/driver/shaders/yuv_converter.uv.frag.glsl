// Copyright 2022, Guillaume Meunier
// Copyright 2022, Patrick Nicolas
// SPDX-License-Identifier: BSL-1.0

#version 450

layout (binding = 0) uniform sampler2D image;

layout (location = 0) in vec2 pos;

layout (location = 0) out vec2 uv;

float gamma(float x)
{
	if (x <= 0.0031308)
		return 12.92 * x;
	else
		return 1.055 * pow(x, 1/2.4) - 0.055;
}

vec4 gamma4(vec4 x)
{
	return vec4(gamma(x.r), gamma(x.g), gamma(x.b), gamma(x.a));
}

float reduce_range(float x)
{
	return (128 + x * 224) / 256;
}

void main() {
	vec4 rgba = gamma4(texture(image, pos));
	uv.x = reduce_range(rgba.r * -0.1146 + rgba.g * -0.3854 + rgba.b *  0.5);
	uv.y = reduce_range(rgba.r *  0.5    + rgba.g * -0.4542 + rgba.b * -0.0458);
}

