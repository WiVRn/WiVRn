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
#version 450

layout(push_constant) uniform pc
{
	ivec4 rgb_rect;
	ivec4 a_rect;
	vec4 scale;
	vec4 bias;
	// Client-side chroma key passthrough. Packed into 3 vec4s to stay well
	// under the 128-byte Vulkan push constant minimum.
	//   x = enabled (0/1), y = curve, z = despill, w = reserved.
	vec4 chroma_key_params;
	vec4 chroma_key_hsv_min; // xyz = HSV min, w unused
	vec4 chroma_key_hsv_max; // xyz = HSV max, w unused
};

#define chroma_key_enabled (chroma_key_params.x > 0.5)
#define chroma_key_curve   chroma_key_params.y
#define chroma_key_despill chroma_key_params.z

#ifdef VERT_SHADER

layout (location = 0) in vec2 vPosition;
layout (location = 1) in uvec2 vUV;

layout(location = 0) out vec4 outUV;

void main()
{
	gl_Position = vec4(vPosition, 0.0, 1.0);
	vec2 uv = vUV;
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

// Standard RGB -> HSV conversion (Sam Hocevar's trick).
vec3 rgb_to_hsv(vec3 c)
{
	vec4 K = vec4(0.0, -1.0/3.0, 2.0/3.0, -1.0);
	vec4 p = mix(vec4(c.bg, K.wz), vec4(c.gb, K.xy), step(c.b, c.g));
	vec4 q = mix(vec4(p.xyw, c.r), vec4(c.r, p.yzx), step(p.x, c.r));
	float d = q.x - min(q.w, q.y);
	float e = 1.0e-10;
	return vec3(abs(q.z + (q.w - q.y) / (6.0 * d + e)),
	            d / (q.x + e),
	            q.x);
}

// Hue is circular: if min.h > max.h the range wraps around 0/1.
float hue_in_range(float h, float hmin, float hmax, float soft)
{
	if (hmin <= hmax)
	{
		float lo = smoothstep(hmin - soft, hmin, h);
		float hi = 1.0 - smoothstep(hmax, hmax + soft, h);
		return lo * hi;
	}
	else
	{
		float lo = smoothstep(hmin - soft, hmin, h);
		float hi = 1.0 - smoothstep(hmax, hmax + soft, h);
		return max(lo, hi);
	}
}

// Returns 1.0 when the pixel should be kept, 0.0 when it should be keyed out.
float chroma_key_mask(vec3 rgb_sample)
{
	vec3 hsv = rgb_to_hsv(rgb_sample);
	float soft = max(chroma_key_curve, 1.0e-4);
	float h = hue_in_range(hsv.x, chroma_key_hsv_min.x, chroma_key_hsv_max.x, soft);
	float s = smoothstep(chroma_key_hsv_min.y - soft, chroma_key_hsv_min.y, hsv.y)
	        * (1.0 - smoothstep(chroma_key_hsv_max.y, chroma_key_hsv_max.y + soft, hsv.y));
	float v = smoothstep(chroma_key_hsv_min.z - soft, chroma_key_hsv_min.z, hsv.z)
	        * (1.0 - smoothstep(chroma_key_hsv_max.z, chroma_key_hsv_max.z + soft, hsv.z));
	// All three must be inside the range (product) to count as keyed.
	float inside = h * s * v;
	return 1.0 - inside;
}

// Reduce residual spill of the key color (typically green) on kept pixels.
vec3 despill(vec3 c)
{
	if (chroma_key_despill <= 0.0)
		return c;
	// Use the midpoint of the HSV hue range as the key hue.
	float hmin = chroma_key_hsv_min.x;
	float hmax = chroma_key_hsv_max.x;
	float hkey = (hmax >= hmin) ? 0.5 * (hmin + hmax) : fract(0.5 * (hmin + hmax + 1.0));
	// Map hue in [0, 1] to a color channel emphasis: green around 1/3, red 0, blue 2/3.
	// Simple subtractive desaturation: clamp the key channel to the average of the others.
	vec3 limit;
	if (hkey < 1.0/6.0 || hkey >= 5.0/6.0)
		limit = vec3(0.5 * (c.g + c.b), c.g, c.b); // red key
	else if (hkey < 0.5)
		limit = vec3(c.r, 0.5 * (c.r + c.b), c.b); // green key
	else
		limit = vec3(c.r, c.g, 0.5 * (c.r + c.g)); // blue key
	return mix(c, min(c, limit), clamp(chroma_key_despill, 0.0, 1.0));
}

void main()
{
	if (alpha == 1)
	{
		// Avoid sampling between the eyes
		vec2 a = inUV.zw;
		float d = a.x - 0.5;
		if (abs(d) *a_rect.z < 1)
			a.x += (d > 0 ? 1 : -1)  / float(a_rect.z);
		outColor = vec4(texture(rgb[0], inUV.xy).rgb, texture(rgb[1], a).r);
	}
	else
		outColor = texture(rgb[0], inUV.xy).rgba;

	if (do_srgb)
	{
		outColor = sRGB_to_linear_rgba(outColor);
	}

	// Apply chroma key only when the stream has no real alpha channel.
	// When the app itself provides alpha (alpha == 1) we trust it and avoid
	// double-masking.
	bool ck_active = (alpha == 0) && chroma_key_enabled;
	if (ck_active)
	{
		float m = chroma_key_mask(outColor.rgb);
		outColor.rgb = despill(outColor.rgb);
		outColor.a = m;
	}

	if (alpha == 0 && !ck_active)
	{
		outColor = outColor * scale + bias;
	}
	else if (outColor.a < 0.02)
	{
		outColor = vec4(0);
	}
	else
	{
		outColor.rgb /= outColor.a;
		outColor = outColor * scale + bias;
		outColor.rgb *= outColor.a;
	}
}

#endif
