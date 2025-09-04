/*
 * WiVRn VR streaming
 * Copyright (C) 2022  Guillaume Meunier <guillaume.meunier@centraliens.net>
 * Copyright (C) 2022  Patrick Nicolas <patricknicolas@laposte.net>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#ifdef VERT_SHADER

#if VSR
	#extension GL_EXT_fragment_shading_rate: enable
#endif


layout (location = 0) in vec2 vPosition;
layout (location = 1) in uvec2 vUV;

layout(location = 0) out vec4 outUV;

layout(push_constant) uniform pc
{
	ivec4 rgb_rect;
	ivec4 a_rect;
};

void main()
{
	gl_Position = vec4(vPosition, 0.0, 1.0);
#if VSR
	gl_PrimitiveShadingRateEXT = int(bitfieldExtract(vUV.x, 28, 4));
	vec2 uv = bitfieldInsert(vUV, uvec2(0), 28, 4);
#else
	vec2 uv = vUV;
#endif
	outUV.xy = (uv + rgb_rect.xy) / rgb_rect.zw;
	outUV.zw = (uv + a_rect.xy) / a_rect.zw;
}
#endif

#ifdef FRAG_SHADER

layout(constant_id = 0) const int alpha = 1;
layout(constant_id = 1) const bool do_srgb = false;

layout(set = 0, binding = 0) uniform sampler2D rgb[alpha + 1];

layout(location = 0) in vec4 inUV;

layout(location = 0) out vec4 outColor;

float sRGB_to_linear(float x)
{
	if (x <= 0.04045)
		return x / 12.92;
	return pow((x + 0.055) / 1.055, 2.4);
}

vec4 sRGB_to_linear_rgba(vec4 x)
{
	return vec4(
	        sRGB_to_linear(x.r),
	        sRGB_to_linear(x.g),
	        sRGB_to_linear(x.b),
	        x.a);
}

void main()
{
	if (alpha == 1)
		outColor = vec4(texture(rgb[0], inUV.xy).rgb, texture(rgb[1], inUV.zw).r);
	else
		outColor = texture(rgb[0], inUV.xy).rgba;

	if (do_srgb)
	{
		outColor = sRGB_to_linear_rgba(outColor);
	}
}

#endif
